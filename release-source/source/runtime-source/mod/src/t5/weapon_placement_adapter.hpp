// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
// Passive only. Requires BOVR_WEAPON_RESERVATION_PROBE=1 and the installed
// guarded weapon hook. Derives/logs explicit experimental native alignment;
// never changes a native placement argument, pose, command or shot.
bool configure_weapon_placement_adapter(HMODULE,bool weapon_hook) noexcept;
}
