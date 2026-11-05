// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/manual_grenade_renderer.hpp"
#include "t5/manual_grenade_native_guards.hpp"
#include "t5/manual_grenade_adapter.hpp"
#include "t5/manual_grenade_render_math.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"

#include <array>
#include <atomic>
#include <tlhelp32.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <span>
#include <string>

#if !defined(_M_IX86)
#error The pinned Black Ops grenade renderer requires MSVC x86.
#endif

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t selector_rva=0x540b0;
constexpr std::uintptr_t update_view_weapon_anim_rva=manual_grenade_native_guards::update_view_weapon_anim_rva;
constexpr std::uintptr_t add_view_weapon_rva=manual_grenade_native_guards::add_view_weapon_rva;
constexpr std::uintptr_t dobj_create_rva=0x2193e0;
constexpr std::uintptr_t add_dobj_rva=0x2bfdf0;
constexpr std::uintptr_t scene_xmodel_pointer_rva=0x371f7b4;
constexpr std::uintptr_t scene_dobj_pointer_rva=0x371f7b8;
constexpr std::uint32_t first_scene_lease=0x400,last_scene_lease=0x5ff;
constexpr std::size_t scene_table_entries=last_scene_lease+1;
constexpr std::size_t dobj_size=0x7c,pose_size=0x174,xmodel_size=0xfc;
constexpr std::size_t dobj_num_models_offset=0x09,dobj_models_offset=0x78;
constexpr std::size_t pose_type_offset=0x02,pose_local_client_offset=0x04;
constexpr std::size_t pose_origin_offset=0x24,pose_angles_offset=0x30;
// M67 local +Y extends 1.43821 IW units from its origin.  The anatomical hand
// basis calls +left "into palm", so this places the sphere just outside the
// palm plane with a small authored-looking overlap instead of behind the glove.
constexpr float palm_inset=-1.20f;
constexpr unsigned render_flags=0x07;

constexpr native_guard::Span selector_guard{selector_rva,32,"0e63e359ddff0a273fad3b1df2bfe9985e2c5708e8f71d690eed2ef6f0b1b824"};
constexpr native_guard::Span dobj_create_guard{dobj_create_rva,32,"e545f446e309ef55a91c690305dcf6cabd1724a667463b70bcbf6b8d4ef548f9"};
constexpr native_guard::Span add_dobj_guard{add_dobj_rva,32,"36df0e80b4a32eff14a1fe0e7f55ae30038c1bad75b9bd89daa10305530aecb3"};
enum class VisualCallKind {update_animation,add_weapon};
struct VisualCall final {std::uintptr_t rva,target_rva;std::size_t size;const char* sha256;VisualCallKind kind;};
constexpr std::array<VisualCall,3> visual_calls{{
    // Main-frame and CG_InitView animation updates both reach the same mutator.
    {0x1c392d,update_view_weapon_anim_rva,5,"eab9e310cd3941ccff3e35cd0b91eb378a0932b48d435a936a66c02aaef9d150",VisualCallKind::update_animation},
    {0x29a069,update_view_weapon_anim_rva,5,"f1632835649ec66ef92f5ffb8edb63e2190c38c2c91629ce43c9d4e6d9e719b4",VisualCallKind::update_animation},
    {0x1c3b10,add_view_weapon_rva,5,"6a0570806f233a32b9a4f1e38c1d8e4e461d63dc1f422c3dcff4984ce288f5c5",VisualCallKind::add_weapon},
}};
std::array<std::array<unsigned char,5>,visual_calls.size()> visual_originals{};

struct DObjModelDescription final {void* model{};std::uint16_t bone_name{};bool ignore_collision{};std::uint8_t padding{};};
static_assert(sizeof(void*)==4&&sizeof(DObjModelDescription)==8);
using UpdateViewWeaponAnim=void(__cdecl*)(int,int);
using AddViewWeapon=void(__cdecl*)(int);
using DObjCreate=void(__cdecl*)(DObjModelDescription*,unsigned,void*,unsigned char*,short);
using AddDObj=void(__cdecl*)(const void*,const void*,unsigned,unsigned,float*,float,float,float,float,char,int,const void*,const void*,float,float);

struct alignas(16) Asset final {
    std::uintptr_t model{};
    alignas(16) std::array<std::byte,dobj_size> dobj{};
    alignas(16) std::array<std::byte,pose_size> pose{};
    std::uint64_t last_transaction{},last_generation{};
    std::uint32_t last_scene_slot=grenade_render_detail::no_scene_slot;
    std::uint16_t last_scene_index=grenade_render_detail::empty_scene_entry;
    bool ready{},failed{};
};
struct SceneTables final {std::uint16_t* xmodel{};std::uint16_t* dobj{};};

