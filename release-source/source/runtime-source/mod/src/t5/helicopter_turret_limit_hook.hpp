// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <cstdint>
namespace bovr::t5 {
bool configure_helicopter_turret_limit_hook(HMODULE executable) noexcept;
// Zero preserves the original native info pointer. A selected private clone
// must remain immutable through the native caller's four clamp reads.
std::uintptr_t select_helicopter_turret_limits(std::uintptr_t returned,
    std::uintptr_t vehicle,std::uintptr_t scr_vehicle,std::uintptr_t info,
    std::uintptr_t pilot,std::uintptr_t original_stack) noexcept;
}
