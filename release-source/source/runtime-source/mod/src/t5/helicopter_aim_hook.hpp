// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <cstdint>
namespace bovr::t5 {
bool configure_helicopter_aim_hook(HMODULE executable) noexcept;
// Called only by the exact borrowed native trace CALL. The adapter owns all
// selection and guarded original-buffer writes; refusal must be a no-op.
void select_helicopter_aim_trace(std::uintptr_t return_address,std::uintptr_t vehicle,
    std::uintptr_t scr_vehicle,std::uintptr_t info,std::uintptr_t player,
    std::uintptr_t entry_stack) noexcept;
}
