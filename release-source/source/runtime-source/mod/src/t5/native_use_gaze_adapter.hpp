// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include "input/command_rig_ledger.hpp"
#include "t5/command_probe.hpp"
namespace bovr::t5 {
bool configure_native_use_gaze_adapter(HMODULE executable,bool input_ready) noexcept;
void publish_native_use_gaze_scene(const input::CommandRigSample&,
    const wawvr::xr::Vec3f&,const wawvr::xr::Basis3f&,const PreCommandSnapshot&,
    std::uintptr_t refdef) noexcept;
}
