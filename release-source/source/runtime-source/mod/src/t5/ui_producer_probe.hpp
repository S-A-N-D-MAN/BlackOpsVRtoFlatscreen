// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
#include "t5/menu_input_adapter.hpp"

namespace bovr::t5 {
struct NativeUiProduction final {
    std::uint64_t call_serial{};
    DWORD thread_id{};
    int local_client{},refreshed_ui{};
    std::uintptr_t frontend{},command_array{},bytes{},commands{};
    std::uint32_t native_frame{};
    int used_total{},used_critical{},capacity{},warn_size{},configured_max_views{};
    int connection_state{},key_catchers{},menu_count{};
    NativeMenuPage page{};
    bool valid{};
    // Only true in the after callback for the same complete producer call.
    // The final shared list may still receive commands after this boundary.
    bool completed_range{};
};
using BeforeUiProduction=void(*)(const NativeUiProduction&);
using AfterUiProduction=void(*)(const NativeUiProduction&,const NativeUiProduction&);

// Process-lifetime callbacks, registered at first-factory startup only. All
// values are copied; native bytes/pointers remain owned by that backend frame.
// Off by default: BOVR_UI_PRODUCER_PROBE=1 and pinned native image required.
bool install_ui_producer_probe(HMODULE) noexcept;
bool set_ui_producer_callbacks(BeforeUiProduction,AfterUiProduction) noexcept;

// Checks generation/range/thread identity only. A consumer must additionally
// compare its begin/end revision and page/context before publishing interaction.
bool same_ui_production_range(const NativeUiProduction&,const NativeUiProduction&) noexcept;
}
