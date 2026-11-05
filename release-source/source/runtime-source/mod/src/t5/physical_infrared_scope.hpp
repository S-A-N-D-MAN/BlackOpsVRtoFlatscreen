// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/crossbow_optic.hpp"
#include "input/command_rig_ledger.hpp"
namespace bovr::t5 {
// Uses the same physical optic geometry as composition. No ADS/FOV mutation:
// the authored infrared scene is requested only while this optic is raised.
inline bool physical_infrared_scope(const input::CommandRigSample& rig,
    const crossbow::LensFit& lens,std::uint64_t generation,std::uint64_t now) noexcept {
    const auto& frame=rig.frame;
    if(!generation||rig.xr_generation!=generation||rig.anchor_generation!=generation||
       !rig.published_ms||now<rig.published_ms||now-rig.published_ms>100||
       !frame.frame_id||!frame.should_render||!frame.views_valid||!frame.actions.focused||
       (frame.actions.menu.active&&frame.actions.menu.current))return false;
    wawvr::xr::StereoSourceLayout layout{};
    layout.eyes[0]={0,0,.5f,1};layout.eyes[1]={.5f,0,.5f,1};
    layout.destinations[0]=layout.destinations[1]={0,0,1,1};
    return crossbow::scope(frame,&layout,lens)&&layout.physical_scope.active;
}
}
