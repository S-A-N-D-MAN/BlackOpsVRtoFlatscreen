// SPDX-License-Identifier: GPL-3.0-only
#include "t5/borrowed_weapon_writer.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace bovr;
namespace a=t5::animation_detail;namespace w=t5::borrowed_writer;
namespace {
unsigned checks{},failures{};
constexpr a::Axis identity_axis{{1,0,0},{0,1,0},{0,0,1}};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
input::BorrowedPlacementValues proposal(){input::BorrowedPlacementValues p{};p.valid=true;p.identity={2,3,4,5};p.original_published_ms=1000;return p;}
void authored_precision(){
    namespace precision=t5::native_precision;
    a::Transform pose{},gun_local{},flash_local{};
    check(a::pose_transform({{8192.25F,-16384.5F,4096.75F},{17,-43,8}},&pose)&&
          a::pose_transform({{11.25F,-2.5F,3.125F},{4,7,-3}},&gun_local)&&
          a::pose_transform({{6.25F,.125F,-.25F},{}},&flash_local),"authored far-coordinate transforms are rigid");
    a::LocalGeometry camera{};camera.pose_world=pose;camera.gun_in_dobj=gun_local;camera.flash_in_gun=flash_local;
    camera.gun_world=a::compose(pose,gun_local);camera.flash_world=a::compose(camera.gun_world,flash_local);
    auto normal=camera;auto p=proposal();p.dobj=normal.pose_world;p.gun=normal.gun_world;p.barrel=normal.flash_world;
    w::Witness witness{true};
    check(precision::same_local_geometry(camera,normal)&&w::observe_normal(witness,p,normal,camera)&&w::observe_after(witness,p,normal),
        "authored identical camera and normal geometry complete the world witness");
    auto changed=normal;changed.flash_world.origin.x+=.03F;w::Witness refused{true};
    check(!w::observe_normal(refused,p,changed,camera),"barrel displacement beyond the physical cap rejects");
    changed=normal;changed.flash_in_gun.origin.x+=.03F;
    check(!precision::same_local_geometry(camera,changed),"changed local barrel geometry rejects");
    changed=normal;changed.gun_world.origin.x=std::numeric_limits<float>::quiet_NaN();
    check(!precision::same_local_geometry(camera,changed),"nonfinite geometry cannot expand precision allowance");
    check(precision::world_position_close({}, {.002F,0,0})&&
          !precision::world_position_close({}, {std::nextafter(.002F,1.F),0,0}),"near-origin position floor remains exact");
    check(!precision::world_position_close({262144,0,0},{std::nextafter(262144.F,300000.F),0,0}),
        "large coordinates cannot authorize one representable step beyond the cap");
}
}