std::atomic<bool> renderer_enabled{};
std::uintptr_t image_base{},scene_xmodel_pointer{},scene_dobj_pointer{};
UpdateViewWeaponAnim native_update_view_weapon_anim{};AddViewWeapon native_add_view_weapon{};
DObjCreate dobj_create{};AddDObj add_dobj{};
std::array<Asset,grenade_render_detail::projectile_model_cache_capacity> assets{};std::mutex asset_gate;
std::atomic<unsigned long> render_calls{},render_failures{};
struct VisualFlagScope final {
    ManualGrenadePrimaryVisual identity{};
    volatile LONG* flags{};volatile LONG* primary_word{};
    LONG original{},masked{},primary_original{},primary_injected{};
    bool active{};
};
thread_local VisualFlagScope* active_visual_scope{};
std::atomic<unsigned long> visual_scope_calls{},visual_scope_restores{};
std::array<std::atomic<unsigned long>,3> animation_skips{};

bool allowed_protection(DWORD protection,bool writable) noexcept {
    const DWORD base=protection&0xff;
    if(writable)return base==PAGE_READWRITE||base==PAGE_WRITECOPY||base==PAGE_EXECUTE_READWRITE||base==PAGE_EXECUTE_WRITECOPY;
    return base==PAGE_READONLY||base==PAGE_READWRITE||base==PAGE_WRITECOPY||base==PAGE_EXECUTE||
        base==PAGE_EXECUTE_READ||base==PAGE_EXECUTE_READWRITE||base==PAGE_EXECUTE_WRITECOPY;
}
bool accessible(std::uintptr_t address,std::size_t size,bool writable=false) noexcept {
    if(!address||!size||address>(std::numeric_limits<std::uintptr_t>::max)()-size)return false;
    const auto end=address+size;auto cursor=address;
    while(cursor<end){
        MEMORY_BASIC_INFORMATION memory{};
        if(VirtualQuery(reinterpret_cast<const void*>(cursor),&memory,sizeof(memory))!=sizeof(memory)||
           memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!allowed_protection(memory.Protect,writable))return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if(memory.RegionSize>(std::numeric_limits<std::uintptr_t>::max)()-begin)return false;
        const std::uintptr_t region_end=begin+static_cast<std::uintptr_t>(memory.RegionSize);
        if(cursor<begin||cursor>=region_end)return false;cursor=end<region_end?end:region_end;
    }
    return true;
}
template<class T>bool copy_value(std::uintptr_t address,T* output,bool writable=false) noexcept {
    if(!output||!accessible(address,sizeof(T),writable))return false;
    std::memcpy(output,reinterpret_cast<const void*>(address),sizeof(T));return true;
}
bool current_scene_tables(SceneTables* output) noexcept {
    if(!output)return false;*output={};std::uintptr_t xmodel{},dobj{};
    if(!copy_value(scene_xmodel_pointer,&xmodel)||!copy_value(scene_dobj_pointer,&dobj)||
       !accessible(xmodel,scene_table_entries*sizeof(std::uint16_t),true)||
       !accessible(dobj,scene_table_entries*sizeof(std::uint16_t),true))return false;
    output->xmodel=reinterpret_cast<std::uint16_t*>(xmodel);output->dobj=reinterpret_cast<std::uint16_t*>(dobj);return true;
}
void render_failure(const char* reason,const ManualGrenadePose* ticket=nullptr) noexcept {
    const auto count=render_failures.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=6||count%120==0)trace("t5_manual_grenade_model_rejected","count=%lu reason=%s transaction=%llu weapon=%u model=%p",
        count,reason,ticket?ticket->transaction:0,ticket?ticket->weapon:0,reinterpret_cast<void*>(ticket?ticket->projectile_model:0));
}

bool restore_primary_word(volatile LONG* word,LONG original,LONG injected,bool* exact=nullptr) noexcept {
    if(exact)*exact=false;if(!word)return false;
    LONG observed=InterlockedCompareExchange(word,0,0);
    for(unsigned attempt=0;attempt<8;++attempt){
        if(!grenade_render_detail::low_word_is(static_cast<std::uint32_t>(observed),static_cast<unsigned>(injected)))return false;
        const LONG desired=static_cast<LONG>(grenade_render_detail::replace_low_word(
            static_cast<std::uint32_t>(observed),static_cast<unsigned>(original)));
        const LONG prior=InterlockedCompareExchange(word,desired,observed);
        if(prior==observed){if(exact)*exact=observed==injected;return true;}observed=prior;
    }
    return false;
}

