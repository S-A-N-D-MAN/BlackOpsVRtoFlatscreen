// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
struct NativeWeaponContext;
// Startup only. BOVR_WEAPON_PROFILE_PROBE=1 plus an existing unique directory
// BOVR_PROFILE_OUTPUT_DIR and both existing weapon/command hooks are required.
bool configure_weapon_profile_probe(HMODULE,bool existing_weapon_and_command_hooks) noexcept;
bool weapon_profile_probe_enabled() noexcept;
// Protected copies only, in the current post-native weapon borrow. No native
// invocation, callback replacement, extra patch, profile fit or engine write.
void observe_weapon_profile_after(const NativeWeaponContext&) noexcept;
}
