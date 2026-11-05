// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/final_ui_probe.hpp"
namespace bovr::t5 {
// Bounded passive copy at exact final UI entry; no command/resource mutation.
void observe_hud_commands(std::uintptr_t image_base,const NativeFinalUi&,unsigned long call_serial) noexcept;
}
