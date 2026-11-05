// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"
#include "xr_math.h"
#include "camera_comfort_logic.hpp"
#include "head_relative_pose_freeze_logic.hpp"
#include <cmath>

namespace bovr::input {
namespace helicopter_head_detail {
namespace xr=wawvr::xr;
inline bool finite(xr::Vec3f v) noexcept {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
inline float dot(xr::Vec3f a,xr::Vec3f b) noexcept {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
inline bool pose(const xr::Posef& p) noexcept {
    const auto& q=p.orientation;
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return finite(p.position)&&std::isfinite(n)&&n>.98F&&n<1.02F;
}
inline bool basis(const xr::Basis3f& b) noexcept {
    const auto& f=b.forward;const auto& l=b.left;const auto& u=b.up;
    const xr::Vec3f crossed{f.y*l.z-f.z*l.y,f.z*l.x-f.x*l.z,f.x*l.y-f.y*l.x};
    return finite(f)&&finite(l)&&finite(u)&&std::fabs(dot(f,f)-1)<.02F&&
        std::fabs(dot(l,l)-1)<.02F&&std::fabs(dot(u,u)-1)<.02F&&
        std::fabs(dot(f,l))<.02F&&std::fabs(dot(f,u))<.02F&&
        std::fabs(dot(l,u))<.02F&&dot(crossed,u)>.98F;
}
}

// Pure common-head geometry from the original command prediction. The ray is
// out->position + distance * out->axis.forward, with distance in IW units.
// This uses precisely the stereo renderer's gravity-level body composition;
// it neither changes native flight/look angles nor grants shot authority.
// The caller must independently prove the copied stock refdef and current
// vehicle/command ownership. No later XR pose may replace this rig's values.
inline bool helicopter_head_ray(const CommandRigSample& rig,
    const wawvr::xr::EnginePose& stock,std::uint64_t now,
    wawvr::xr::EnginePose* out) noexcept {
    if(!out)return false;*out={};
    const auto& f=rig.frame;
    if(!rig.xr_generation||rig.anchor_generation!=rig.xr_generation||
       !rig.published_ms||now<rig.published_ms||now-rig.published_ms>100||
       !f.frame_id||!f.actions.sequence||!f.actions.focused||
       !f.should_render||!f.views_valid||f.predicted_display_time<=0||f.predicted_display_period<=0||
       !helicopter_head_detail::pose(f.head_center)||!helicopter_head_detail::pose(rig.tracking_anchor)||
       !helicopter_head_detail::finite(stock.position)||!helicopter_head_detail::basis(stock.axis))return false;
    for(const auto& eye:f.eyes)if(!helicopter_head_detail::pose(eye.pose))return false;
    wawvr::xr::EnginePose body{stock.position,{}};
    if(!wawvr::mod::gravity_level_t4_camera_axis(stock.axis,&body.axis))return false;
    return wawvr::mod::compose_head_world_pose(body,f.head_center,rig.tracking_anchor,
        wawvr::xr::kIwUnitsPerMeter,out);
}
}
