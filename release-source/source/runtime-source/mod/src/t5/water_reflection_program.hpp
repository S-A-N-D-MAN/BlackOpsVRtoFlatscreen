// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "render/hud_eye_commands.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace bovr::t5 {
struct WaterReflectionProgram {
    std::string_view name,sha256;
    unsigned bytes,sample_word,sampler;
    std::array<unsigned,3> vpos_registers;
    std::array<std::uint32_t,4> sample;
};
// Fingerprints and four instruction words only. No game shader asset is embedded.
inline constexpr std::array<WaterReflectionProgram,4> water_reflection_programs{{
    {"sw_waterflow_fresnel_no_feather_w_sun.hlsl","6458bfcf06d0cab7db5c200618a7598ccffbac981f0397116f9337eaa28a3ee4",4268,774,0,{9,10,11},{0x03000042,0x802f0001,0x80e40001,0xa0e40800}},
    {"sw_waterflow_fresnel_no_feather_w_sun_shad.hlsl","0459ecfd87052c67fbf24f3b53069132386ce5cca1432d8ca6063e13d0f3884b",5524,966,1,{9,10,11},{0x03000042,0x802f0000,0x80e40000,0xa0e40801}},
    {"sw_waterflow_fresnel_w_sun.hlsl","4d394bc8ec97173da85ec66709a2f9fe5f13bb5e6539d950b15241c2a9cd9c49",4556,791,1,{10,11,20},{0x03000042,0x802f0002,0x80e40002,0xa0e40801}},
    {"sw_waterflow_fresnel_w_sun_shad.hlsl","4b4231b270bcbdce1a87b3a8387852fdf37e646c867c596f82286739bc94c59e",5812,891,2,{10,11,20},{0x03000042,0x802f0000,0x80e40000,0xa0e40802}}
}};
inline constexpr unsigned water_affine_register=220,water_bounds_register=221;
struct WaterReflectionUv {
    std::array<float,4> affine{},bounds{};
    friend bool operator==(const WaterReflectionUv&,const WaterReflectionUv&)=default;
};
bool water_reflection_uv(const render::HudEyeViewport&,unsigned target_width,unsigned target_height,
    unsigned texture_width,unsigned texture_height,WaterReflectionUv*) noexcept;
// Structural insertion helper is separate from runtime recognition to test it
// with synthetic shader programs. The runtime must use make_derivative below.
bool water_reflection_insert(std::span<const std::uint32_t>,unsigned sample_word,
    const std::array<std::uint32_t,4>& sample,std::vector<std::uint32_t>*) noexcept;
bool water_reflection_make_derivative(unsigned program,std::span<const std::uint32_t>,
    std::vector<std::uint32_t>*) noexcept;
inline constexpr std::string_view water_underwater_vertex_sha256=
    "16880398eebaa1ad45ede03d4ed4bde2b1f041c9a7c9b11cb16abe6c3af200e3";
inline constexpr unsigned water_underwater_vertex_bytes=1596,water_underwater_vertex_varying_word=392,
    water_underwater_view_register=222;
// Only the camera-relative o1 varying changes; geometry/projection tokens do
// not. The pixel shader uses this varying for reflection/Fresnel transparency.
// The structural helper is for synthetic tests; production uses the exact
// captured-program fingerprint gate, without embedding the game asset.
bool water_underwater_vertex_insert(std::span<const std::uint32_t>,unsigned varying_word,
    std::vector<std::uint32_t>*) noexcept;
bool water_underwater_vertex_make_derivative(std::span<const std::uint32_t>,
    std::vector<std::uint32_t>*) noexcept;
// GetFunction does not make a guessed destination capacity safe. The COM
// object is retained by the caller so its bytecode is immutable across reads.
template<class Reader> bool water_shader_capture(Reader&& read,std::vector<std::uint32_t>* output,unsigned* actual_bytes) noexcept {
    if(!output||!actual_bytes)return false;output->clear();*actual_bytes=0;
    try {
        unsigned bytes{};if(!read(nullptr,&bytes))return false;*actual_bytes=bytes;
        if(!bytes||bytes>65536||bytes%4)return false;
        output->resize(bytes/4);unsigned returned=bytes;
        if(!read(output->data(),&returned)||returned!=bytes){output->clear();*actual_bytes=returned;return false;}
        return true;
    }catch(...){output->clear();return false;}
}
bool water_shader_sha256(std::span<const std::uint32_t>,std::array<char,65>*) noexcept;
bool water_shader_fingerprint(std::span<const std::uint32_t>,std::string_view expected_sha256) noexcept;
}
