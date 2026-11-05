// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_math.h"

namespace bovr::t5 {

// Captures the current HMD position with a yaw-only orientation. Live head
// pitch and roll remain in the current pose; keeping them out of the persistent
// anchor prevents a tilted startup/recenter pose from tilting the game world.
// A near-vertical headset pose has no stable observable yaw and is refused.
[[nodiscard]] bool level_tracking_anchor(
    const wawvr::xr::Posef& current,
    wawvr::xr::Posef* output) noexcept;

} // namespace bovr::t5
