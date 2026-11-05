// SPDX-License-Identifier: GPL-3.0-only
#include "t5/dual_pistol_effects.hpp"
#if !defined(BOVR_TEST_DUAL_EFFECTS_ABI)
#include "t5/dual_pistol_shot.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/identity.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#endif
#include <atomic>
#include <cstring>
#if !defined(_M_IX86)
#error Pinned dual pistol client effects require MSVC x86.
#endif
namespace bovr::t5 {
namespace {
void* original_angles{};
bool installed{};
#if !defined(BOVR_TEST_DUAL_EFFECTS_ABI)
bool prepared{};
std::array<unsigned char,0x286> prepared_caller{};
std::array<unsigned char,0x22> prepared_dispatch{};
std::array<unsigned char,16> prepared_native{};
constexpr unsigned char original_call[]{0xe8,0xc6,0x36,0xe2,0xff};
template<class T> T load(std::uintptr_t p) noexcept {T v{};memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return v;}
struct Owner {
    dual_pistol_effects::CallLayout call{};
    std::uintptr_t frame{},cg{},ps{},cent{},next_snap{},variant{},definition{},partner_variant{},partner_definition{};
    unsigned primary{},partner{},event_weapon{},event{},bone{};
    bool dual{};
    int command_time{};
    primary_weapon_tracking::Player player{};
    primary_weapon_tracking::View view{};
    NativeBulletGeometry original{};
    float angles[3]{},tracer[3]{};
};
bool owner(std::uintptr_t entry,Owner* out) noexcept {
    if(!entry||!out)return false;
    ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
    if(entry<low||entry>high-20)return false;
    __try {
        Owner o{};o.call={entry,load<std::uintptr_t>(entry),load<std::uintptr_t>(entry+4),
            load<std::uintptr_t>(entry+8),load<std::uintptr_t>(entry+12),load<std::uintptr_t>(entry+16)};
        if(!dual_pistol_effects::regular_local_layout(o.call,low,high))return false;
        o.frame=entry+20;
        if(load<unsigned>(o.frame+0x154)||load<unsigned>(o.frame+0x16c)!=1)return false;
        o.cent=load<std::uintptr_t>(o.frame+0x158);o.bone=load<unsigned short>(o.frame+0x15c);
        o.ps=load<std::uintptr_t>(o.frame+0x160);o.event_weapon=load<unsigned>(o.frame+0x164);
        o.event=load<unsigned>(o.frame+0x168);o.cg=load<std::uintptr_t>(0x2ff5354);
        if(o.cg<0x10000||o.cg>UINT32_MAX-0xcc844||o.ps!=o.cg+0x8a364||o.cent<0x10000||
           o.cent>UINT32_MAX-0x300||!o.bone||dual_pistol_shot::hand(o.event)<0)return false;
        o.player={load<int>(o.ps+4),load<int>(o.ps+0x1c4),load<unsigned>(o.ps+0xc),
            load<unsigned>(o.ps+0x10),load<unsigned>(o.ps+0xe0),load<unsigned>(o.ps+0xe4)};
        o.view={load<int>(o.cg+0x8a360),load<int>(0x2910160),load<int>(0x2910164),
            load<int>(0x2911e2c)!=0,load<int>(o.cg+0xc)!=0,load<int>(o.cg+0xa9d44)!=0};
        if(primary_weapon_tracking::client_mode(o.player,o.view)!=primary_weapon_tracking::Mode::ordinary||
           load<unsigned char>(o.ps+0x130)||load<int>(o.cent+0x164)!=0)return false;
        o.next_snap=load<std::uintptr_t>(o.cg+0x34);
        if(o.next_snap<0x10000||o.next_snap>UINT32_MAX-0x140||!(load<unsigned>(o.next_snap+0x20)&6)||
           load<unsigned char>(o.next_snap+0x13c))return false;
        o.primary=load<unsigned char>(o.ps+0x144);o.command_time=load<int>(o.ps);
        if(!o.primary||o.command_time<0)return false;
        o.variant=load<std::uintptr_t>(0xbe19a8+4*o.primary);
        if(o.variant<0x10000||o.variant>UINT32_MAX-0x50)return false;
        o.definition=load<std::uintptr_t>(o.variant+8);
        if(o.definition<0x10000||o.definition>UINT32_MAX-0x598||load<int>(o.definition+0x1c)!=0)return false;
        const auto kind=load<int>(o.definition+0x20);const auto dual=load<unsigned char>(o.definition+0x56a);
        if(dual>1)return false;o.dual=dual==1;
        if(o.dual){
        if(kind!=4)return false;
        o.partner=load<unsigned>(o.definition+0x594);
        if(!dual_pistol_effects::event_weapon(o.event,o.event_weapon,o.primary,o.partner))return false;
        o.partner_variant=load<std::uintptr_t>(0xbe19a8+4*o.partner);
        if(o.partner_variant<0x10000||o.partner_variant>UINT32_MAX-0x50)return false;
        o.partner_definition=load<std::uintptr_t>(o.partner_variant+8);
        if(o.partner_definition<0x10000||o.partner_definition>UINT32_MAX-0x598||
           load<int>(o.partner_definition+0x1c)!=0||load<int>(o.partner_definition+0x20)!=4||
           load<unsigned char>(o.partner_definition+0x56a)!=1||
           load<unsigned>(o.partner_definition+0x594)!=o.primary)return false;
        }else if(dual_pistol_effects::ordinary_mode()==dual_pistol_effects::OrdinaryMode::disabled||
                 !dual_pistol_effects::ordinary_weapon(0,kind,load<int>(o.definition+0x30),dual)||
                 !dual_pistol_effects::ordinary_event(o.event,o.event_weapon,o.primary))return false;
        memcpy(o.original.forward,reinterpret_cast<void*>(o.call.forward),12);
        memcpy(o.original.right,reinterpret_cast<void*>(o.call.right),12);
        memcpy(o.original.up,reinterpret_cast<void*>(o.call.up),12);
        memcpy(o.original.muzzle,reinterpret_cast<void*>(o.frame+0x2c),12);
        memcpy(o.angles,reinterpret_cast<void*>(o.call.angles),12);
        memcpy(o.tracer,reinterpret_cast<void*>(o.frame+0x14),12);
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool same_owner(const Owner& a,const Owner& b) noexcept {
    return a.call.entry==b.call.entry&&a.call.return_address==b.call.return_address&&a.call.angles==b.call.angles&&
        a.call.forward==b.call.forward&&a.call.right==b.call.right&&a.call.up==b.call.up&&a.frame==b.frame&&
        a.cg==b.cg&&a.ps==b.ps&&a.cent==b.cent&&a.next_snap==b.next_snap&&a.variant==b.variant&&
        a.definition==b.definition&&a.partner_variant==b.partner_variant&&a.partner_definition==b.partner_definition&&
        a.primary==b.primary&&a.partner==b.partner&&a.event_weapon==b.event_weapon&&a.event==b.event&&
        a.bone==b.bone&&a.dual==b.dual&&a.command_time==b.command_time&&a.player==b.player&&a.view==b.view&&
        !memcmp(&a.original,&b.original,sizeof(a.original))&&!memcmp(a.angles,b.angles,sizeof(a.angles))&&
        !memcmp(a.tracer,b.tracer,sizeof(a.tracer));
}
void select_effects(std::uintptr_t entry) noexcept {
    Owner first{},second{};CommandAimPublication aim{},again{};NativeBulletGeometry selected{};EffectAimDiagnostic diagnostic{};
    if(!exact_profile_aim_enabled()||!owner(entry,&first))return;
    const auto failed=[&](unsigned phase) {
        static std::array<std::atomic<unsigned>,5> counts{};const auto count=++counts[phase];
        if(count<=64||count%120==0)trace(first.dual?"t5_dual_pistol_effects_rejected":"t5_hitscan_effects_rejected",
            "phase=%u count=%u event=%u hand=%d weapon=%u partner=%u command_time=%d serial=%llu mode=%u matches=%u lookup_rejection=0x%x latest_command_time=%d command_age_ms=%llu pose_age_ms=%llu original=%.7f,%.7f,%.7f origin=%.4f,%.4f,%.4f tracer=%.4f,%.4f,%.4f writes=0",
            phase,count,first.event,dual_pistol_shot::hand(first.event),first.primary,first.partner,
            first.command_time,aim.command.publication_serial,unsigned(dual_pistol_effects::ordinary_mode()),diagnostic.matches,
            diagnostic.rejection,diagnostic.latest_command_time,diagnostic.command_age_ms,diagnostic.pose_age_ms,
            first.original.forward[0],first.original.forward[1],first.original.forward[2],
            first.original.muzzle[0],first.original.muzzle[1],first.original.muzzle[2],first.tracer[0],first.tracer[1],first.tracer[2]);
    };
    if(!read_native_command_aim_for_effect(first.command_time,first.cg,first.primary,&aim,&diagnostic)||
       aim.dual_wield!=first.dual||aim.weapon!=first.primary||aim.dual_weapon!=first.partner||
       aim.variant_identity!=first.variant||aim.definition_identity!=first.definition||
       aim.dual_variant_identity!=first.partner_variant||aim.dual_definition_identity!=first.partner_definition){failed(1);return;}
    std::uint32_t packed{};memcpy(&packed,aim.command.bytes.data()+0x20,4);
    const bool proposed=first.dual?dual_pistol_shot::geometry(aim.command.rig,aim.command.rig.frame.actions,aim.aim.body,
        first.event,GetTickCount64(),first.original,&selected):
        dual_pistol_effects::command_geometry(packed,first.original,&selected);
    if(!proposed){failed(2);return;}
    if(!owner(entry,&second)||!same_owner(first,second)||
       !read_native_command_aim_for_effect(first.command_time,first.cg,first.primary,&again)||
       again.command.publication_serial!=aim.command.publication_serial||
       again.invalidation_revision!=aim.invalidation_revision){failed(3);return;}
    // These three caller-owned arrays are the only changed bytes. Native
    // CG_BulletEndpos keeps its spread/seed; native tracing keeps origin and
    // the actual firing pistol's flash-bone tracer start. No cg angle writes.
    const bool correct=first.dual||dual_pistol_effects::ordinary_mode()==dual_pistol_effects::OrdinaryMode::correct;
    if(correct)__try {
        dual_pistol_effects::apply_basis(dual_pistol_effects::ordinary_mode(),first.dual,selected,
            reinterpret_cast<float*>(first.call.forward),reinterpret_cast<float*>(first.call.right),reinterpret_cast<float*>(first.call.up));
    }__except(EXCEPTION_EXECUTE_HANDLER){failed(4);return;}
    static std::atomic<unsigned> count{};const auto n=++count;
    if(n<=64||n%120==0)trace(first.dual?"t5_dual_pistol_effects":"t5_hitscan_effects",
        "count=%u event=%u hand=%d weapon=%u primary=%u partner=%u command_time=%d serial=%llu xr_sequence=%llu forward=%.7f,%.7f,%.7f original=%.7f,%.7f,%.7f origin=%.4f,%.4f,%.4f tracer=%.4f,%.4f,%.4f mode=%u packed=0x%08x writes=%u cg_writes=0 native_calls_added=0",
        n,first.event,dual_pistol_shot::hand(first.event),first.event_weapon,first.primary,first.partner,
        first.command_time,aim.command.publication_serial,aim.command.rig.frame.actions.sequence,
        selected.forward[0],selected.forward[1],selected.forward[2],first.original.forward[0],first.original.forward[1],
        first.original.forward[2],first.original.muzzle[0],first.original.muzzle[1],first.original.muzzle[2],
        first.tracer[0],first.tracer[1],first.tracer[2],unsigned(dual_pistol_effects::ordinary_mode()),packed,correct?36u:0u);
}
#else
void (__cdecl* test_after)(std::uint32_t*){};
#endif
void __cdecl after_angles(std::uint32_t* saved) noexcept {
    const auto error=GetLastError();
#if defined(BOVR_TEST_DUAL_EFFECTS_ABI)
    if(test_after)test_after(saved);
#else
    select_effects(reinterpret_cast<std::uintptr_t>(saved)+36);
#endif
    SetLastError(error);
}
__declspec(naked) void effects_angles() {
    __asm {
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        call dword ptr [original_angles]
        lea esp,[esp+16]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call after_angles
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
}
#if defined(BOVR_TEST_DUAL_EFFECTS_ABI)
void* dual_pistol_effects_test_entry(void* native,void (__cdecl* after)(std::uint32_t*)) noexcept {
    original_angles=native;test_after=after;return reinterpret_cast<void*>(&effects_angles);
}
#else
bool prepare_dual_pistol_effects(HMODULE executable) noexcept {
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    if(installed)return true;
    prepared=false;
    try {
        std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        // Pinned complete local setup and its normal-branch backwards jump,
        // plus the actual CG_FireWeapon caller and AngleVectors entry.
        if(native_guard::compare(reinterpret_cast<void*>(0x799a90),0x286,
           "7bba148c68b5600be8ab20c6a9aaa03a26f6a4f32c97b5987c826c4a2320884e")||
           native_guard::compare(reinterpret_cast<void*>(0x65639e),0x22,
           "e4d937778d4aa8b59d79cc9244cef1b04f23e5637842254f13963f39df769ef6")||
           native_guard::compare(reinterpret_cast<void*>(0x5bd300),16,
           "099055c4ad8447557f099c8f0b00e0330ca80d546a809d0ce96e7e7db572da21"))return false;
        memcpy(prepared_caller.data(),reinterpret_cast<const void*>(0x799a90),prepared_caller.size());
        memcpy(prepared_dispatch.data(),reinterpret_cast<const void*>(0x65639e),prepared_dispatch.size());
        memcpy(prepared_native.data(),reinterpret_cast<const void*>(0x5bd300),prepared_native.size());
        // Hash the retained copies too: commit must compare the exact bytes
        // that passed validation, never a newer unvalidated native snapshot.
        prepared=native_guard::matches(prepared_caller.data(),prepared_caller.size(),
            "7bba148c68b5600be8ab20c6a9aaa03a26f6a4f32c97b5987c826c4a2320884e")&&
            native_guard::matches(prepared_dispatch.data(),prepared_dispatch.size(),
            "e4d937778d4aa8b59d79cc9244cef1b04f23e5637842254f13963f39df769ef6")&&
            native_guard::matches(prepared_native.data(),prepared_native.size(),
            "099055c4ad8447557f099c8f0b00e0330ca80d546a809d0ce96e7e7db572da21");
        return prepared;
    }catch(...){return false;}
}
bool commit_dual_pistol_effects() noexcept {
    if(installed)return true;
    if(!prepared)return false;
    // Called with peer threads suspended. No logging, allocation, hashing,
    // file work, or mod locks may be introduced inside this section.
    __try {
        auto* site=reinterpret_cast<unsigned char*>(0x799c35);
        if(memcmp(reinterpret_cast<const void*>(0x799a90),prepared_caller.data(),prepared_caller.size())||
           memcmp(reinterpret_cast<const void*>(0x65639e),prepared_dispatch.data(),prepared_dispatch.size())||
           memcmp(reinterpret_cast<const void*>(0x5bd300),prepared_native.data(),prepared_native.size())||
           memcmp(site,original_call,5))return false;
        DWORD protection{},old{};if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        if(memcmp(site,original_call,5)){VirtualProtect(site,5,protection,&old);return false;}
        original_angles=reinterpret_cast<void*>(0x5bd300);
        const auto offset=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&effects_angles)-reinterpret_cast<std::uintptr_t>(site+5));
        memcpy(site+1,&offset,4);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
        const bool restored=VirtualProtect(site,5,protection,&old)!=FALSE;
        if(!flushed||!restored){
            DWORD writable{},ignored{};
            if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&writable)){
                memcpy(site,original_call,5);FlushInstructionCache(GetCurrentProcess(),site,5);
                VirtualProtect(site,5,protection,&ignored);
            }
            return false;
        }
        installed=true;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install_dual_pistol_effects(HMODULE executable) noexcept {
    const bool okay=prepare_dual_pistol_effects(executable)&&commit_dual_pistol_effects();
    trace("t5_dual_pistol_effects_hook","installed=%u call=00799c35 target=005bd300 stack_basis_only=1",okay);
    return okay;
}
#endif
}
