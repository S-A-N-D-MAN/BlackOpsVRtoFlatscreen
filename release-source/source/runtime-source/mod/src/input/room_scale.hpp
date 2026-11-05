// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"
#include "xr_math.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bovr::input::room_scale {
namespace xr=wawvr::xr;
inline constexpr float units_per_meter=xr::kIwUnitsPerMeter;
inline constexpr float lean_units=.10f*units_per_meter;
inline constexpr float maximum_residual_units=.30f*units_per_meter;
inline constexpr std::uint64_t maximum_pose_age_ms=100,settle_ms=250;
struct Sample {
    CommandRigSample rig{};
    xr::Vec3f origin{};
    float body_yaw{};
    std::uint64_t epoch{},now{};
    std::uintptr_t player{};
    int command_number{};
    bool eligible{},user_movement{};
};
struct Plan {
    xr::Posef anchor{};
    xr::Vec3f residual{},observed_body{}; // World units; horizontal only.
    float forward{},right{};
    bool valid{},rebased{},blocked{},moving{};
};
inline bool finite(xr::Vec3f v) noexcept {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
inline float length(xr::Vec3f v) noexcept {return std::hypot(v.x,v.y);}
inline xr::Vec3f world(xr::Vec3f v,float yaw) noexcept {
    const float r=yaw*.0174532925199433f,c=std::cos(r),s=std::sin(r);
    return {v.x*c-v.y*s,v.x*s+v.y*c,0};
}
inline xr::Vec3f relative(xr::Vec3f v,float yaw) noexcept {return world(v,-yaw);}
inline xr::Vec3f subtract(xr::Vec3f a,xr::Vec3f b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline xr::Vec3f scaled(xr::Vec3f a,float s) noexcept {return {a.x*s,a.y*s,a.z*s};}
// Normal body feedback stays bounded to half a metre. A tracking discontinuity
// may need a larger coordinate rebase; it can only move the horizontal anchor
// toward this exact head sample and leave at most the existing30cm residual.
// This never authorizes native body displacement or a vertical/yaw change.
inline bool horizontal_anchor_transfer_allowed(const xr::Posef& original,const xr::Posef& head,
                                               const xr::Posef& desired,bool discontinuity) noexcept {
    if(!finite(original.position)||!finite(head.position)||!finite(desired.position)||
       std::memcmp(&original.orientation,&desired.orientation,sizeof(original.orientation))||
       desired.position.y!=original.position.y)return false;
    const double dx=double(desired.position.x)-original.position.x;
    const double dz=double(desired.position.z)-original.position.z;
    if(std::hypot(dx,dz)<=.5)return true;
    if(!discontinuity)return false;
    const double hx=double(head.position.x)-original.position.x;
    const double hz=double(head.position.z)-original.position.z;
    const double distance=std::hypot(hx,hz),dot=dx*hx+dz*hz;
    return distance>.5&&dot>=0&&dot<=distance*distance+.0001*distance&&
        std::fabs(dx*hz-dz*hx)<=.0001*distance&&
        std::hypot(hx-dx,hz-dz)<=.3001;
}
inline void shift_anchor(xr::Posef& anchor,xr::Vec3f world_delta,float yaw) noexcept {
    const auto body=relative(world_delta,yaw);
    // IW +forward/+left -> OpenXR -Z/-X, then back to tracking space.
    const auto tracking=xr::Rotate(anchor.orientation,{-body.y/units_per_meter,0,-body.x/units_per_meter});
    anchor.position.x+=tracking.x;anchor.position.z+=tracking.z;
}
inline xr::Vec3f head_residual(const Sample& s,const xr::Posef& anchor) noexcept {
    return world(xr::OpenXrPoseToIwRelative(s.rig.frame.head_center,anchor,units_per_meter).position,s.body_yaw);
}
inline bool valid(const Sample& s) noexcept {
    const auto& r=s.rig;const auto& f=r.frame;
    const auto unit=[](xr::Quaternionf q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        return std::isfinite(n)&&n>.98f&&n<1.02f;};
    return s.eligible&&s.epoch&&s.player&&s.command_number>=0&&s.now&&
        r.xr_generation&&r.anchor_generation==r.xr_generation&&r.published_ms&&
        s.now>=r.published_ms&&s.now-r.published_ms<=maximum_pose_age_ms&&
        f.frame_id&&f.actions.sequence&&f.actions.focused&&!(f.actions.menu.active&&f.actions.menu.current)&&
        f.views_valid&&f.should_render&&finite(s.origin)&&finite(f.head_center.position)&&
        finite(r.tracking_anchor.position)&&unit(f.head_center.orientation)&&unit(r.tracking_anchor.orientation)&&
        std::fabs(r.tracking_anchor.orientation.x)<.0001f&&std::fabs(r.tracking_anchor.orientation.z)<.0001f&&
        std::isfinite(s.body_yaw);
}

// No native state writes. The adapter submits these bounded axes to stock
// movement and feeds the next observed collision-resolved origin back here.
// The caller works on a copy and adopts it only after the planned anchor was
// committed. Repeated XR frames may observe body movement but never add demand.
class Follow final {
public:
    void reset() noexcept {*this={};}
    Plan step(const Sample& s) noexcept {
        Plan p{};p.anchor=s.rig.tracking_anchor;
        if(!valid(s)){reset();return p;}
        const auto& head=s.rig.frame.head_center.position;
        const bool tracking_jump=active_&&std::hypot(head.x-head_.x,head.z-head_.z)>.25f;
        const bool discontinuity=!active_||s.epoch!=epoch_||s.player!=player_||
            s.rig.xr_generation!=generation_||s.command_number<=command_||s.now<time_||s.now-time_>100||
            std::memcmp(&s.rig.tracking_anchor,&anchor_,sizeof(anchor_))||
            std::fabs(std::remainder(s.body_yaw-yaw_,360.f))>.25f||
            length(subtract(s.origin,origin_))>units_per_meter*.5f||
            tracking_jump;
        p.valid=true;p.rebased=discontinuity;
        if(discontinuity){last_motion_=0;followed_=false;}
        else {
            const auto body=subtract(s.origin,origin_);
            // Movement from a previously owned request must stay bounded and
            // point into its requested half-plane. External motion is rebased.
            if(followed_&&length(body)>.0001f) {
                if(body.x*direction_.x+body.y*direction_.y>=-.01f&&length(body)<=4.f) {
                    p.observed_body={body.x,body.y,0};shift_anchor(p.anchor,p.observed_body,s.body_yaw);
                } else {p.rebased=true;followed_=false;}
            }
            if(std::hypot(head.x-head_.x,head.z-head_.z)>.001f)last_motion_=s.now;
        }
        // A discontinuous tracking position is a new horizontal baseline, not
        // a30cm lean to repay when the user next moves. Keep ordinary yaw/owner
        // rebaselines separate so they do not swallow a continuous physical lean.
        // The adapter still has to approve this exact head-derived anchor and
        // cancel the native movement request before adopting this state.
        if(tracking_jump){p.anchor.position.x=head.x;p.anchor.position.z=head.z;}
        p.residual=head_residual(s,p.anchor);
        // Bound normal lean and non-position lifecycle rebaselines. A tracking
        // jump above has zero horizontal residual and never requests a walk.
        const float n=length(p.residual);
        if(n>maximum_residual_units) {
            const auto excess=scaled(p.residual,(n-maximum_residual_units)/n);
            shift_anchor(p.anchor,excess,s.body_yaw);p.residual=scaled(p.residual,maximum_residual_units/n);
        }
        if(!p.rebased&&!s.user_movement&&s.now-last_motion_<=settle_ms) {
            const float distance=length(p.residual);
            if(distance>lean_units+.1f) {
                const float speed=std::min((distance-lean_units)/20.f,.35f);
                const auto local=relative(scaled(p.residual,1.f/distance),s.body_yaw);
                // PM_CmdScale uses maximum component, not vector length.
                const float m=std::max(std::fabs(local.x),std::fabs(local.y));
                p.forward=local.x*speed/m;p.right=-local.y*speed/m;p.moving=true;
            }
        }
        if(!p.rebased&&!s.user_movement&&followed_&&length(p.observed_body)<.02f) {
            if(!blocked_since_)blocked_since_=s.now;
            if(s.now-blocked_since_>=100){p.forward=p.right=0;p.moving=false;p.blocked=true;last_motion_=0;}
        } else blocked_since_=0;
        if(s.user_movement){p.forward=p.right=0;p.moving=false;last_motion_=0;}
        remember(s,p);return p;
    }
    // The native camera sweep supplies the accepted fraction. Consume only
    // rejected horizontal tracking displacement; vertical stance stays intact.
    void clip(const Sample& s,Plan& p,float fraction,bool solid) noexcept {
        if(!p.valid)return;
        if(!std::isfinite(fraction)||fraction<0||fraction>1)fraction=0;
        if(solid)fraction=0;
        if(fraction<1) {
            const auto rejected=scaled(p.residual,1-fraction);
            shift_anchor(p.anchor,rejected,s.body_yaw);p.residual=scaled(p.residual,fraction);
            p.forward=p.right=0;p.moving=false;p.blocked=true;
        }
        remember(s,p);
    }
private:
    void remember(const Sample& s,const Plan& p) noexcept {
        active_=true;generation_=s.rig.xr_generation;epoch_=s.epoch;player_=s.player;
        command_=s.command_number;time_=s.now;head_=s.rig.frame.head_center.position;
        origin_=s.origin;yaw_=s.body_yaw;anchor_=p.anchor;followed_=p.moving;
        direction_=length(p.residual)>.0001f?scaled(p.residual,1.f/length(p.residual)):xr::Vec3f{};
    }
    bool active_{},followed_{};
    std::uint64_t generation_{},epoch_{},time_{},last_motion_{},blocked_since_{};
    std::uintptr_t player_{};int command_{};float yaw_{};
    xr::Vec3f head_{},origin_{},direction_{};xr::Posef anchor_{};
};
}
