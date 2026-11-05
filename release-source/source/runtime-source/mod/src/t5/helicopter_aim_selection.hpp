// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/helicopter_context.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "input/helicopter_head_ray.hpp"
#include "input/boat_controller_ray.hpp"
#include "input/vehicle_aim_convergence.hpp"

namespace bovr::t5::helicopter_aim {
namespace xr=wawvr::xr;
struct Call {
    std::uintptr_t returned{},vehicle{},scr{},info{},player{},stack{},stack_low{},stack_high{};
    DWORD thread{};
};
struct Plan {
    helicopter::Facts owner{};
    HelicopterSceneView view{};
    PreCommandSnapshot pre{};
    xr::EnginePose head{};
    xr::Vec3f old_start{},old_end{},start{},end{},direction{};
    std::uintptr_t start_address{},end_address{},target_address{};
};
// River's boat crew and Redemption's Huey rockets select their targets from
// server getplayerangles(). Preserve the boat's right-hand ray and use the HMD
// ray for the exact scripted Huey; leave native camera/body angles untouched.
inline bool vehicle_script_angles(const helicopter::Host& h,std::uint32_t entref,DWORD thread,
                              const HelicopterSceneView& view,const PreCommandSnapshot& pre,
                              std::uint64_t now,xr::Vec3f* out) noexcept {
    if(!out||entref||!h.copy||!thread||!pre.sequence||!pre.gameplay_epoch||
       !pre.observed_ms||now<pre.observed_ms||now-pre.observed_ms>100||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9||!pre.native_gameplay_fields_valid||view.epoch!=pre.gameplay_epoch||
       !view.sequence||view.sequence>pre.sequence)return false;
    using namespace helicopter::detail;
    DWORD main_thread{},server_thread{};helicopter::Facts first{},last{};xr::EnginePose head{};
    if(!value(h,0x251ca98,0,main_thread)||!value(h,0x251cac0,0,server_thread)||
       (thread!=main_thread&&thread!=server_thread)||!read_graph(h,first)||
       (first.info_type!=4&&!first.scripted_huey)||
       !helicopter::same_vehicle(first,view.owner)||first.cg!=pre.native_cg_identity||
       view.refdef!=first.cg+0x8c100||view.native_time<=0||first.game_time<view.native_time||
       first.game_time-view.native_time>100||
       !(first.info_type==4?input::boat_controller_ray(view.rig,view.stock,now,&head)
                          :input::helicopter_head_ray(view.rig,view.stock,now,&head))||
       !read_graph(h,last)||first!=last)return false;
    const auto& f=head.axis.forward;
    constexpr float degrees=57.29577951308232f;
    *out={-std::atan2(f.z,std::hypot(f.x,f.y))*degrees,std::atan2(f.y,f.x)*degrees,0.f};
    return true;
}
// No mutation and no native calls. The bridge separately owns the executing
// trace stack. Two equal graph copies detect drift; they are not a thread lock.
inline bool propose(const helicopter::Host& h,const Call& c,const HelicopterSceneView& view,
                    const PreCommandSnapshot& pre,std::uint64_t now,Plan* out) noexcept {
    if(!out)return false;*out={};
    using namespace helicopter::detail;
    if(c.returned!=0x81141c||!c.thread||!h.copy||c.stack%4||
       c.stack<c.stack_low||c.stack_high<c.stack||c.stack_high-c.stack<0x180||
       !bounded(c.stack,0x180)||!pre.sequence||!pre.gameplay_epoch||
       !pre.observed_ms||now<pre.observed_ms||now-pre.observed_ms>100||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9||!pre.native_gameplay_fields_valid||view.epoch!=pre.gameplay_epoch||
       !view.sequence||view.sequence>pre.sequence)return false;
    std::array<std::uint32_t,8> args{};DWORD main_thread{},server_thread{};
    if(!bytes(h,c.stack,args.data(),sizeof(args))||args[0]!=c.returned||
       args[1]!=c.stack+0xa8||args[2]!=c.stack+0x30||args[3]!=c.stack+0x48||
       args[4]||args[5]!=0x280e091||args[6]||args[7]||
       !value(h,0x251ca98,0,main_thread)||!value(h,0x251cac0,0,server_thread)||
       (c.thread!=main_thread&&c.thread!=server_thread))return false;
    Plan p{};p.view=view;p.pre=pre;
    if(!read_graph(h,p.owner)||!helicopter::same_vehicle(p.owner,view.owner)||
       p.owner.vehicle_entity!=c.vehicle||p.owner.scr_vehicle!=c.scr||
       p.owner.info!=c.info||p.owner.player_entity!=c.player||
       p.owner.cg!=pre.native_cg_identity||view.refdef!=p.owner.cg+0x8c100||
       view.native_time<=0||p.owner.game_time<view.native_time||p.owner.game_time-view.native_time>100||
       !(p.owner.info_type==4
           ?input::boat_controller_ray(view.rig,view.stock,now,&p.head)
           :input::helicopter_head_ray(view.rig,view.stock,now,&p.head)))return false;
    std::uint32_t has_target{},target_entity{};std::uint16_t owner_handle{};
    p.start_address=args[2];p.end_address=args[3];p.target_address=c.scr+0x434;
    xr::Vec3f target{};
    if(!value(h,c.scr,0x418,has_target)||
       // Gunner-only boat/Huey drivers have no main turret weapon, so their
       // native driver trace runs with hasTarget=0 and still feeds gunner aim.
       (has_target!=1&&!(has_target==0&&(p.owner.info_type==4||p.owner.scripted_huey)))||
       !value(h,c.scr,0x42c,target_entity)||target_entity!=1023||
       !value(h,c.vehicle,0x134,owner_handle)||owner_handle!=1||
       !bytes(h,p.start_address,&p.old_start,sizeof(p.old_start))||
       !bytes(h,p.end_address,&p.old_end,sizeof(p.old_end))||
       !bytes(h,p.target_address,&target,sizeof(target))||
       memcmp(&target,&p.old_end,sizeof(target))||
       !input::helicopter_head_detail::finite(p.old_start)||!input::helicopter_head_detail::finite(p.old_end))return false;
    p.start=p.head.position;
    // Native driver trace range is10240. Native turret processing keeps the
    // barrel/pod origin and converges on this collision-tested selected ray.
    if(!input::vehicle_aim_convergence(p.start,p.head.axis.forward,p.old_start,10240.f,&p.end,&p.direction))return false;
    helicopter::Facts again{};
    if(!read_graph(h,again)||again!=p.owner)return false;
    *out=p;return true;
}
}
