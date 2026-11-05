// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
#include "t5/menu_input_adapter.hpp"
namespace bovr::t5 {
struct NativeFinalUi {
    std::uintptr_t backend{},commands{};
    std::uint32_t native_frame{};
    int connection_state{},key_catchers{},menu_count{};
    bool valid{};
    NativeMenuPage page{};
};
using BeforeFinalUi=void(*)(const NativeFinalUi&);
using AfterFinalUi=void(*)(const NativeFinalUi&,const NativeFinalUi&);
// Static process-lifetime functions, registered only at first-factory startup.
// Original command loop runs exactly once; callbacks never own native pointers.
bool set_final_ui_callbacks(BeforeFinalUi,AfterFinalUi) noexcept;
bool install_final_ui_probe(HMODULE) noexcept;
bool install_event_loop_probe(HMODULE) noexcept;
bool set_event_loop_callback(void(*)()) noexcept;
}
