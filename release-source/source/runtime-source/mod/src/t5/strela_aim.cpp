// SPDX-License-Identifier: GPL-3.0-only
#include "t5/strela_aim.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/native_read_recipe.hpp"
#include "t5/identity.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <intrin.h>
#include <atomic>

#if !defined(_M_IX86)
#error The native Strela target consumer requires MSVC x86.
#endif
namespace bovr::t5 {
namespace strela_aim_detail {
namespace s=strela;
using Native=void(__cdecl*)(std::uintptr_t,float*,float*,float*);
Native original=reinterpret_cast<Native>(0x5bebb0);
std::atomic<bool> ready{};
std::atomic<bool> configured{};
thread_local bool selecting{};
SRWLOCK install_lock=SRWLOCK_INIT;
std::array<std::uint8_t,5> owned_call{};
constexpr std::array<std::uint8_t,5> original_call{0xe8,0x74,0xbf,0xda,0xff};
bool copy(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {
    __try {std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
s::Host host{nullptr,copy};
bool tracked(const wawvr::xr::ActionSnapshot& actions) noexcept {
    const auto& aim=actions.hands[1].aim;
    if(!aim.active||!aim.orientation_valid||!aim.orientation_tracked)return false;
    for(const auto& hand:actions.hands)if(!hand.grip.active||!hand.grip.position_valid||
        !hand.grip.position_tracked||!hand.grip.orientation_valid||!hand.grip.orientation_tracked)return false;
    return true;
}
bool current_angles(std::uint32_t entref,wawvr::xr::Vec3f* out,unsigned* weapon,std::uint64_t* serial) noexcept {
    if(!out||!weapon||!serial)return false;
    // Each retry recopies the complete current owner and publication. It never
    // refreshes a cached pose's timestamp or borrows a renderer prediction.
    for(unsigned attempt=0;attempt<3;++attempt){
        PreCommandSnapshot pre{},after{};s::Owner first{},last{};CommandAimPublication p{};
        const auto now=GetTickCount64();
        if(!read_pre_command_snapshot(&pre)||!s::copy_owner(host,entref,GetCurrentThreadId(),pre,now,&first)||
           !read_latest_native_command_aim_publication(&p)||p.cg_identity!=first.cg||
           p.refdef_identity!=first.cg+0x8c100||p.command.bytes[0x18]!=first.weapon.weapon||
           p.command.gameplay_epoch!=pre.gameplay_epoch||!p.command.gameplay_active||!p.command.publication_serial||
           !p.command.rig.xr_generation||p.command.rig.xr_generation!=p.command.rig.anchor_generation||
           !p.command.rig.published_ms||now<p.command.rig.published_ms||now-p.command.rig.published_ms>100||
           !p.command.rig.frame.should_render||!p.command.rig.frame.views_valid||!p.command.rig.frame.actions.focused||
           (p.command.rig.frame.actions.menu.active&&p.command.rig.frame.actions.menu.current))return false;
        s::Axes axes{};
        if(!s::axes(p.aim.pitch_degrees,p.aim.yaw_degrees,&axes)||
           !std::isfinite(p.aim.forward.x)||!std::isfinite(p.aim.forward.y)||!std::isfinite(p.aim.forward.z)||
           std::fabs(axes.forward.x-p.aim.forward.x)>.002f||
           std::fabs(axes.forward.y-p.aim.forward.y)>.002f||
           std::fabs(axes.forward.z-p.aim.forward.z)>.002f||!command_input_generation_current(p.command.rig))return false;
        wawvr::xr::ActionSnapshot actions{};std::uint64_t generation{};input::CommandRigSample current{};
        if(!read_vr_command_input_snapshot(&actions,&generation,&current)||generation!=p.command.rig.xr_generation||
           !actions.focused||(actions.menu.active&&actions.menu.current)||!current.frame.views_valid||
           !current.frame.should_render||!actions.sequence||actions.sequence!=current.frame.actions.sequence||
           !tracked(actions)||!tracked(current.frame.actions)||!command_input_generation_current(current))return false;
        if(!read_pre_command_snapshot(&after)||after.sequence!=pre.sequence||after.gameplay_epoch!=pre.gameplay_epoch)continue;
        if(!s::copy_owner(host,entref,GetCurrentThreadId(),after,GetTickCount64(),&last))continue;
        if(first!=last)continue;
        const auto final_now=GetTickCount64();
        if(final_now<p.command.rig.published_ms||final_now-p.command.rig.published_ms>100||
           !command_input_generation_current(p.command.rig))return false;
        *out={p.aim.pitch_degrees,p.aim.yaw_degrees,0.f};*weapon=first.weapon.weapon;*serial=p.command.publication_serial;
        return true;
    }
    return false;
}
bool commit(const s::Call& call,const s::Axes& before,const s::Axes& after) noexcept {
    static_assert(sizeof(s::Axes)==36);
    __try {
        if(!s::call_valid(host,call)||std::memcmp(reinterpret_cast<const void*>(call.forward),&before,sizeof before))return false;
        std::memcpy(reinterpret_cast<void*>(call.forward),&after,sizeof after);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        // Exact writable stack locals are validated above. If a native fault
        // nevertheless interrupts the copy, retain the original native basis.
        __try {std::memcpy(reinterpret_cast<void*>(call.forward),&before,sizeof before);}
        __except(EXCEPTION_EXECUTE_HANDLER){}
        return false;
    }
}
void select(const s::Call& call) noexcept {
    NativeReadPreserve preserve;
    if(!ready.load(std::memory_order_acquire)||selecting||call.thread!=GetCurrentThreadId()||!s::call_valid(host,call))return;
    selecting=true;struct Exit{~Exit(){selecting=false;}} exit;
    s::Axes before{},after{};wawvr::xr::Vec3f angles{};unsigned weapon{};std::uint64_t serial{};
    const bool selected=copy(nullptr,call.forward,&before,sizeof before)&&
        current_angles(0,&angles,&weapon,&serial)&&s::axes(angles.x,angles.y,&after)&&commit(call,before,after);
    static std::atomic<unsigned> accepted{},refused{};const auto count=selected?++accepted:++refused;
    if(count<=12||count%240==0)trace(selected?"t5_strela_target_aim":"t5_strela_target_aim_refused",
        "count=%u weapon=%u serial=%llu pitch=%.4f yaw=%.4f output_bytes=%u native_target_radius_lock_occlusion=1",
        count,weapon,serial,angles.x,angles.y,selected?36u:0u);
}
// Native computation runs exactly once before the optional scoped replacement.
void invoke(const s::Call& call,float* forward,float* right,float* up) {
    original(call.entity,forward,right,up);select(call);
}
__declspec(noinline) void __cdecl bridge(std::uintptr_t entity,float* forward,float* right,float* up) {
    const auto stack=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    s::Call call{};
    {NativeReadPreserve preserve;ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        call={reinterpret_cast<std::uintptr_t>(_ReturnAddress()),entity,reinterpret_cast<std::uintptr_t>(forward),
            reinterpret_cast<std::uintptr_t>(right),reinterpret_cast<std::uintptr_t>(up),stack,low,high,GetCurrentThreadId()};}
    invoke(call,forward,right,up);
}
constexpr bovr::native_guard::Span guards[]{
    {0x812c20,0x119,"665aa9aff340f8c22758265f6db6a6612c6f885d36c9ada4b9a66d0e8984e2a6"},
    {0x812d40,0x1bd,"69cb073d9ea604ad40d29687e65b11f8dcae77b752630cee86b0ebbcacd9ada9"},
    {0x4c1100,0x70,"5a264f9f5251ec0d30d11eeada30dd40d7159f43317bcacd1da9c3fc7c5d3496"},
    {0x5bebb0,19,"b207578140bc695cffe07f0ec9ab1675d9d5e291c5dc53aef504345ff16a7077"},
    {0x4ae7f0,299,"9018f1167e0ab595cce7860a206df57d5eefc7a2767287b0b7a63bc4dd3d30be"}
};
bool guards_match(bool installed) noexcept {
    std::array<std::uint8_t,0x119> caller{};
    if(!copy(nullptr,0x812c20,caller.data(),caller.size()))return false;
    const auto& wanted=installed?owned_call:original_call;
    if(std::memcmp(caller.data()+0x17,wanted.data(),5))return false;
    std::memcpy(caller.data()+0x17,original_call.data(),5);
    if(!bovr::native_guard::matches(caller.data(),caller.size(),guards[0].sha256))return false;
    for(unsigned i=1;i<std::size(guards);++i)if(!bovr::native_guard::matches_absolute(guards[i]))return false;
    return true;
}
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire() noexcept {
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
            if(c.Eip>=0x812c37&&c.Eip<0x812c3c)return false;}
        return true;
    }
};
bool put_call(const std::array<std::uint8_t,5>& bytes) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(0x812c37),bytes.data(),bytes.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install() noexcept {
    if(ready.load(std::memory_order_acquire))return guards_match(true);
    if(!guards_match(false))return false;
    // Cryptographic validation runs before suspension: BCrypt may acquire
    // process locks. With peers stopped, only recheck the exact owned CALL.
    Peers peers;if(!peers.acquire())return false;
    std::array<std::uint8_t,5> before{};
    if(!copy(nullptr,0x812c37,before.data(),before.size())||before!=original_call)return false;
    auto patch=original_call;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bridge)-0x812c3c);
    std::memcpy(patch.data()+1,&displacement,4);
    auto* site=reinterpret_cast<void*>(0x812c37);DWORD previous{},ignored{};
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous))return false;
    owned_call=patch;const bool wrote=put_call(patch);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
    const bool restored=VirtualProtect(site,5,previous,&ignored)!=FALSE;
    std::array<std::uint8_t,5> check{};
    if(!wrote||!flushed||!restored||!copy(nullptr,0x812c37,check.data(),check.size())||check!=patch){
        DWORD rollback{};if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&rollback)){
            put_call(original_call);FlushInstructionCache(GetCurrentProcess(),site,5);VirtualProtect(site,5,previous,&ignored);}
        return false;
    }
    ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
