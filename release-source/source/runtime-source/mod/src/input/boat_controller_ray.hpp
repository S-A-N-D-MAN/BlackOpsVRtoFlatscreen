// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/helicopter_head_ray.hpp"

namespace bovr::input {
// The mounted guns keep their native muzzle origins. Select a distant target
// using the right aim orientation in the same body/anchor frame as rendering.
// Retain the view origin so hand translation cannot pull the mounted gun off
// its pivot. Head rotation does not participate in the pointing direction.
inline bool boat_controller_ray(const CommandRigSample& rig,
    const wawvr::xr::EnginePose& stock,std::uint64_t now,
    wawvr::xr::EnginePose* out) noexcept {
    if(!out)return false;*out={};
    wawvr::xr::EnginePose head{};
    if(!helicopter_head_ray(rig,stock,now,&head))return false;
    const auto& aim=rig.frame.actions.hands[1].aim;
    if(!aim.active||!aim.orientation_valid||!aim.orientation_tracked)return false;
    const wawvr::xr::Posef pointing{aim.pose.orientation,rig.frame.head_center.position};
    if(!helicopter_head_detail::pose(pointing))return false;
    wawvr::xr::EnginePose body{stock.position,{}},hand{};
    if(!wawvr::mod::gravity_level_t4_camera_axis(stock.axis,&body.axis)||
       !wawvr::mod::compose_head_world_pose(body,pointing,rig.tracking_anchor,
           wawvr::xr::kIwUnitsPerMeter,&hand))return false;
    *out={head.position,hand.axis};return true;
}
}
