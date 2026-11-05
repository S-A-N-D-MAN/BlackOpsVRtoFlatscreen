// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/tracked_gun_pose.hpp"

namespace bovr::t5::grenade_visual_pose {
namespace a=animation_detail;
namespace xr=wawvr::xr;
// The caller separately validates the current grip, anchor and native body.
// This target belongs only to the glove/grenade visual ticket. Gesture zones,
// release velocity and weapon aim continue to use their original tracked poses.
inline a::Transform hand(const xr::HandActionState& support,const xr::Posef& anchor,
    const a::Transform& body) noexcept {
    const auto& surface=support.palm;
    const bool palm_surface=surface.active&&surface.position_valid&&surface.position_tracked&&
        surface.orientation_valid&&surface.orientation_tracked&&a::finite(surface.pose.position)&&
        input::tracked_detail::unit(surface.pose.orientation);
    const auto& pose=palm_surface?surface.pose:support.grip.pose;
    const auto local=xr::OpenXrPoseToIwRelative(pose,anchor,xr::kIwUnitsPerMeter);
    auto target=a::compose(body,{local.position,local.axis});
    if(palm_surface){
        // reload_pose::pose maps the anatomical source palm to palm_ext by
        // {up,-left,forward}. That rotation is its own inverse: applying it to
        // the target gives the identical glove pose with the canonical source
        // palm used by grenade_pose, and keeps the grenade's inset with it.
        const auto left=target.axis.left;
        target.axis={target.axis.up,{-left.x,-left.y,-left.z},target.axis.forward};
    }
    return target;
}
}
