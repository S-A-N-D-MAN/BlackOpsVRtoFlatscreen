// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/weapon_placement.hpp"
#include "input/two_hand_aim.hpp"
#include "t5/weapon_animation_math.hpp"
#include "camera_comfort_logic.hpp"
#include <cstring>

namespace bovr::input {
namespace tracked_detail {
namespace a=t5::animation_detail;
inline bool unit(xr::Quaternionf q) noexcept {
    const auto n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&std::fabs(n-1)<0.002F;
}
// Shared value-only core: callers supply their independently proved original
// prediction, anchor and body. This function does not mint a command/scene lease.
inline bool compose_tracked_gun(const xr::FrameState& f,const xr::Posef& anchor,
    const xr::EnginePose& body,xr::Hand hand,xr::Vec3f offset,xr::Quaternionf calibration,
    a::Transform* out,float units=xr::kIwUnitsPerMeter,bool two_hand_support=false,SupportAimBasis* support=nullptr) noexcept {
    if(support)*support={};
    if(!out)return false;*out={};const auto h=static_cast<unsigned>(hand);
    if(h>=xr::kHandCount||!a::finite(body.position)||!a::rigid(body.axis)||
       !a::finite(anchor.position)||!unit(anchor.orientation)||!a::finite(offset)||a::dot(offset,offset)>4||
       !unit(calibration)||!std::isfinite(units)||units<=0||units>1000)return false;
    const auto& grip=f.actions.hands[h].grip;const auto& aim=f.actions.hands[h].aim;
    if(!f.actions.focused||!grip.active||!grip.position_valid||!grip.position_tracked||!a::finite(grip.pose.position)||
       !aim.active||!aim.orientation_valid||!aim.orientation_tracked||!unit(aim.pose.orientation))return false;
    xr::Posef calibrated{aim.pose.orientation,a::add(grip.pose.position,xr::Rotate(aim.pose.orientation,offset))};
    auto local=xr::OpenXrPoseToIwRelative(calibrated,anchor,units);
    // This opt-in is confined to A2's exact zero-offset identity policy.
    if(two_hand_support&&offset.x==0&&offset.y==0&&offset.z==0&&calibration.x==0&&calibration.y==0&&calibration.z==0&&calibration.w==1){
        const auto pair=two_hand_aim_basis(f.actions,anchor,hand,true);if(support)*support=pair;
        if(pair.paired)local.axis=pair.local;}
    const a::Transform p{body.position,body.axis},l{local.position,local.axis};
    const a::Transform c{{},{xr::Rotate(calibration,{1,0,0}),xr::Rotate(calibration,{0,1,0}),xr::Rotate(calibration,{0,0,1})}};
    const auto value=a::compose(a::compose(p,l),c);
    if(!a::finite(value.origin)||!a::rigid(value.axis))return false;*out=value;return true;
}
}
struct BorrowedPlacementValues {
    WeaponFrameIdentity identity{};
    t5::animation_detail::Transform body{},gun{},dobj{},barrel{};
    std::int64_t predicted_time{};std::uint64_t action_sequence{},original_published_ms{};
    bool valid{};
    SupportAimBasis support{};
};
enum class BorrowedPlacementStatus {proposed,identity,stale,prediction,body,geometry,tracking};
// Explicit experimental profile: j_gun at right grip, aim rotation, zero metric
// offset. Fixed authored barrel is checked by the native borrowed adapter. No
// physical fit, allocation generation, shot authority or pointer is asserted.
inline BorrowedPlacementStatus propose_borrowed_placement(const ReservedWeaponFrame& r,const WeaponBodySample& body,
    const t5::animation_detail::LocalGeometry& current,std::uint64_t now,BorrowedPlacementValues* out,bool two_hand_support=false,float kickback_meters=0.f) noexcept {
    using S=BorrowedPlacementStatus;namespace a=t5::animation_detail;
    if(!out)return S::identity;*out={};const auto& k=r.identity;const auto& f=r.prediction;
    if(!k.generation||!k.frame_id||!k.reservation_serial||!k.native_build_serial||!r.exclusively_reserved_for_scene||
       body.identity!=k||r.anchor_generation!=k.generation||f.frame_id!=k.frame_id)return S::identity;
    if(!r.published_ms||now<r.published_ms||now-r.published_ms>100)return S::stale;
    if(!f.should_render||!f.views_valid||f.predicted_display_time<=0||f.predicted_display_period<=0||!f.actions.sequence||
       !a::finite(f.head_center.position)||!tracked_detail::unit(f.head_center.orientation))return S::prediction;
    xr::EnginePose level{body.stock_camera.position,{}};
    if(body.source!=BodyCameraSource::completed_native_refdef||!a::finite(level.position)||!a::rigid(body.stock_camera.axis)||
       !wawvr::mod::gravity_level_t4_camera_axis(body.stock_camera.axis,&level.axis))return S::body;
    if(!a::finite(r.camera_translation))return S::body;
    level.position=a::add(level.position,r.camera_translation);
    if(!std::isfinite(kickback_meters)||kickback_meters<0.f||kickback_meters>0.03f||
       !a::finite(current.gun_in_dobj.origin)||!a::rigid(current.gun_in_dobj.axis)||
       !a::finite(current.flash_in_gun.origin)||!a::rigid(current.flash_in_gun.axis))return S::geometry;
    BorrowedPlacementValues value{};a::Transform aimed{};
    if(!tracked_detail::compose_tracked_gun(f,r.tracking_anchor,level,xr::Hand::Right,{},xr::Quaternionf{},&aimed,xr::kIwUnitsPerMeter,two_hand_support,&value.support))return S::tracking;
    // The controller supplies the desired barrel frame. Most weapons author
    // tag_flash straight along j_gun, but the FAL has a fixed bind rotation.
    // Solve only that exact current local axis out of j_gun while preserving the
    // tracked grip position. The adapter separately proves this axis against the
    // copied runtime profile before this value-only function is called.
    const a::Transform identity{{},{{1,0,0},{0,1,0},{0,0,1}}};
    const auto muzzle_inverse=a::relative({{},current.flash_in_gun.axis},identity);
    value.gun=a::compose(aimed,{{},muzzle_inverse.axis});
    // Visual firing feedback displaces the weapon only. Preserve every axis so
    // the accepted command/barrel direction and headset camera stay unchanged.
    const float kick=kickback_meters*xr::kIwUnitsPerMeter;
    value.gun.origin=a::sub(value.gun.origin,{aimed.axis.forward.x*kick,aimed.axis.forward.y*kick,aimed.axis.forward.z*kick});
    // D = G * inverse(A). Native original would own D's persistent pose for the
    // entire frame; this experiment ONLY computes the value and never selects it.
    value.dobj=a::compose(value.gun,a::relative(current.gun_in_dobj,identity));
    value.barrel=a::compose(value.gun,current.flash_in_gun);value.body={level.position,level.axis};
    const auto check=a::compose(value.dobj,current.gun_in_dobj);
    if(!a::finite(value.dobj.origin)||!a::rigid(value.dobj.axis)||!a::finite(value.barrel.origin)||!a::rigid(value.barrel.axis)||
       a::distance(check.origin,value.gun.origin)>0.02F||a::axis_error(check.axis,value.gun.axis)>0.01F||
       a::axis_error(value.barrel.axis,aimed.axis)>0.002F)return S::geometry;
    value.identity=k;value.predicted_time=f.predicted_display_time;value.action_sequence=f.actions.sequence;
    value.original_published_ms=r.published_ms;value.valid=true;*out=value;return S::proposed;
}
}
