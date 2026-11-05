// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/helicopter_head_ray.hpp"

namespace bovr::input {
// Geometry for the native local-player use query only. The adapter separately
// verifies the exact callsite, ordinary local owner, current stock refdef and
// generation. Keep the native eye origin, trigger range and look cone unchanged;
// this returns only the forward axis the stereo renderer presents to the HMD.
inline bool native_use_gaze(const CommandRigSample& rig,
    const wawvr::xr::EnginePose& stock,std::uint64_t now,
    wawvr::xr::Vec3f* out) noexcept {
    if(!out)return false;*out={};
    if(rig.frame.actions.menu.active&&rig.frame.actions.menu.current)return false;
    wawvr::xr::EnginePose head{};
    if(!helicopter_head_ray(rig,stock,now,&head))return false;
    *out=head.axis.forward;
    return true;
}
}
