// SPDX-License-Identifier: GPL-3.0-only
// Legacy callback unit fixtures do not configure or execute grenade delivery.
// Production grenade gesture and native call behavior have dedicated targets.
#include "t5/manual_grenade_adapter.hpp"
#include "t5/manual_grenade_renderer.hpp"
namespace bovr::t5 {
bool configure_manual_grenades(HMODULE,bool) noexcept {return false;}
void update_manual_grenade_input(const CommandContext&,const input::CommandRigSample&,bool){}
bool manual_grenade_uses_offhand() noexcept {return false;}
bool manual_grenade_consumes_trigger() noexcept {return false;}
bool manual_grenade_gameplay_current(const CommandContext&) noexcept {return false;}
bool read_manual_grenade_visual(input::grenade::Output*) noexcept {return false;}
bool read_manual_grenade_pose(ManualGrenadePose*) noexcept {return false;}
bool read_manual_grenade_render_pose(ManualGrenadePose*) noexcept {return false;}
bool manual_grenade_render_pending() noexcept {return false;}
bool manual_grenade_pose_current(const ManualGrenadePose&) noexcept {return false;}
bool read_manual_grenade_primary_visual(int,ManualGrenadePrimaryVisual*) noexcept {return false;}
bool manual_grenade_primary_visual_current(const ManualGrenadePrimaryVisual&,std::uint32_t) noexcept {return false;}
bool manual_grenade_standalone_renderer_enabled() noexcept {return false;}
bool manual_grenade_primary_visual_scope(const NativeWeaponContext&) noexcept {return false;}
void publish_manual_grenade_pose(std::uint64_t,std::uint64_t) noexcept {}
bool manual_grenade_authored_visible(std::uint64_t,std::uint64_t) noexcept {return false;}
}
