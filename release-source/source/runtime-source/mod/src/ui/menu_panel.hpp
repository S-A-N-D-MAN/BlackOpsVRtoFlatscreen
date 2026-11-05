// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <cstdint>
namespace bovr::ui {
enum class OpaqueContext : std::uint8_t { none, menu, cinematic };
// Owner-thread value state. The finite panel is latched in application space on
// context/generation entry; subsequent head translation cannot move the panel.
class MenuPanel final {
public:
    bool update(const wawvr::xr::Posef& head,std::uint64_t generation,OpaqueContext context,
                std::uint32_t source_width,std::uint32_t source_height,wawvr::xr::QuadLayer* output) noexcept;
    void clear() noexcept;
private:
    bool valid_{};
    std::uint64_t generation_{};
    OpaqueContext context_{OpaqueContext::none};
    wawvr::xr::QuadLayer panel_{};
};
}