bool begin_visual_scope(int local_client,VisualFlagScope* scope) noexcept {
    if(!scope||active_visual_scope)return false;*scope={};
    ManualGrenadePrimaryVisual identity{};
    if(!read_manual_grenade_primary_visual(local_client,&identity)||
       identity.ps>(std::numeric_limits<std::uintptr_t>::max)()-0x144)return false;
    auto* flags=reinterpret_cast<volatile LONG*>(identity.ps+0x10);
    auto* primary_word=reinterpret_cast<volatile LONG*>(identity.ps+0x144);
    const LONG original=static_cast<LONG>(identity.weap_flags),masked=original&~2L;
    const LONG primary_original=static_cast<LONG>(identity.primary_word);
    const unsigned original_weapon=static_cast<std::uint32_t>(primary_original)&0xffffU;
    if(identity.primary_weapon>=256U||(original_weapon!=0U&&original_weapon!=identity.primary_weapon)||
       !accessible(reinterpret_cast<std::uintptr_t>(flags),sizeof(LONG),true)||
       !accessible(reinterpret_cast<std::uintptr_t>(primary_word),sizeof(LONG),true))return false;
    const LONG primary_injected=static_cast<LONG>(grenade_render_detail::replace_low_word(
        static_cast<std::uint32_t>(primary_original),identity.primary_weapon));
    if(InterlockedCompareExchange(primary_word,primary_injected,primary_original)!=primary_original)return false;
    if(InterlockedCompareExchange(flags,masked,original)!=original){
        (void)restore_primary_word(primary_word,primary_original,primary_injected);return false;
    }
    if(!manual_grenade_primary_visual_current(identity,static_cast<std::uint32_t>(masked))){
        const LONG observed=InterlockedCompareExchange(flags,original,masked);if(observed!=masked)InterlockedOr(flags,2L);
        (void)restore_primary_word(primary_word,primary_original,primary_injected);return false;
    }
    scope->identity=identity;scope->flags=flags;scope->primary_word=primary_word;scope->original=original;scope->masked=masked;
    scope->primary_original=primary_original;scope->primary_injected=primary_injected;scope->active=true;
    active_visual_scope=scope;
    const auto count=visual_scope_calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=6||count%120==0)trace("t5_manual_grenade_visual_scope","phase=begin count=%lu transaction=%llu generation=%llu weapon=%u primary=%u state=%d original_flags=0x%x masked_flags=0x%x original_primary_word=0x%x injected_primary_word=0x%x transient_owned_bit=1",
        count,identity.transaction,identity.generation,identity.grenade_weapon,identity.primary_weapon,identity.weapon_state,
        static_cast<unsigned>(original),static_cast<unsigned>(masked),static_cast<unsigned>(primary_original),static_cast<unsigned>(primary_injected));
    return true;
}
void end_visual_scope(VisualFlagScope* scope) noexcept {
    if(!scope||!scope->active)return;
    if(active_visual_scope==scope)active_visual_scope=nullptr;
    const LONG observed=InterlockedCompareExchange(scope->flags,scope->original,scope->masked);
    const bool exact=observed==scope->masked;
    if(!exact)InterlockedOr(scope->flags,2L);
    bool primary_exact{};const bool primary_restored=restore_primary_word(
        scope->primary_word,scope->primary_original,scope->primary_injected,&primary_exact);
    scope->active=false;
    const auto count=visual_scope_restores.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=6||count%120==0)trace("t5_manual_grenade_visual_scope","phase=end count=%lu transaction=%llu exact_flag_restore=%u merged_owned_bit=%u primary_restored=%u exact_primary_restore=%u",
        count,scope->identity.transaction,exact,!exact,primary_restored,primary_exact);
}
__declspec(noinline) void __cdecl grenade_update_view_weapon_anim(int local_client,int new_player_state) {
    const DWORD previous=GetLastError();ManualGrenadePrimaryVisual identity{};
    if(new_player_state==0&&read_manual_grenade_primary_visual(local_client,&identity)){
        const auto return_address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto caller_rva=return_address>=image_base?return_address-image_base:0;
        const auto caller_index=caller_rva==visual_calls[0].rva+5?0U:caller_rva==visual_calls[1].rva+5?1U:2U;
        const auto count=animation_skips[caller_index].fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=6||count%120==0)trace("t5_manual_grenade_animation_frozen","caller_rva=0x%lx caller_count=%lu transaction=%llu generation=%llu weapon=%u primary=%u dobj=%p stock_belt_reach_suppressed=1",
            static_cast<unsigned long>(caller_rva),count,identity.transaction,identity.generation,identity.grenade_weapon,identity.primary_weapon,reinterpret_cast<void*>(identity.dobj));
        SetLastError(previous);return;
    }
    SetLastError(previous);native_update_view_weapon_anim(local_client,new_player_state);
}
void __cdecl grenade_add_view_weapon(int local_client) {
    const DWORD previous=GetLastError();VisualFlagScope scope{};
    const bool active=begin_visual_scope(local_client,&scope);
    __try {native_add_view_weapon(local_client);}
    __finally {if(active)end_visual_scope(&scope);SetLastError(previous);}
}
void* visual_wrapper(std::size_t index) noexcept {
    return visual_calls[index].kind==VisualCallKind::update_animation?
        reinterpret_cast<void*>(&grenade_update_view_weapon_anim):reinterpret_cast<void*>(&grenade_add_view_weapon);
}
bool verify_visual_calls(std::uintptr_t base) noexcept {
    for(std::size_t index=0;index<visual_calls.size();++index){const auto& call=visual_calls[index];
        const auto* site=reinterpret_cast<const unsigned char*>(base+call.rva);
        if(!native_guard::matches(site,call.size,call.sha256))return false;
        std::int32_t relative{};std::memcpy(&relative,site+1,sizeof(relative));
        if(reinterpret_cast<std::uintptr_t>(site+5)+relative!=base+call.target_rva)return false;
        const auto delta=reinterpret_cast<std::intptr_t>(visual_wrapper(index))-reinterpret_cast<std::intptr_t>(site+5);
        if(delta<(std::numeric_limits<std::int32_t>::min)()||delta>(std::numeric_limits<std::int32_t>::max)())return false;
    }
    return true;
}
// Installation runs at the existing graphics-factory seam. Suspend peer
// threads while changing the two neighboring CALL instructions so no peer can
// execute a partially written displacement. Handles are acquired first.
struct Peers final {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire(std::uintptr_t base) noexcept {
        const DWORD process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);bool ok=Thread32First(snapshot,&entry)!=FALSE;
        if(ok)do {
            if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){ok=false;break;}
            const HANDLE handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!handle){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;ok=false;break;}handles[count++]=handle;
        }while(Thread32Next(snapshot,&entry));
        if(ok&&GetLastError()!=ERROR_NO_MORE_FILES)ok=false;CloseHandle(snapshot);if(!ok)return false;
        for(std::size_t i=0;i<count;++i){
            if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&context))return false;
            for(const auto& call:visual_calls)if(context.Eip>=base+call.rva&&context.Eip<base+call.rva+call.size)return false;
        }
        return true;
    }
};
bool restore_visual_call(std::uintptr_t base,std::size_t index) noexcept {
    const auto& call=visual_calls[index];const auto& original=visual_originals[index];
    auto* site=reinterpret_cast<unsigned char*>(base+call.rva);DWORD old{},unused{};
    if(!VirtualProtect(site,call.size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(site,original.data(),call.size);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,call.size)!=FALSE;
    const bool restored=VirtualProtect(site,call.size,old,&unused)!=FALSE;
    return flushed&&restored&&!std::memcmp(site,original.data(),call.size);
}
bool patch_visual_calls(std::uintptr_t base) noexcept {
    if(!verify_visual_calls(base))return false;Peers peers;if(!peers.acquire(base)||!verify_visual_calls(base))return false;
    for(std::size_t index=0;index<visual_calls.size();++index)
        std::memcpy(visual_originals[index].data(),reinterpret_cast<const void*>(base+visual_calls[index].rva),visual_calls[index].size);
    std::size_t patched{};bool protection_restored=true;
    for(std::size_t index=0;index<visual_calls.size();++index){const auto& call=visual_calls[index];
        auto* site=reinterpret_cast<unsigned char*>(base+call.rva);
        const auto delta=reinterpret_cast<std::intptr_t>(visual_wrapper(index))-reinterpret_cast<std::intptr_t>(site+5);
        const auto relative=static_cast<std::int32_t>(delta);unsigned char bytes[5]{0xe8};std::memcpy(bytes+1,&relative,sizeof(relative));
        DWORD old{},unused{};
        if(!VirtualProtect(site,sizeof(bytes),PAGE_EXECUTE_READWRITE,&old))break;
        if(!native_guard::matches(site,call.size,call.sha256)){
            protection_restored=VirtualProtect(site,sizeof(bytes),old,&unused)!=FALSE&&protection_restored;break;
        }
        std::memcpy(site,bytes,sizeof(bytes));++patched;
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,sizeof(bytes))!=FALSE;
        const bool restored=VirtualProtect(site,sizeof(bytes),old,&unused)!=FALSE;protection_restored=protection_restored&&restored;
        if(!flushed||!restored||std::memcmp(site,bytes,sizeof(bytes)))break;
    }
    if(patched!=visual_calls.size()){
        bool rolled_back=true;while(patched)rolled_back=restore_visual_call(base,--patched)&&rolled_back;
        trace("t5_manual_grenade_visual_scopes_rejected","reason=patch_transaction rollback=%u",rolled_back);return false;
    }
    peers.resume();
    trace("t5_manual_grenade_visual_scopes_installed","sites=3 update_sites=2 update=%p add=%p protection_restored=%u selector_patches=0 gameplay_state_writes=transient_owned_bit exact_restore=1",
        native_update_view_weapon_anim,native_add_view_weapon,protection_restored);
    return true;
}

