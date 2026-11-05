// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <array>
namespace bovr::t5 {
struct NativeWeaponContext;
// Owned by one existing native weapon wrapper invocation, below its original
// return address. No TLS-held native pointer or independent pre/post counter join.
// No constructor/destructor crosses native code; values expire with this stack.
// The linked client/server mode and link identity remain part of the complete
// per-call copy. Reserve one additional 64-byte block; wrapper offsets derive
// from sizeof, and AnimationFrame retains its independent fit/type assertions.
struct alignas(4) WeaponAnimationStorage {std::array<std::byte,1600> bytes;};
static_assert(sizeof(WeaponAnimationStorage)==1600&&alignof(WeaponAnimationStorage)==4);
bool configure_weapon_animation_probe(HMODULE,bool existing_weapon_hook) noexcept;
bool weapon_animation_probe_enabled() noexcept;
void observe_weapon_animation_before(const NativeWeaponContext&,WeaponAnimationStorage*);
void observe_weapon_animation_after(const NativeWeaponContext&,WeaponAnimationStorage*);
bool configure_weapon_native_clear_probe(HMODULE,bool existing_weapon_and_command_hooks) noexcept;
bool weapon_native_clear_probe_enabled() noexcept;
void observe_native_clear_weapon_return(const NativeWeaponContext&,std::uint64_t invocation_token);
}
