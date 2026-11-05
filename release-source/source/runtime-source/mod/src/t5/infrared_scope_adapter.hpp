// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
namespace bovr::t5 {
bool configure_infrared_scope_adapter(HMODULE,bool input_ready) noexcept;
// Called only after the command adapter admits a live thermal weapon profile.
bool ensure_infrared_scope_adapter() noexcept;
}
