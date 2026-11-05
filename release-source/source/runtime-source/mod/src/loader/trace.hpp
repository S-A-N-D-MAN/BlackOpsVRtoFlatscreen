// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace bovr {
void initialize_trace(HMODULE module) noexcept;
void trace(const char* event, const char* format = "", ...) noexcept;
}
