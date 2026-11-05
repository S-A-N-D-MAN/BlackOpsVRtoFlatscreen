// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "input/command_rig_ledger.hpp"
namespace bovr::input::manual {struct Output;}
namespace bovr::t5 {
bool configure_manual_reload(HMODULE,bool input_ready) noexcept;
bool manual_reload_enabled() noexcept;
bool manual_reload_uses_offhand() noexcept;
// Cancel the current owner's physical gesture before a deliberate selection.
// No ammo transfer or retry; a failed switch retains unresolved pump/charge.
bool cancel_manual_reload_for_selection(const CommandContext&) noexcept;
void update_manual_reload_input(const CommandContext&,const input::CommandRigSample&,bool live) noexcept;
void apply_manual_reload_command(const CommandContext&,unsigned char*) noexcept;
bool read_manual_reload_visual(input::manual::Output*) noexcept;
void publish_manual_reload_pose_visual(std::uint64_t frame,std::uint64_t transaction,bool device) noexcept;
bool manual_reload_authored_device_visible(std::uint64_t frame,std::uint64_t transaction) noexcept;
}
