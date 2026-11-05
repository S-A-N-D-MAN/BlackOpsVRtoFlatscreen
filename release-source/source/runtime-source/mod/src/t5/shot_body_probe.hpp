// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include "input/command_rig_ledger.hpp"
namespace bovr::t5 {
struct NativeBulletSelectionContext;
enum class ShotBodyStatus : unsigned {accepted,disabled,ineligible,missing_scope,missing_rig,identity_changed,
    unreadable,unsupported_context,nonfinite,lean_unsupported,stale,changed,count};
// Copied values only; this is an observation of the actual pre-Bullet_Fire scope,
// not a retained native lease or permission to substitute physical aiming.
struct NativeShotBody {
    input::CommandRigMatch rig{};
    std::uint64_t observed_ms{},scope_observed_ms{};
    DWORD native_thread{};
    std::uintptr_t attacker_identity{},client_identity{},variant_identity{},definition_identity{};
    int weapon_index{},weapon_state{},other_weapon_state{},native_command_time{},shot_game_time{};
    int current_server_time{},current_level_time{},current_last_server_time{};
    float native_origin[3]{},view_height{},yaw_degrees{},lean{},body_origin[3]{};
    bool valid{};
};
// Uses the current consumed-command getter and its exact original matched rig.
// No native helper call or direct rendering-cache read; public route is pinned
// base400000 and requires the configured command-rig probe. Output is cleared on
// every failure; body is finite server origin + viewheight with finite zero lean.
bool read_native_shot_body(std::uintptr_t image_base,const NativeBulletSelectionContext&,NativeShotBody*) noexcept;
// Bounded passive evidence under the existing command-rig probe, before native
// Bullet_Fire. This does not replace the selector or completed-shot callbacks.
void trace_native_shot_body(std::uintptr_t image_base,const NativeBulletSelectionContext&) noexcept;
}
