// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

namespace bovr::t5 {
using VPosMatrix = std::array<float,16>;
using VPosVector = std::array<float,4>;
struct VPosViewport {
    std::int32_t x{},y{},width{},height{};
    bool operator==(const VPosViewport&) const = default;
};
struct VPosWorldInput {
    unsigned target_width{},target_height{};
    VPosViewport viewport{};
    // Direction-to-world rigid transform. For pinned retail this is code203
    // TRANSPOSE_VIEW after eye-offset compensation, not code202 INVERSE_VIEW.
    // The donor calls this invViewM44 although its code index is 203.
    VPosMatrix projection{},inverse_view{};
};
struct VPosWorldConstants {
    VPosVector x{},y{},one{};
    bool operator==(const VPosWorldConstants&) const = default;
};
enum class VPosWorldStatus : std::uint8_t {
    accepted,output,viewport,nonfinite,projection,inverse_view,result
};

// Pure value recipe: no native/device reads, cache writes or render ownership.
// Pinned SP UpdateVPosToWorld RVA 0x3263d0 uses positive-Z row-vector
// projection and produces a NEGATIVE view ray with homogeneous w=0:
//     world_ray = absolute_VPOS.x*x + absolute_VPOS.y*y + one.
// D3D9 VPOS pixel centers are integer coordinates. No D3D10 half-pixel shift
// is introduced. Full zero-origin symmetric viewports retain the native recipe.
bool vpos_world_constants(const VPosWorldInput&,VPosWorldConstants*,
                          VPosWorldStatus* =nullptr) noexcept;

// Convert the actual submitted full-target vectors to the selected viewport.
// The caller proves the native projection is symmetric and GPU vectors agree
// with this camera's source. No shader/device or native cache is touched here.
bool vpos_viewport_constants(const VPosWorldConstants& native,unsigned target_width,
                            unsigned target_height,VPosViewport,VPosWorldConstants*) noexcept;

// Initial integration must additionally prove the actual local source belongs
// to the selected ordinary eye and the active shader consumes the constants.
// These value flags do not establish that proof themselves. Deliberately admit
// only RT3 SCENE here: shadow maps, fullscreen reflection RT10, auxiliary views,
// menu/postfx and unproven target roles remain outside the correction policy.
struct VPosCameraPolicy {
    unsigned eye{},view_count{},view_mode{},viewport_behavior{},target{};
    bool source_camera_matches{},ordinary_eye_owned{},fullscreen{},extra_view{};
};
constexpr bool vpos_camera_correction_allowed(const VPosCameraPolicy& p) noexcept {
    return p.source_camera_matches&&p.ordinary_eye_owned&&!p.fullscreen&&!p.extra_view&&
        p.eye<2&&p.view_count==2&&p.view_mode==1&&p.viewport_behavior==0&&p.target==3;
}
}
