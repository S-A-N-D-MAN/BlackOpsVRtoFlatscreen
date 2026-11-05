// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/boat_controller_ray.hpp"
#include <string_view>

namespace bovr::t5::support_weapon {
constexpr bool script_pre_current(std::uint64_t observed,std::uint64_t now) noexcept {
    return observed&&now>=observed&&now-observed<=250;
}
constexpr bool script_read_retry(std::string_view stage,unsigned completed_attempts) noexcept {
    return completed_attempts<3&&(stage=="pre_changed"||stage=="owner_changed"||
        stage=="stock_changed"||stage=="scene_clocks");
}
// The server script can run after the frontend advances its coherent stock
// refdef but before the next pre-command publication. Admit only that bounded
// forward progression; backwards time and stale/mismatched views still fail.
constexpr bool script_scene_current(int scene_time,int refdef_time,int pre_time) noexcept {
    return scene_time>0&&pre_time>0&&scene_time==refdef_time&&scene_time>=pre_time&&
        static_cast<std::int64_t>(scene_time)-pre_time<=100;
}
// Geometry only: the native query owner must independently establish the exact
// equipped marker asset and current local gameplay/refdef before using this.
inline bool script_angles(const input::CommandRigSample& rig,
    const wawvr::xr::EnginePose& stock,std::uint64_t now,wawvr::xr::Vec3f* out) noexcept {
    if(!out)return false;*out={};
    if(rig.frame.actions.menu.active&&rig.frame.actions.menu.current)return false;
    wawvr::xr::EnginePose ray{};
    if(!input::boat_controller_ray(rig,stock,now,&ray))return false;
    const auto& f=ray.axis.forward;constexpr float degrees=57.29577951308232f;
    *out={-std::atan2(f.z,std::hypot(f.x,f.y))*degrees,std::atan2(f.y,f.x)*degrees,0.f};
    return std::isfinite(out->x)&&std::isfinite(out->y);
}
}
