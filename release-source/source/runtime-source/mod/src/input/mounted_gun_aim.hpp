// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/native_command_aim.hpp"
#include <algorithm>
#include <cmath>

namespace bovr::input::mounted_gun_aim {

struct Angles { float pitch{}, yaw{}; };
struct ClampedAim {
    Angles relative{};
    Angles world{};
    wawvr::xr::Vec3f forward{};
};

// Pure geometry from the original copied rig and unmodified stock scene basis.
// No head-directed aiming, hand translation, support-hand pose, native writes,
// or refresh of the original publication time. The caller must separately
// recheck current anchor/generation and exact native mounted ownership.
inline CommandAimStatus right_hand(const CommandRigSample& rig,
    const wawvr::xr::Basis3f& stock, std::uint64_t now,
    CommandAimProposal* out) noexcept {
    return propose_native_command_aim(rig, rig.frame.actions, stock,
        wawvr::xr::Hand::Right, now, out, false);
}

inline bool finite(Angles a) noexcept {
    return std::isfinite(a.pitch) && std::isfinite(a.yaw);
}

inline float delta(float angle, float base) noexcept {
    // Reduce separately so two otherwise finite large values cannot overflow
    // the subtraction. Native mechanical limits are expressed around the mount.
    return std::remainder(std::remainder(angle, 360.f) -
        std::remainder(base, 360.f), 360.f);
}

// Asymmetric native arcs, in degrees relative to the mount's base angles.
// This result is suitable for a visual/aim proposal only: authoritative shot
// consumers must use the actual native-clamped barrel angles when available.
inline bool clamp(Angles requested, Angles base, Angles minimum,
    Angles maximum, ClampedAim* out) noexcept {
    if (!out) return false;
    *out = {};
    if (!finite(requested) || !finite(base) || !finite(minimum) ||
        !finite(maximum) || minimum.pitch < -180.f || maximum.pitch > 180.f ||
        minimum.yaw < -180.f || maximum.yaw > 180.f ||
        minimum.pitch > maximum.pitch || minimum.yaw > maximum.yaw) return false;
    ClampedAim p{};
    p.relative = {
        std::clamp(delta(requested.pitch, base.pitch), minimum.pitch, maximum.pitch),
        std::clamp(delta(requested.yaw, base.yaw), minimum.yaw, maximum.yaw)};
    p.world = {
        std::remainder(std::remainder(base.pitch, 360.f) + p.relative.pitch, 360.f),
        std::remainder(std::remainder(base.yaw, 360.f) + p.relative.yaw, 360.f)};
    constexpr float radians = .01745329251994329577f;
    const float pitch = p.world.pitch * radians, yaw = p.world.yaw * radians;
    const float horizontal = std::cos(pitch);
    // IW pitch is positive down. Preserve the native muzzle/pivot elsewhere.
    p.forward = {horizontal * std::cos(yaw), horizontal * std::sin(yaw), -std::sin(pitch)};
    *out = p;
    return true;
}

} // namespace bovr::input::mounted_gun_aim
