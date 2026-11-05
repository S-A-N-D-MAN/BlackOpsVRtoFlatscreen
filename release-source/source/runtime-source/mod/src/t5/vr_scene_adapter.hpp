// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/borrowed_weapon_claim.hpp"
#include <windows.h>
#include "xr_types.h"
#include "t5/menu_input_adapter.hpp"
#include "input/weapon_placement.hpp"
#include "input/command_rig_ledger.hpp"
#include "t5/helicopter_context.hpp"
namespace bovr::t5 {
using NativeRenderScene = void(__cdecl*)(void*, int);
using NativeDraw3D = void(__cdecl*)();
// Experimental integration. Requires all three explicit opt-ins: observation,
// Ex promotion, and BOVR_XR_STEREO_PROBE. Caller owns full-image validation.
bool weapon_integration_renderer_ready() noexcept;
bool configure_vr_scene_adapter(HMODULE, bool scene_hook, bool backend_hook,bool final_ui_hook=false,bool ui_producer_hook=false,bool hud_pass_hook=false,bool frame_begin_hook=false,bool event_hook=false) noexcept;
// Optional successor: called only after the original native event drain. It
// services configured menu input, then records one copied producer event stamp.
void service_menu_frame_after_events() noexcept;
// Read only at serialized first-factory configuration, after adapter setup.
bool menu_input_epoch_ready() noexcept;
bool render_stereo_scene(void*, int, NativeRenderScene) noexcept;
bool render_stereo_backend(NativeDraw3D);
// True permits native quit (no live XR ownership); false requests owner-thread
// retirement. The quit gate queues native quit only after explicit retirement ACK.
bool prepare_quit_for_vr(void*) noexcept;
// Thread-safe copied actions, no runtime calls or game writes. False clears both
// outputs when inactive, resetting/stopped, or older than 250 ms. Unfocused
// snapshots remain readable so consumers can release held controls promptly.
bool read_vr_input_snapshot(wawvr::xr::ActionSnapshot*,std::uint64_t* generation) noexcept;
// One locked read of the input actions and their original published XR frame.
// A true return means actions are available; rig may remain zero until the
// renderer has an anchor in that generation. Never refreshes publication time.
bool read_vr_command_input_snapshot(wawvr::xr::ActionSnapshot*,std::uint64_t* generation,
                                   input::CommandRigSample*) noexcept;
// Recheck only the original generation/anchor. Never reads or adopts another pose.
bool command_input_generation_current(const input::CommandRigSample&) noexcept;
// Exact retained native commands keep their authored body/anchor when a later
// command turns. Only lifecycle/session generation is current at consumption.
bool consumed_command_input_generation_current(const input::CommandRigSample&) noexcept;
// Commits one equal native-body/tracking-anchor yaw transfer after every other
// consumer of the original command rig has retired. The transaction fails if
// either the native yaw or the XR publication changed in the meantime.
bool commit_vr_body_yaw(const input::CommandRigSample& original,
                        const wawvr::xr::Posef& desired_anchor,
                        float expected_native_yaw,float desired_native_yaw) noexcept;
struct RoomScaleView {
    wawvr::xr::Vec3f eye{};
    std::uintptr_t cg{};
    std::uint64_t epoch{};
    bool vertical_clipped{},horizontal_discontinuity{};
    // Zero means query unavailable: only the existing stock-eye fallback is allowed.
    float camera_radius{},near_clip{};
};
// Only horizontal anchor translation. The collision-approved view is published
// atomically with the anchor; neither native origin nor stance is modified.
bool commit_vr_room_scale(const input::CommandRigSample&,const wawvr::xr::Posef&,
                         const RoomScaleView&) noexcept;
void revoke_room_scale_view() noexcept;
// Copied from one successful native stereo scene build. No game pointer is
// borrowed, and this does not grant server vehicle ownership or shot authority.
struct HelicopterSceneView {
    helicopter::Facts owner{};
    input::CommandRigSample rig{};
    wawvr::xr::EnginePose stock{};
    std::uintptr_t refdef{};
    std::uint64_t epoch{},sequence{};
    int native_time{};
};
bool read_helicopter_scene_view(HelicopterSceneView*) noexcept;
struct MenuPanelSnapshot {
    wawvr::xr::QuadLayer panel{};
    std::uint64_t generation{},submission_serial{};
    std::uint32_t source_width{},source_height{};
    int connection_state{},key_catchers{};
    bool interactive{};
    NativeMenuPage page{};
    std::uint64_t production_epoch{};
    std::uint64_t production_ms{};
};
bool read_menu_panel_snapshot(MenuPanelSnapshot*) noexcept;
void invalidate_menu_panel_interaction() noexcept;
struct NativeWeaponContext;
// Distinct passive borrowed-call transaction, BOVR_BORROWED_WEAPON_PROBE only.
// Completion is a copied same-invocation witness, never an allocation generation.
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept;
// Atomically reserves the current native build before the wrapper suppresses a
// recognized stock knife. False leaves the native call completely unchanged.
bool reserve_hidden_primary_weapon_scene_frame(const NativeWeaponContext&) noexcept;
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&,std::uint64_t normal_serial,bool world_verified=false) noexcept;
// Commits selection intent for this exact still-current reserved build, without
// refreshing, consuming or granting a retained native-object lease.
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&) noexcept;
// Original lifetime-based producer, BOVR_WEAPON_RESERVATION_PROBE only. Its
// independent validation and optional writer still require verified lifetime.
bool reserve_weapon_scene_frame(const NativeWeaponContext&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept;
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity&) noexcept;
// Point-in-time read-only check, not a native object lifetime/worker barrier.
// Never reserves, consumes, cancels, changes clocks, or refreshes a ticket.
bool validate_weapon_scene_reservation(const NativeWeaponContext&,const input::WeaponFrameIdentity&) noexcept;
}
