// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/swimming_context.hpp"
#include "input/room_scale_camera.hpp"
#include "loader/native_span_guard.hpp"

namespace bovr::t5::room_scale_native {
namespace math=input::room_scale;
namespace rd=swimming::rd;
struct Facts {
    swimming::Facts walking{};
    wawvr::xr::Vec3f origin{},eye{};
    float near_clip{};
};
inline bool same(const Facts& a,const Facts& b) noexcept {
    return a.walking==b.walking&&!std::memcmp(&a.origin,&b.origin,sizeof(a.origin))&&
        !std::memcmp(&a.eye,&b.eye,sizeof(a.eye))&&!std::memcmp(&a.near_clip,&b.near_clip,sizeof(a.near_clip));
}
inline bool copy(const swimming::Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                 const CommandContext& c,std::uint64_t now,Facts* out) noexcept {
    if(!out)return false;*out={};Facts a{},b{};
    const auto read=[&](Facts& f){return swimming::copy_walking_current(h,base,pre,c,now,&f.walking)&&
        rd::value(h,f.walking.predicted.ps,0x24,f.origin)&&
        rd::value(h,f.walking.cg,0x8c120,f.eye)&&rd::value(h,f.walking.cg,0x8c15c,f.near_clip)&&
        math::finite(f.origin)&&math::finite(f.eye)&&std::isfinite(f.near_clip)&&
        std::hypot(f.eye.x-f.origin.x,f.eye.y-f.origin.y)<8.f&&
        f.eye.z-f.origin.z>=5.f&&f.eye.z-f.origin.z<=80.f;};
    if(!read(a)||!read(b)||!same(a,b))return false;*out=b;return true;
}
inline bool copy(std::uintptr_t base,const PreCommandSnapshot& pre,const CommandContext& c,
                 std::uint64_t now,Facts* out) noexcept {
    const auto error=GetLastError();const bool result=copy(
        {nullptr,rd::protected_copy,rd::native_pre,rd::native_thread},base,pre,c,now,out);
    SetLastError(error);return result;
}
// This is the client pmoveHandlers trace, also called by native third-person
// camera collision. No code patch or server collision query is involved.
inline bool guards(std::uintptr_t base) noexcept {
    return base==0x400000&&
        native_guard::matches(base,{0x28fd90,679,"ea74757089228bcfccf6669b1fbd0810e5a5c696eb6c98014ce785bc0a13e682"})&&
        native_guard::matches(base,{0xb3e90,39,"a12105dadf900d3f68fafc40df9cc020ec751c860eaaafea29a6f4874e5574ea"})&&
        native_guard::matches(base,{0xb9500,80,"9e3528114242cc7809a521971d7b9270bd1d63976db606bb89f6c2159cc25fae"})&&
        native_guard::matches(base,{0x2c7f80,263,"3267d3aa12933c3a953baa99572ab335d4f3d7e482cf502abc7f51a4e9f1cce5"})&&
        native_guard::matches(base,{0x74bd68,4,"4f4b9b7d8b86633e2824e2f439819357b0cd010ab410ea1a691b12c5f94e91e0"})&&
        native_guard::matches(base,{0x74623c,4,"e00e5eb9444182f352323374ef4e08ebcb784725fdd4fd612d7730540b3e0c8c"});
}
struct Sweep {float fraction{};bool solid{};};
using Trace=void(__cdecl*)(void*,const float*,const float*,const float*,const float*,int,int,void*);
inline bool query(Trace trace,const wawvr::xr::Vec3f& start,
                  const wawvr::xr::Vec3f& end,Sweep* out,float radius=math::minimum_camera_radius) noexcept {
    if(!out||!trace||!math::finite(start)||!math::finite(end)||!std::isfinite(radius)||
       radius<math::minimum_camera_radius||radius>math::maximum_camera_radius)return false;
    const auto bounded=[](wawvr::xr::Vec3f v){return std::fabs(v.x)<=1e7f&&std::fabs(v.y)<=1e7f&&std::fabs(v.z)<=1e7f;};
    const auto delta=math::subtract(end,start);
    if(!bounded(start)||!bounded(end)||delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>128.f*128.f)return false;
    *out={};
    alignas(4) std::array<unsigned char,0x38> result{};
    std::array<int,10> context{};context[4]=context[5]=1023;
    const float mins[]{-radius,-radius,-radius},maxs[]{radius,radius,radius};
    __try {
        trace(result.data(),&start.x,mins,maxs,&end.x,0,0x280ea31,context.data());
        std::memcpy(&out->fraction,result.data()+0x10,sizeof(float));
        out->solid=result[0x2a]!=0||result[0x2b]!=0;
        return std::isfinite(out->fraction)&&out->fraction>=0&&out->fraction<=1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool sweep(std::uintptr_t base,const wawvr::xr::Vec3f& start,
                  const wawvr::xr::Vec3f& end,Sweep* out,float radius=math::minimum_camera_radius) noexcept {
    return base==0x400000&&query(reinterpret_cast<Trace>(base+0x28fd90),start,end,out,radius);
}
}
