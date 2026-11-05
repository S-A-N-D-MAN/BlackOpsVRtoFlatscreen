// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
struct NativeWeaponContext;
bool configure_manual_grenade_renderer(HMODULE) noexcept;
bool manual_grenade_standalone_renderer_enabled() noexcept;
bool manual_grenade_primary_visual_scope(const NativeWeaponContext&) noexcept;
bool submit_manual_grenade_model(const NativeWeaponContext&) noexcept;
}
