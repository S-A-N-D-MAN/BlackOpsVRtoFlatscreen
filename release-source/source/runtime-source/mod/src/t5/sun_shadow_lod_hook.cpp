// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/sun_shadow_lod_hook.hpp"
#include "t5/identity.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>

#if !defined(_M_IX86)
#error The pinned sun-shadow CALL bridge requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
#include "t5/sun_shadow_lod_guards.inc"
constexpr std::uintptr_t call0=0x2c7445,call1=0x2c7455,target_rva=0x329b10;
constexpr std::uintptr_t spot_call=0x3355d2,spot_target_rva=0x329ea0;
constexpr std::uintptr_t first=0x3740100,stride=0x186300,views_first=0x420f730,views_stride=0xe380;
std::uintptr_t hook_base{},native_target{},spot_native_target{};
std::array<std::uint8_t,0x15> owned_calls{},original_calls{};
std::array<std::uint8_t,5> owned_spot_call{},original_spot_call{};
std::atomic<bool> hook_ready{};bool diagnostic_enabled{};
bool spot_correction_enabled{true};
std::atomic<unsigned> diagnostic_count{};
std::atomic<unsigned> spot_diagnostic_count{};
SRWLOCK install_lock=SRWLOCK_INIT;
thread_local SunShadowLodScope* active_scope{};
template<class T>bool get(std::uintptr_t address,T* output) noexcept {
    __try {std::memcpy(output,reinterpret_cast<const void*>(address),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool put(std::uintptr_t address,const T& input) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(address),&input,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::uint32_t word(const std::uint8_t* p) noexcept {std::uint32_t v{};std::memcpy(&v,p,4);return v;}
bool finite_bits(std::uint32_t v) noexcept{return (v&0x7f800000u)!=0x7f800000u;}
bool positive_bits(std::uint32_t v) noexcept{return finite_bits(v)&&!(v&0x80000000u)&&(v&0x7fffffffu)!=0;}
bool owned_current(std::uintptr_t base) noexcept {
    std::array<std::uint8_t,0x15> bytes{};std::array<std::uint8_t,5> spot{};
    return hook_ready.load(std::memory_order_acquire)&&base==hook_base&&get(base+call0,&bytes)&&bytes==owned_calls&&
        get(base+spot_call,&spot)&&spot==owned_spot_call;
}
bool guards_match(std::uintptr_t base,bool owned) noexcept {
    std::array<std::uint8_t,calls_guard.size> calls{};
    std::array<std::uint8_t,spot_call_guard.size> spot{};
    if(!base||!get(base+calls_guard.address,&calls)||!get(base+spot_call_guard.address,&spot)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+target_rva),
           callee_guard.size,callee_guard.sha256)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+spot_target_rva),
           spot_callee_guard.size,spot_callee_guard.sha256)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+updater_localclient_guard.address),
           updater_localclient_guard.size,updater_localclient_guard.sha256))return false;
    if(owned){if(!owned_current(base))return false;std::memcpy(calls.data()+0xf,original_calls.data(),original_calls.size());
        std::memcpy(spot.data()+spot_call-spot_call_guard.address,original_spot_call.data(),original_spot_call.size());}
    return bovr::native_guard::matches(calls.data(),calls.size(),calls_guard.sha256)&&
        bovr::native_guard::matches(spot.data(),spot.size(),spot_call_guard.sha256);
}
bool frame_identity(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
                    std::uintptr_t view,std::uintptr_t refdef,bool built,unsigned view_slot=1,bool require_sun=true) noexcept {
    if(!base||base>(std::numeric_limits<std::uintptr_t>::max)()-0x4240000||
       (buffer!=base+first&&buffer!=base+first+stride))return false;
    const auto views=base+views_first+(buffer==base+first?0:views_stride);
    if((view_slot!=1&&view_slot!=3)||view!=views+view_slot*0x38e0)return false;
    std::uint32_t owner{},stamp{},count{},index{},array{},parms{},client{};
    if(!(get(base+0x373708c,&owner)&&owner==buffer&&get(buffer+0x15d7a0,&stamp)&&stamp==frame&&
       get(buffer+0x16cc3c,&count)&&count==(built?view_slot+1:view_slot)&&get(buffer+0x16cc38,&index)&&index==(built?view_slot:(view_slot==3?1u:0u))&&
       get(buffer+0x16cc40,&array)&&array==views&&get(buffer+0x16cbe0,&parms)&&parms==count&&
       get(refdef+0x183a4,&client)&&client==0))return false;
    if(!built)return true;
    std::uint32_t data{},view_client{},shadow{},world{},sun_index{},sun_count{},mask{};std::uint8_t extra{};
    if(!(get(view+0x3510,&data)&&data==buffer&&get(view+0x188,&view_client)&&view_client==0&&
        get(view+0x184,&shadow)&&shadow==1&&get(view+0x38c4,&extra)&&extra==0))return false;
    if(!require_sun)return true;
    return get(base+0x356a24c,&world)&&world&&get(world+0xfc,&sun_index)&&sun_index<256&&
        get(buffer+0x15d780+4*(sun_index/32),&mask)&&(mask&(1u<<(sun_index%32)))&&
        get(buffer+0x183fe0,&sun_count)&&sun_count==2;
}
bool fresh_lod(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
               std::uintptr_t view,std::uintptr_t refdef,unsigned view_slot,
               const std::array<std::uint32_t,3>& origin,bool require_sun,
               std::array<std::uint8_t,0x34>* output) noexcept {
    std::array<std::uint32_t,3> ref_origin{},view_origin{};std::array<std::uint8_t,0x34> lod{},again{};
    if(!frame_identity(base,buffer,frame,view,refdef,true,view_slot,require_sun)||
       !get(refdef+0x20,&ref_origin)||ref_origin!=origin||!get(view+0x100,&view_origin)||view_origin!=ref_origin||
       !get(base+0x356ecf8,&lod)||std::memcmp(lod.data(),ref_origin.data(),12)||
       !positive_bits(word(lod.data()+0xc))||!finite_bits(word(lod.data()+0x10))||
       !get(base+0x356ecf8,&again)||again!=lod||!frame_identity(base,buffer,frame,view,refdef,true,view_slot,require_sun))return false;
    *output=lod;return true;
}
// Installation-only peer quiescence. No allocation or native callback while
// peers are suspended, and neither patched instruction may be in flight.
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire(std::uintptr_t base) noexcept {
        const auto process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;bool okay=Thread32First(snapshot,&entry)!=FALSE;
        if(okay)do {if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){okay=false;break;}
            const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!handle){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;okay=false;break;}handles[count++]=handle;
        }while(Thread32Next(snapshot,&entry));
        if(okay&&GetLastError()!=ERROR_NO_MORE_FILES)okay=false;CloseHandle(snapshot);if(!okay)return false;
        for(std::size_t i=0;i<count;++i){if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c))return false;
            if((c.Eip>=base+call0&&c.Eip<base+call1+5)||(c.Eip>=base+spot_call&&c.Eip<base+spot_call+5))return false;}
        return true;
    }
};
// Entry stack is [native return][viewIndex][maxCount]. Callback preservation
// leaves it intact except for the intentionally redirected viewIndex word.
// The caller never reads this consumed word again; it discards both pairs at
// 0x6C745A. Tail JMP preserves the original native return/unwind stack shape.
__declspec(naked) void sun_shadow_lod_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        push dword ptr [ebp+24]
        push dword ptr [ebp+36]
        call select_sun_shadow_lod_index
        add esp,16
        mov dword ptr [ebp+40],eax
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [native_target]
    }
}
// Entry is [native return][viewIndex], ECX=primary light and EDX=spot slot.
// The complete pinned callee uses viewIndex only to select correctedLodParms;
// all visibility and draw-list ownership use the untouched light/slot inputs.
__declspec(naked) void spot_shadow_lod_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        push dword ptr [ebp+20]
        push dword ptr [ebp+40]
        push dword ptr [ebp+24]
        push dword ptr [ebp+36]
        call select_spot_shadow_lod_index
        add esp,16
        mov dword ptr [ebp+40],eax
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [spot_native_target]
    }
}
bool install(std::uintptr_t base) noexcept {
    if(hook_ready.load(std::memory_order_acquire))return guards_match(base,true);
    if(!guards_match(base,false))return false;
    Peers peers;if(!peers.acquire(base)||!guards_match(base,false))return false;
    std::array<std::uint8_t,0x15> original{};std::array<std::uint8_t,5> spot_original{};
    if(!get(base+call0,&original)||!get(base+spot_call,&spot_original))return false;
    auto patch=original;
    for(const auto offset:{0u,0x10u}){const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&sun_shadow_lod_bridge)-(base+call0+offset+5));std::memcpy(patch.data()+offset+1,&displacement,4);}
    auto spot_patch=spot_original;
    const auto spot_displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&spot_shadow_lod_bridge)-(base+spot_call+5));
    std::memcpy(spot_patch.data()+1,&spot_displacement,4);
    auto* site=reinterpret_cast<void*>(base+call0);auto* spot_site=reinterpret_cast<void*>(base+spot_call);DWORD previous{},spot_previous{},ignored{};
    if(!VirtualProtect(site,patch.size(),PAGE_EXECUTE_READWRITE,&previous))return false;
    if(!VirtualProtect(spot_site,spot_patch.size(),PAGE_EXECUTE_READWRITE,&spot_previous)){
        VirtualProtect(site,patch.size(),previous,&ignored);return false;}
    std::array<std::uint8_t,0x15> before{};std::array<std::uint8_t,5> spot_before{};
    if(!get(base+call0,&before)||before!=original||!get(base+spot_call,&spot_before)||spot_before!=spot_original){
        VirtualProtect(spot_site,spot_patch.size(),spot_previous,&ignored);VirtualProtect(site,patch.size(),previous,&ignored);return false;}
    // A reachable bridge always retains its native passthrough target, even
    // if rollback or protection restoration cannot subsequently be confirmed.
    hook_base=base;native_target=base+target_rva;spot_native_target=base+spot_target_rva;
    original_calls=original;owned_calls=patch;original_spot_call=spot_original;owned_spot_call=spot_patch;
    const bool wrote=put(base+call0,patch);
    const bool spot_wrote=put(base+spot_call,spot_patch);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,patch.size())!=FALSE;
    const bool spot_flushed=FlushInstructionCache(GetCurrentProcess(),spot_site,spot_patch.size())!=FALSE;
    const bool protected_again=VirtualProtect(site,patch.size(),previous,&ignored)!=FALSE;
    const bool spot_protected_again=VirtualProtect(spot_site,spot_patch.size(),spot_previous,&ignored)!=FALSE;
    if(!wrote||!spot_wrote||!flushed||!spot_flushed||!protected_again||!spot_protected_again||
       !get(base+call0,&before)||before!=patch||!get(base+spot_call,&spot_before)||spot_before!=spot_patch){
        DWORD rollback{};if(VirtualProtect(site,patch.size(),PAGE_EXECUTE_READWRITE,&rollback)){
            put(base+call0,original);FlushInstructionCache(GetCurrentProcess(),site,patch.size());VirtualProtect(site,patch.size(),previous,&ignored);}
        if(VirtualProtect(spot_site,spot_patch.size(),PAGE_EXECUTE_READWRITE,&rollback)){
            put(base+spot_call,spot_original);FlushInstructionCache(GetCurrentProcess(),spot_site,spot_patch.size());VirtualProtect(spot_site,spot_patch.size(),spot_previous,&ignored);}
        return false;
    }
    hook_ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
