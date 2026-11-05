// SPDX-License-Identifier: GPL-3.0-only
#include "input/weapon_placement.hpp"
#include "camera_comfort_logic.hpp"
#include "head_relative_pose_freeze_logic.hpp"
#include "xr_math.h"
#include <cmath>

namespace bovr::input {
namespace {
bool finite(const xr::Vec3f& v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float dot(const xr::Vec3f& a,const xr::Vec3f& b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
xr::Vec3f cross(const xr::Vec3f& a,const xr::Vec3f& b) noexcept {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
xr::Vec3f add(const xr::Vec3f& a,const xr::Vec3f& b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
bool quaternion(const xr::Quaternionf& q) noexcept {
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>0.25F&&n<4.0F;
}
bool pose(const xr::Posef& p) noexcept {return finite(p.position)&&quaternion(p.orientation);}
bool axis(const xr::Basis3f& b) noexcept {
    if(!finite(b.forward)||!finite(b.left)||!finite(b.up)) return false;
    const auto u=cross(b.forward,b.left);
    const xr::Vec3f e{u.x-b.up.x,u.y-b.up.y,u.z-b.up.z};
    return std::fabs(dot(b.forward,b.forward)-1)<0.02F && std::fabs(dot(b.left,b.left)-1)<0.02F &&
        std::fabs(dot(b.up,b.up)-1)<0.02F && std::fabs(dot(b.forward,b.left))<0.02F &&
        std::fabs(dot(b.forward,b.up))<0.02F && std::fabs(dot(b.left,b.up))<0.02F && dot(e,e)<0.0004F;
}
xr::Vec3f compose(const xr::Basis3f& b,const xr::Vec3f& v) noexcept {
    return {b.forward.x*v.x+b.left.x*v.y+b.up.x*v.z,
            b.forward.y*v.x+b.left.y*v.y+b.up.y*v.z,
            b.forward.z*v.x+b.left.z*v.y+b.up.z*v.z};
}
xr::Basis3f compose(const xr::Basis3f& b,const xr::Basis3f& local) noexcept {
    return {compose(b,local.forward),compose(b,local.left),compose(b,local.up)};
}
// Native UnitQuatToAxis stores the three rotated IW unit axes as rows.
xr::Basis3f quaternion_axis(const xr::Quaternionf& q) noexcept {
    return {xr::Rotate(q,{1,0,0}),xr::Rotate(q,{0,1,0}),xr::Rotate(q,{0,0,1})};
}
xr::Quaternionf axis_quaternion(const xr::Basis3f& b) noexcept {
    // Stable largest-component conversion of the column rotation matrix;
    // b.forward/left/up are its columns despite native row-major storage.
    const float m00=b.forward.x,m01=b.left.x,m02=b.up.x;
    const float m10=b.forward.y,m11=b.left.y,m12=b.up.y;
    const float m20=b.forward.z,m21=b.left.z,m22=b.up.z;
    xr::Quaternionf q{};
    const float trace=m00+m11+m22;
    if(trace>0) {
        const float s=2*std::sqrt(trace+1);q={(m21-m12)/s,(m02-m20)/s,(m10-m01)/s,s/4};
    } else if(m00>m11 && m00>m22) {
        const float s=2*std::sqrt(1+m00-m11-m22);q={s/4,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s};
    } else if(m11>m22) {
        const float s=2*std::sqrt(1+m11-m00-m22);q={(m01+m10)/s,s/4,(m12+m21)/s,(m02-m20)/s};
    } else {
        const float s=2*std::sqrt(1+m22-m00-m11);q={(m02+m20)/s,(m12+m21)/s,s/4,(m10-m01)/s};
    }
    return xr::Normalize(q);
}
bool identity(const WeaponFrameIdentity& i) noexcept {
    return i.generation&&i.frame_id&&i.reservation_serial&&i.native_build_serial;
}
}
WeaponPlacementStatus propose_weapon_placement(const ReservedWeaponFrame& reserved,
    const WeaponBodySample& body,const WeaponCalibration& calibration,const WeaponPlacementContext& context,
    const ScaledWeaponPlacement& original,WeaponPlacementProposal* output,float units) noexcept {
    using S=WeaponPlacementStatus;
    if(!output) return S::invalid_output;
    *output={};
    if(!context.gameplay_allowed) return S::context_rejected;
    const auto& key=reserved.identity;const auto& frame=reserved.prediction;
    if(!identity(key)||!reserved.exclusively_reserved_for_scene||key!=context.expected||key!=body.identity||
       key.frame_id!=frame.frame_id||reserved.anchor_generation!=key.generation) return S::reservation_mismatch;
    if(!reserved.published_ms||context.now_ms<reserved.published_ms||
       context.now_ms-reserved.published_ms>kMaximumWeaponPredictionAgeMs) return S::stale_prediction;
    if(!frame.should_render||!frame.views_valid||frame.predicted_display_time<=0||
       frame.predicted_display_period<=0||!frame.actions.sequence||!pose(frame.head_center)||
       !pose(reserved.tracking_anchor)||!std::isfinite(units)||units<=0||units>1000) return S::invalid_prediction;
    const auto hand=static_cast<unsigned>(calibration.hand);
    if(hand>=xr::kHandCount) return S::invalid_calibration;
    const auto& grip=frame.actions.hands[hand].grip;const auto& aim=frame.actions.hands[hand].aim;
    if(!frame.actions.focused||!grip.active||!grip.position_valid||!grip.position_tracked||!finite(grip.pose.position)||
       !aim.active||!aim.orientation_valid||!aim.orientation_tracked||!quaternion(aim.pose.orientation)) return S::invalid_tracking;
    if(!calibration.confirmed||!calibration.weapon_index||!calibration.model_generation||!calibration.revision||
       calibration.weapon_index!=context.weapon_index||calibration.model_generation!=context.model_generation||
       calibration.revision!=context.calibration_revision) return S::calibration_mismatch;
    if(!finite(calibration.origin_from_grip_aim_local_meters)||!quaternion(calibration.model_to_aim_iw)||
       dot(calibration.origin_from_grip_aim_local_meters,calibration.origin_from_grip_aim_local_meters)>4.0F) return S::invalid_calibration;
    if(body.source!=BodyCameraSource::completed_native_refdef||!finite(body.stock_camera.position)||!axis(body.stock_camera.axis)) return S::invalid_body;
    if(!quaternion(original.quaternion)||!finite(original.origin)||!std::isfinite(original.scale)||original.scale<=0||original.scale>100) return S::invalid_native_placement;
    WeaponPlacementProposal result{};
    result.body_world.position=body.stock_camera.position;
    if(!wawvr::mod::gravity_level_t4_camera_axis(body.stock_camera.axis,&result.body_world.axis)||
       !wawvr::mod::compose_head_world_pose(result.body_world,frame.head_center,reserved.tracking_anchor,units,&result.head_world)) return S::invalid_body;
    // Grip supplies translation and aim supplies orientation. There is no
    // attachment to current head: a head turn alone cannot move the weapon.
    xr::Posef calibrated{};calibrated.orientation=aim.pose.orientation;
    calibrated.position=add(grip.pose.position,xr::Rotate(aim.pose.orientation,calibration.origin_from_grip_aim_local_meters));
    const auto local=xr::OpenXrPoseToIwRelative(calibrated,reserved.tracking_anchor,units);
    result.weapon_world.position=add(result.body_world.position,compose(result.body_world.axis,local.position));
    result.weapon_world.axis=compose(compose(result.body_world.axis,local.axis),quaternion_axis(calibration.model_to_aim_iw));
    if(!finite(result.weapon_world.position)||!axis(result.weapon_world.axis)) return S::invalid_result;
    result.placement={axis_quaternion(result.weapon_world.axis),result.weapon_world.position,original.scale};
    if(!quaternion(result.placement.quaternion)) return S::invalid_result;
    result.identity=key;result.predicted_display_time=frame.predicted_display_time;
    result.action_sequence=frame.actions.sequence;result.calibration_revision=calibration.revision;result.valid=true;
    *output=result;return S::accepted;
}
WeaponPlacementStatus derive_experimental_native_alignment(const ReservedWeaponFrame& frame,
    const WeaponBodySample& body,const WeaponPlacementContext& context,const ScaledWeaponPlacement& original,
    xr::Hand hand,WeaponCalibration* output,float units) noexcept {
    using S=WeaponPlacementStatus;
    if(!output) return S::invalid_output;
    *output={};
    WeaponCalibration calibration{};calibration.confirmed=true;calibration.hand=hand;
    calibration.weapon_index=context.weapon_index;calibration.model_generation=context.model_generation;
    calibration.revision=context.calibration_revision;
    WeaponPlacementProposal aim{};
    const auto status=propose_weapon_placement(frame,body,calibration,context,original,&aim,units);
    if(status!=S::accepted) return status;
    const auto& parent=aim.weapon_world.axis;
    const xr::Vec3f delta{original.origin.x-aim.weapon_world.position.x,original.origin.y-aim.weapon_world.position.y,
        original.origin.z-aim.weapon_world.position.z};
    const xr::Vec3f offset{dot(delta,parent.forward),dot(delta,parent.left),dot(delta,parent.up)};
    calibration.origin_from_grip_aim_local_meters={-offset.y/units,offset.z/units,-offset.x/units};
    const auto native=quaternion_axis(original.quaternion);
    const auto project=[&](const xr::Vec3f& v) {return xr::Vec3f{dot(v,parent.forward),dot(v,parent.left),dot(v,parent.up)};};
    calibration.model_to_aim_iw=axis_quaternion({project(native.forward),project(native.left),project(native.up)});
    WeaponPlacementProposal aligned{};
    const auto validated=propose_weapon_placement(frame,body,calibration,context,original,&aligned,units);
    if(validated!=S::accepted) return validated;
    *output=calibration;return S::accepted;
}
}
