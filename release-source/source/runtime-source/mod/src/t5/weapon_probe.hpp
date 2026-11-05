// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include "t5/stereo_view_math.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "input/weapon_placement.hpp"
namespace bovr::t5 {
// Copied values only. Integer identities are never permission to dereference
// or retain a borrowed native object; they can only compare current samples.
struct NativeWeaponContext {
    DWORD native_thread{};
    std::uint64_t call_serial{};
    std::uintptr_t cg_identity{}, refdef_identity{}, viewmodel_identity{}, dobj_identity{}, variant_identity{}, definition_identity{};
    SceneView stock_view{};
    input::ScaledWeaponPlacement placement{};
    int local_client{}, draw_gun{}, command_number{}, native_time{}, native_frame_time{}, refdef_time{};
    int offhand{}, weapon_state{}, other_weapon_state{}, stack_count{}, weapon_type{}, weapon_class{};
    std::uint32_t weapon_index{};
    std::uint64_t model_generation{};
    int command_clock{};
    bool model_lifetime_verified{};
    bool valid{}, body_valid{}, normal_context{}, server_normal{}, primary_viewmodel{}, weapon_definition_valid{}, dual_wield{};
    bool manual_grenade_primary_visual{},primary_knife_variant{};
    // Exact current rocket_barrage_sp descriptor, paired to both local player
    // states. Its radio is a native mission prop, not a tracked gun profile.
    bool support_marker_native_visual{};
    // Exact rebirth_hands_sp / rebirth_hatchet_sp props paired to both players.
    // Their native scripted visuals have no firearm tags and retain stereo.
    bool rebirth_native_prop_visual{};
    // Tracking-only eligibility. The ordinary flags retain their existing
    // reload/cycle meaning; linked mode cannot manufacture ordinary gameplay.
    bool linked_weapon_context{}, linked_server_context{};
    std::uint32_t server_link_identity{};
};
inline primary_weapon_tracking::Mode primary_weapon_mode(const NativeWeaponContext& c) noexcept {
    using Mode=primary_weapon_tracking::Mode;
    if((c.normal_context&&c.linked_weapon_context)||(c.server_normal&&c.linked_server_context))return Mode::rejected;
    const auto mode=primary_weapon_tracking::paired_mode(
        c.normal_context?Mode::ordinary:(c.linked_weapon_context?Mode::linked:Mode::rejected),
        c.server_normal?Mode::ordinary:(c.linked_server_context?Mode::linked:Mode::rejected));
    return primary_weapon_tracking::same_link_mode(mode,c.server_link_identity,mode,c.server_link_identity)?mode:Mode::rejected;
}
inline bool primary_weapon_context(const NativeWeaponContext& c) noexcept {
    return primary_weapon_mode(c)!=primary_weapon_tracking::Mode::rejected;
}
inline bool tracked_primary_weapon_state(const NativeWeaponContext& c) noexcept {
    return native_weapon_tracking_state(c.weapon_state)||c.manual_grenade_primary_visual;
}
// One frontend invocation's copied render expectation. This is not authority
// to reuse a pose or native pointer. The scene consumer takes it once, even
// when that scene follows a native/menus path instead of producing VR eyes.
struct WeaponRenderWitness {
    NativeWeaponContext producer{};
    bool observed{}, expected{}, substituted{}, nested{}, hidden_primary_weapon{}, native_call_suppressed{};
};
WeaponRenderWitness take_weapon_render_witness() noexcept;
struct WeaponCallbacks {
    void* context{};
    void (*before_native)(void*,const NativeWeaponContext&){};
    void (*after_native)(void*,const NativeWeaponContext&){};
    // Writes an owned32-byte value only, not the native argument. The wrapper
    // uses it synchronously when explicitly enabled and independently finite.
    bool (*select_placement)(void*,const NativeWeaponContext&,input::ScaledWeaponPlacement*){};
};
// Empty by default. Install at startup; replacement/unbind is rejected inside
// callbacks. No lock spans a callback or a native call. Pre/post snapshots are
// independent. Native normally runs once; the copied Vorkuta-knife fallback can
// suppress only its render call and marks that fact in the one-shot witness.
// Scratch substitution is independently gated by BOVR_WEAPON_PLACEMENT=1;
// original argument bytes remain unchanged.
bool set_weapon_callbacks(const WeaponCallbacks&) noexcept;
// Explicit BOVR_WEAPON_PROBE=1; accepted pinned SP image at its startup factory.
// Observes the normal first-person placement before/after one native call.
// The base observer makes no skeleton/tag calls. Independent explicit barrel
// observation may calculate a native bone after the original call returns.
// The exact Vorkuta fallback can suppress only that invocation's render-only
// stock weapon call; its witness must prove the suppression before world-only
// stereo is admitted.
bool install_weapon_probe(HMODULE executable) noexcept;
// After a completed native pose-update call only. Placement is a copied,
// reconstructed persistent-pose value, NOT the original AddPlayerWeapon argument.
// Current draw visibility is independently read; no lifecycle is minted.
bool copy_pose_clear_weapon_context(std::uintptr_t object,int local,std::uint64_t serial,NativeWeaponContext*) noexcept;
// Value-only authority for one current wrapper invocation. Nested entry
// invalidates its parent. TLS retains no native object or stack pointer.
struct WeaponNativeClearScope {std::uint64_t token{};unsigned depth{};bool valid{};};
bool enable_weapon_native_clear_scope() noexcept;
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept;
std::uint64_t weapon_native_clear_last_token() noexcept;
}
