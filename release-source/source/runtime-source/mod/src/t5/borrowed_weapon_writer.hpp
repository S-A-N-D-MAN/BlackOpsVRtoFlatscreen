// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/tracked_gun_pose.hpp"
#include "t5/native_transform_precision.hpp"
#include <algorithm>

namespace bovr::t5 {
namespace borrowed_writer {
namespace a=animation_detail;
using Placement=input::ScaledWeaponPlacement;
struct Witness {
    bool selected{},normal_verified{};
    a::Transform normal_gun{},normal_barrel{},normal_pose{};
    native_precision::ComposedPositionBudget position_budget{};
};
inline bool close(const a::Transform& x,const a::Transform& y) noexcept {
    return a::finite(x.origin)&&a::finite(y.origin)&&a::rigid(x.axis)&&a::rigid(y.axis)&&
        a::distance(x.origin,y.origin)<=0.002F&&a::axis_error(x.axis,y.axis)<=0.002F;
}
inline bool close_native(const a::Transform& expected,const a::Transform& actual) noexcept {
    return a::rigid(expected.axis)&&a::rigid(actual.axis)&&
        native_precision::world_position_close(expected.origin,actual.origin)&&
        a::axis_error(expected.axis,actual.axis)<=0.002F;
}
inline bool close_composed(const a::Transform& expected,const a::Transform& actual,double budget) noexcept {
    return a::rigid(expected.axis)&&a::rigid(actual.axis)&&
        native_precision::within_position(expected.origin,actual.origin,budget)&&
        a::axis_error(expected.axis,actual.axis)<=0.002F;
}
inline wawvr::xr::Quaternionf quaternion(const a::Axis& b) noexcept {
    // Columns are the rotated IW F/L/U basis, matching native UnitQuatToAxis.
    const float m00=b.forward.x,m01=b.left.x,m02=b.up.x;
    const float m10=b.forward.y,m11=b.left.y,m12=b.up.y;
    const float m20=b.forward.z,m21=b.left.z,m22=b.up.z;
    wawvr::xr::Quaternionf q{};const auto trace=m00+m11+m22;
    if(trace>0){const auto s=std::sqrt(trace+1)*2;q={ (m21-m12)/s,(m02-m20)/s,(m10-m01)/s,s*.25F};}
    else if(m00>m11&&m00>m22){const auto s=std::sqrt(1+m00-m11-m22)*2;q={s*.25F,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s};}
    else if(m11>m22){const auto s=std::sqrt(1+m11-m00-m22)*2;q={(m01+m10)/s,s*.25F,(m12+m21)/s,(m02-m20)/s};}
    else{const auto s=std::sqrt(1+m22-m00-m11)*2;q={(m02+m20)/s,(m12+m21)/s,s*.25F,(m10-m01)/s};}
    const auto n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(n)||n<1.e-6F)return {0,0,0,0};
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
inline bool make_value(const input::BorrowedPlacementValues& p,const Placement& original,
                       std::uint64_t now,Placement* out) noexcept {
    if(!out)return false;*out={};
    if(!p.valid||!p.identity.generation||!p.identity.frame_id||!p.identity.reservation_serial||!p.identity.native_build_serial||
       !p.original_published_ms||now<p.original_published_ms||now-p.original_published_ms>100||
       !input::tracked_detail::unit(original.quaternion)||!a::finite(original.origin)||original.scale!=1.F||
       !a::finite(p.dobj.origin)||!a::rigid(p.dobj.axis)||!a::finite(p.gun.origin)||!a::rigid(p.gun.axis)||
       !a::finite(p.barrel.origin)||!a::rigid(p.barrel.axis))return false;
    Placement candidate{quaternion(p.dobj.axis),p.dobj.origin,original.scale};a::Transform decoded{};
    const auto q=candidate.quaternion;
    if(!a::convert_native({{q.x,q.y,q.z,q.w},candidate.origin,2},&decoded)||!close(decoded,p.dobj))return false;
    *out=candidate;return true;
}
inline bool observe_normal(Witness& w,const input::BorrowedPlacementValues& expected,
                           const a::LocalGeometry& actual,const a::LocalGeometry& camera) noexcept {
    const auto budget=native_precision::composed_position_budget(camera,expected.dobj.origin,
        expected.gun.origin,expected.barrel.origin,actual.gun_world.origin,actual.flash_world.origin);
    if(!w.selected||w.normal_verified||!expected.valid||!budget.valid||
       !a::rigid(camera.pose_world.axis)||!a::rigid(camera.gun_world.axis)||!a::rigid(camera.flash_world.axis)||
       !close_composed(expected.gun,actual.gun_world,budget.gun)||
       !close_composed(expected.barrel,actual.flash_world,budget.barrel)||!close_native(expected.dobj,actual.pose_world))return false;
    w.normal_gun=actual.gun_world;w.normal_barrel=actual.flash_world;w.normal_pose=actual.pose_world;
    w.position_budget=budget;w.normal_verified=true;return true;
}
inline bool observe_after(const Witness& w,const input::BorrowedPlacementValues& expected,
                          const a::LocalGeometry& actual) noexcept {
    // A second read of the same native occurrence must still match the saved
    // native witness at the original tolerance; it has no independent rounding.
    return w.selected&&w.normal_verified&&expected.valid&&w.position_budget.valid&&
        close_composed(expected.gun,actual.gun_world,w.position_budget.gun)&&
        close_composed(expected.barrel,actual.flash_world,w.position_budget.barrel)&&close_native(expected.dobj,actual.pose_world)&&
        close(w.normal_gun,actual.gun_world)&&close(w.normal_barrel,actual.flash_world)&&close(w.normal_pose,actual.pose_world);
}
// Passive limits remain exact when writing is disabled. Active preparation is
// fresh per native camera clear; only logs/raw evidence retain the old budget.
inline bool prepare_camera(bool writer,std::uint64_t attempts,std::uint64_t previous,std::uint64_t now) noexcept {
    return writer||(attempts<512&&(!previous||(now>=previous&&now-previous>=100)));
}
}
}
