// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
// Explicit experimental controller delivery. Requires guarded command probe,
// validated retail identity and the latest live XR action publication.
// Configure during the serialized factory bootstrap; disabled by default.
// Optional command-rig observation is configured separately and shares this
// adapter's exact pre-command sample; it never replaces these input callbacks.
bool configure_input_adapter(HMODULE,bool command_probe_installed,bool weapon_probe_installed) noexcept;
}
