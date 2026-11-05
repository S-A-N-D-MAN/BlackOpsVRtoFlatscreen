// SPDX-License-Identifier: GPL-3.0-only
#include "input/knife_swing_gesture.hpp"

#include <cmath>

namespace bovr::input {
namespace {
namespace xr = wawvr::xr;

bool tracked(const xr::TrackedPose& pose) noexcept {
    const auto& p = pose.pose.position;
    return pose.active && pose.position_valid && pose.position_tracked &&
        std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
}

void KnifeSwingGesture::reset() noexcept {
    previous_position_ = {};
    previous_ms_ = previous_sequence_ = last_pulse_ms_ = 0;
    sampled_ = armed_ = false;
}

KnifeSwingSample KnifeSwingGesture::update(
    const xr::ActionSnapshot& actions,
    const std::uint64_t now_ms,
    const bool enabled) noexcept {
    KnifeSwingSample result{};
    const auto& grip = actions.hands[static_cast<unsigned>(xr::Hand::Right)].grip;
    if (!enabled || !actions.focused || !actions.sequence || !now_ms ||
        !tracked(grip)) {
        reset();
        return result;
    }
    if (sampled_ && actions.sequence == previous_sequence_) return result;
    const auto current = grip.pose.position;
    if (!sampled_) {
        previous_position_ = current;
        previous_ms_ = now_ms;
        previous_sequence_ = actions.sequence;
        sampled_ = armed_ = true;
        return result;
    }
    if (actions.sequence < previous_sequence_ || now_ms <= previous_ms_ ||
        now_ms - previous_ms_ > 100) {
        reset();
        previous_position_ = current;
        previous_ms_ = now_ms;
        previous_sequence_ = actions.sequence;
        sampled_ = true;
        return result;
    }

    const float dx = current.x - previous_position_.x;
    const float dy = current.y - previous_position_.y;
    const float dz = current.z - previous_position_.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float seconds = static_cast<float>(now_ms - previous_ms_) * 0.001f;
    previous_position_ = current;
    previous_ms_ = now_ms;
    previous_sequence_ = actions.sequence;
    if (!std::isfinite(distance) || distance > 0.35f || seconds <= 0.0f) {
        armed_ = false;
        return result;
    }
    result.speed_meters_per_second = distance / seconds;
    if (!std::isfinite(result.speed_meters_per_second)) {
        reset();
        return {};
    }
    if (result.speed_meters_per_second <= rearm_speed_meters_per_second)
        armed_ = true;
    const bool cooldown = !last_pulse_ms_ ||
        (now_ms >= last_pulse_ms_ && now_ms - last_pulse_ms_ >= cooldown_milliseconds);
    if (armed_ && cooldown && distance >= 0.015f &&
        result.speed_meters_per_second >= minimum_speed_meters_per_second) {
        result.pulse = true;
        armed_ = false;
        last_pulse_ms_ = now_ms;
    }
    return result;
}

} // namespace bovr::input
