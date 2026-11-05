// SPDX-License-Identifier: GPL-3.0-only
// Both real value kernels; no retail functions, graphics, clocks or XR runtime.
#include "input/native_command_aim.hpp"
#include "input/tracked_gun_pose.hpp"
#include "t5/runtime_profile_catalog.hpp"
#include "t5/borrowed_weapon_writer.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include "xr_math.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace bovr;namespace xr=wawvr::xr;namespace a=t5::animation_detail;
unsigned checks{},failures{};float maximum_forward_error{},maximum_decode_error{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
xr::Quaternionf rotation(float yaw,float pitch,float roll) {
    return xr::Normalize(xr::Multiply(xr::Multiply({0,std::sin(yaw/2),0,std::cos(yaw/2)},
        {std::sin(pitch/2),0,0,std::cos(pitch/2)}),{0,0,std::sin(roll/2),std::cos(roll/2)}));
}
struct Inputs {
    input::ReservedWeaponFrame scene{};
    input::CommandRigSample command{};
    input::WeaponBodySample body{};
    a::LocalGeometry geometry{};
    Inputs(unsigned row,unsigned sample){
        const float t=float(sample)*.031F;
        scene.identity={7,100+sample,200+sample,300+sample};scene.anchor_generation=7;
        scene.published_ms=1000;scene.exclusively_reserved_for_scene=true;
        scene.tracking_anchor={rotation(.3F,-.1F,.08F),{.7F,1.3F,-.2F}};
        auto& frame=scene.prediction;frame.frame_id=scene.identity.frame_id;
        frame.should_render=frame.views_valid=true;frame.predicted_display_time=1000000;
        frame.predicted_display_period=11111111;frame.actions.sequence=900+sample;frame.actions.focused=true;
        frame.head_center={rotation(-t*.3F,.1F,t*.1F),{.2F,1.7F,-.2F}};
        auto& right=frame.actions.hands[1];right.grip.active=right.grip.position_valid=right.grip.position_tracked=true;
        right.grip.pose.position={.5F+.1F*std::sin(t),1.1F,-.4F};
        right.aim.active=right.aim.orientation_valid=right.aim.orientation_tracked=true;
        right.aim.pose.orientation=rotation(t-.8F,.7F*std::sin(t),t*.7F);
        a::Transform native_body{};
        check(a::pose_transform({{50,-20,70},{12*std::sin(t),61+t*20,7*std::cos(t)}},&native_body),"independent pitched rolled native body");
        body={scene.identity,{native_body.origin,native_body.axis},input::BodyCameraSource::completed_native_refdef};
        check(a::pose_transform({{5,-3,2},{17+t*2,-41+t*3,28-t*4}},&geometry.gun_in_dobj),"noncommuting current animation transform");
        const auto runtime_row=synthetic_profile_fixture::row(row);
        const auto* profile=t5::borrowed_profile::get(runtime_row);const auto* shape=t5::borrowed_profile::shape(runtime_row);
        check(profile&&shape,"runtime admitted row exists");
        a::NativeMat32 flash{};std::memcpy(&flash,profile->bone[shape->flash].bind.data(),32);
        check(a::convert_native(flash,&geometry.flash_in_gun),"authored flash bind converts");
        command={frame,scene.tracking_anchor,7,7,1000};
    }
};
xr::Vec3f decoded_forward(std::uint32_t packed) {
    const auto pitch=static_cast<std::int16_t>(packed&0xffff),yaw=static_cast<std::int16_t>(packed>>16);
    constexpr float rad=0.0054931640625F*0.017453292519943295F;
    return {std::cos(pitch*rad)*std::cos(yaw*rad),std::cos(pitch*rad)*std::sin(yaw*rad),-std::sin(pitch*rad)};
}
input::CommandAimStatus aim(const Inputs& i,input::CommandAimProposal* out) {
    return input::propose_native_command_aim(i.command,i.command.frame.actions,i.body.stock_camera.axis,xr::Hand::Right,1050,out);
}
input::BorrowedPlacementStatus visible(const Inputs& i,input::BorrowedPlacementValues* out) {
    return input::propose_borrowed_placement(i.scene,i.body,i.geometry,1050,out);
}
}
int main(){
    for(unsigned row=0;row<3;++row)for(unsigned sample=0;sample<180;++sample){
        Inputs i(row,sample);const auto original_scene=i.scene;const auto original_command=i.command;
        input::CommandAimProposal command{};input::BorrowedPlacementValues scene{};
        check(aim(i,&command)==input::CommandAimStatus::accepted,"actual A1 orientation kernel accepts matching original values");
        check(visible(i,&scene)==input::BorrowedPlacementStatus::proposed,"unchanged V22e current placement kernel accepts matching values");
        const auto error=a::distance(command.forward,scene.gun.axis.forward);
        maximum_forward_error=std::max(maximum_forward_error,error);
        check(error<.00001F,"both real kernels have the same world gun forward with nonzero anchor and full body orientation");
        check(a::distance(command.forward,scene.barrel.axis.forward)<.00001F,"all three actual M1911 rows retain matching authored barrel forward");
        const auto quantization=a::distance(decoded_forward(command.packed),scene.gun.axis.forward);
        maximum_decode_error=std::max(maximum_decode_error,quantization);
        check(quantization<.0002F,"decoded native signed angles differ only by bounded angle quantization");
        input::ScaledWeaponPlacement output{},original{{},{1,2,3},1};
        check(t5::borrowed_writer::make_value(scene,original,1050,&output),"unchanged writer selects owned placement from composed output");
        a::Transform pose{};const auto q=output.quaternion;
        check(a::convert_native({{q.x,q.y,q.z,q.w},output.origin,2},&pose),"selected placement roundtrips through native quaternion formula");
        a::LocalGeometry normal{};normal.pose_world=pose;normal.gun_world=a::compose(pose,i.geometry.gun_in_dobj);
        normal.flash_world=a::compose(normal.gun_world,i.geometry.flash_in_gun);
        t5::borrowed_writer::Witness witness{};witness.selected=true;
        check(t5::borrowed_writer::observe_normal(witness,scene,normal,i.geometry)&&t5::borrowed_writer::observe_after(witness,scene,normal),"existing normal and returned geometry witness accepts simulated native composition");
        check(a::distance(normal.gun_world.axis.forward,command.forward)<.00001F,"native-formula recomposed visible gun retains original aim forward");
        check(!std::memcmp(&i.scene,&original_scene,sizeof(i.scene))&&!std::memcmp(&i.command,&original_command,sizeof(i.command)),"value kernels do not mutate scene reservation or original command rig");
        i.scene.prediction.head_center={rotation(1.4F,-.3F,.2F),{4,5,6}};i.command.frame.head_center=i.scene.prediction.head_center;
        input::CommandAimProposal moved{};input::BorrowedPlacementValues moved_scene{};
        check(aim(i,&moved)==input::CommandAimStatus::accepted&&visible(i,&moved_scene)==input::BorrowedPlacementStatus::proposed&&
            a::distance(moved.forward,command.forward)==0&&a::distance(moved_scene.gun.origin,scene.gun.origin)==0,"head-only movement cannot reattach either independently tracked aim or gun");
    }
    {
        Inputs i(0,0);i.command.frame.frame_id=999;i.command.frame.actions.sequence=888;i.command.published_ms=990;
        input::CommandAimProposal command{};input::BorrowedPlacementValues scene{};
        check(aim(i,&command)==input::CommandAimStatus::accepted&&visible(i,&scene)==input::BorrowedPlacementStatus::proposed,"distinct independently fresh command and scene identities remain admitted independently");
        check(i.command.frame.frame_id==999&&i.command.frame.actions.sequence==888&&i.command.published_ms==990&&scene.identity.frame_id==100&&scene.action_sequence==900&&scene.original_published_ms==1000,"no fabricated common identity or refreshed original timestamp");
        i.command.frame.actions.hands[1].aim.pose.orientation=rotation(1,0,0);
        check(aim(i,&command)==input::CommandAimStatus::accepted&&visible(i,&scene)==input::BorrowedPlacementStatus::proposed&&a::distance(command.forward,scene.gun.axis.forward)>.1F,"different original tracking samples remain different rather than taking a newer pose");
    }
    for(unsigned mode=0;mode<8;++mode){
        Inputs i(1,0);input::CommandAimProposal command{};input::BorrowedPlacementValues scene{};
        switch(mode){
        case 0:i.command.published_ms=900;break;
        case 1:i.scene.published_ms=900;break;
        case 2:i.command.anchor_generation=8;break;
        case 3:i.scene.exclusively_reserved_for_scene=false;break;
        case 4:i.scene.prediction.actions.hands[1].grip.position_tracked=false;i.command.frame.actions=i.scene.prediction.actions;break;
        case 5:i.scene.prediction.actions.hands[1].aim.orientation_tracked=false;i.command.frame.actions=i.scene.prediction.actions;break;
        case 6:i.scene.prediction.actions.focused=false;i.command.frame.actions=i.scene.prediction.actions;break;
        case 7:i.scene.prediction.actions.hands[1].grip.pose.position={3,4,5};i.command.frame.actions=i.scene.prediction.actions;break;
        }
        const bool aim_ok=aim(i,&command)==input::CommandAimStatus::accepted;
        const bool visual_ok=visible(i,&scene)==input::BorrowedPlacementStatus::proposed;
        check(aim_ok==(mode!=0&&mode!=2&&mode!=5&&mode!=6),"aim's own age generation and orientation gates remain independent");
        check(visual_ok==(mode!=1&&mode!=3&&mode!=4&&mode!=5&&mode!=6),"visual reservation grip and aim gates remain independent");
    }
    std::printf("command_aim_placement_checks=%u failures=%u maximum_forward_error=%.9g maximum_decoded_forward_error=%.9g native_calls=0 gpu_calls=0 shot_authority=0\n",checks,failures,maximum_forward_error,maximum_decode_error);
    return failures?1:0;
}
