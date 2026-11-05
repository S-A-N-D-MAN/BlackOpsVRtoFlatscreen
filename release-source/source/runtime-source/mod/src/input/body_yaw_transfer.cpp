// SPDX-License-Identifier: GPL-3.0-only
#include "input/body_yaw_transfer.hpp"

#include "xr_math.h"
#include <cmath>

namespace bovr::input {
namespace {
namespace xr = wawvr::xr;

bool finite(const xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool unit(const xr::Quaternionf& value) noexcept {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z) || !std::isfinite(value.w)) return false;
    const float length = value.x * value.x + value.y * value.y +
                         value.z * value.z + value.w * value.w;
    return length > 0.98f && length < 1.02f;
}
}

bool plan_hmd_body_yaw_transfer(
    const xr::FrameState& frame,
    const xr::Posef& tracking_anchor,
    const float minimum_degrees,
    BodyYawTransferPlan* const output) noexcept {
    if (!output) return false;
    *output = {};
    if (!frame.should_render || !frame.views_valid || !finite(frame.head_center.position) ||
        !finite(tracking_anchor.position) || !unit(frame.head_center.orientation) ||
        !unit(tracking_anchor.orientation) || !std::isfinite(minimum_degrees) ||
        minimum_degrees < 0.0f || minimum_degrees > 45.0f) return false;

    xr::Posef head_rotation = frame.head_center;
    head_rotation.position = tracking_anchor.position;
    const auto relative = xr::OpenXrPoseToIwRelative(
        head_rotation, tracking_anchor, 1.0f);
    const float horizontal = std::hypot(
        relative.axis.forward.x, relative.axis.forward.y);
    if (!std::isfinite(horizontal) || horizontal <= 0.01f) return false;
    const float delta = std::atan2(
        relative.axis.forward.y, relative.axis.forward.x) * 57.29577951308232f;
    if (!std::isfinite(delta) || std::fabs(delta) < minimum_degrees) return false;

    const auto normalized = xr::Normalize(frame.head_center.orientation);
    const auto live_forward = xr::Rotate(normalized, {0.0f, 0.0f, -1.0f});
    const float horizontal_squared = live_forward.x * live_forward.x +
                                     live_forward.z * live_forward.z;
    if (!finite(live_forward) || horizontal_squared <= 0.0001f) return false;
    const float yaw = std::atan2(-live_forward.x, -live_forward.z);
    if (!std::isfinite(yaw)) return false;
    const auto yaw_only = xr::Normalize({
        0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)});
    if (!unit(yaw_only)) return false;

    output->desired_anchor = {yaw_only, tracking_anchor.position};
    output->delta_degrees = delta;
    return true;
}

} // namespace bovr::input
