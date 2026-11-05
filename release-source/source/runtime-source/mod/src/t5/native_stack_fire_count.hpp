// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace bovr::t5 {
// Pinned SP ps+0x1b0 is stackFireCount, serialized as three unsigned bits.
// Native 0x7693c0 initializes it for fireType5; switching firing types may
// retain it. Native 0x670c00 uses the same controller gun-angle base for all
// valid counts and adds its own stacked-shot yaw spread above one. Preserve
// that native behavior and every copied count: never clear or rewrite it.
// This only classifies the field; all independent camera, ownership, firing,
// reload and script permissions still apply.
inline constexpr bool native_valid_stack_fire_count(std::uint32_t count) noexcept {
    return count<=7u;
}
}
