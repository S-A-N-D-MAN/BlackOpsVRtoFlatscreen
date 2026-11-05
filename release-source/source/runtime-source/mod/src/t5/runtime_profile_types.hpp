// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace bovr::t5::borrowed_profile {

// Immutable, owned copies of the model metadata needed by the VR pose and
// reload code. Production entries are populated only from a validated pair of
// current native observations; no Black Ops asset payload is compiled here.
struct Bone {
    std::string_view name;
    std::uint8_t parent;
    std::array<std::uint32_t, 8> bind;
};

struct Model {
    std::string_view name;
    unsigned count;
    unsigned root;
    unsigned offset;
};

struct Profile {
    std::span<const Model> model;
    std::span<const Bone> bone;
    std::span<const std::uint8_t> model_parents{};
    std::span<const std::uint8_t> duplicate_data{};
};

} // namespace bovr::t5::borrowed_profile
