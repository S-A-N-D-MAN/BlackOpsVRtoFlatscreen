// SPDX-License-Identifier: GPL-3.0-only
#include "t5/vpos_world_math.hpp"
#include <cmath>
#include <initializer_list>

namespace bovr::t5 {
namespace {
bool bounded(float value,float limit=1.0e7f) noexcept {
    return std::isfinite(value)&&std::fabs(value)<=limit;
}
bool projection(const VPosMatrix& p) noexcept {
    // Exact native perspective sparsity and positive-Z w=z convention.
    // P20/P21 may be nonzero for a later independently proven off-axis camera.
    for(unsigned i:{1u,2u,3u,4u,6u,7u,12u,13u,15u})if(p[i]!=0.f)return false;
    return p[11]==1.f&&p[0]>=0.001f&&p[0]<=1000.f&&
        p[5]>=0.001f&&p[5]<=1000.f&&std::fabs(p[8])<=4.f&&
        std::fabs(p[9])<=4.f&&p[10]>0.f&&p[10]<=1000.f&&p[14]<0.f;
}
bool inverse_view(const VPosMatrix& m) noexcept {
    if(m[3]!=0.f||m[7]!=0.f||m[11]!=0.f||m[15]!=1.f)return false;
    // A rigid camera can have either handedness; native engine axes must not
    // be mistaken for a right-handed OpenXR transform here.
    for(unsigned row=0;row<3;++row)for(unsigned other=row;other<3;++other){
        float product{};
        for(unsigned c=0;c<3;++c)product+=m[4*row+c]*m[4*other+c];
        if(std::fabs(product-(row==other?1.f:0.f))>0.002f)return false;
    }
    return true;
}
}
bool vpos_world_constants(const VPosWorldInput& in,VPosWorldConstants* out,VPosWorldStatus* status) noexcept {
    auto reject=[&](VPosWorldStatus why){if(status)*status=why;return false;};
    if(!out)return reject(VPosWorldStatus::output);
    *out={};
    const auto& v=in.viewport;
    if(!in.target_width||!in.target_height||in.target_width>16384||in.target_height>16384||
       v.x<0||v.y<0||v.width<=0||v.height<=0||
       static_cast<std::uint64_t>(v.x)+static_cast<unsigned>(v.width)>in.target_width||
       static_cast<std::uint64_t>(v.y)+static_cast<unsigned>(v.height)>in.target_height)
        return reject(VPosWorldStatus::viewport);
    for(float f:in.projection)if(!bounded(f))return reject(VPosWorldStatus::nonfinite);
    for(float f:in.inverse_view)if(!bounded(f))return reject(VPosWorldStatus::nonfinite);
    if(!projection(in.projection))return reject(VPosWorldStatus::projection);
    if(!inverse_view(in.inverse_view))return reject(VPosWorldStatus::inverse_view);
    const float iw=1.f/static_cast<float>(v.width),ih=1.f/static_cast<float>(v.height);
    const float px=1.f/in.projection[0],py=1.f/in.projection[5];
    // Native ordered scale operations are retained for symmetric full targets.
    const float sx=(px*iw)*-2.f,sy=(py*ih)*2.f;
    const float cx=(1.f+in.projection[8]+(2.f*static_cast<float>(v.x))*iw)*px;
    const float cy=(-1.f+in.projection[9]-(2.f*static_cast<float>(v.y))*ih)*py;
    VPosWorldConstants value{};
    for(unsigned c=0;c<4;++c){
        value.x[c]=sx*in.inverse_view[c];
        value.y[c]=sy*in.inverse_view[4+c];
        value.one[c]=(cx*in.inverse_view[c]+cy*in.inverse_view[4+c])-in.inverse_view[8+c];
        if(!bounded(value.x[c])||!bounded(value.y[c])||!bounded(value.one[c]))
            return reject(VPosWorldStatus::result);
    }
    *out=value;
    if(status)*status=VPosWorldStatus::accepted;
    return true;
}
bool vpos_viewport_constants(const VPosWorldConstants& native,unsigned width,unsigned height,
                            VPosViewport v,VPosWorldConstants* out) noexcept {
    if(!out)return false;*out={};
    if(!width||!height||width>16384||height>16384||v.x<0||v.y<0||v.width<=0||v.height<=0||
       static_cast<std::uint64_t>(v.x)+static_cast<unsigned>(v.width)>width||
       static_cast<std::uint64_t>(v.y)+static_cast<unsigned>(v.height)>height)return false;
    VPosWorldConstants value{};
    for(unsigned c=0;c<4;++c){
        if(!bounded(native.x[c])||!bounded(native.y[c])||!bounded(native.one[c]))return false;
        value.x[c]=native.x[c]*(static_cast<float>(width)/static_cast<float>(v.width));
        value.y[c]=native.y[c]*(static_cast<float>(height)/static_cast<float>(v.height));
        value.one[c]=(native.one[c]-value.x[c]*static_cast<float>(v.x))-value.y[c]*static_cast<float>(v.y);
        if(!bounded(value.x[c])||!bounded(value.y[c])||!bounded(value.one[c]))return false;
    }
    *out=value;return true;
}
}