int main(){
    authored_precision();
    input::ScaledWeaponPlacement original{};original.origin={7,8,9};
    for(unsigned i=0;i<721;++i){auto p=proposal();a::Transform animated{};
        check(a::pose_transform({{12,-3,8},{float(i)*.63F-180,float(i)*1.07F-360,float(i)*.77F-180}},&p.dobj),"independent full-angle pose fixture");
        check(a::pose_transform({{8,2,-4},{float(i)*.07F,float(i)*.11F,float(i)*.13F}},&animated),"noncommuting authored local pose");
        p.gun=a::compose(p.dobj,animated);p.barrel=a::compose(p.gun,{{5.43F,.07F,1.17F},identity_axis});
        input::ScaledWeaponPlacement scratch{};check(w::make_value(p,original,1100,&scratch),"current proposal converts to native placement");
        a::Transform decoded{};const auto q=scratch.quaternion;
        check(a::convert_native({{q.x,q.y,q.z,q.w},scratch.origin,2},&decoded)&&w::close(decoded,p.dobj),"weighted quaternion reconstructs selected base");
        a::LocalGeometry native{};native.pose_world=decoded;native.gun_world=a::compose(decoded,animated);native.flash_world=a::compose(native.gun_world,{{5.43F,.07F,1.17F},identity_axis});
        native.gun_in_dobj=animated;native.flash_in_gun={{5.43F,.07F,1.17F},identity_axis};
        w::Witness state{true};check(w::observe_normal(state,p,native,native)&&w::observe_after(state,p,native),"normal and after-current transforms prove selected proposal");
        check(!w::observe_normal(state,p,native,native),"one normal proof cannot complete twice");
    }
    for(unsigned v=0;v<14;++v){auto p=proposal();auto source=original;auto now=1050ULL;
        switch(v){case 0:p.valid=false;break;case 1:p.identity.frame_id=0;break;case 2:p.original_published_ms=0;break;case 3:now=1101;break;
        case 4:now=999;break;case 5:source.scale=2;break;case 6:source.scale=1.00001F;break;case 7:source.quaternion={0,0,0,2};break;
        case 8:p.dobj.axis.forward={};break;case 9:p.dobj.origin.x=std::numeric_limits<float>::infinity();break;case 10:p.gun.axis.left={};break;
        case 11:p.barrel.axis.up={};break;case 12:p.gun.origin.x=std::numeric_limits<float>::quiet_NaN();break;case 13:source.origin.y=std::numeric_limits<float>::infinity();break;}
        input::ScaledWeaponPlacement out{};check(!w::make_value(p,source,now,&out),"invalid stale scaled or degenerate proposal never selects input");
    }
    for(unsigned point=0;point<2;++point)for(unsigned field=0;field<8;++field){auto p=proposal();w::Witness state{true};a::LocalGeometry native{};
        if(point)check(w::observe_normal(state,p,native,native),"earlier normal witness exists for after-fault fixture");
        switch(field){case 0:native.gun_world.origin.x=.003F;break;case 1:native.flash_world.origin.z=.003F;break;case 2:native.pose_world.origin.y=.003F;break;
        case 3:native.gun_world.axis.forward={};break;case 4:native.flash_world.axis.left={};break;case 5:native.pose_world.axis.up={};break;
        case 6:state.selected=false;break;case 7:if(point)state.normal_verified=false;else p.valid=false;break;}
        check(!(point?w::observe_after(state,p,native):w::observe_normal(state,p,native,native)),"wrong expected or native geometry prevents completion");
    }
    const auto row=synthetic_profile_fixture::row(2);const auto& shape=*t5::borrowed_profile::shape(row);
    t5::BorrowedSceneRecord scene{};scene.active=scene.completed=true;scene.selected=true;scene.normal_serial=14;
    auto& c=scene.claim;c.wrapper_token=7;c.camera_serial=13;c.camera_published_ms=1000;c.profile_revision=shape.revision;c.key.profile_row=row;c.key.profile_revision=shape.revision;
    c.key.weapon=shape.weapon;c.key.gun_bone=shape.gun;c.key.flash_bone=shape.flash;c.key.bone_count=shape.total;c.key.command_epoch=2;c.key.command_sequence=3;c.key.thread=4;
    check(!t5::borrowed_scene_completed(scene,1050,14,7,2,3,true),"selected but unverified world cannot enter scene predicate");
    scene.world_verified=true;check(t5::borrowed_scene_completed(scene,1050,14,7,2,3,true),"selected current world proof permits scene identity predicate");
    scene.selected=false;scene.world_verified=false;check(t5::borrowed_scene_completed(scene,1050,14,7,2,3,true),"hand-loss fallback still renders without tracked proof");
    check(!w::prepare_camera(false,512,1000,2000)&&!w::prepare_camera(false,3,1000,1099)&&w::prepare_camera(false,3,1000,1100),"passive camera limit and rate remain unchanged");
    for(unsigned i=0;i<10000;++i)check(w::prepare_camera(true,512+i,1000+i,1001+i),"enabled writer prepares a fresh proof per camera");
    std::printf("borrowed_writer_checks=%u failures=%u native_calls=0 game_writes=0\n",checks,failures);return failures?1:0;
}
