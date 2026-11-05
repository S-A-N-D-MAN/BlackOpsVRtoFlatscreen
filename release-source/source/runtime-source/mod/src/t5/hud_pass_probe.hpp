// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>

namespace bovr::t5 {
struct HudPassPoint {
    std::uintptr_t image_base{};
    DWORD native_thread{};
    std::uint64_t call_serial{};
    // Publication only: copied from original inner-CALL entry ESP+8 after an
    // exact R_EndFrame prefix guard proves the sole saved EBX stack slot.
    std::uintptr_t enclosing_return{};
};
using HudPassCallback = void(*)(const HudPassPoint&);
// Install only at the existing serialized startup boundary, before these native
// calls can run. The exact retail image and BOVR_HUD_COMPOSITION_PROBE=1 gate both
// CALL patches. Installation itself does not snapshot, replay or modify HUD data.
bool install_hud_pass_probe(HMODULE) noexcept;
// Process-lifetime callbacks; replacements are copied as one tuple on entry.
// Publication runs after native EndOfList and before the caller clears counters.
// The outer native per-view pass has one before/after pair with the same point.
// Nested native invocations still run once; their callbacks are suppressed.
// Callback failures are contained; the native return state and LastError survive.
bool set_hud_pass_callbacks(HudPassCallback published, HudPassCallback before,
                           HudPassCallback after) noexcept;
}
