// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/room_scale.hpp"
#include "camera_comfort_logic.hpp"
#include "head_relative_pose_freeze_logic.hpp"
namespace bovr::input::room_scale {
inline constexpr float minimum_camera_radius=6.f,maximum_camera_radius=64.f,camera_skin=.5f;
// Pinned retail R_RenderScene setup (6c800e..6c803d): nonpositive raw
// refdef.near_clip selects4; a positive near clip is clamped to at least1.
// This is only the geometric interpretation; never change the native field.
inline float effective_camera_near(float raw) noexcept {return raw>0?std::max(raw,1.f):4.f;}
// The render camera adds symmetric half-IPD after approving the head center,
// and keeps the native near plane. Enclose both complete near-plane rectangles
// in an orientation-independent sphere, so later head rotation is also safe.
// The engine trace receives equal XYZ extents (a sphere/cube enclosing it).
inline bool camera_clearance(const xr::FrameState& frame,float near_clip,float* radius) noexcept {
    if(!radius||!frame.views_valid||!frame.should_render||!frame.frame_id||
       !std::isfinite(near_clip))return false;
    const float effective_near=effective_camera_near(near_clip);
    const auto delta=subtract(frame.eyes[0].pose.position,frame.eyes[1].pose.position);
    if(!finite(delta))return false;
    const double half_ipd=.5*std::sqrt(double(delta.x)*delta.x+double(delta.y)*delta.y+double(delta.z)*delta.z)*units_per_meter;
    if(!std::isfinite(half_ipd)||half_ipd>8)return false;
    double extent=minimum_camera_radius;
    for(const auto& eye:frame.eyes) {
        const auto& f=eye.fov;
        if(!std::isfinite(f.angle_left)||!std::isfinite(f.angle_right)||
           !std::isfinite(f.angle_up)||!std::isfinite(f.angle_down)||
           f.angle_left>=0||f.angle_right<=0||f.angle_up<=0||f.angle_down>=0||
           -f.angle_left>=1.55334306f||f.angle_right>=1.55334306f||
           f.angle_up>=1.55334306f||-f.angle_down>=1.55334306f)return false;
        // Same symmetric projection extents as build_stereo_views.
        const double x=std::max(-std::tan(double(f.angle_left)),std::tan(double(f.angle_right)));
        const double y=std::max(-std::tan(double(f.angle_down)),std::tan(double(f.angle_up)));
        extent=std::max(extent,half_ipd+double(effective_near)*std::sqrt(1+x*x+y*y)+camera_skin);
    }
    if(!std::isfinite(extent)||extent>maximum_camera_radius)return false;
    *radius=static_cast<float>(extent);return true;
}
inline bool camera_clearance_current(float approved,const xr::FrameState& frame,float near_clip) noexcept {
    float needed{};
    return std::isfinite(approved)&&approved>=minimum_camera_radius&&approved<=maximum_camera_radius&&
        camera_clearance(frame,near_clip,&needed)&&needed<=approved+.0001f;
}
inline bool apply_approved_eye(xr::Vec3f stock_origin,const xr::Basis3f& stock_axis,
                              const xr::Posef& anchor,xr::Vec3f eye,bool vertical_clipped,
                              xr::FrameState* frame) noexcept {
    if(!frame||!finite(stock_origin)||!finite(eye))return false;
    xr::Basis3f level{};if(!wawvr::mod::gravity_level_t4_camera_axis(stock_axis,&level))return false;
    const auto delta=subtract(eye,stock_origin);
    const auto dot=[](xr::Vec3f a,xr::Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto tracking=xr::Rotate(anchor.orientation,{-dot(delta,level.left)/units_per_meter,
        dot(delta,level.up)/units_per_meter,-dot(delta,level.forward)/units_per_meter});
    frame->head_center.position.x=anchor.position.x+tracking.x;
    frame->head_center.position.z=anchor.position.z+tracking.z;
    if(vertical_clipped)frame->head_center.position.y=anchor.position.y+tracking.y;
    return true;
}
// The render camera may have an older command-approved world center. Translate
// all tracked visuals by the identical difference; never mutate tracked poses,
// native body or collision approval to make them appear stable.
inline bool camera_translation(xr::Vec3f stock_origin,const xr::Basis3f& stock_axis,
    const xr::Posef& anchor,const xr::Posef& raw_head,const xr::Posef& camera_head,
    xr::Vec3f* translation,xr::Vec3f* raw_world=nullptr,xr::Vec3f* camera_world=nullptr) noexcept {
    if(!translation)return false;*translation={};
    xr::Basis3f level{};xr::EnginePose raw{},camera{};
    if(!wawvr::mod::gravity_level_t4_camera_axis(stock_axis,&level)||
       !wawvr::mod::compose_head_world_pose({stock_origin,level},raw_head,anchor,units_per_meter,&raw)||
       !wawvr::mod::compose_head_world_pose({stock_origin,level},camera_head,anchor,units_per_meter,&camera))return false;
    const auto delta=subtract(camera.position,raw.position);
    if(!finite(delta))return false;
    *translation=delta;if(raw_world)*raw_world=raw.position;if(camera_world)*camera_world=camera.position;
    return true;
}
}
