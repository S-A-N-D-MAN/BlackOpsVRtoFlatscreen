// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <span>

namespace bovr::t5 {
struct NativeCinematicSnapshot {
    std::uintptr_t image_base{},bink_handle{},cinematic_material{};
    std::array<char,256> current_name{};
    std::array<std::uintptr_t,3> renderer_images{}; // Y, Cr, Cb; copied identities only
    std::uint64_t observed_ms{};
    DWORD thread_id{};
    int active_image_frame{-1},bound_texture_set{-1};
    bool initialized{},finished{},cg_fullscreen{},cg_fullscreen_known{},valid{};
};
enum class CinematicCommandStatus : std::uint8_t {
    no_current_movie,changed_snapshot,invalid_canvas,malformed_list,
    unsupported_list,no_movie_primitive,partial_movie_primitive,full_canvas_movie_primitive
};
struct CinematicCommandEvidence {
    CinematicCommandStatus status{CinematicCommandStatus::no_current_movie};
    std::uint32_t movie_primitives{},full_canvas_primitives{},tagged_movie_primitives{};
    float last_x{},last_y{},last_width{},last_height{};
    std::size_t consumed_bytes{};
};

// Read-only observer, independently off by default (BOVR_CINEMATIC_PROBE=1).
// Configure once after the normal identity-checked factory startup. No native
// movie routines, locks, decoder methods, renderer calls or hooks are invoked.
bool configure_native_cinematic_observer(HMODULE) noexcept;
bool read_native_cinematic_snapshot(NativeCinematicSnapshot*) noexcept;

// Applies only to an owned, bounded copy of the exact final RB_Draw2D list.
// Its caller must prove producer/backend epoch, source dimensions, and the
// native full-frame viewport/scissor at list entry. Subsequent viewport,
// scissor and projection commands are honored conservatively here.
// A positive result proves a complete-canvas movie primitive was queued; it
// does NOT prove opaque shader output, final visible pixels, panel routing,
// or a complete movie/page generation (same-address reuse remains possible).
CinematicCommandEvidence classify_cinematic_commands(std::span<const std::uint8_t>,
    const NativeCinematicSnapshot& before,const NativeCinematicSnapshot& after,
    std::uint32_t source_width,std::uint32_t source_height) noexcept;
const char* cinematic_command_status_name(CinematicCommandStatus) noexcept;
// C4 only: independently guarded native clear record and bounded movie
// overscan. This does not change HUD replay or the legacy C1 classifier.
bool validate_cinematic_movie_guards(HMODULE) noexcept;
CinematicCommandEvidence classify_cinematic_movie_commands(std::span<const std::uint8_t>,
    const NativeCinematicSnapshot&,const NativeCinematicSnapshot&,
    std::uint32_t source_width,std::uint32_t source_height) noexcept;
}
