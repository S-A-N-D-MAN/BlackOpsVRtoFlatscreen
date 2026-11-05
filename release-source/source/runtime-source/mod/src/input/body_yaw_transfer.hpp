// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace bovr::input {

// Consume a physical head turn only after it is clearly intentional.  The
// previous half-degree threshold let ordinary tracking motion repeatedly
// rewrite native body yaw while walking, which made scripted camera handoffs
// such as Vorkuta's opening knife transition visibly unstable.
inline constexpr float kHmdBodyYawTransferDeadbandDegrees = 15.0f;

struct BodyYawTransferPlan final {
    wawvr::xr::Posef desired_anchor{};
    float delta_degrees{};
};

// Plans the equal body-yaw/tracking-anchor change used while the player is
// moving in HMD-oriented mode. The anchor position is retained so physical
// room-scale translation never becomes artificial locomotion.
[[nodiscard]] bool plan_hmd_body_yaw_transfer(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    float minimum_degrees,
    BodyYawTransferPlan* output) noexcept;

} // namespace bovr::input
