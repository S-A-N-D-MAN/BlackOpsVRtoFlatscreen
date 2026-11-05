// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "camera_comfort_logic.hpp"
#include "xr_math.h"
#include <algorithm>
#include <cmath>

namespace bovr::input {
struct SwimmingMovement {
    float forward{},right{},pitch_degrees{};
    bool pitch_limited{};
    wawvr::xr::Vec3f desired_world{};
};

// Express the HMD's 3D movement vector using the native swim command's existing
// yaw and roll-zero forward/right basis. Native camera yaw is never changed.
inline bool orient_swimming(const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& anchor,const wawvr::xr::Basis3f& stock_axis,
    float native_yaw,float native_roll,float pitch_up_limit,float pitch_down_limit,
    float stick_forward,float stick_right,SwimmingMovement* out) noexcept {
    namespace xr=wawvr::xr;
    if(!out)return false;*out={};
    const auto unit=[](const xr::Quaternionf& q) {
        const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        return std::isfinite(n)&&n>.98f&&n<1.02f;
    };
    if(!frame.views_valid||!frame.should_render||!unit(frame.head_center.orientation)||!unit(anchor.orientation)||
       !std::isfinite(native_yaw)||!std::isfinite(native_roll)||std::fabs(native_roll)>.02f||
       !std::isfinite(pitch_up_limit)||!std::isfinite(pitch_down_limit)||
       pitch_up_limit<.2f||pitch_up_limit>90.f||pitch_down_limit<.2f||pitch_down_limit>90.f||
       !std::isfinite(stick_forward)||!std::isfinite(stick_right)||
       std::fabs(stick_forward)>1.001f||std::fabs(stick_right)>1.001f)return false;
    xr::Basis3f body{};
    if(!wawvr::mod::gravity_level_t4_camera_axis(stock_axis,&body))return false;
    auto head=frame.head_center;head.position=anchor.position;
    const auto local=xr::OpenXrPoseToIwRelative(head,anchor,1.f).axis;
    const auto world=[&](const xr::Vec3f& p) {
        return xr::Vec3f{body.forward.x*p.x+body.left.x*p.y+body.up.x*p.z,
                        body.forward.y*p.x+body.left.y*p.y+body.up.y*p.z,
                        body.forward.z*p.x+body.left.z*p.y+body.up.z*p.z};
    };
    auto h=world(local.forward);
    const float length=std::sqrt(h.x*h.x+h.y*h.y+h.z*h.z);
    if(!std::isfinite(length)||length<.001f)return false;
    h={h.x/length,h.y/length,h.z/length};
    const float horizontal=std::hypot(h.x,h.y);
    xr::Vec3f right{};
    if(horizontal>.0001f)right={h.y/horizontal,-h.x/horizontal,0};
    else {
        // At a vertical pole forward has no heading. Its perpendicular head
        // left axis still supplies a finite lateral direction.
        const auto left=world(local.left);const float lateral=std::hypot(left.x,left.y);
        if(!std::isfinite(lateral)||lateral<.001f)return false;
        right={-left.x/lateral,-left.y/lateral,0};
    }
    SwimmingMovement result{};
    result.desired_world={stick_forward*h.x+stick_right*right.x,
                          stick_forward*h.y+stick_right*right.y,stick_forward*h.z};
    const float angle=std::remainder(native_yaw,360.f)*.0174532925199433f;
    const float c=std::cos(angle),s=std::sin(angle);
    const float x=result.desired_world.x*c+result.desired_world.y*s;
    const float r=result.desired_world.x*s-result.desired_world.y*c;
    const float z=result.desired_world.z;
    // A backward-facing target needs a negative native forward command and
    // reversed pitch. Choosing the sign this way preserves backward swimming.
    const float sign=std::fabs(x)>.000001f?(x<0.f?-1.f:1.f):(stick_forward<0.f?-1.f:1.f);
    result.forward=sign*std::hypot(x,z);result.right=r;
    const float pitch=std::atan2(-z*sign,std::fabs(x))*57.29577951308232f;
    // Stay inside the live engine limits, including angle16 quantization and
    // the retail float32 normalize at the admitted +/-360000 delta bound, so
    // PM_UpdateViewAngles does not alter delta_angles when it clamps pitch.
    result.pitch_degrees=std::clamp(pitch,-pitch_up_limit+.1f,pitch_down_limit-.1f);
    result.pitch_limited=std::fabs(result.pitch_degrees-pitch)>.0001f;
    const float original_max=std::max(std::fabs(stick_forward),std::fabs(stick_right));
    const float native_max=std::max(std::fabs(result.forward),std::fabs(result.right));
    if(original_max>0.f) {
        if(!std::isfinite(native_max)||native_max<.000001f)return false;
        // PM_CmdScale uses max(|forward|,|right|), not their vector length.
        // Preserve the original stick speed after changing its coordinate basis.
        const float scale=original_max/native_max;
        result.forward*=scale;result.right*=scale;
    }
    if(!std::isfinite(result.pitch_degrees)||!std::isfinite(result.forward)||!std::isfinite(result.right))return false;
    *out=result;return true;
}
}
