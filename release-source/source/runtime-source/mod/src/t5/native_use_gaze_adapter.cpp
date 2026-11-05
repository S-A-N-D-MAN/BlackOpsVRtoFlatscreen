// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_use_gaze_adapter.hpp"
#include "t5/native_use_gaze_context.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/native_read_recipe.hpp"
#include "t5/identity.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <intrin.h>

#if !defined(_M_IX86)
#error The native use gaze consumer requires MSVC x86.
#endif
namespace bovr::t5 {
namespace native_use_gaze_detail {
namespace n=native_use_gaze;
using Native=void(__cdecl*)(std::uintptr_t,float*,float*,float*);
Native original=reinterpret_cast<Native>(0x5bebb0);
std::atomic<bool> ready{};
thread_local bool selecting{};
SRWLOCK install_lock=SRWLOCK_INIT;
SRWLOCK scene_lock=SRWLOCK_INIT;
struct Scene {
    input::CommandRigSample rig{};wawvr::xr::EnginePose stock{};
    std::uintptr_t cg{},refdef{};std::uint64_t epoch{},sequence{};int native_time{};
};
Scene scene{};
Scene scene_copy() noexcept {AcquireSRWLockShared(&scene_lock);const auto result=scene;ReleaseSRWLockShared(&scene_lock);return result;}
std::array<std::uint8_t,5> owned_call{};
constexpr std::array<std::uint8_t,5> original_call{0xe8,0x7e,0x6d,0xda,0xff};
bool copy(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {
    __try {std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
n::Host host{nullptr,copy};
bool commit(std::uintptr_t address,const wawvr::xr::Vec3f& expected,const wawvr::xr::Vec3f& value) noexcept {
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(address),&expected,sizeof expected))return false;
        std::memcpy(reinterpret_cast<void*>(address),&value,sizeof value);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void select(const n::Call& call) noexcept {
    NativeReadPreserve preserve;
    if(!ready.load(std::memory_order_acquire)||selecting)return;
    selecting=true;struct Exit{~Exit(){selecting=false;}} exit;
    PreCommandSnapshot pre{},after{};n::Owner first{},last{};
    const auto view=scene_copy();const auto& rig=view.rig;const auto generation=rig.xr_generation;
    wawvr::xr::ActionSnapshot current_actions{};std::uint64_t current_generation{};input::CommandRigSample current_rig{};
    wawvr::xr::Vec3f before{},forward{};const auto now=GetTickCount64();
    const bool selected=read_pre_command_snapshot(&pre)&&
        n::copy_owner(host,call,pre,now,&first)&&n::get(host,call.forward,before)&&
        input::helicopter_head_detail::finite(before)&&
        view.cg==first.cg&&view.refdef==first.cg+0x8c100&&view.epoch==pre.gameplay_epoch&&
        view.sequence&&view.sequence<=pre.sequence&&view.native_time>0&&
        first.scene_time>=view.native_time&&first.scene_time-view.native_time<=100&&
        read_vr_command_input_snapshot(&current_actions,&current_generation,&current_rig)&&current_generation==generation&&
        current_rig.xr_generation==generation&&current_rig.frame.views_valid&&current_rig.frame.should_render&&
        current_actions.focused&&!(current_actions.menu.active&&current_actions.menu.current)&&
        input::native_use_gaze(rig,view.stock,now,&forward)&&command_input_generation_current(rig)&&
        read_pre_command_snapshot(&after)&&n::same_pre(pre,after)&&
        n::copy_owner(host,call,after,GetTickCount64(),&last)&&n::same_owner(first,last)&&
        command_input_generation_current(rig)&&commit(call.forward,before,forward);
    static std::atomic<unsigned> accepted{},refused{};
    const auto count=selected?++accepted:++refused;
    if(count<=8||count%240==0)trace(selected?"t5_native_use_gaze":"t5_native_use_gaze_refused",
        "count=%u entity=%p thread=%lu pre=%llu generation=%llu xr_frame=%llu forward=%.5f,%.5f,%.5f native_before=%.5f,%.5f,%.5f output_writes=%u origin_writes=0 viewangle_writes=0 weapon_writes=0",
        count,reinterpret_cast<void*>(call.entity),call.thread,pre.sequence,generation,rig.frame.frame_id,
        forward.x,forward.y,forward.z,before.x,before.y,before.z,selected?12u:0u);
}
// Only the pinned Player_GetUseList call reaches this wrapper. Calling the
// original first retains native outputs on every refusal and runs it once.
void invoke(const n::Call& call,float* forward,float* right,float* up) noexcept {
    original(call.entity,forward,right,up);
    select(call);
}
__declspec(noinline) void __cdecl bridge(std::uintptr_t entity,float* forward,float* right,float* up) {
    const auto stack=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    n::Call call{};
    {NativeReadPreserve preserve;ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        call={reinterpret_cast<std::uintptr_t>(_ReturnAddress()),entity,
            reinterpret_cast<std::uintptr_t>(forward),reinterpret_cast<std::uintptr_t>(right),
            reinterpret_cast<std::uintptr_t>(up),stack,low,high,GetCurrentThreadId()};}
    invoke(call,forward,right,up);
}
constexpr bovr::native_guard::Span guards[]{
    {0x817db0,130,"7c4893a3b3932fb0e4bb4dc2bc0fe49e1b2be6ed412785953ff709cfb808a8bc"},
    {0x5bebb0,19,"b207578140bc695cffe07f0ec9ab1675d9d5e291c5dc53aef504345ff16a7077"},
    {0x4ae7f0,299,"9018f1167e0ab595cce7860a206df57d5eefc7a2767287b0b7a63bc4dd3d30be"},
    {0x81830a,94,"a4672e0acb841dcd87096376abe2a2ac5d6be27eabe85bfc0ea4f6905bc42df2"},
    {0x8184a5,310,"596c4c211f774ef1da39eee601f84c7e64e3a7cbf5fe4ef4fe133eb79dfc67a8"}
};
bool guards_match(bool installed) noexcept {
    std::array<std::uint8_t,130> caller{};
    if(!copy(nullptr,0x817db0,caller.data(),caller.size()))return false;
    const auto& wanted=installed?owned_call:original_call;
    if(std::memcmp(caller.data()+125,wanted.data(),5))return false;
    std::memcpy(caller.data()+125,original_call.data(),5);
    if(!bovr::native_guard::matches(caller.data(),caller.size(),guards[0].sha256))return false;
    for(unsigned i=1;i<std::size(guards);++i)if(!bovr::native_guard::matches_absolute(guards[i]))return false;
    return true;
}
// Same bounded startup peer protocol as the existing helicopter call hook.
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
            if(c.Eip>=0x817e2d&&c.Eip<0x817e32)return false;}
        return true;
    }
};
bool put_call(const std::array<std::uint8_t,5>& bytes) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(0x817e2d),bytes.data(),bytes.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install() noexcept {
    if(ready.load(std::memory_order_acquire))return guards_match(true);
    if(!guards_match(false))return false;
    Peers peers;if(!peers.acquire()||!guards_match(false))return false;
    auto patch=original_call;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bridge)-0x817e32);
    std::memcpy(patch.data()+1,&displacement,4);
    auto* site=reinterpret_cast<void*>(0x817e2d);DWORD previous{},ignored{};
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous))return false;
    owned_call=patch;
    const bool wrote=put_call(patch);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
    const bool restored=VirtualProtect(site,5,previous,&ignored)!=FALSE;
    std::array<std::uint8_t,5> check{};
    if(!wrote||!flushed||!restored||!copy(nullptr,0x817e2d,check.data(),check.size())||check!=patch){
        DWORD rollback{};if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&rollback)){
            put_call(original_call);FlushInstructionCache(GetCurrentProcess(),site,5);VirtualProtect(site,5,previous,&ignored);}
        return false;
    }
    ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
