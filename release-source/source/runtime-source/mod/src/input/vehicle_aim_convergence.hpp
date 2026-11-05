// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cmath>
#include <limits>
#include "xr_types.h"

namespace bovr::input {

namespace xr = wawvr::xr;

namespace vehicle_aim_detail {
inline bool finite(float value) noexcept { return std::isfinite(value); }

inline bool finite(const xr::Vec3f& value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}
}

inline bool vehicle_aim_convergence(
    const xr::Vec3f& eye,
    const xr::Vec3f& unit_forward,
    const xr::Vec3f& muzzle,
    float distance,
    xr::Vec3f* target,
    xr::Vec3f* direction) noexcept
{
    if (target == nullptr || direction == nullptr || target == direction)
    {
        return false;
    }

    if (!vehicle_aim_detail::finite(distance) || !vehicle_aim_detail::finite(eye) ||
        !vehicle_aim_detail::finite(unit_forward) || !vehicle_aim_detail::finite(muzzle))
    {
        return false;
    }

    if (distance < 1.0f || distance > 100000.0f)
    {
        return false;
    }

    const double forward_x = static_cast<double>(unit_forward.x);
    const double forward_y = static_cast<double>(unit_forward.y);
    const double forward_z = static_cast<double>(unit_forward.z);
    const double forward_len2 = forward_x * forward_x + forward_y * forward_y + forward_z * forward_z;
    if (!std::isfinite(forward_len2) || forward_len2 < 0.25 || forward_len2 > 4.0)
    {
        return false;
    }

    const double forward_len = std::sqrt(forward_len2);
    if (!std::isfinite(forward_len) || forward_len <= 0.0)
    {
        return false;
    }

    const double forward_scale = static_cast<double>(distance) / forward_len;
    const double forward_norm_x = forward_x * forward_scale;
    const double forward_norm_y = forward_y * forward_scale;
    const double forward_norm_z = forward_z * forward_scale;

    const double target_x = static_cast<double>(eye.x) + forward_norm_x;
    const double target_y = static_cast<double>(eye.y) + forward_norm_y;
    const double target_z = static_cast<double>(eye.z) + forward_norm_z;

    const double max_float = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(target_x) || !std::isfinite(target_y) || !std::isfinite(target_z) ||
        target_x < -max_float || target_x > max_float ||
        target_y < -max_float || target_y > max_float ||
        target_z < -max_float || target_z > max_float)
    {
        return false;
    }

    const double direction_x = target_x - static_cast<double>(muzzle.x);
    const double direction_y = target_y - static_cast<double>(muzzle.y);
    const double direction_z = target_z - static_cast<double>(muzzle.z);
    const double direction_len2 = direction_x * direction_x + direction_y * direction_y + direction_z * direction_z;
    if (!std::isfinite(direction_len2) || direction_len2 < 0.000001)
    {
        return false;
    }

    const double direction_len = std::sqrt(direction_len2);
    if (!std::isfinite(direction_len) || direction_len <= 0.0)
    {
        return false;
    }

    const double direction_scale = 1.0 / direction_len;
    const double direction_norm_x = direction_x * direction_scale;
    const double direction_norm_y = direction_y * direction_scale;
    const double direction_norm_z = direction_z * direction_scale;

    if (!std::isfinite(direction_norm_x) || !std::isfinite(direction_norm_y) || !std::isfinite(direction_norm_z))
    {
        return false;
    }

    xr::Vec3f proposed_target{
        static_cast<float>(target_x),
        static_cast<float>(target_y),
        static_cast<float>(target_z),
    };
    xr::Vec3f proposed_direction{
        static_cast<float>(direction_norm_x),
        static_cast<float>(direction_norm_y),
        static_cast<float>(direction_norm_z),
    };

    if (!vehicle_aim_detail::finite(proposed_target) || !vehicle_aim_detail::finite(proposed_direction))
    {
        return false;
    }

    *target = proposed_target;
    *direction = proposed_direction;
    return true;
}

} // namespace bovr::input
