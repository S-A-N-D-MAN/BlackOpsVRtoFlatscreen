// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
struct NativeWeaponContext;
// Independent BOVR_WEAPON_BARREL_PROBE=1, no additional native patch.
// This calls the native bone calculator on the existing native frontend scope;
// it never selects placement, writes shots, or publishes native pointers.
bool configure_weapon_barrel_probe(HMODULE,bool existing_weapon_hook) noexcept;
bool weapon_barrel_probe_enabled() noexcept;
// Native bone exceptions propagate unchanged; the observer must not swallow a
// fault while the engine owns its skeleton lock or infer an unlock operation.
void observe_weapon_barrel_after(const NativeWeaponContext&);
}
