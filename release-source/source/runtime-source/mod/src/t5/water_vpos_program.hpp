// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/vpos_world_math.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace bovr::t5 {
struct WaterVPosProgram {
    const char* pixel_sha256;
    const char* vertex_sha256;
    unsigned pixel_bytes,vertex_bytes;
    std::array<unsigned,3> registers; // ray x, ray y, ray constant
    unsigned reflection_sample_word,reflection_sampler;
    std::array<std::uint32_t,4> reflection_sample;
};
const std::array<WaterVPosProgram,3>& water_vpos_programs() noexcept;
bool water_vpos_make_derivative(unsigned program,std::span<const std::uint32_t>,
                               std::vector<std::uint32_t>*) noexcept;

// Actual GPU VS c0..3 are the transposed camera-relative view/projection.
// Validate a symmetric rigid camera and the full-target native ray recipe
// against those actual GPU values before correcting the three PS XYZ vectors.
// No game/source pointers, native caches, shader changes or GPU writes here.
// Preserve each unused W component exactly; the recognized PS consume XYZ.
bool water_vpos_gpu_constants(const VPosMatrix& vertex_c0_to_c3,
                             const VPosWorldConstants& actual,
                             unsigned target_width,unsigned target_height,
                             VPosViewport,VPosWorldConstants*) noexcept;
}
