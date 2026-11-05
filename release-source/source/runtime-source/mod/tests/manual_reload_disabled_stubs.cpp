// SPDX-License-Identifier: GPL-3.0-only
// Existing isolated input fixtures exercise their own feature only. Production
// reload integration is linked by bovr_observation and tested independently.
#include "t5/manual_reload_adapter.hpp"
#include "t5/crossbow_adapter.hpp"
#include "t5/crossbow_script_aim.hpp"
#include "t5/hand_mesh_draw.hpp"
namespace bovr::t5 {
bool update_crossbow_input(const CommandContext&,const input::CommandRigSample&,bool) noexcept{return false;}
bool configure_crossbow_projectile_hook(HMODULE) noexcept{return false;}
bool configure_crossbow_script_aim(HMODULE) noexcept{return false;}
bool manual_reload_enabled() noexcept{return false;}
bool manual_reload_uses_offhand() noexcept{return false;}
bool cancel_manual_reload_for_selection(const CommandContext&) noexcept{return true;}
void update_manual_reload_input(const CommandContext&,const input::CommandRigSample&,bool) noexcept{}
void apply_manual_reload_command(const CommandContext&,unsigned char*) noexcept{}
bool read_manual_reload_visual(input::manual::Output*) noexcept{return false;}
void publish_manual_reload_pose_visual(std::uint64_t,std::uint64_t,bool) noexcept{}
bool publish_hand_mesh(std::uintptr_t,unsigned,std::uint64_t,std::uint64_t) noexcept{return true;}
bool publish_left_hand_mesh(std::uintptr_t,const borrowed_profile::Profile&,unsigned,std::uint64_t,std::uint64_t) noexcept{return false;}
bool manual_reload_authored_device_visible(std::uint64_t,std::uint64_t) noexcept{return false;}
}
