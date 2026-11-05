// SPDX-License-Identifier: GPL-3.0-only
#include "t5/helicopter_aim_adapter.hpp"
#include "t5/helicopter_aim_hook.hpp"
#include "t5/helicopter_aim_selection.hpp"
#include "t5/helicopter_turret_limit_hook.hpp"
#include "t5/helicopter_turret_limit_selection.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <atomic>

namespace bovr::t5 {
namespace {
std::atomic<bool> aim_ready{};
thread_local bool in_trace{};
std::atomic<std::uint64_t> accepted{},refused{};
thread_local helicopter_turret_limits::TargetAdmission target_admission;
thread_local helicopter_turret_limits::Info private_turret_info;
thread_local bool in_limits{};
std::atomic<std::uint64_t> widened{},limit_refused{};
// These are original caller-owned writable arrays, validated before entry.
// Keep them changed through native trace and the caller's subsequent lerp.
bool commit(const helicopter_aim::Plan& p) noexcept {
    __try {
        if(memcmp(reinterpret_cast<void*>(p.start_address),&p.old_start,12)||
           memcmp(reinterpret_cast<void*>(p.end_address),&p.old_end,12)||
           memcmp(reinterpret_cast<void*>(p.target_address),&p.old_end,12))return false;
        memcpy(reinterpret_cast<void*>(p.start_address),&p.start,12);
        memcpy(reinterpret_cast<void*>(p.end_address),&p.end,12);
        memcpy(reinterpret_cast<void*>(p.target_address),&p.end,12);
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool option(const wchar_t* name) noexcept {wchar_t value[8]{};return GetEnvironmentVariableW(name,value,8)==1&&value[0]==L'1';}
}
bool configure_helicopter_aim_adapter(HMODULE executable,bool input_ready) noexcept {
    if(!input_ready||!option(L"BOVR_COD4_CONTROLS")||!option(L"BOVR_MOUNTED_LOOK_PROBE")||
       !option(L"BOVR_WEAPON_CYCLE_PROBE"))return false;
    const bool trace_okay=configure_helicopter_aim_hook(executable);
    const bool okay=trace_okay&&configure_helicopter_turret_limit_hook(executable);
    aim_ready.store(okay,std::memory_order_release);
    trace("t5_helicopter_aim_configured","enabled=%u aim=headset trace=811417 range=10240 flight_angles_changed=0",okay);
    return okay;
}
void select_helicopter_aim_trace(std::uintptr_t returned,std::uintptr_t vehicle,
    std::uintptr_t scr,std::uintptr_t info,std::uintptr_t player,std::uintptr_t stack) noexcept {
    NativeReadPreserve preserve;
    if(!aim_ready.load(std::memory_order_acquire)||in_trace)return;
    in_trace=true;struct Exit{~Exit(){in_trace=false;}} exit;
    HelicopterSceneView view{};PreCommandSnapshot pre{};helicopter_aim::Plan plan{};
    ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
    const helicopter_aim::Call call{returned,vehicle,scr,info,player,stack,low,high,GetCurrentThreadId()};
    const helicopter::Host host{nullptr,helicopter::detail::protected_copy,nullptr,nullptr};
    const auto now=GetTickCount64();
    if(!read_helicopter_scene_view(&view)||!read_pre_command_snapshot(&pre)||
       !helicopter_aim::propose(host,call,view,pre,now,&plan)||
       !command_input_generation_current(view.rig)) {
        const auto n=++refused;if(n<=8||n%240==0)
            trace("t5_helicopter_aim_refused","count=%llu entity=%p thread=%lu view=%llu pre=%llu native_time=%d",n,reinterpret_cast<void*>(vehicle),call.thread,view.sequence,pre.sequence,view.native_time);
        return;
    }
    PreCommandSnapshot again{};HelicopterSceneView current{};
    if(!read_pre_command_snapshot(&again)||again.gameplay_epoch!=pre.gameplay_epoch||
       !read_helicopter_scene_view(&current)||current.epoch!=view.epoch||current.refdef!=view.refdef||
       current.rig.xr_generation!=view.rig.xr_generation||!command_input_generation_current(view.rig)||!commit(plan))return;
    int native_level{};
    target_admission={};
    if(helicopter::detail::value(host,0x1c04890,0,native_level)&&native_level>0)
        target_admission={plan.owner,pre.gameplay_epoch,view.rig.xr_generation,now,native_level,call.thread,true};
    const auto n=++accepted;
    if(n<=12||n%20==0)trace("t5_helicopter_aim_trace",
        "count=%llu entity=%d xr_frame=%llu original_age=%llu native_time=%d start=%.6f,%.6f,%.6f end=%.6f,%.6f,%.6f forward=%.7f,%.7f,%.7f native_calls_added=0 flight_writes=0",
        n,plan.owner.entity_index,view.rig.frame.frame_id,now-view.rig.published_ms,view.native_time,
        plan.start.x,plan.start.y,plan.start.z,plan.end.x,plan.end.y,plan.end.z,
        plan.head.axis.forward.x,plan.head.axis.forward.y,plan.head.axis.forward.z);
}
std::uintptr_t select_helicopter_turret_limits(std::uintptr_t returned,std::uintptr_t vehicle,
    std::uintptr_t scr,std::uintptr_t info,std::uintptr_t pilot,std::uintptr_t stack) noexcept {
    NativeReadPreserve preserve;
    if(!aim_ready.load(std::memory_order_acquire)||in_limits)return 0;
    in_limits=true;struct Exit{~Exit(){in_limits=false;}} exit;
    const auto admission=target_admission;
    // One new admitted head trace is required for every native aim invocation.
    // An NPC call cannot consume the local pilot's pending admission.
    if(admission.owner.vehicle_entity==vehicle)target_admission.valid=false;
    HelicopterSceneView view{};PreCommandSnapshot pre{};helicopter_turret_limits::Info selected{};
    ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
    const helicopter_turret_limits::Call call{returned,vehicle,scr,info,pilot,stack,low,high,GetCurrentThreadId()};
    const helicopter::Host host{nullptr,helicopter::detail::protected_copy,nullptr,nullptr};
    if(!read_helicopter_scene_view(&view)||!read_pre_command_snapshot(&pre)||
       !helicopter_turret_limits::select(host,call,view,pre,admission,GetTickCount64(),&selected)||
       !command_input_generation_current(view.rig)) {
        const auto n=++limit_refused;
        if(n<=8||n%240==0)trace("t5_helicopter_limits_refused","count=%llu vehicle=%p admitted=%u native_level=%d",n,reinterpret_cast<void*>(vehicle),admission.valid,admission.level_time);
        return 0;
    }
    private_turret_info=selected;
    const auto n=++widened;
    if(n<=8||n%40==0)trace("t5_helicopter_limits_selected","count=%llu entity=%d yaw=179 pitch=89 native_level=%d shared_info_writes=0",n,admission.owner.entity_index,admission.level_time);
    return reinterpret_cast<std::uintptr_t>(private_turret_info.data());
}
}
