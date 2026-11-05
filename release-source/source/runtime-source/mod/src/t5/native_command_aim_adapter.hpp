// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "input/native_command_aim.hpp"
#include "t5/weapon_command_profile.hpp"
#include "t5/native_clear_pair.hpp"
namespace bovr::t5 {
struct CommandAimPublication {
    input::ProducedRigCommand command{};
    input::CommandAimProposal aim{};
    std::uintptr_t cg_identity{},refdef_identity{};
    int native_time{};
    std::uint64_t invalidation_revision{};
    std::uintptr_t variant_identity{},definition_identity{},dual_variant_identity{},dual_definition_identity{};
    unsigned weapon{},dual_weapon{};
    bool dual_wield{};
};
// Exact-profile combined policy. Current is an observation, not a native lease.
bool exact_profile_aim_enabled() noexcept;
bool exact_profile_input_owner(const CommandContext&,bool bind) noexcept;
// Same-command copied dual observation after begin_native_command_aim. This
// does not carry native model ownership or reuse a previous admission token.
bool native_command_dual_current(const CommandContext&) noexcept;
bool native_command_hitscan_current(const CommandContext&) noexcept;
// True only for a fresh exact ordinary firearm occurrence waiting for aim
// neutral, or a proved grip+melee occurrence preceding that ownership gap.
bool native_command_ordinary_attack_inhibited(const CommandContext&) noexcept;
// Fresh copied render admission only: it can exist during a native selection
// with no gun-angle write. Shot consumers must use the separate aim publication.
bool read_command_profile_admission(const animation_detail::ClearComparisonKey&,
    input::profile_admission::Token*,command_profile::Copy*) noexcept;
bool command_profile_admission_current(input::profile_admission::Token) noexcept;
// Atomic only; safe while renderer metadata lock is held. No current/native calls.
bool command_profile_admission_witness(input::profile_admission::Token) noexcept;
bool configure_native_command_aim(HMODULE,bool command_hook) noexcept;
bool native_command_aim_enabled() noexcept;
void invalidate_native_command_aim() noexcept;
// Owner's deliberate selection retires the old render token and prohibits this
// occurrence's aim write. Its original rig may still admit a freshly checked
// current model for rendering after native selection; this does not rearm input.
void retire_native_command_aim_for_selection(const CommandContext&) noexcept;
void begin_native_command_aim(const CommandContext&,const wawvr::xr::ActionSnapshot&,
    std::uint64_t generation,const input::CommandRigSample&,bool live) noexcept;
void finish_native_command_aim(const CommandContext&,unsigned char*) noexcept;
// Commit the same occurrence's planned HMD body turn. A successful owned
// coordinate rebase may carry armed intent, but retires all old admissions.
bool commit_native_command_aim_body_yaw(const CommandContext&,const input::CommandRigSample&,
    const wawvr::xr::Posef& desired_anchor,float expected_yaw,float desired_yaw) noexcept;
struct RoomScaleView;
bool commit_native_command_aim_room_scale(const CommandContext&,const input::CommandRigSample&,
    const wawvr::xr::Posef& desired_anchor,const RoomScaleView&) noexcept;
// Exact retained producer serial, bounded age/epoch/generation checks. This
// never proves server consumption, a shot, native completion or scene ownership.
bool read_native_command_aim_publication(std::uint64_t serial,CommandAimPublication*) noexcept;
// Exact copied final-command lookup for a synchronous native dual-pistol shot.
// Every retained fingerprint counts; ambiguity, retirement and stale poses fail.
// The caller must independently prove the actual native shot/command ownership.
bool read_native_command_aim_for_shot(const input::NativeCommandBytes&,std::uintptr_t cg,CommandAimPublication*,
                                    bool allow_ordinary=false) noexcept;
// Client predicted effects have commandTime but no authoritative command bytes.
// This unique-time lookup is for effects only and grants no server shot authority.
struct EffectAimDiagnostic {
    unsigned matches{},rejection{};
    int latest_command_time{-1};
    std::uint64_t publication{},command_age_ms{},pose_age_ms{};
};
bool read_native_command_aim_for_effect(int command_time,std::uintptr_t cg,unsigned primary_weapon,CommandAimPublication*,
                                      EffectAimDiagnostic* diagnostic=nullptr) noexcept;
// Script look-direction queries only. Callers must revalidate their exact
// local-player context; this does not grant firing or native scene authority.
bool read_latest_native_command_aim_publication(CommandAimPublication*) noexcept;
}
