// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/two_hand_aim.hpp"
#include "xr_math.h"
#include <cmath>
namespace bovr::t5::crossbow {
namespace xr=wawvr::xr;
inline xr::Vec3f add(xr::Vec3f a,xr::Vec3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline xr::Vec3f scale(xr::Vec3f a,float s){return {a.x*s,a.y*s,a.z*s};}
inline xr::Vec3f from_iw(xr::Vec3f a){return {-a.y,a.z,-a.x};}
inline bool project(xr::Vec3f p,const xr::EyeView& e,float* u,float* v){
    const auto q=xr::Rotate(xr::Conjugate(e.pose.orientation),add(p,scale(e.pose.position,-1)));
    const float l=std::tan(e.fov.angle_left),r=std::tan(e.fov.angle_right),b=std::tan(e.fov.angle_down),t=std::tan(e.fov.angle_up);
    if(!std::isfinite(q.z)||q.z>=-.005f||!(r>l&&t>b))return false;
    *u=(q.x/-q.z-l)/(r-l);*v=1-(q.y/-q.z-b)/(t-b);
    return std::isfinite(*u)&&std::isfinite(*v);
}
// Current capture poses only. Prefer the eye whose distant sight line is
// furthest from the nearby lens. The unobstructed eye changes as the bow rolls.
// Never zoom the surrounding stereo world or reuse an old image.
struct LensFit {
    // Forward, left, up in the controller's barrel-aligned metric frame.
    xr::Vec3f offset{.025f,0,.137f};
    float radius{.018f},angular_half{.045f},magnification{};
};
// Rebirth's authored Enfield IR rear glass has a 4.20 mm radius.
// Keep model fitting and composition on the same bounded geometry contract.
inline constexpr float kMinimumLensRadiusMeters=.004f;
// Eye relief is measured from an actual eye, not the head midpoint. At normal
// close relief, the midpoint's half-IPD offset can make a perfectly aligned
// optic appear too close or sideways. Keep the lens in front of a visible eye
// and inside the existing forward-facing cone; binocular projection is still
// validated separately by the compositor.
inline bool eye_relief(const xr::FrameState& f,xr::Vec3f lens,xr::Vec3f forward,float* relief) noexcept {
    if(!relief)return false;
    float best_alignment=-1.f,best_distance{};
    for(const auto& eye:f.eyes){
        const auto delta=add(lens,scale(eye.pose.position,-1));
        const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        if(!std::isfinite(distance)||distance<.01f||distance>.70f)continue;
        const float alignment=(delta.x*forward.x+delta.y*forward.y+delta.z*forward.z)/distance;
        float u{},v{};
        if(!std::isfinite(alignment)||alignment<.80f||!project(lens,eye,&u,&v)||u<0||u>1||v<0||v>1)continue;
        if(alignment>best_alignment){best_alignment=alignment;best_distance=distance;}
    }
    if(best_alignment<0)return false;
    *relief=best_distance;return true;
}
inline bool scope(const xr::FrameState& f,xr::StereoSourceLayout* layout,const LensFit& fit={}) noexcept {
    if(!layout||!f.views_valid||!f.actions.focused)return false;
    const auto& h=f.actions.hands[1];
    if(!h.grip.active||!h.grip.position_valid||!h.grip.position_tracked||!h.aim.active||
       !h.aim.orientation_valid||!h.aim.orientation_tracked)return false;
    const auto pair=input::two_hand_aim_basis(f.actions,{},xr::Hand::Right,true);
    xr::Vec3f forward=xr::Rotate(h.aim.pose.orientation,{0,0,-1}),right=xr::Rotate(h.aim.pose.orientation,{1,0,0}),up=xr::Rotate(h.aim.pose.orientation,{0,1,0});
    if(pair.paired){forward=from_iw(pair.local.forward);right=scale(from_iw(pair.local.left),-1);up=from_iw(pair.local.up);}
    if(!std::isfinite(fit.offset.x)||!std::isfinite(fit.offset.y)||!std::isfinite(fit.offset.z)||
       !std::isfinite(fit.radius)||fit.radius<kMinimumLensRadiusMeters||fit.radius>.06f||
       !std::isfinite(fit.angular_half)||fit.angular_half<.01f||fit.angular_half>.3f||
       !std::isfinite(fit.magnification)||fit.magnification<0||fit.magnification>12)return false;
    const auto lens=add(h.grip.pose.position,add(scale(forward,fit.offset.x),
        add(scale(right,-fit.offset.y),scale(up,fit.offset.z))));
    float distance{};
    if(!pair.paired||!eye_relief(f,lens,forward,&distance))return false;
    const auto aim=add(f.head_center.position,scale(forward,100.f));
    unsigned source_eye{};float separation=-1.f;
    for(unsigned i=0;i<2;++i){
        float aim_u{},aim_v{},lens_u{},lens_v{};
        if(!project(aim,f.eyes[i],&aim_u,&aim_v)||!project(lens,f.eyes[i],&lens_u,&lens_v))continue;
        const float dx=aim_u-lens_u,dy=aim_v-lens_v;
        const float candidate=dx*dx+dy*dy;
        if(candidate>separation){separation=candidate;source_eye=i;}
    }
    if(separation<0)return false;
    float u{},v{};if(!project(aim,f.eyes[source_eye],&u,&v))return false;
    const float half=fit.magnification>=1.f?fit.radius/(distance*fit.magnification):std::tan(fit.angular_half);
    const auto& e=f.eyes[source_eye];
    const float w=2*half/(std::tan(e.fov.angle_right)-std::tan(e.fov.angle_left));
    const float height=2*half/(std::tan(e.fov.angle_up)-std::tan(e.fov.angle_down));
    if(u-w*.5f<0||u+w*.5f>1||v-height*.5f<0||v+height*.5f>1)return false;
    const auto& destination=layout->destinations[source_eye];
    if(!(destination.width>0&&destination.height>0))return false;
    u=(u-destination.x)/destination.width;v=(v-destination.y)/destination.height;
    const float source_w=w/destination.width,source_h=height/destination.height;
    if(u-source_w*.5f<0||u+source_w*.5f>1||v-source_h*.5f<0||v+source_h*.5f>1)return false;
    const auto& rect=layout->eyes[source_eye];auto& s=layout->physical_scope;
    s={};s.active=true;s.source={rect.x+(u-source_w*.5f)*rect.width,rect.y+(v-source_h*.5f)*rect.height,source_w*rect.width,source_h*rect.height};
    s.lens_origin_reference_meters=lens;s.lens_right_reference=right;s.lens_up_reference=up;s.lens_radius_meters=fit.radius;
    const auto camera_right=xr::Rotate(xr::Conjugate(e.pose.orientation),right);
    s.source_rotation_radians=std::atan2(-camera_right.y,camera_right.x);
    return true;
}
}
