// SPDX-License-Identifier: GPL-3.0-only
// Runtime profile and real geometry kernels over independently authored values.
#include "input/native_command_aim.hpp"
#include "input/tracked_gun_pose.hpp"
#include "t5/borrowed_barrel_geometry.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include "t5/borrowed_weapon_writer.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include "xr_math.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace bovr;namespace xr=wawvr::xr;namespace a=t5::animation_detail;
namespace profile=t5::borrowed_profile;namespace writer=t5::borrowed_writer;
unsigned checks{},failures{};
constexpr a::Axis identity_axis{{1,0,0},{0,1,0},{0,0,1}};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
xr::Quaternionf rotation(float y,float p,float r){return xr::Normalize(xr::Multiply(xr::Multiply({0,std::sin(y/2),0,std::cos(y/2)},
    {std::sin(p/2),0,0,std::cos(p/2)}),{0,0,std::sin(r/2),std::cos(r/2)}));}
struct Inputs {
    input::ReservedWeaponFrame scene{};input::WeaponBodySample body{};input::CommandRigSample command{};a::LocalGeometry geometry{};
    explicit Inputs(unsigned sample,unsigned row){const float t=float(sample)*.037F;
        scene.identity={9,100+sample,300+sample,500+sample};scene.anchor_generation=9;scene.published_ms=2000;scene.exclusively_reserved_for_scene=true;
        scene.tracking_anchor={rotation(.28F,-.19F,.13F),{-.2F,1.1F,.4F}};
        auto& frame=scene.prediction;frame.frame_id=scene.identity.frame_id;frame.should_render=frame.views_valid=true;frame.predicted_display_time=900000;
        frame.predicted_display_period=11111111;frame.actions.sequence=700+sample;frame.actions.focused=true;
        auto& right=frame.actions.hands[1];right.grip.active=right.grip.position_valid=right.grip.position_tracked=true;right.grip.pose.position={.18F,1.2F,-.43F};
        right.aim.active=right.aim.orientation_valid=right.aim.orientation_tracked=true;right.aim.pose.orientation=rotation(-1.1F+t,.32F*std::cos(t),t*.53F);
        for(auto& hand:frame.actions.hands){hand.squeeze.active=true;hand.squeeze.current=.8F;hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;}
        frame.actions.hands[0].grip.pose.position=a::add(right.grip.pose.position,xr::Rotate(right.aim.pose.orientation,{.08F,.07F,-.38F}));
        a::Transform native_body{};check(a::pose_transform({{37,-11,64},{11*std::sin(t),27+t*13,9*std::cos(t)}},&native_body),"authored native body");
        body={scene.identity,{native_body.origin,native_body.axis},input::BodyCameraSource::completed_native_refdef};
        check(a::pose_transform({{4+std::sin(t),-2,3},{-29+t*11,41-t*7,16+t*5}},&geometry.gun_in_dobj),"authored animated gun");
        const auto& shape=*profile::shape(row);a::NativeMat32 flash{};std::memcpy(&flash,profile::get(row)->bone[shape.flash].bind.data(),32);
        check(a::convert_native(flash,&geometry.flash_in_gun),"runtime flash bind converts");command={frame,scene.tracking_anchor,9,9,2000};
    }
};
}

