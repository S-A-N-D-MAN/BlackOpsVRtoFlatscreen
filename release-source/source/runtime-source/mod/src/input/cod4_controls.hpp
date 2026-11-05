// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/two_hand_aim.hpp"
namespace bovr::input {
// The sight comes to the head; left trigger remains available for jump/mantle.
// Uses only the original current command prediction, with release hysteresis.
inline bool cod4_shouldered(const wawvr::xr::FrameState& f,bool previous) noexcept {
    namespace x=wawvr::xr;
    if(!f.views_valid||!f.should_render||!f.actions.focused)return false;
    const auto pair=two_hand_aim_basis(f.actions,f.head_center,x::Hand::Right,true);
    if(!pair.paired||pair.local.forward.x<(previous?.72f:.82f))return false;
    const auto local=x::OpenXrPoseToIwRelative(f.actions.hands[1].grip.pose,f.head_center,1.f).position;
    const float radius=previous?.52f:.44f;
    return std::isfinite(local.x)&&std::isfinite(local.y)&&std::isfinite(local.z)&&
        local.x>.04f&&local.x<radius&&std::fabs(local.y)<.30f&&local.z>-.23f&&local.z<.16f&&
        local.x*local.x+local.y*local.y+local.z*local.z<radius*radius;
}
}
