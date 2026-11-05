// SPDX-License-Identifier: GPL-3.0-only
#include "input/weapon_placement.hpp"
#include "t5/stereo_view_math.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace {
using namespace bovr::input;
using S=WeaponPlacementStatus;
unsigned failures{},checks{};
void check(bool value,const char* text) {++checks;printf("%s %s\n",value?"PASS":"FAIL",text);if(!value) ++failures;}
bool close(float a,float b) {return std::fabs(a-b)<0.002F;}
bool close(const xr::Vec3f& a,const xr::Vec3f& b) {return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z);}
bool close(const xr::Basis3f& a,const xr::Basis3f& b) {return close(a.forward,b.forward)&&close(a.left,b.left)&&close(a.up,b.up);}
bool close(const xr::EnginePose& a,const xr::EnginePose& b) {return close(a.position,b.position)&&close(a.axis,b.axis);}
xr::Quaternionf rotation(xr::Vec3f axis,float degrees) {
    const float half=degrees*0.00872664625997F,s=std::sin(half);return {axis.x*s,axis.y*s,axis.z*s,std::cos(half)};
}
xr::Basis3f basis(const xr::Quaternionf& q) {return {xr::Rotate(q,{1,0,0}),xr::Rotate(q,{0,1,0}),xr::Rotate(q,{0,0,1})};}
// Independent expansion of the pinned native UnitQuatToAxis instruction math.
xr::Basis3f native_axis(const xr::Quaternionf& q) {
    const float xx=2*q.x*q.x,xy=2*q.x*q.y,xz=2*q.x*q.z,xw=2*q.x*q.w;
    const float yy=2*q.y*q.y,yz=2*q.y*q.z,yw=2*q.y*q.w,zz=2*q.z*q.z,zw=2*q.z*q.w;
    return {{1-yy-zz,xy+zw,xz-yw},{xy-zw,1-xx-zz,yz+xw},{xz+yw,yz-xw,1-xx-yy}};
}
struct Fixture {
    ReservedWeaponFrame frame{};WeaponBodySample body{};WeaponCalibration calibration{};
    WeaponPlacementContext context{};ScaledWeaponPlacement native{};
    Fixture() {
        frame.identity={3,42,71,97};frame.anchor_generation=3;frame.published_ms=1000;frame.exclusively_reserved_for_scene=true;
        auto& f=frame.prediction;f.frame_id=42;f.predicted_display_time=123456789;f.predicted_display_period=11111111;
        f.should_render=f.views_valid=true;f.actions.focused=true;f.actions.sequence=19;
        f.head_center.position={0,1.6F,0};frame.tracking_anchor=f.head_center;
        f.eyes[0].pose=f.eyes[1].pose=f.head_center;f.eyes[0].pose.position.x=-0.032F;f.eyes[1].pose.position.x=0.032F;
        f.eyes[0].fov=f.eyes[1].fov={-0.8F,0.8F,0.8F,-0.8F};
        auto& hand=f.actions.hands[1];hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;
        hand.grip.pose.position={0.2F,1.4F,-0.5F};hand.aim.active=hand.aim.orientation_valid=hand.aim.orientation_tracked=true;
        // Aim translation and grip orientation are intentionally unusable.
        hand.aim.pose.position={999,888,777};hand.grip.pose.orientation={0,0,0,0};
        body.identity=frame.identity;body.source=BodyCameraSource::completed_native_refdef;body.stock_camera.position={100,200,300};
        calibration.confirmed=true;calibration.weapon_index=4;calibration.model_generation=7;calibration.revision=2;
        context.expected=frame.identity;context.now_ms=1010;context.model_generation=7;context.calibration_revision=2;
        context.weapon_index=4;context.gameplay_allowed=true;native.origin={1,2,3};native.scale=1.25F;
    }
    S run(WeaponPlacementProposal* out) const {return propose_weapon_placement(frame,body,calibration,context,native,out);}
};
}
int main() {
    Fixture f;WeaponPlacementProposal p{};
    const auto original=f.native;
    check(f.run(&p)==S::accepted&&p.valid,"explicit reserved alive calibrated sample produces values");
    check(close(p.placement.origin,{119.68504F,192.12598F,292.12598F}),"OpenXR right/up/forward metres map to correct IW world axes");
    check(close(p.weapon_world.axis,xr::Basis3f{}),"identity controller aim yields native identity forward/left/up");
    check(p.placement.scale==original.scale&&!memcmp(&original,&f.native,sizeof(original)),"all32 original placement bytes survive and scale is copied exactly");
    check(p.identity==f.frame.identity&&p.predicted_display_time==123456789&&p.action_sequence==19&&p.calibration_revision==2,"result carries exact reservation prediction action and calibration identities");
    const auto stable=p.weapon_world;
    f.frame.prediction.head_center.orientation=rotation({0,1,0},80);f.frame.prediction.head_center.position.x+=0.3F;
    check(f.run(&p)==S::accepted&&close(p.weapon_world,stable),"head motion alone never drags a stationary tracked weapon");
    check(!close(p.head_world.position,f.body.stock_camera.position),"current head is still composed from the same anchor and body");
    f=Fixture{};f.body.stock_camera.axis=basis(rotation({0,0,1},90));
    check(f.run(&p)==S::accepted&&close(p.placement.origin,{107.87402F,219.68504F,292.12598F})&&close(p.weapon_world.axis.forward,{0,1,0}),"body yaw rotates hand position and aim in the same world direction");
    f=Fixture{};f.body.stock_camera.axis=basis(rotation({0,1,0},45));
    check(f.run(&p)==S::accepted&&close(p.weapon_world,stable),"stock pitch is gravity leveled exactly like stereo");
    f=Fixture{};f.calibration.origin_from_grip_aim_local_meters={0,0,-0.1F};
    f.frame.prediction.actions.hands[1].aim.pose.orientation=rotation({0,1,0},90);
    check(f.run(&p)==S::accepted&&close(p.placement.origin,{119.68504F,196.06299F,292.12598F})&&close(p.weapon_world.axis.forward,{0,1,0}),"grip position plus current aim rotates the explicit model-origin offset");
    f=Fixture{};f.calibration.model_to_aim_iw=rotation({1,0,0},90);
    check(f.run(&p)==S::accepted&&close(p.placement.origin,stable.position)&&close(p.weapon_world.axis.up,{0,-1,0}),"model calibration rotates orientation without moving the grip anchor");
    bool quaternions_match=true;
    for(const auto a:{xr::Vec3f{1,0,0},xr::Vec3f{0,1,0},xr::Vec3f{0,0,1}}) for(float angle:{0.F,45.F,90.F,179.F,180.F,-90.F}) {
        f=Fixture{};f.calibration.model_to_aim_iw=rotation(a,angle);
        quaternions_match=quaternions_match&&f.run(&p)==S::accepted&&close(native_axis(p.placement.quaternion),p.weapon_world.axis);
    }
    check(quaternions_match,"native quaternion expansion roundtrips all three axes including180degree branches");
    f=Fixture{};f.frame.tracking_anchor.orientation=rotation({0,1,0},35);
    f.frame.prediction.actions.hands[1].aim.pose.orientation=rotation({0,1,0},35);
    f.frame.prediction.head_center.orientation=rotation({0,1,0},55);
    f.body.stock_camera.axis=basis(rotation({0,0,1},-25));
    bovr::t5::SceneView stock{};stock.origin=f.body.stock_camera.position;stock.axis=f.body.stock_camera.axis;
    stock.width=1280;stock.height=720;stock.tan_half_fov_x=stock.tan_half_fov_y=1;stock.near_clip=0.1F;
    bovr::t5::StereoViews eyes{};
    check(f.run(&p)==S::accepted&&bovr::t5::build_stereo_views(stock,f.frame.prediction,f.frame.tracking_anchor,&eyes)&&
        close(p.head_world.position,{(eyes.eyes[0].origin.x+eyes.eyes[1].origin.x)*0.5F,(eyes.eyes[0].origin.y+eyes.eyes[1].origin.y)*0.5F,
            (eyes.eyes[0].origin.z+eyes.eyes[1].origin.z)*0.5F})&&close(p.head_world.axis,eyes.eyes[0].axis),"proposal head and actual imported stereo share exactly one body anchor and prediction");
    // Each independent rejection follows a previously accepted output; stale
    // proposal data must never survive an invalid current sample.
    const auto reject=[&](auto mutate,S status,const char* label) {
        f=Fixture{};(void)f.run(&p);mutate(f);const auto result=f.run(&p);check(result==status&&!p.valid&&p.identity.frame_id==0,label);
    };
    reject([](auto& x){x.context.gameplay_allowed=false;},S::context_rejected,"gameplay context rejection clears prior result");
    reject([](auto& x){x.frame.exclusively_reserved_for_scene=false;},S::reservation_mismatch,"peeked pending frame is not a reservation");
    reject([](auto& x){++x.body.identity.native_build_serial;},S::reservation_mismatch,"different native scene build is rejected");
    reject([](auto& x){++x.context.expected.reservation_serial;},S::reservation_mismatch,"expired reservation is rejected");
    reject([](auto& x){++x.frame.anchor_generation;},S::reservation_mismatch,"reset or recenter anchor generation mismatch is rejected");
    reject([](auto& x){++x.frame.prediction.frame_id;},S::reservation_mismatch,"action frame cannot be borrowed from another prediction");
    reject([](auto& x){x.context.now_ms=1101;},S::stale_prediction,"prediction age over100ms is rejected without refresh");
    reject([](auto& x){x.context.now_ms=999;},S::stale_prediction,"backwards wall clock is rejected");
    reject([](auto& x){x.frame.prediction.actions.focused=false;},S::invalid_tracking,"lost runtime focus rejects physical placement");
    reject([](auto& x){x.frame.prediction.actions.hands[1].grip.position_tracked=false;},S::invalid_tracking,"untracked grip rejects physical placement");
    reject([](auto& x){x.frame.prediction.actions.hands[1].aim.orientation_valid=false;},S::invalid_tracking,"invalid aim rejects physical placement");
    reject([](auto& x){x.frame.prediction.actions.hands[1].aim.pose.orientation={0,0,0,0};},S::invalid_tracking,"zero aim quaternion cannot silently normalize to identity");
    reject([](auto& x){x.frame.prediction.head_center.orientation.x=std::numeric_limits<float>::infinity();},S::invalid_prediction,"nonfinite head sample is rejected");
    reject([](auto& x){x.frame.prediction.views_valid=false;},S::invalid_prediction,"invalid eye/head prediction is rejected");
    reject([](auto& x){x.calibration.confirmed=false;},S::calibration_mismatch,"zero defaults never imply physical calibration");
    reject([](auto& x){++x.context.model_generation;},S::calibration_mismatch,"weapon model rebuild invalidates calibration");
    reject([](auto& x){++x.context.weapon_index;},S::calibration_mismatch,"weapon switch invalidates calibration");
    reject([](auto& x){++x.context.calibration_revision;},S::calibration_mismatch,"superseded calibration is rejected");
    reject([](auto& x){x.calibration.origin_from_grip_aim_local_meters.x=3;},S::invalid_calibration,"out-of-policy calibration offset is rejected");
    reject([](auto& x){x.body.source=BodyCameraSource::unknown;},S::invalid_body,"unproven body camera source is rejected");
    reject([](auto& x){x.body.stock_camera.axis.up.z=-1;},S::invalid_body,"reflected native camera basis is rejected");
    reject([](auto& x){x.native.scale=0;},S::invalid_native_placement,"unsupported original native placement is rejected");
    f=Fixture{};f.context.now_ms=1100;check(f.run(&p)==S::accepted,"exact age bound remains accepted");
    f=Fixture{};f.calibration.hand=xr::Hand::Left;f.frame.prediction.actions.hands[0]=f.frame.prediction.actions.hands[1];
    check(f.run(&p)==S::accepted,"explicit left-hand calibration uses the current left-hand sample");
    check(f.run(nullptr)==S::invalid_output,"null output is rejected");
    f=Fixture{};f.native.origin={110,198,296};f.native.quaternion=rotation({0,0,1},37);
    f.frame.prediction.actions.hands[1].aim.pose.orientation=rotation({1,0,0},-20);
    f.body.stock_camera.axis=basis(rotation({0,0,1},68));
    WeaponCalibration derived{};
    check(derive_experimental_native_alignment(f.frame,f.body,f.context,f.native,xr::Hand::Right,&derived)==S::accepted&&derived.confirmed,
        "experimental profile is derived from one explicit reserved native placement");
    f.calibration=derived;
    check(f.run(&p)==S::accepted&&close(p.placement.origin,f.native.origin)&&close(native_axis(p.placement.quaternion),native_axis(f.native.quaternion)),
        "derived explicit profile reproduces original native translation and rotation");
    f.frame.prediction.actions.hands[1].grip.pose.position.x+=0.1F;
    check(f.run(&p)==S::accepted&&!close(p.placement.origin,f.native.origin),"derived profile stays fixed while current tracked hand moves");
    f.context.gameplay_allowed=false;
    check(derive_experimental_native_alignment(f.frame,f.body,f.context,f.native,xr::Hand::Right,&derived)==S::context_rejected&&!derived.confirmed,
        "invalid native context cannot generate or retain an experimental profile");
    printf("checks=%u failures=%u game_writes=0 runtime_calls=0\n",checks,failures);return failures?1:0;
}
