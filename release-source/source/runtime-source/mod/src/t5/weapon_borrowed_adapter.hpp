// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include "t5/weapon_probe.hpp"
namespace bovr::t5 {
bool configure_weapon_borrowed_probe(HMODULE,bool prerequisites) noexcept;
bool weapon_borrowed_probe_enabled() noexcept;
bool weapon_olympia_profile_enabled() noexcept;
// Existing wrapper owns invocation and original argument. No selector callback.
void observe_borrowed_weapon_before(const NativeWeaponContext&);
// Distinct writer; never registers/replaces the legacy callback selector.
bool weapon_borrowed_placement_enabled() noexcept;
// Immutable accepted T2 startup policy, owned by the existing aim adapter.
// No state advance, native/context/pose read or per-frame environment lookup.
bool weapon_two_hand_support_enabled() noexcept;
bool select_borrowed_weapon_placement(const NativeWeaponContext&,input::ScaledWeaponPlacement*);
// Extra native/history draws cannot reserve a second copy of an already-owned
// XR frame. For the recognized knife only, lend the last fully completed
// placement for a tightly bounded same-model/same-episode continuity draw.
bool select_borrowed_knife_visual_continuity(const NativeWeaponContext&,input::ScaledWeaponPlacement*) noexcept;
void retire_borrowed_weapon_invocation(std::uint64_t token) noexcept;
}
