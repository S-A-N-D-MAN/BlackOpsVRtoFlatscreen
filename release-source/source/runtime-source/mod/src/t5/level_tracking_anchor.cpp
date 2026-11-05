// SPDX-License-Identifier: GPL-3.0-only
#include "t5/level_tracking_anchor.hpp"

#include <cmath>

namespace bovr::t5 {

bool level_tracking_anchor(
    const wawvr::xr::Posef& current,
    wawvr::xr::Posef* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    *output = {};
    const auto finite = [](const float value) noexcept {
        return std::isfinite(value);
    };
    const auto& p = current.position;
    const auto& q = current.orientation;
    if (!finite(p.x) || !finite(p.y) || !finite(p.z) ||
        !finite(q.x) || !finite(q.y) || !finite(q.z) || !finite(q.w)) {
        return false;
    }
    const float length_squared =
        q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!finite(length_squared) || length_squared <= 1.0e-12F) {
        return false;
    }

    const auto normalized = wawvr::xr::Normalize(q);
    const auto live_forward = wawvr::xr::Rotate(
        normalized, {0.0F, 0.0F, -1.0F});
    const float horizontal_length_squared =
        live_forward.x * live_forward.x + live_forward.z * live_forward.z;
    if (!finite(horizontal_length_squared) ||
        horizontal_length_squared <= 0.0001F) {
        return false;
    }

    const float yaw = std::atan2(-live_forward.x, -live_forward.z);
    if (!finite(yaw)) {
        return false;
    }
    const auto yaw_only = wawvr::xr::Normalize({
        0.0F,
        std::sin(yaw * 0.5F),
        0.0F,
        std::cos(yaw * 0.5F),
    });
    if (!finite(yaw_only.x) || !finite(yaw_only.y) ||
        !finite(yaw_only.z) || !finite(yaw_only.w)) {
        return false;
    }
    *output = {yaw_only, current.position};
    return true;
}

} // namespace bovr::t5
