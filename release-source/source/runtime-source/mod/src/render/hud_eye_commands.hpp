// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <array>
#include <span>
#include <vector>
namespace bovr::render {
struct HudEyeViewport {
    std::int32_t x{},y{},width{},height{};
    friend bool operator==(const HudEyeViewport&,const HudEyeViewport&)=default;
};
struct HudEyeDestination {float x{},y{},width{},height{};};
struct HudVisibleRegion {float x{},y{},width{1},height{1};};
// Centered intersection of source pixels that survive BOTH actual asymmetric
// compositor viewports. Values must come from the same staged XR world ticket.
bool common_visible_hud_region(const std::array<HudEyeDestination,2>&,
                              HudVisibleRegion*) noexcept;
enum class HudEyeStatus : std::uint8_t {
    accepted,invalid_canvas,invalid_list,unsupported_command,ambiguous_overlay,
    unsupported_geometry,no_draws,allocation_failed
};
enum class HudCompassRejection : std::uint8_t {
    none,prior_constant,wrong_index,missing_draw,draw_not_quad,missing_after,
    // Retained numeric slot for old diagnostics; scoped restoration now permits later commands.
    draw_not_final,missing_policy,material_unproved
};
struct HudEyeCommands {
    HudEyeStatus status{HudEyeStatus::invalid_list};
    std::vector<std::uint8_t> bytes;
    float uniform_scale{},offset_x{},offset_y{};
    std::uint32_t draws{},skipped_tagged{},centered_reticles{};
    // Diagnostic values copied only from a parser-bounded rejected record.
    // word4 is opaque; material is populated only for known image layouts.
    bool rejected_record{};
    std::uint32_t rejected_offset{},rejected_word4{},rejected_material{};
    std::uint16_t rejected_length{};
    std::uint8_t rejected_id{},rejected_flags{};
    // ID1 diagnostics retain the existing short-circuit policy: an uncalled
    // material proof is distinct from a proof that ran and returned false.
    HudCompassRejection compass_rejection{};
    bool compass_next_present{},compass_after_present{},compass_proof_called{},compass_proved{};
    std::uint8_t compass_next_id{},compass_after_id{};
    std::uint32_t compass_next_material{};
};
// Required only for the known VPOS compass rectangle command. The caller
// verifies its current native material/shader and retains backend ownership.
struct HudMaterialPolicy {
    void* context{};
    bool (*compass)(void*,std::uint32_t) noexcept{};
    // Optional cosmetic policy, supplied only by the exact produced helicopter
    // world frame. Proves the current native hind_reticle material; never owns
    // its lifetime and never changes ordinary HUD, menu or scope geometry.
    bool (*hind_reticle)(void*,std::uint32_t) noexcept{};
};
// Owns transformed bytes only. Native material/font integers are neither
// dereferenced nor lifetime-extended: caller must prove same backend frame.
// The declared initial layout is an aspect-preserving centered fit of the
// native full backbuffer canvas into each eye. It is not native HUD parity.
HudEyeCommands fit_hud_commands_to_eye(std::span<const std::uint8_t> source,
    std::uint32_t source_width,std::uint32_t source_height,HudEyeViewport eye,
    HudVisibleRegion visible={},HudMaterialPolicy materials={}) noexcept;
} // namespace bovr::render
