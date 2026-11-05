// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

namespace bovr::t5 {
struct GrenadeThrowArguments {
    std::uintptr_t parent{};
    unsigned weapon{}, model{};
    int rotate{}, fuse_time{};
    std::array<float,3> origin{}, velocity{};
};
struct GrenadeThrowReplacement {
    std::array<float,3> origin{}, velocity{};
};
// Values only. The host owns local-player/weapon identity, freshness, and the
// exactly-once pending release. Returning false must not consume that release.
using GrenadeThrowCallback = bool(*)(const GrenadeThrowArguments&,GrenadeThrowReplacement*) noexcept;
// Installs only the pinned Weapon_Throw_Grenade -> G_FireGrenade CALL. The
// callback has process lifetime; repeat configuration must use the same one.
bool configure_grenade_throw_hook(HMODULE,GrenadeThrowCallback) noexcept;
}
