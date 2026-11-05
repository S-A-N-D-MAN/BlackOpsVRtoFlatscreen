// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
// Caller must have accepted the exact SP file identity and loaded PE headers.
// Install during initial graphics creation, before scene rendering can start.
// Disabled unless BOVR_SCENE_PROBE=1. Does not alter camera or gameplay data.
bool install_scene_probe(HMODULE executable) noexcept;
}
