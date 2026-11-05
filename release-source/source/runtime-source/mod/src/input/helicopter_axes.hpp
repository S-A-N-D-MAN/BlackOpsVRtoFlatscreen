// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>

namespace bovr::input {

struct HelicopterAxes {
    float forward{};
    float right{};
    float yaw{};
    float pitch{};
    bool valid{};
};

namespace detail {
inline float apply_deadzone(float axis, float deadzone) noexcept {
    axis = std::clamp(axis, -1.0F, 1.0F);
    const float magnitude=std::fabs(axis);
    if (magnitude <= deadzone) return 0.0F;
    return (axis < 0 ? -1.0F : 1.0F) * (magnitude - deadzone) / (1.0F - deadzone);
}
}

inline HelicopterAxes helicopter_axes(float left_x,float left_y,float right_x,float right_y,bool active) noexcept {
    constexpr float deadzone{0.20F};
    if(!active) return {};
    if(!std::isfinite(left_x)||!std::isfinite(left_y)||!std::isfinite(right_x)||!std::isfinite(right_y)) return {};
    return {detail::apply_deadzone(left_y,deadzone),detail::apply_deadzone(left_x,deadzone),
            detail::apply_deadzone(right_x,deadzone),detail::apply_deadzone(right_y,deadzone),true};
}

}
