// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/vpos_world_math.hpp"
#include <Windows.h>
#include <cstdint>
namespace bovr::t5 {
struct VPosEyeOwner {
    void* context{};
    bool (*current)(void*) noexcept{};
    bool valid() const noexcept {return current&&current(context);}
};
// A read-copy witness only. No native pointer in this value grants write access.
struct VPosEyeContext {
    std::uintptr_t base{},source{},state{},view{},buffer{},device{};
    std::uint64_t producer{},generation{},xr_frame{};
    unsigned native_frame{},eye{},target_width{},target_height{};
    VPosViewport viewport{};
    VPosWorldConstants constants{},corrected_constants{};
    bool operator==(const VPosEyeContext&) const = default;
};
bool configure_vpos_eye_scope(HMODULE) noexcept;
struct WaterDrawOwnerSnapshot;
using WaterEyeOrigins=std::array<std::array<float,3>,2>;
struct WaterShadingEyeContext;
class VPosEyeScope {
public:
    bool begin(std::uintptr_t base,std::uintptr_t buffer,unsigned native_frame,
               unsigned eye,VPosViewport viewport,std::uintptr_t device,
               std::uint64_t producer,std::uint64_t generation,std::uint64_t xr_frame,
               VPosEyeOwner,const WaterEyeOrigins* water_origins=nullptr) noexcept;
    void finish() noexcept;
private:
    friend bool read_vpos_eye_context(VPosEyeContext*) noexcept;
    friend bool read_vpos_eye_owner(VPosEyeContext*) noexcept;
    friend bool read_water_draw_owner(WaterDrawOwnerSnapshot*) noexcept;
    friend bool read_water_shading_eye(WaterShadingEyeContext*) noexcept;
    friend bool water_shading_eye_current(const WaterShadingEyeContext&) noexcept;
    VPosEyeContext owner_{};
    VPosEyeOwner current_{};
    WaterEyeOrigins water_origins_{};
    DWORD thread_{};
    std::uint64_t scope_serial_{};
    bool active_{},blocked_{},water_pair_valid_{};
};
// Read-only native context, available only inside the owned ordinary eye.
// GPU consumers must also check bound shader, viewport, target, and constants.
bool read_vpos_eye_context(VPosEyeContext*) noexcept;
// Diagnostic camera ownership only, deliberately independent of code22-24 or
// their matrix cache. constants/corrected_constants remain zero. This is not
// authorization to perform the VPOS correction on shaders that do not use it.
bool read_vpos_eye_owner(VPosEyeContext*) noexcept;
struct WaterEyeNativeSnapshot {
    VPosEyeContext owner{};
    VPosVector source_eye_offset{},source_game_time{},source_hdr{};
    std::array<float,3> source_view_origin{},selected_view_origin{};
    VPosMatrix source_world{},source_transpose_world{},source_transpose_view_projection{};
    // cached code199/current world and cached code215/current viewProjection.
    std::array<std::uint16_t,4> matrix_versions{};
    std::uint32_t source_camera_view{};
    bool operator==(const WaterEyeNativeSnapshot&) const = default;
};
bool read_water_eye_native(WaterEyeNativeSnapshot*) noexcept;
// Diagnostic ownership only: this proves the selected ordinary eye, not the
// native source currently serving a draw. In particular, source/state and both
// VPOS vectors remain zero. Consumers separately prove actual GPU bindings.
struct WaterDrawOwnerSnapshot {
    VPosEyeContext owner{};
    std::array<float,3> selected_view_origin{};
    bool operator==(const WaterDrawOwnerSnapshot&) const = default;
};
bool read_water_draw_owner(WaterDrawOwnerSnapshot*) noexcept;
// Separate production witness for one exact water shader. The immutable pair
// comes from the same generated stereo scene, never the immediate globals.
// Consumers must still prove actual GPU shaders, viewport and render target.
struct WaterShadingEyeContext {
    WaterDrawOwnerSnapshot draw{};
    WaterEyeOrigins eye_origins{};
    VPosVector reflection_offset{};
    std::uint64_t scope_serial{};
    bool operator==(const WaterShadingEyeContext&) const = default;
};
bool read_water_shading_eye(WaterShadingEyeContext*) noexcept;
// Revalidate an already acquired draw-local witness without rebuilding its
// double-copy snapshot. Checks the same mutable native frame, dimensions and
// both origins; callers still take full snapshots at transaction boundaries.
bool water_shading_eye_current(const WaterShadingEyeContext&) noexcept;
// Opt-in, bounded rejection evidence; caller first proves a water material.
void trace_vpos_eye_refusal() noexcept;
void invoke_vpos_eye_native(VPosEyeScope*,void(*)(void*),void*);
}
