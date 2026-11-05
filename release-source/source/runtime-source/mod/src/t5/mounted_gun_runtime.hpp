// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"
#include "t5/command_probe.hpp"
#include <windows.h>
namespace bovr::t5 {
// Pinned retail SP only. Changes three native call sites; each original runs
// exactly once. Server view angles and the visible pose pointer are borrowed
// only around their specific turret consumer and restored before returning.
bool configure_mounted_gun_runtime(HMODULE,bool requested) noexcept;
void publish_mounted_gun_scene(const input::CommandRigSample&,
    const wawvr::xr::Vec3f& stock_origin,const wawvr::xr::Basis3f& stock_axis,
    const PreCommandSnapshot&,std::uintptr_t refdef) noexcept;
void revoke_mounted_gun_scene() noexcept;
}
