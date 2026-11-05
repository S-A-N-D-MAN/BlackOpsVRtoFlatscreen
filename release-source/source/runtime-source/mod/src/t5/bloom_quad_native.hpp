// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_values.hpp"
#include <cstddef>
namespace bovr::t5 {
enum class BloomQuadStatus : unsigned { none, input, copy, context, geometry, matrix, binding, changed, accepted };
struct BloomQuadSnapshot {
    std::array<std::uint32_t,32> vertices{};
    std::array<std::uint16_t,6> indices{};
    std::array<std::uint32_t,16> matrix{}; // Current transposed code219, exact VS c0..c3.
    // Roles5/6 only: current c23/code121 and c36/code122 raw float4 bits,
    // source version words and destination cache tuples, in that order.
    // Other roles leave all these copied values zero; no float normalization.
    std::array<std::array<std::uint32_t,4>,2> uv_constants{};
    std::array<std::uint16_t,2> uv_versions{};
    std::array<std::array<std::uint32_t,2>,2> uv_bind_cache{};
    std::uint32_t stream_buffer{},stream_offset{},stream_stride{},index_buffer{},declaration{};
    // All remaining native pointers are copied identities only; never COM acquisition.
    std::uint32_t source{},state{},device{},buffer{},native_frame{},view_count{},selected{},view_array{},view{};
    std::uint32_t full_screen{},rendering_mode{},declaration_type{};
    std::uint32_t source_mode{},selector{},width{},height{},target{},material{},technique{},pass{},pass_index{};
    std::uint32_t vertex_count{},index_count{},pixel_cost{},depth_hack{},fp_control{};
    std::uint32_t vertex_pool_pointer{},index_pool_pointer{};
    std::array<std::uint32_t,4> vertex_pool{},index_pool{};
    std::array<std::uint32_t,9> streams{};
    std::array<std::uint32_t,5> scene{};
    render::HudEyeViewport display{},source_viewport{};
    std::array<std::uint32_t,4> eye_offset{};
    std::array<std::uint32_t,16> world{},view_matrix{},projection{},view_projection{},world_view_projection{};
    // World const/base, WVP const/base, transpose-WVP const, in that order.
    std::array<std::uint32_t,5> versions{};
    std::array<std::uint32_t,8> matrix_bind_cache{};
    friend bool operator==(const BloomQuadSnapshot&,const BloomQuadSnapshot&)=default;
};
struct BloomQuadMemory {
    void* context{};
    bool (*copy)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
bool validate_bloom_quad_guards(HMODULE) noexcept;
// Both entry points preserve the full entry FP image and LastError, zero output
// on rejection, and perform the same header/full-data late-copy validation.
// Production always selects its protected native copy; caller owns epoch/owner
// authority and must revalidate that independently before replay.
bool read_bloom_quad(std::uintptr_t,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot*,BloomQuadStatus* =nullptr) noexcept;
// Copied-memory fixture seam. This does not admit arbitrary caller-provided
// snapshots to the active adapter, whose only reader is read_bloom_quad.
bool read_bloom_quad_copied(std::uintptr_t,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot*,const BloomQuadMemory&,BloomQuadStatus* =nullptr) noexcept;
}
