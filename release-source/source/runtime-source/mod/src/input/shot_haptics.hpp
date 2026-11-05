// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../../vendor/waw-interaction-core/src/mod/firing_haptic_logic.hpp"
#include <array>
#include <cstdint>
#include <mutex>

namespace bovr::input {
// The adapter supplies the original XR publication time and a native gameplay
// lifetime epoch. A recent copied sample cannot refresh its own permission.
struct HapticContext final {
    std::uint64_t xr_generation{}, gameplay_epoch{}, sequence{}, published_ms{};
    bool focused{}, gameplay{};
    std::array<bool,2> tracked{};
};
struct ConfirmedLocalShot final {
    // serial is process-lifetime monotonic at the native shot seam, including
    // across map/recenter/focus changes. It is not a native reused frame index.
    std::uint64_t xr_generation{}, gameplay_epoch{}, serial{}, observed_ms{};
    unsigned hand{};
    bool local_shot_confirmed{};
};
struct HapticPlan final {
    std::array<std::uint32_t,2> shots{};
    unsigned stop_mask{};
    float amplitude{wawvr::mod::kFiringHapticAmplitude};
    float duration_seconds{wawvr::mod::kFiringHapticDurationSeconds};
};
// Metadata only: producer queues a value after a verified local native shot.
// The XR owner calls service and executes its bounded plan outside this lock.
// Failed XR pulse application must not be retried as a new shot.
class ShotHaptics final {
public:
    HapticPlan service(const HapticContext&,std::uint64_t now_ms);
    bool queue(const ConfirmedLocalShot&,std::uint64_t now_ms);
    HapticPlan reset();
private:
    HapticPlan clear_locked();
    std::mutex mutex_;
    std::array<wawvr::mod::FiringHapticMailbox,2> mailboxes_{};
    HapticContext context_{};
    std::uint64_t last_service_{},last_shot_serial_{};
    unsigned allowed_mask_{},pulse_mask_{};
};
}