bool strela_script_angles(std::uint32_t entref,wawvr::xr::Vec3f* out) noexcept {
    NativeReadPreserve preserve;using namespace strela_aim_detail;
    if(!out||!ready.load(std::memory_order_acquire)||selecting)return false;
    selecting=true;struct Exit{~Exit(){selecting=false;}} exit;
    unsigned weapon{};std::uint64_t serial{};
    if(!current_angles(entref,out,&weapon,&serial))return false;
    static std::atomic<unsigned> calls{};const auto count=++calls;
    if(count<=12||count%240==0)trace("t5_strela_script_aim","count=%u weapon=%u serial=%llu pitch=%.4f yaw=%.4f native_playerstate_writes=0",
        count,weapon,serial,out->x,out->y);
    return true;
}
bool configure_strela_aim(HMODULE image) noexcept {
    NativeReadPreserve preserve;
    if(!native_command_aim_enabled())return false;
    AcquireSRWLockExclusive(&strela_aim_detail::install_lock);bool okay{};
    try {std::string reason;okay=reinterpret_cast<std::uintptr_t>(image)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&strela_aim_detail::guards_match(false);}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&strela_aim_detail::install_lock);
    strela_aim_detail::configured.store(okay,std::memory_order_release);
    trace("t5_strela_aim_configured","accepted=%u call=00812c37 native=005bebb0 deferred_until_live_strela=1 startup_code_writes=0",okay);
    return okay;
}
bool ensure_strela_aim() noexcept {
    NativeReadPreserve preserve;using namespace strela_aim_detail;
    if(ready.load(std::memory_order_acquire))return true;
    if(!configured.load(std::memory_order_acquire))return false;
    // Recheck the exact live owner independently of the caller before any
    // executable-page write. Paused menus and unrelated weapons do not install.
    PreCommandSnapshot pre{};strela::Owner owner{};
    if(!read_pre_command_snapshot(&pre)||!strela::copy_owner(host,0,GetCurrentThreadId(),pre,GetTickCount64(),&owner))return false;
    AcquireSRWLockExclusive(&install_lock);bool okay{};
    try {okay=install();}catch(...){okay=false;}
    ReleaseSRWLockExclusive(&install_lock);
    static unsigned attempts{};
    if(++attempts<=4||okay)trace("t5_strela_aim_installed","accepted=%u attempt=%u owner=live_strela_input original_once=1 controller_aim=1 native_lock_rules_retained=1",okay,attempts);
    return okay;
}
}
