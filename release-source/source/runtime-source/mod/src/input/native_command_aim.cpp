// SPDX-License-Identifier: GPL-3.0-only
#include "input/native_command_aim.hpp"
#include "camera_comfort_logic.hpp"
#include "xr_math.h"
#include <cmath>
#include <xmmintrin.h>
namespace bovr::input {
namespace {
namespace xr=wawvr::xr;
bool finite(const xr::Vec3f& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool quat(const xr::Quaternionf& q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&n>.98F&&n<1.02F;}
float dot(const xr::Vec3f& a,const xr::Vec3f& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool basis(const xr::Basis3f& b){
    const auto& f=b.forward;const auto& l=b.left;const auto& u=b.up;
    const xr::Vec3f c{f.y*l.z-f.z*l.y,f.z*l.x-f.x*l.z,f.x*l.y-f.y*l.x};
    return finite(f)&&finite(l)&&finite(u)&&std::fabs(dot(f,f)-1)<.02F&&std::fabs(dot(l,l)-1)<.02F&&
        std::fabs(dot(u,u)-1)<.02F&&std::fabs(dot(f,l))<.02F&&std::fabs(dot(f,u))<.02F&&
        std::fabs(dot(l,u))<.02F&&dot(c,u)>.98F;
}
bool same(const xr::Vec3f&a,const xr::Vec3f&b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool same(const xr::Quaternionf&a,const xr::Quaternionf&b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w;}
bool same(const xr::TrackedPose&a,const xr::TrackedPose&b){return a.active==b.active&&a.orientation_valid==b.orientation_valid&&a.position_valid==b.position_valid&&a.orientation_tracked==b.orientation_tracked&&a.position_tracked==b.position_tracked&&same(a.pose.orientation,b.pose.orientation)&&same(a.pose.position,b.pose.position);}
// The selected pose and action identity must be the value returned with this
// original rig. T2 separately joins its additional grip/squeeze fields below;
// no snapshot field is changed or transferred from the existing router.
bool joined(const xr::ActionSnapshot&a,const xr::ActionSnapshot&b,unsigned hand){return a.sequence==b.sequence&&a.focused==b.focused&&same(a.hands[hand].aim,b.hands[hand].aim);}
}
bool pack_native_gun_angles(float pitch,float yaw,std::uint32_t* out) noexcept {
    if(!out||!std::isfinite(pitch)||!std::isfinite(yaw))return false;
    // Match881540's float32 multiplication/CVTTSS2SI and low-word stores.
    // Reduce first to keep float-to-int conversion finite and within int32.
    const float p=std::remainder(pitch,360.F),y=std::remainder(yaw,360.F);
    constexpr float units=182.0444488525390625F;
    // Explicit SSE keeps the native MULSS/MXCSR rounding domain for both
    // words. Ordinary x86 C++ may otherwise keep the second value in x87.
    const auto scale=_mm_set_ss(units);
    const auto a=_mm_cvtt_ss2si(_mm_mul_ss(_mm_set_ss(p),scale));
    const auto b=_mm_cvtt_ss2si(_mm_mul_ss(_mm_set_ss(y),scale));
    *out=std::uint32_t(std::uint16_t(a))|(std::uint32_t(std::uint16_t(b))<<16);return true;
}
CommandAimStatus propose_native_command_aim(const CommandRigSample&r,const xr::ActionSnapshot&a,
    const xr::Basis3f& stock,xr::Hand which,std::uint64_t now,CommandAimProposal*out,bool two_hand_support) noexcept {
    using S=CommandAimStatus;if(!out)return S::storage;*out={};const auto h=static_cast<unsigned>(which);
    if(h>=xr::kHandCount||!r.xr_generation||r.anchor_generation!=r.xr_generation||!r.frame.frame_id||
       !a.sequence||!joined(a,r.frame.actions,h)||(two_hand_support&&!same_support_actions(a,r.frame.actions)))return S::identity;
    if(!r.published_ms||now<r.published_ms||now-r.published_ms>100)return S::stale;
    const auto& f=r.frame;
    if(!f.should_render||!f.views_valid||f.predicted_display_time<=0||f.predicted_display_period<=0||
       !finite(f.head_center.position)||!quat(f.head_center.orientation)||!finite(r.tracking_anchor.position)||!quat(r.tracking_anchor.orientation))return S::prediction;
    const auto& aim=a.hands[h].aim;
    if(!a.focused||!aim.active||!aim.orientation_valid||!aim.orientation_tracked||!quat(aim.pose.orientation))return S::tracking;
    CommandAimProposal p{};
    if(!basis(stock)||!wawvr::mod::gravity_level_t4_camera_axis(stock,&p.body))return S::body;
    auto rotation=aim.pose;rotation.position=r.tracking_anchor.position;
    auto local=xr::OpenXrPoseToIwRelative(rotation,r.tracking_anchor,1.F).axis.forward;
    if(two_hand_support){p.support=two_hand_aim_basis(a,r.tracking_anchor,which,true);if(p.support.paired)local=p.support.local.forward;}
    p.forward={dot({p.body.forward.x,p.body.left.x,p.body.up.x},local),
        dot({p.body.forward.y,p.body.left.y,p.body.up.y},local),dot({p.body.forward.z,p.body.left.z,p.body.up.z},local)};
    const float horizontal=std::hypot(p.forward.x,p.forward.y);
    if(!finite(p.forward)||!std::isfinite(horizontal)||horizontal<.001F)return S::pole;
    constexpr float degrees=57.29577951308232F;
    p.pitch_degrees=-std::atan2(p.forward.z,horizontal)*degrees;
    p.yaw_degrees=std::atan2(p.forward.y,p.forward.x)*degrees;
    if(!pack_native_gun_angles(p.pitch_degrees,p.yaw_degrees,&p.packed))return S::prediction;
    *out=p;return S::accepted;
}
}