void publish_native_use_gaze_scene(const input::CommandRigSample& rig,const wawvr::xr::Vec3f& origin,
    const wawvr::xr::Basis3f& axis,const PreCommandSnapshot& pre,std::uintptr_t refdef) noexcept {
    using namespace native_use_gaze_detail;
    Scene copy{};
    if(ready.load(std::memory_order_acquire)&&pre.gameplay_active&&pre.native_gameplay_fields_valid&&
       pre.sequence&&pre.gameplay_epoch&&pre.native_game_time>0&&pre.native_cg_identity&&
       pre.native_cg_identity+0x8c100==refdef)
        copy={rig,{origin,axis},pre.native_cg_identity,refdef,pre.gameplay_epoch,pre.sequence,pre.native_game_time};
    AcquireSRWLockExclusive(&scene_lock);scene=copy;ReleaseSRWLockExclusive(&scene_lock);
}
bool configure_native_use_gaze_adapter(HMODULE executable,bool input_ready) noexcept {
    NativeReadPreserve preserve;wchar_t option[8]{};
    if(!input_ready||GetEnvironmentVariableW(L"BOVR_COD4_CONTROLS",option,8)!=1||option[0]!=L'1')return false;
    AcquireSRWLockExclusive(&native_use_gaze_detail::install_lock);bool okay{};
    try {std::string reason;okay=reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&native_use_gaze_detail::install();}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&native_use_gaze_detail::install_lock);
    trace("t5_native_use_gaze_configured","accepted=%u call=00817e2d native=005bebb0 original_once=1 gaze=hmd native_range_cone_occlusion_retained=1",okay);
    return okay;
}
}
