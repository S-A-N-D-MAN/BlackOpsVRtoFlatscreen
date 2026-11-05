// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/helicopter_aim_selection.hpp"
#include "t5/helicopter_turret_limit_clone.hpp"

namespace bovr::t5::helicopter_turret_limits {
struct TargetAdmission {
    helicopter::Facts owner{};
    std::uint64_t epoch{},generation{},published_ms{};
    int level_time{};
    DWORD thread{};
    bool valid{};
};
struct Call {
    std::uintptr_t returned{},vehicle{},scr{},info{},pilot{},stack{},stack_low{},stack_high{};
    DWORD thread{};
};
// The clone is borrowed by one original VEH_UpdateAim invocation. Only its
// four scalar clamp reads use the replacement; native state never retains it.
inline bool select(const helicopter::Host& h,const Call& c,const HelicopterSceneView& view,
    const PreCommandSnapshot& pre,const TargetAdmission& target,std::uint64_t now,Info* out) noexcept {
    using namespace helicopter::detail;
    if(!out||!h.copy||c.returned!=0x80c49f||!c.thread||c.stack%4||
       c.stack<c.stack_low||c.stack_high<c.stack||c.stack_high-c.stack<0x80||!bounded(c.stack,0x80)||
       !target.valid||target.thread!=c.thread||!target.published_ms||now<target.published_ms||
       now-target.published_ms>100||!pre.sequence||!pre.gameplay_epoch||!pre.observed_ms||
       now<pre.observed_ms||now-pre.observed_ms>100||!pre.native_gameplay_fields_valid||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9||view.epoch!=pre.gameplay_epoch||target.epoch!=view.epoch||
       target.generation!=view.rig.xr_generation||!view.sequence||view.sequence>pre.sequence)return false;
    std::uint32_t returned{};std::array<float,4> args{};float normalize_input{};int level{};
    if(!value(h,c.stack,0,returned)||returned!=c.returned||
       !bytes(h,c.stack+4,args.data(),sizeof(args))||!value(h,c.stack,0x60,normalize_input)||
       !value(h,0x1c04890,0,level)||level<=0||level!=target.level_time)return false;
    for(float v:args)if(!std::isfinite(v))return false;
    if(std::fabs(args[0])>3600||std::fabs(args[1])>3600||args[2]<0||args[2]>36000||
       args[3]!=0.05f||!std::isfinite(normalize_input)||std::fabs(normalize_input)>3600)return false;
    helicopter::Facts owner{},again{};wawvr::xr::EnginePose head{};
    if(!read_graph(h,owner)||!helicopter::same_vehicle(owner,target.owner)||
       !helicopter::same_vehicle(owner,view.owner)||owner.vehicle_entity!=c.vehicle||
       owner.scr_vehicle!=c.scr||owner.info!=c.info||owner.player_entity!=c.pilot||
       owner.cg!=pre.native_cg_identity||view.refdef!=owner.cg+0x8c100||view.native_time<=0||
       owner.game_time<view.native_time||owner.game_time-view.native_time>100||
       !input::helicopter_head_ray(view.rig,view.stock,now,&head))return false;
    std::uint32_t has_target{},target_entity{};std::uint16_t owner_handle{};
    if(!value(h,c.scr,0x418,has_target)||has_target!=1||!value(h,c.scr,0x42c,target_entity)||target_entity!=1023||
       !value(h,c.vehicle,0x134,owner_handle)||owner_handle!=1)return false;
    Info first{},rechecked{};
    if(!bytes(h,c.info,first.data(),first.size())||!bytes(h,c.info,rechecked.data(),rechecked.size())||
       !read_graph(h,again)||again!=owner||!value(h,0x1c04890,0,level)||level!=target.level_time)return false;
    return clone(first,rechecked,out);
}
}
