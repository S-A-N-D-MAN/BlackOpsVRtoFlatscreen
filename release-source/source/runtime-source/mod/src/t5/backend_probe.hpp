// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr::t5 {
// Independent BOVR_BACKEND_PROBE=1 opt-in, after full SP identity validation.
bool install_backend_probe(HMODULE executable) noexcept;
}
