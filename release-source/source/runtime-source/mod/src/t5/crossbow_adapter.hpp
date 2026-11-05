// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "input/command_rig_ledger.hpp"
namespace bovr::t5 {
bool update_crossbow_input(const CommandContext&,const input::CommandRigSample&,bool live) noexcept;
void crossbow_scope_layout(void*,const wawvr::xr::FrameState&,wawvr::xr::StereoSourceLayout*) noexcept;
bool configure_crossbow_projectile_hook(HMODULE) noexcept;
bool crossbow_native_infrared(int local_client) noexcept;
float select_crossbow_spread(std::uintptr_t entity,unsigned weapon,std::uintptr_t parameters,float spread) noexcept;
}
