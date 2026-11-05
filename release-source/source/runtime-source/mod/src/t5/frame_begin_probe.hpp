// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
namespace bovr::t5 {
struct FrameBeginPoint {
    std::uintptr_t image_base{},caller_return{};
    DWORD native_thread{};
    std::uint64_t call_serial{};
    bool known_direct_caller{},ordinary_screen_frame{};
};
using FrameBeginCallback=void(*)(const FrameBeginPoint&);
// Original no-argument routine always returns before this optional callback.
// No frame success/result interpretation and no TLS scope across native code.
bool install_frame_begin_probe(HMODULE) noexcept;
bool set_frame_begin_callback(FrameBeginCallback) noexcept;
// Separate optional pre-native observer. It cannot skip/replace the original.
// Registration occurs at startup. Entry GP/flags/FP/LastError are preserved.
bool set_frame_begin_pre_callback(FrameBeginCallback) noexcept;
}
