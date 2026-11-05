// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "stereo_scene_math.hpp"
namespace bovr::t5 {
// Value-only structures. No T4 field offsets or native types cross this API.
// Actual SP refdefs are decoded independently by the T5 adapter.
using SceneView = wawvr::mod::T4SceneView;
using StereoViews = wawvr::mod::T4StereoSceneViews;
inline bool build_stereo_views(const SceneView& stock,
                               const wawvr::xr::FrameState& frame,
                               const wawvr::xr::Posef& anchor,
                               StereoViews* output,
                               float units_per_meter=wawvr::xr::kIwUnitsPerMeter) noexcept {
    return wawvr::mod::build_t4_stereo_scene_views(stock,frame,anchor,output,units_per_meter);
}
}
