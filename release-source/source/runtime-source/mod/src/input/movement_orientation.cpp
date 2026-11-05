// SPDX-License-Identifier: GPL-3.0-only
#include "movement_orientation.hpp"
#include "xr_math.h"
#include <cmath>

namespace bovr::input {
namespace {
namespace xr = wawvr::xr;

bool finite(const xr::Quaternionf& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool unit(const xr::Quaternionf& value) noexcept {
    if (!finite(value)) return false;
    const float length = value.x * value.x + value.y * value.y +
                         value.z * value.z + value.w * value.w;
    return length > 0.98f && length < 1.02f;
}

bool tracked_orientation(const xr::TrackedPose& pose) noexcept {
    return pose.active && pose.orientation_valid && pose.orientation_tracked &&
           unit(pose.pose.orientation);
}
}

bool orient_movement(
    const xr::FrameState& frame,
    const xr::Posef& tracking_anchor,
    bool left_handed,
    MovementOrientation orientation,
    float stick_forward,
    float stick_right,
    float* forward,
    float* right) noexcept {
    if (!forward || !right) return false;
    *forward = 0.0f;
    *right = 0.0f;
    if (!std::isfinite(stick_forward) || !std::isfinite(stick_right) ||
        !unit(tracking_anchor.orientation)) return false;

    xr::Posef reference{};
    if (orientation == MovementOrientation::hmd) {
        if (!frame.views_valid || !unit(frame.head_center.orientation)) return false;
        reference = frame.head_center;
    } else {
        const unsigned dominant = left_handed ? 0u : 1u;
        const auto& support = frame.actions.hands[dominant ^ 1u];
        if (tracked_orientation(support.aim)) reference = support.aim.pose;
        else if (tracked_orientation(support.grip)) reference = support.grip.pose;
        else return false;
    }

    // Translation is irrelevant here. Giving both poses the same finite
    // position makes the conversion's orientation-only contract explicit.
    reference.position = tracking_anchor.position;
    const auto local = xr::OpenXrPoseToIwRelative(reference, tracking_anchor, 1.0f);
    float basis_forward = local.axis.forward.x;
    float basis_left = local.axis.forward.y;
    const float horizontal = std::sqrt(
        basis_forward * basis_forward + basis_left * basis_left);
    if (!std::isfinite(horizontal) || horizontal <= 0.0001f) return false;
    basis_forward /= horizontal;
    basis_left /= horizontal;

    // CoD commands use +forward/+right while the converted pose uses
    // +forward/+left. This matches COD4 VR's established movement transform.
    *forward = stick_forward * basis_forward + stick_right * basis_left;
    *right = stick_right * basis_forward - stick_forward * basis_left;
    return std::isfinite(*forward) && std::isfinite(*right);
}

}