bool configure_sun_shadow_lod_hook(HMODULE executable) noexcept {
    NativeReadPreserve preserve;AcquireSRWLockExclusive(&install_lock);bool okay{};
    try{std::string reason;okay=validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&install(reinterpret_cast<std::uintptr_t>(executable));
        if(okay){wchar_t flag[8]{};diagnostic_enabled=GetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",flag,8)==1&&flag[0]==L'1';
            flag[0]=0;spot_correction_enabled=!(GetEnvironmentVariableW(L"BOVR_SPOT_SHADOW_LOD_CORRECTION",flag,8)==1&&flag[0]==L'0');}}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&install_lock);
    trace("t5_sun_lod_configured","accepted=%u calls=006c7445,006c7455 target=00729b10 change=index_only view1_preserved=1 native_slot_writes=0 native_calls_added=0",okay);
    trace("t5_spot_lod_configured","accepted=%u enabled=%u call=007355d2 target=00729ea0 change=index_only light_and_slot_preserved=1 native_slot_writes=0 native_calls_added=0",okay,spot_correction_enabled);return okay;
}
bool SunShadowLodScope::begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
                            std::uintptr_t refdef,std::uint64_t producer,unsigned view_slot) noexcept {
    NativeReadPreserve preserve;
    if(active_||active_scope){if(active_scope)active_scope->blocked_=true;return false;}
    const auto view=base+views_first+(buffer==base+first?0:views_stride)+view_slot*0x38e0;
    std::array<std::uint32_t,3> origin{};
    if(!guards_match(base,true)||!frame_identity(base,buffer,frame,view,refdef,false,view_slot)||!get(refdef+0x20,&origin))return false;
    for(auto value:origin)if(!finite_bits(value))return false;
    base_=base;buffer_=buffer;frame_=frame;refdef_=refdef;view_=view;producer_=producer;origin_=origin;
    thread_=GetCurrentThreadId();view_slot_=view_slot;substituted_=spot_substituted_=spot_seen_=spot_expected_=0;
    blocked_=false;active_=true;active_scope=this;return true;
}
unsigned select_sun_shadow_lod_index(std::uintptr_t return_address,unsigned partition,unsigned original_index,unsigned max_count) noexcept {
    NativeReadPreserve preserve;auto* s=active_scope;
    if(!s||!s->active_||s->blocked_||s->thread_!=GetCurrentThreadId())return original_index;
    if(partition>1||original_index!=s->view_slot_||max_count!=(partition?0x2000u:0x1000u)||
       return_address!=s->base_+(partition?call1:call0)+5||partition!=s->substituted_||!owned_current(s->base_)){
        s->blocked_=true;return original_index;}
    std::array<std::uint8_t,0x34> lod{};
    if(!fresh_lod(s->base_,s->buffer_,s->frame_,s->view_,s->refdef_,s->view_slot_,s->origin_,true,&lod)){
        s->blocked_=true;return original_index;}
    s->used_lod_=lod;++s->substituted_;return 0;
}
unsigned select_spot_shadow_lod_index(std::uintptr_t return_address,unsigned primary_light,
                                     unsigned original_index,unsigned spot_slot) noexcept {
    NativeReadPreserve preserve;auto* s=active_scope;
    if(!spot_correction_enabled||!s||!s->active_||s->blocked_||s->thread_!=GetCurrentThreadId())return original_index;
    std::array<std::uint8_t,0x34> lod{};
    std::uint32_t count{},light_count{},primary_count{},again_count{},again_primary{},mask{};
    std::array<std::uint8_t,4*0x1d0> spots{},again{};unsigned expected{};
    if(return_address!=s->base_+spot_call+5||original_index!=s->view_slot_||spot_slot>=4||!owned_current(s->base_)||
       !fresh_lod(s->base_,s->buffer_,s->frame_,s->view_,s->refdef_,s->view_slot_,s->origin_,false,&lod)||
       !get(s->buffer_+0x184390,&count)||!count||count>4||spot_slot>=count||
       !get(s->buffer_+0x183db0,&light_count)||light_count>255||
       !get(s->base_+0x203fb70,&primary_count)||!primary_count||primary_count>light_count||primary_light>=primary_count||
       !get(s->buffer_+0x1843a0,&spots)) {s->blocked_=true;return original_index;}
    for(unsigned slot=0;slot<count;++slot){const auto* record=spots.data()+slot*0x1d0;const auto light=record[0x180];
        if(light>=light_count||word(record+0x184)!=s->buffer_+0x16cf20+light*0x170){s->blocked_=true;return original_index;}
        if(light<primary_count)expected|=1u<<slot;
    }
    const auto bit=1u<<spot_slot;const auto* record=spots.data()+spot_slot*0x1d0;
    std::uint32_t light_slot{};std::uint8_t type{};
    if(record[0x180]!=primary_light||!(expected&bit)||(s->spot_seen_&bit)||
       (expected&(bit-1))!=s->spot_seen_||(s->spot_expected_&&s->spot_expected_!=expected)||
       !get(s->buffer_+0x16cf20+primary_light*0x170,&type)||(type!=2&&type!=3)||
       !get(s->buffer_+0x16cf20+primary_light*0x170+0x38,&light_slot)||light_slot!=spot_slot||
       !get(s->buffer_+0x15d780+4*(primary_light/32),&mask)||!(mask&(1u<<(primary_light%32)))||
       !get(s->buffer_+0x184390,&again_count)||again_count!=count||
       !get(s->base_+0x203fb70,&again_primary)||again_primary!=primary_count||
       !get(s->buffer_+0x1843a0,&again)||again!=spots||
       !frame_identity(s->base_,s->buffer_,s->frame_,s->view_,s->refdef_,true,s->view_slot_,false)){
        s->blocked_=true;return original_index;}
    s->used_lod_=lod;s->spot_expected_=expected;s->spot_seen_|=bit;++s->spot_substituted_;return 0;
}
void SunShadowLodScope::finish() noexcept {
    if(!active_)return;NativeReadPreserve preserve;
    if(GetCurrentThreadId()!=thread_){blocked_=true;return;}
    if(active_scope==this)active_scope=nullptr;else blocked_=true;
    active_=false;
    if(diagnostic_enabled&&substituted_){const auto n=diagnostic_count.fetch_add(1,std::memory_order_relaxed);
        if(n<3){std::array<float,3> origin{};std::memcpy(origin.data(),used_lod_.data(),12);float scale{};std::memcpy(&scale,used_lod_.data()+0xc,4);
            trace("t5_sun_lod_redirect","sample=%u producer=%llu frame=%u buffer=0x%lx eye=1 local_client=0 original_index=1 selected_index=0 substituted=%u complete=%u native_calls_added=0 native_slot_writes=0 camera_origin=%.9g,%.9g,%.9g selected_rigid_scale=%.9g witness=bridge_selected_consumed_argument",
                n+1,producer_,frame_,static_cast<unsigned long>(buffer_),substituted_,!blocked_&&substituted_==2,double(origin[0]),double(origin[1]),double(origin[2]),double(scale));}}
    if(diagnostic_enabled&&spot_substituted_){const auto n=spot_diagnostic_count.fetch_add(1,std::memory_order_relaxed);
        if(n<3)trace("t5_spot_lod_redirect","sample=%u producer=%llu frame=%u buffer=0x%lx view_slot=%u local_client=0 original_index=%u selected_index=0 substituted=%u seen_mask=0x%x expected_mask=0x%x complete=%u native_calls_added=0 native_slot_writes=0 witness=bridge_selected_consumed_argument",
            n+1,producer_,frame_,static_cast<unsigned long>(buffer_),view_slot_,view_slot_,spot_substituted_,spot_seen_,spot_expected_,!blocked_&&spot_seen_==spot_expected_);}
}
void invoke_sun_shadow_lod_native(SunShadowLodScope* scope,void(*body)(void*),void* context) {
    __try{body(context);}__finally{if(scope){if(AbnormalTermination())scope->blocked_=true;scope->finish();}}
}
}