Asset* resolve_asset(const ManualGrenadePose& ticket) noexcept {
    if(!ticket.projectile_model||!accessible(ticket.projectile_model,xmodel_size))return nullptr;
    const auto slot=grenade_render_detail::claim_model_cache_slot(assets,ticket.projectile_model);
    if(slot==grenade_render_detail::no_model_cache_slot)return nullptr;
    auto& asset=assets[slot];
    if(asset.ready||asset.failed)return asset.ready&&!asset.failed?&asset:nullptr;
    DObjModelDescription description{reinterpret_cast<void*>(ticket.projectile_model),0,false,0};
    dobj_create(&description,1,nullptr,reinterpret_cast<unsigned char*>(asset.dobj.data()),0);
    std::uint32_t models{};std::memcpy(&models,asset.dobj.data()+dobj_models_offset,sizeof(models));
    std::uint32_t created_model{};
    const auto count=static_cast<unsigned char>(asset.dobj[dobj_num_models_offset]);
    asset.ready=count==1&&copy_value(models,&created_model)&&created_model==ticket.projectile_model;
    asset.failed=!asset.ready;
    if(asset.ready)trace("t5_manual_grenade_model_created","weapon=%u definition=%p model=%p dobj=%p persistent_lifetime=1",
        ticket.weapon,reinterpret_cast<void*>(ticket.definition),reinterpret_cast<void*>(ticket.projectile_model),asset.dobj.data());
    return asset.ready?&asset:nullptr;
}
}

