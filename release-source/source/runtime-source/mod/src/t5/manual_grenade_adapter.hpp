// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "t5/weapon_animation_math.hpp"
namespace bovr::input {struct CommandRigSample; namespace grenade {struct Output;}}
namespace bovr::t5 {
bool configure_manual_grenades(HMODULE,bool weapon_probe_installed) noexcept;
void update_manual_grenade_input(const CommandContext&,const input::CommandRigSample&,bool live);
bool manual_grenade_uses_offhand() noexcept;
bool manual_grenade_consumes_trigger() noexcept;
bool manual_grenade_gameplay_current(const CommandContext&) noexcept;
bool read_manual_grenade_visual(input::grenade::Output*) noexcept;
// Owned visual ticket for the grenade selected by native offhand handling.
// It remains fresh through native release recovery, with holding=false, so
// recovery never displays the stock arms/throw animation over the tracked hand.
struct ManualGrenadePose {
    CommandContext context{};
    std::uint64_t transaction{},generation{},updated_ms{};
    std::uintptr_t cg{},server{},variant{},definition{},projectile_model{};
    std::uintptr_t primary_variant{},primary_definition{},viewmodel{},dobj{};
    unsigned weapon{},primary_weapon{},previous_viewmodel_weapon{};
    int native_start_time{};
    animation_detail::Transform hand{};
    bool holding{};
};
// Copied authority for one visual-only first-person scope. It identifies the
// primary DObj that existed before the native offhand command was issued.
struct ManualGrenadePrimaryVisual {
    std::uint64_t transaction{},generation{};
    std::uintptr_t ps{},cg{},server{},viewmodel{},dobj{};
    unsigned grenade_weapon{},primary_weapon{},previous_viewmodel_weapon{};
    std::uint32_t weap_flags{},primary_word{};
    int weapon_state{};
};
bool read_manual_grenade_pose(ManualGrenadePose*) noexcept;
// Independent renderer state. Unlike the legacy authored-arm path, this is
// available from the grab edge and does not wait for the native offhand DObj.
bool read_manual_grenade_render_pose(ManualGrenadePose*) noexcept;
bool manual_grenade_render_pending() noexcept;
bool manual_grenade_pose_current(const ManualGrenadePose&) noexcept;
bool read_manual_grenade_primary_visual(int local_client,ManualGrenadePrimaryVisual*) noexcept;
bool manual_grenade_primary_visual_current(const ManualGrenadePrimaryVisual&,std::uint32_t expected_flags) noexcept;
void publish_manual_grenade_pose(std::uint64_t transaction,std::uint64_t generation) noexcept;
bool manual_grenade_authored_visible(std::uint64_t transaction,std::uint64_t generation) noexcept;
}
