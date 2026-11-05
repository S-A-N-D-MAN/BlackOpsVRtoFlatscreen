// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace bovr::render {
enum class CommandListStatus : std::uint8_t { complete,empty,truncated_header,invalid_id,invalid_length,overrun,missing_terminator };
struct CommandCensus {
    CommandListStatus status{CommandListStatus::empty};
    std::array<std::uint32_t,29> active{},tagged{};
    std::array<std::uint16_t,29> shortest{},longest{};
    std::uint32_t unsupported_layout_mask{},malformed_layout_mask{};
    std::size_t consumed_bytes{},records{};
    // Layout recognition is only a parser result. It does not prove native
    // material shader purity or permit replay/retention of resource pointers.
    // ID15 recognizes the pinned 48-byte four-corner image record; arbitrary
    // finite XY corners are valid layout data, not proof of visible geometry.
    bool all_active_layouts_recognized{};
};
CommandCensus census_hud_commands(std::span<const std::uint8_t>) noexcept;
const char* command_list_status_name(CommandListStatus) noexcept;
}