int main(){
    const auto row=synthetic_profile_fixture::row(3);const auto& shape=*profile::shape(row);
    t5::BorrowedWeaponClaim claim{};claim.wrapper_token=11;claim.camera_serial=22;claim.camera_published_ms=2000;claim.profile_revision=shape.revision;
    auto& key=claim.key;key.profile_row=row;key.profile_revision=shape.revision;key.weapon=shape.weapon;key.gun_bone=shape.gun;key.flash_bone=shape.flash;
    key.bone_count=shape.total;key.command_epoch=4;key.command_sequence=5;key.thread=6;
    check(t5::valid_borrowed_claim(claim,2050),"runtime profile claim is valid");
    for(unsigned field=0;field<6;++field){auto bad=claim;switch(field){case 0:bad.key.profile_row=0;break;case 1:++bad.key.profile_revision;break;
        case 2:++bad.key.weapon;break;case 3:++bad.key.gun_bone;break;case 4:++bad.key.flash_bone;break;case 5:++bad.key.bone_count;break;}
        check(!t5::valid_borrowed_claim(bad,2050),"mismatched runtime claim rejects");}

    a::LocalGeometry local{};local.gun_in_dobj={{1,2,3},identity_axis};a::NativeMat32 flash{};std::memcpy(&flash,profile::get(row)->bone[shape.flash].bind.data(),32);
    check(a::convert_native(flash,&local.flash_in_gun),"authored bind converts for barrel gate");
    local.gun_world={{100,200,300},identity_axis};local.flash_world=a::compose(local.gun_world,local.flash_in_gun);
    check(profile::barrel_geometry(row,local)==profile::BarrelGeometryStatus::fixed,"runtime bind geometry accepts fixed muzzle");
    local.flash_in_gun.origin.x+=.25F;local.flash_world=a::compose(local.gun_world,local.flash_in_gun);
    check(profile::barrel_geometry(row,local)==profile::BarrelGeometryStatus::animated_translation,"same-axis animated muzzle translation remains current");
    local.flash_in_gun.axis.forward={0,1,0};check(profile::barrel_geometry(row,local)==profile::BarrelGeometryStatus::invalid,"muzzle direction mismatch rejects");

    synthetic_profile_fixture::Fixture angled_fixture{6};
    constexpr float half=.010193455F;
    const std::array<float,8> angled_bind{0,-half,0,std::sqrt(1-half*half),10,0,0,2};
    auto& angled_bits=angled_fixture.copies.before[1].bones[6].bind_bits;
    for(unsigned i=0;i<angled_bits.size();++i)angled_bits[i]=std::bit_cast<std::uint32_t>(angled_bind[i]);
    angled_fixture.recanonical();
    const auto angled_row=angled_fixture.enroll();const auto* angled_shape=profile::shape(angled_row);
    a::NativeMat32 angled_raw{};a::LocalGeometry angled_local{};
    check(angled_shape&&angled_row!=profile::invalid_runtime_row,"angled exact runtime profile enrolls");
    if(angled_shape){std::memcpy(&angled_raw,profile::get(angled_row)->bone[angled_shape->flash].bind.data(),32);}
    check(angled_shape&&a::convert_native(angled_raw,&angled_local.flash_in_gun),"angled copied muzzle bind converts");
    angled_local.gun_in_dobj={{1,2,3},identity_axis};angled_local.gun_world={{100,200,300},identity_axis};
    angled_local.flash_world=a::compose(angled_local.gun_world,angled_local.flash_in_gun);
    check(profile::barrel_geometry(angled_row,angled_local)==profile::BarrelGeometryStatus::fixed,
        "exact nonstraight copied bind muzzle is admitted");
    angled_local.flash_in_gun.axis=identity_axis;
    check(profile::barrel_geometry(angled_row,angled_local)==profile::BarrelGeometryStatus::invalid,
        "nonstraight runtime profile still rejects a current axis that differs from its copied bind");

    for(unsigned sample=0;sample<160;++sample){Inputs in(sample,row);input::CommandAimProposal command{};input::BorrowedPlacementValues visible{};
        check(input::propose_native_command_aim(in.command,in.command.frame.actions,in.body.stock_camera.axis,xr::Hand::Right,2050,&command,true)==input::CommandAimStatus::accepted,
            "paired command aim accepts authored input");
        check(input::propose_borrowed_placement(in.scene,in.body,in.geometry,2050,&visible,true)==input::BorrowedPlacementStatus::proposed,
            "paired visible placement accepts same input");
        check(a::distance(command.forward,visible.barrel.axis.forward)<.00001F,
            "command and visible barrel share tracked forward");
        input::ScaledWeaponPlacement original{{},{7,8,9},1},scratch{};
        check(writer::make_value(visible,original,2050,&scratch),"writer converts runtime placement into scratch value");
        a::Transform pose{};const auto q=scratch.quaternion;check(a::convert_native({{q.x,q.y,q.z,q.w},scratch.origin,2},&pose),"scratch placement decodes");
        a::LocalGeometry normal{};normal.pose_world=pose;normal.gun_world=a::compose(pose,in.geometry.gun_in_dobj);normal.flash_world=a::compose(normal.gun_world,in.geometry.flash_in_gun);
        writer::Witness witness{true};check(writer::observe_normal(witness,visible,normal,in.geometry)&&writer::observe_after(witness,visible,normal),"normal and after witness exact recomposition");
    }
    std::printf("runtime_geometry_checks=%u failures=%u native_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;
}
