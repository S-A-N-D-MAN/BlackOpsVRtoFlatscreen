// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_vpos_program.hpp"
#include "t5/water_reflection_program.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace bovr::t5 {
const std::array<WaterVPosProgram,3>& water_vpos_programs() noexcept {
    static constexpr std::array<WaterVPosProgram,3> values{{
        {"0459ecfd87052c67fbf24f3b53069132386ce5cca1432d8ca6063e13d0f3884b",
         "7c552f850d5ac7cb0871612b220c6dbfbf7fd5bd64f567de9589ead5cf72b633",
         5524,1708,{9,10,11},966,1,{0x03000042,0x802f0000,0x80e40000,0xa0e40801}},
        {"c3c7a1fc757395f7f2a87eda9fd2a5bf2e926e1c48118c9be00b9b18a5650dd2",
         "862fbf32a93860dc293b550c485342cfa9cd9580332de3d99644602ba9023119",
         4656,1620,{10,11,20},808,1,{0x03000042,0x802f0001,0x80e40001,0xa0e40801}},
        {"55a80dd475df6804affd31e424782e457e2a156a84358c254eb116beb603c760",
         "ca6a5c6147b40352b2935c034da663f835fc1cd460bad8e8b0d6577455f61fb4",
         5912,1792,{10,11,20},897,2,{0x03000042,0x802f0000,0x80e40000,0xa0e40802}},
    }};
    return values;
}
bool water_vpos_make_derivative(unsigned id,std::span<const std::uint32_t> words,
                               std::vector<std::uint32_t>* out) noexcept {
    if(out)out->clear();if(!out||id>=water_vpos_programs().size())return false;
    const auto& p=water_vpos_programs()[id];
    return words.size_bytes()==p.pixel_bytes&&water_shader_fingerprint(words,p.pixel_sha256)&&
        water_reflection_insert(words,p.reflection_sample_word,p.reflection_sample,out);
}

bool water_vpos_gpu_constants(const VPosMatrix& matrix,const VPosWorldConstants& actual,
                             unsigned width,unsigned height,VPosViewport viewport,
                             VPosWorldConstants* out) noexcept {
    if(!out)return false;
    *out={};
    if(!width||!height||width>16384||height>16384)return false;
    for(float value:matrix)if(!std::isfinite(value)||std::fabs(value)>1.0e7f)return false;
    for(const auto* vector:{&actual.x,&actual.y,&actual.one})
        for(float value:*vector)if(!std::isfinite(value)||std::fabs(value)>1.0e7f)return false;
    auto dot=[&](unsigned a,unsigned b){
        double result{};for(unsigned i=0;i<3;++i)result+=static_cast<double>(matrix[a*4+i])*matrix[b*4+i];
        return result;
    };
    const double xx=dot(0,0),yy=dot(1,1),zz=dot(3,3),depth=dot(2,3);
    // Orthogonal scaled right/up and unit forward prove symmetric projection.
    // Reject off-axis/skewed, nonperspective and auxiliary projection shapes.
    // Homogeneous translations are not projection-shape invariants: native
    // R_DeriveViewProjectionMatrix adds eyeOffset to them while world vertices
    // subtract the same offset. Directional VPOS XYZ is independent of those
    // terms; all matrix entries still require finite bounded values above.
    if(xx<1.0e-6||xx>1.0e6||yy<1.0e-6||yy>1.0e6||
       std::fabs(zz-1.0)>0.002||depth<=0.0||depth>1000.0||
       std::fabs(dot(0,1))>0.0002*std::sqrt(xx*yy)||
       std::fabs(dot(0,3))>0.0002*std::sqrt(xx)||
       std::fabs(dot(1,3))>0.0002*std::sqrt(yy))return false;
    for(unsigned i=0;i<3;++i){
        if(std::fabs(matrix[8+i]-depth*matrix[12+i])>0.0002*depth)return false;
        const double expected_x=(-2.0/static_cast<double>(width))*matrix[i]/xx;
        const double expected_y=(2.0/static_cast<double>(height))*matrix[4+i]/yy;
        const double expected_one=matrix[i]/xx-matrix[4+i]/yy-matrix[12+i];
        // Native single-precision products and logged fixtures vary slightly.
        // This admits numerical rounding, not the already-corrected half-width
        // vectors or a stale camera direction; those differ by much more.
        if(std::fabs(actual.x[i]-expected_x)>std::max(1.0e-8,std::fabs(expected_x)*0.0005)||
           std::fabs(actual.y[i]-expected_y)>std::max(1.0e-8,std::fabs(expected_y)*0.0005)||
           std::fabs(actual.one[i]-expected_one)>std::max(2.0e-5,std::fabs(expected_one)*0.00005))return false;
    }
    VPosWorldConstants corrected{};
    if(!vpos_viewport_constants(actual,width,height,viewport,&corrected))return false;
    corrected.x[3]=actual.x[3];corrected.y[3]=actual.y[3];corrected.one[3]=actual.one[3];
    *out=corrected;return true;
}
}
