// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t5/weapon_animation_math.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace bovr::t5::grenade_render_detail {

inline constexpr std::uint16_t empty_scene_entry = 0xFFFFU;
inline constexpr std::uint32_t no_scene_slot =
    (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::size_t projectile_model_cache_capacity = 256U;
inline constexpr std::size_t no_model_cache_slot =
    (std::numeric_limits<std::size_t>::max)();

constexpr std::uint32_t replace_low_word(
    std::uint32_t word,
    unsigned value) noexcept {
    return (word & 0xFFFF0000U) | (value & 0xFFFFU);
}

constexpr bool low_word_is(std::uint32_t word, unsigned value) noexcept {
    return (word & 0xFFFFU) == (value & 0xFFFFU);
}

constexpr bool owns_primary_visual(
    bool ticket_valid,
    std::uint32_t weap_flags,
    int weapon_state,
    unsigned offhand,
    unsigned ticket_weapon,
    unsigned primary,
    unsigned ticket_primary) noexcept {
    return ticket_valid &&
        (weap_flags & 2U) != 0U &&
        weapon_state >= 21 &&
        weapon_state <= 26 &&
        offhand == ticket_weapon &&
        ticket_primary != 0U &&
        ticket_primary != ticket_weapon &&
        (primary == 0U || primary == ticket_primary);
}

template <class Entries>
constexpr std::size_t claim_model_cache_slot(
    Entries& entries,
    std::uintptr_t projectile_model) noexcept {
    if (projectile_model == 0U) {
        return no_model_cache_slot;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].model == projectile_model) {
            return index;
        }
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].model == 0U) {
            entries[index].model = projectile_model;
            return index;
        }
    }
    return no_model_cache_slot;
}

inline bool axis_to_angles(
    const animation_detail::Axis& axis,
    float out[3]) noexcept {
    if (out == nullptr) {
        return false;
    }
    out[0] = 0.0F;
    out[1] = 0.0F;
    out[2] = 0.0F;
    if (!animation_detail::rigid(axis)) {
        return false;
    }

    constexpr float radians_to_degrees = 57.29577951308232F;
    const float horizontal = std::hypot(axis.forward.x, axis.forward.y);
    const float pitch =
        std::atan2(-axis.forward.z, horizontal) * radians_to_degrees;

    float yaw = 0.0F;
    float roll = 0.0F;
    if (horizontal > 1.0e-6F) {
        yaw = std::atan2(axis.forward.y, axis.forward.x) *
            radians_to_degrees;
        roll = std::atan2(axis.left.z, axis.up.z) *
            radians_to_degrees;
    } else {
        // Euler yaw and roll are coupled at vertical pitch. Choose zero roll
        // and preserve the equivalent heading represented by the left axis.
        yaw = std::atan2(-axis.left.x, axis.left.y) *
            radians_to_degrees;
    }

    if (!std::isfinite(pitch) || !std::isfinite(yaw) ||
        !std::isfinite(roll)) {
        return false;
    }
    out[0] = pitch;
    out[1] = yaw;
    out[2] = roll;
    return true;
}

inline bool find_scene_slot(
    std::span<const std::uint16_t> xmodel,
    std::span<const std::uint16_t> dobj,
    std::uint32_t first,
    std::uint32_t last,
    std::uint32_t* out) noexcept {
    if (out == nullptr) {
        return false;
    }
    *out = no_scene_slot;
    if (xmodel.data() == nullptr || dobj.data() == nullptr || first > last ||
        static_cast<std::size_t>(last) >= xmodel.size() ||
        static_cast<std::size_t>(last) >= dobj.size()) {
        return false;
    }

    for (std::uint32_t candidate = last;; --candidate) {
        if (xmodel[candidate] == empty_scene_entry &&
            dobj[candidate] == empty_scene_entry) {
            *out = candidate;
            return true;
        }
        if (candidate == first) {
            break;
        }
    }
    return false;
}

} // namespace bovr::t5::grenade_render_detail
