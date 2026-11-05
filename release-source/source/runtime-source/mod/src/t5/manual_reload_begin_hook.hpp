// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>

namespace bovr::t5 {
using ManualReloadBeginCallback=bool(*)(void*) noexcept;
using NativeReloadBegin=void(__cdecl*)(void*);
// The callback only decides whether the current local manual owner consumes
// this reload start. False preserves the native call and its argument.
bool configure_manual_reload_begin_hook(HMODULE,ManualReloadBeginCallback) noexcept;
void dispatch_manual_reload_begin(void*,ManualReloadBeginCallback,NativeReloadBegin);
}
