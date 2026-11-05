// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"
#include <cstdint>

namespace bovr::input {

enum class MovementOrientation : std::uint8_t {
    hmd,
    controller,
};

// Rotates the already deadzone-filtered movement stick into the selected
// tracked reference. The controller reference is the same semantic support
// hand that owns movement, so left-handed controls select the right controller.
// Failure leaves both outputs neutral instead of silently changing direction.
bool orient_movement(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    bool left_handed,
    MovementOrientation orientation,
    float stick_forward,
    float stick_right,
    float* forward,
    float* right) noexcept;

}
