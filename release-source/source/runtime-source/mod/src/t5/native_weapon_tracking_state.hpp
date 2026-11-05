// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/native_stack_fire_count.hpp"
namespace bovr::t5 {
// Retain all existing hitscan dual profiles. Projectile duals are limited to
// native pistols; each reader separately proves the reciprocal companion has
// the same type and its exact model occupies the native companion ordinal.
inline constexpr bool native_dual_weapon_tracking_type(int type,int weapon_class) noexcept {
    return type==0||(type==2&&weapon_class==4);
}
// Pinned retail SP, not the differently numbered Kisak MP enum:
// 0x7673cd/0x767473/0x76751d set sprint raise/loop/drop to28/29/30.
// 0x7675cd/0x767616/0x76764d set continuous fire in/loop/out to31/32/33.
// 0x766d4d/0x766dbe/0x766e0d set low-ready raise/loop/drop to48/49/50.
// Executive Order keeps its MP5K in49 during the opening infiltration.
// BG_GetViewmodelWeaponIndex0x4540b0 returns current ps+0x144 in all nine
// states; only18..20 and21..26 can select previous/offhand viewmodels.
// This permits copied model/aim tracking only. It never changes native state,
// firing eligibility, animation progression, or physical reload permissions.
inline constexpr bool native_weapon_tracking_state(int state) noexcept {
    return (state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
}
}
