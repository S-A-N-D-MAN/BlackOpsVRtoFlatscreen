// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"
#include <cstdint>

namespace bovr::input {

struct KnifeSwingSample final {
    bool pulse{};
    float speed_meters_per_second{};
};

// Recognizes one deliberate swing of the physical right controller. A slow
// sample must arm each swing, and invalid/stale tracking resets the history.
class KnifeSwingGesture final {
public:
    KnifeSwingSample update(const wawvr::xr::ActionSnapshot& actions,
                            std::uint64_t now_ms,
                            bool enabled) noexcept;
    void reset() noexcept;

    static constexpr float minimum_speed_meters_per_second = 1.25f;
    static constexpr float rearm_speed_meters_per_second = 0.45f;
    static constexpr std::uint64_t cooldown_milliseconds = 350;

private:
    wawvr::xr::Vec3f previous_position_{};
    std::uint64_t previous_ms_{}, previous_sequence_{}, last_pulse_ms_{};
    bool sampled_{}, armed_{};
};

} // namespace bovr::input