bool configure_manual_grenade_renderer(HMODULE executable) noexcept {
    if(renderer_enabled.load(std::memory_order_acquire))return reinterpret_cast<std::uintptr_t>(executable)==image_base;
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    try {
        std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);
        if(!manual_grenade_native_guards::renderer_loaded(reinterpret_cast<const unsigned char*>(base))||
           !native_guard::matches(base,selector_guard)||!native_guard::matches(base,dobj_create_guard)||
           !native_guard::matches(base,add_dobj_guard))return false;
        image_base=base;native_update_view_weapon_anim=reinterpret_cast<UpdateViewWeaponAnim>(base+update_view_weapon_anim_rva);
        native_add_view_weapon=reinterpret_cast<AddViewWeapon>(base+add_view_weapon_rva);
        dobj_create=reinterpret_cast<DObjCreate>(base+dobj_create_rva);add_dobj=reinterpret_cast<AddDObj>(base+add_dobj_rva);
        scene_xmodel_pointer=base+scene_xmodel_pointer_rva;scene_dobj_pointer=base+scene_dobj_pointer_rva;
        if(!patch_visual_calls(base))return false;
        renderer_enabled.store(true,std::memory_order_release);
        trace("t5_manual_grenade_renderer_configured","dobj_create=%p add_dobj=%p leases=0x%x..0x%x cache=%u colored_fallback=0",
            dobj_create,add_dobj,first_scene_lease,last_scene_lease,static_cast<unsigned>(assets.size()));
        return true;
    }catch(...){return false;}
}

bool manual_grenade_standalone_renderer_enabled() noexcept {
    return renderer_enabled.load(std::memory_order_acquire);
}

