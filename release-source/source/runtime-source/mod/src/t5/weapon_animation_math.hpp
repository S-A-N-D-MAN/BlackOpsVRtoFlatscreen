// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/stereo_view_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace bovr::t5::animation_detail {
using Vec=wawvr::xr::Vec3f;
using Axis=wawvr::xr::Basis3f;
struct NativeMat32 {std::array<float,4> quaternion{};Vec origin{};float trans_weight{};};
static_assert(sizeof(NativeMat32)==32&&std::is_trivially_copyable_v<NativeMat32>);
struct Transform {Vec origin{};Axis axis{};};
struct PoseCopy {Vec origin{},angles{};};
struct LocalGeometry {Transform gun_world{},flash_world{},pose_world{},gun_in_dobj{},flash_in_gun{};float roundtrip_position{},roundtrip_axis{};};
struct Difference {float gun_position{},gun_angle_radians{},flash_position{},flash_angle_radians{};};
inline float dot(Vec a,Vec b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec add(Vec a,Vec b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec sub(Vec a,Vec b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec cross(Vec a,Vec b) noexcept {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool finite(Vec a) noexcept {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline float distance(Vec a,Vec b) noexcept {const auto d=sub(a,b);return std::sqrt(dot(d,d));}
inline bool rigid(const Axis& a) noexcept {
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
        std::fabs(dot(a.forward,a.forward)-1)<0.003F&&std::fabs(dot(a.left,a.left)-1)<0.003F&&
        std::fabs(dot(a.up,a.up)-1)<0.003F&&std::fabs(dot(a.forward,a.left))<0.003F&&
        std::fabs(dot(a.forward,a.up))<0.003F&&std::fabs(dot(a.left,a.up))<0.003F&&
        dot(cross(a.forward,a.left),a.up)>0.997F;
}
inline Vec local(const Axis& a,Vec v) noexcept {return {dot(a.forward,v),dot(a.left,v),dot(a.up,v)};}
inline Vec world(const Axis& a,Vec v) noexcept {return {a.forward.x*v.x+a.left.x*v.y+a.up.x*v.z,
    a.forward.y*v.x+a.left.y*v.y+a.up.y*v.z,a.forward.z*v.x+a.left.z*v.y+a.up.z*v.z};}
inline Transform relative(const Transform& parent,const Transform& child) noexcept {
    return {local(parent.axis,sub(child.origin,parent.origin)),
        {local(parent.axis,child.axis.forward),local(parent.axis,child.axis.left),local(parent.axis,child.axis.up)}};
}
inline Transform compose(const Transform& parent,const Transform& child) noexcept {
    return {add(parent.origin,world(parent.axis,child.origin)),
        {world(parent.axis,child.axis.forward),world(parent.axis,child.axis.left),world(parent.axis,child.axis.up)}};
}
inline bool convert_native(const NativeMat32& m,Transform* out) noexcept {
    if(!out)return false;*out={};
    const auto x=m.quaternion[0],y=m.quaternion[1],z=m.quaternion[2],w=m.quaternion[3],s=m.trans_weight;
    const auto norm=x*x+y*y+z*z+w*w;
    if(!finite(m.origin)||!std::isfinite(norm)||!std::isfinite(s)||norm<=1.e-12F||s<=0||std::fabs(norm*s-2)>0.003F)return false;
    const Axis a{{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
                 {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
                 {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
    if(!rigid(a))return false;*out={m.origin,a};return true;
}
inline bool pose_transform(const PoseCopy& p,Transform* out) noexcept {
    if(!out)return false;*out={};if(!finite(p.origin)||!finite(p.angles))return false;
    constexpr float radians=0.017453292519943295769F;
    const auto pitch=p.angles.x*radians,yaw=p.angles.y*radians,roll=p.angles.z*radians;
    const auto sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(yaw),cy=std::cos(yaw),sr=std::sin(roll),cr=std::cos(roll);
    const Axis a{{cp*cy,cp*sy,-sp},{sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp},{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp}};
    if(!rigid(a))return false;*out={p.origin,a};return true;
}
inline float axis_error(const Axis& a,const Axis& b) noexcept {
    return distance(a.forward,b.forward)+distance(a.left,b.left)+distance(a.up,b.up);
}
inline float angle(const Axis& a,const Axis& b) noexcept {
    const auto c=(dot(a.forward,b.forward)+dot(a.left,b.left)+dot(a.up,b.up)-1)*0.5F;
    return std::acos(std::clamp(c,-1.F,1.F));
}
inline bool derive_local(const NativeMat32& gun,const NativeMat32& flash,const PoseCopy& pose,LocalGeometry* out) noexcept {
    if(!out)return false;*out={};LocalGeometry v{};
    if(!convert_native(gun,&v.gun_world)||!convert_native(flash,&v.flash_world)||!pose_transform(pose,&v.pose_world))return false;
    v.gun_in_dobj=relative(v.pose_world,v.gun_world);v.flash_in_gun=relative(v.gun_world,v.flash_world);
    if(!finite(v.gun_in_dobj.origin)||!finite(v.flash_in_gun.origin)||!rigid(v.gun_in_dobj.axis)||!rigid(v.flash_in_gun.axis))return false;
    const auto restored_gun=compose(v.pose_world,v.gun_in_dobj),restored_flash=compose(v.gun_world,v.flash_in_gun);
    v.roundtrip_position=std::max(distance(restored_gun.origin,v.gun_world.origin),distance(restored_flash.origin,v.flash_world.origin));
    v.roundtrip_axis=std::max(axis_error(restored_gun.axis,v.gun_world.axis),axis_error(restored_flash.axis,v.flash_world.axis));
    if(!std::isfinite(v.roundtrip_position)||!std::isfinite(v.roundtrip_axis)||v.roundtrip_position>0.02F||v.roundtrip_axis>0.01F)return false;
    *out=v;return true;
}
inline bool difference(const LocalGeometry& before,const LocalGeometry& after,Difference* out) noexcept {
    if(!out)return false;*out={};
    if(!finite(before.gun_in_dobj.origin)||!finite(after.gun_in_dobj.origin)||!finite(before.flash_in_gun.origin)||!finite(after.flash_in_gun.origin)||
       !rigid(before.gun_in_dobj.axis)||!rigid(after.gun_in_dobj.axis)||!rigid(before.flash_in_gun.axis)||!rigid(after.flash_in_gun.axis))return false;
    Difference d{distance(before.gun_in_dobj.origin,after.gun_in_dobj.origin),angle(before.gun_in_dobj.axis,after.gun_in_dobj.axis),
        distance(before.flash_in_gun.origin,after.flash_in_gun.origin),angle(before.flash_in_gun.axis,after.flash_in_gun.axis)};
    if(!std::isfinite(d.gun_position)||!std::isfinite(d.gun_angle_radians)||!std::isfinite(d.flash_position)||!std::isfinite(d.flash_angle_radians))return false;
    *out=d;return true;
}
}
