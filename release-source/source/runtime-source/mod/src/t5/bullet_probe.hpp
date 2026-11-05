// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
#include <array>
namespace bovr::t5 {
struct NativeLocalShot {
    std::uint64_t serial{},observed_ms{};
    DWORD native_thread{};
    std::uintptr_t attacker_identity{},client_identity{},variant_identity{},definition_identity{};
    int game_time{},weapon_index{},server_pm_type{},server_health{},server_weapon_state{};
    float spread{},forward[3]{},right[3]{},up[3]{},muzzle[3]{},gun_forward[3]{};
    bool readable{},local_identity{},normal_context{},weapon_identity{},finite{},local_shot_confirmed{};
    bool linked_context{};
    std::uint32_t server_link{};
};
struct LocalShotCallbacks {void* context{};void(*after_native)(void*,const NativeLocalShot&){};};
bool set_local_shot_callbacks(const LocalShotCallbacks&) noexcept;
// Borrowed synchronous pre-call values. Identity addresses are observation-only;
// neither these values nor callback output may establish a retained native lease.
// native.local_shot_confirmed is always false before original execution.
struct NativeBulletSelectionContext {
    NativeLocalShot native{};
    std::array<unsigned char,68> native_parameters{};
    bool eligible{};
};
struct NativeBulletGeometry {
    float forward[3]{},right[3]{},up[3]{},muzzle[3]{};
};
static_assert(sizeof(NativeBulletGeometry)==48);
// A pure copied-value selector. May change only geometry, never native memory.
// Returning false or throwing leaves the original pointer exactly unchanged.
// Output must be finite orthonormal F/R/U with cross(forward,right)=-up.
// No implementation is registered here; actual pose/profile policy is separate.
struct BulletSelector {
    void* context{};
    bool(*select)(void*,const NativeBulletSelectionContext&,NativeBulletGeometry*){};
};
// Independent default-off BOVR_BULLET_SELECTION=1, exact bullet hook required.
// Registration does not replace the completed-shot callback.
bool set_bullet_selector(const BulletSelector&) noexcept;
// BOVR_BULLET_PROBE=1. One exact ordinary FireWeapon CALL and one native execution.
// Arguments remain unchanged unless the separate explicit selector accepts.
// Callback reports completed local bullet-fire calls with actual selected values;
// it does not imply a hit, physical aiming, or haptic/runtime calls.
bool install_bullet_probe(HMODULE executable) noexcept;
// Independent of diagnostic bullet observation; exact Vorkuta motorcycle shot only.
bool install_motorcycle_script_shot(HMODULE executable) noexcept;
// Exact-profile dual-pistol selection; the ordinary single-weapon policy is unchanged.
bool install_dual_pistol_shot(HMODULE executable) noexcept;
struct CommandContext;
// No startup patch: called only with the same owned live dual command pending.
bool ensure_dual_pistol_hooks(const CommandContext&) noexcept;
}