bool manual_grenade_primary_visual_scope(const NativeWeaponContext& context) noexcept {
    auto* scope=active_visual_scope;
    if(!scope||!scope->active||context.native_thread!=GetCurrentThreadId()||context.local_client!=0||!context.draw_gun)return false;
    const auto& identity=scope->identity;
    return context.cg_identity==identity.cg&&context.viewmodel_identity==identity.viewmodel&&
        context.dobj_identity==identity.dobj&&context.weapon_index==identity.primary_weapon&&
        context.offhand==static_cast<int>(identity.grenade_weapon)&&context.weapon_state==identity.weapon_state&&
        manual_grenade_primary_visual_current(identity,static_cast<std::uint32_t>(scope->masked));
}

bool submit_manual_grenade_model(const NativeWeaponContext& context) noexcept {
    if(!renderer_enabled.load(std::memory_order_acquire))return false;
    ManualGrenadePose ticket{};
    if(!read_manual_grenade_render_pose(&ticket))return false;
    if(!context.valid||context.local_client||!context.draw_gun||context.native_thread!=GetCurrentThreadId()||
       context.cg_identity!=ticket.cg||context.native_time<ticket.native_start_time||
       !animation_detail::finite(context.stock_view.origin)){render_failure("producer_identity",&ticket);return false;}
    try {
        std::lock_guard lock(asset_gate);auto* asset=resolve_asset(ticket);
        if(!asset){render_failure("model_create",&ticket);return false;}
        SceneTables tables{};if(!current_scene_tables(&tables)){render_failure("scene_tables",&ticket);return false;}
        if(asset->last_transaction==ticket.transaction&&asset->last_generation==ticket.generation&&
           asset->last_scene_slot>=first_scene_lease&&asset->last_scene_slot<=last_scene_lease&&
           tables.xmodel[asset->last_scene_slot]==grenade_render_detail::empty_scene_entry&&
           tables.dobj[asset->last_scene_slot]==asset->last_scene_index){
            publish_manual_grenade_pose(ticket.transaction,ticket.generation);return true;
        }
        const std::span<const std::uint16_t> xmodel(tables.xmodel,scene_table_entries),dobj(tables.dobj,scene_table_entries);
        std::uint32_t lease{};
        if(!grenade_render_detail::find_scene_slot(xmodel,dobj,first_scene_lease,last_scene_lease,&lease)){
            render_failure("scene_lease",&ticket);return false;
        }
        float angles[3]{};if(!grenade_render_detail::axis_to_angles(ticket.hand.axis,angles)){
            render_failure("pose_axis",&ticket);return false;
        }
        std::memset(asset->pose.data(),0,asset->pose.size());
        asset->pose[pose_type_offset]=std::byte{0};asset->pose[pose_local_client_offset]=std::byte{0};
        const animation_detail::Vec origin{
            ticket.hand.origin.x+ticket.hand.axis.left.x*palm_inset,
            ticket.hand.origin.y+ticket.hand.axis.left.y*palm_inset,
            ticket.hand.origin.z+ticket.hand.axis.left.z*palm_inset};
        std::memcpy(asset->pose.data()+pose_origin_offset,&origin,sizeof(origin));
        std::memcpy(asset->pose.data()+pose_angles_offset,angles,sizeof(angles));
        float lighting[3]{context.stock_view.origin.x,context.stock_view.origin.y,context.stock_view.origin.z};
        add_dobj(asset->dobj.data(),asset->pose.data(),lease,render_flags,lighting,0,0,0,0,static_cast<char>(-1),-1,nullptr,nullptr,0,1);
        const auto scene_index=tables.dobj[lease];
        if(scene_index==grenade_render_detail::empty_scene_entry){render_failure("scene_submit",&ticket);return false;}
        asset->last_transaction=ticket.transaction;asset->last_generation=ticket.generation;
        asset->last_scene_slot=lease;asset->last_scene_index=scene_index;
        publish_manual_grenade_pose(ticket.transaction,ticket.generation);
        const auto count=render_calls.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=6||count%120==0)trace("t5_manual_grenade_model_submitted","call=%lu transaction=%llu weapon=%u model=%p scene_entity=0x%x scene_index=%u origin=%.3f,%.3f,%.3f angles=%.3f,%.3f,%.3f",
            count,ticket.transaction,ticket.weapon,reinterpret_cast<void*>(ticket.projectile_model),lease,scene_index,
            origin.x,origin.y,origin.z,angles[0],angles[1],angles[2]);
        return true;
    }catch(...){render_failure("exception",&ticket);return false;}
}
}
