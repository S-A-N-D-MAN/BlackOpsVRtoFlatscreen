// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "render/hud_eye_commands.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace bovr::t5 {
struct NativeHudView {
    std::uintptr_t address{},commands{};
    render::HudEyeViewport scene{},display{};
    std::uint8_t missile{};
    friend bool operator==(const NativeHudView&,const NativeHudView&)=default;
};
struct NativeHudFrame {
    std::uintptr_t buffer{},array{},bytes{},view_array{},shared_commands{};
    std::uint32_t frame{},view_count{},last{};
    std::int32_t used{},critical{},capacity{},warning{},configured_views{};
    std::array<NativeHudView,4> views{};
    std::uint8_t renderer_active{};
    friend bool operator==(const NativeHudFrame&,const NativeHudFrame&)=default;
};
struct HudCommandPublication {
    NativeHudFrame native{};
    std::vector<std::uint8_t> bytes;
};
// Called only at the proven post-R_AddCmdEndOfList EndFrame site. This copies
// exactly finalized used bytes before the native counters reset, not capacity.
bool capture_native_hud_publication(std::uintptr_t image_base,HudCommandPublication*) noexcept;
// Stable copied source metadata. Begin snapshots do not require a final ID0.
bool read_native_command_frame(std::uintptr_t image_base,bool frontend,NativeHudFrame*) noexcept;
// Opaque menu capture does not execute/replay these records. This validates only
// the bounded native ID/length/terminator envelope inside the owned allocation.
std::span<const std::uint8_t> published_shared_list(const HudCommandPublication&) noexcept;
// Requires the exact frame/allocation/shared pointer and every published byte,
// with two stable native snapshots. Backend reset allocation counters may be 0.
bool match_native_shared_backend(std::uintptr_t image_base,const HudCommandPublication&) noexcept;
// Extracts a single ID0-terminated list from the owned published extent.
std::span<const std::uint8_t> published_hud_list(const HudCommandPublication&,unsigned view) noexcept;
// Backend-side check accepts the documented reset counters, but requires the
// original frame/storage/views plus byte-for-byte current source list equality.
// Must run before temporarily substituting any native view command pointer.
bool match_native_hud_backend(std::uintptr_t image_base,const HudCommandPublication&,unsigned view) noexcept;
} // namespace bovr::t5
