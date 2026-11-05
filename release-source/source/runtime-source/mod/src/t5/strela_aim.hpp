// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/strela_context.hpp"
namespace bovr::t5 {
bool configure_strela_aim(HMODULE image) noexcept;
bool ensure_strela_aim() noexcept;
// Shared direction for target acquisition and script muzzle-clearance rays.
// This never changes playerstate, target/lock flags, radius, time or occlusion.
bool strela_script_angles(std::uint32_t entref,wawvr::xr::Vec3f* out) noexcept;
}
