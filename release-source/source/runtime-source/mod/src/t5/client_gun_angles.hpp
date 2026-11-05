// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_animation_math.hpp"
namespace bovr::t5::client_aim {
struct Angles {float pitch{},yaw{};};
// Native client effects and the command/server path must use the same barrel
// direction. Pitch/yaw describe IW forward; the camera's view angles stay alone.
inline bool from_forward(animation_detail::Vec forward,Angles* out) noexcept {
    if(!out)return false;*out={};
    if(!animation_detail::finite(forward)||
       std::fabs(animation_detail::dot(forward,forward)-1)>0.003f)return false;
    const auto horizontal=std::hypot(forward.x,forward.y);
    if(horizontal<0.001f)return false;
    constexpr float degrees=57.29577951308232f;
    *out={-std::atan2(forward.z,horizontal)*degrees,std::atan2(forward.y,forward.x)*degrees};
    return std::isfinite(out->pitch)&&std::isfinite(out->yaw);
}
}
