// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include "t5/postfx_resolution.hpp"
#include "t5/bloom_correction_shaders.inc"
namespace bovr::t5 {
struct BloomReplayStateRule {D3DRENDERSTATETYPE state;DWORD value;};
// Exact opaque, unmasked, non-depth 2D overwrite subset. No state is set to
// manufacture admission. Captured shaders do not read fog or vertex colors.
inline constexpr BloomReplayStateRule bloom_replay_states[]{
    {D3DRS_ZENABLE,D3DZB_FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},
    {D3DRS_TWOSIDEDSTENCILMODE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},
    {D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
    {D3DRS_COLORWRITEENABLE,15},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CULLMODE,D3DCULL_NONE},
    {D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},
    {D3DRS_MULTISAMPLEANTIALIAS,FALSE},{D3DRS_MULTISAMPLEMASK,0xffffffff},
    {D3DRS_ANTIALIASEDLINEENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},
    {D3DRS_FOGENABLE,FALSE},{D3DRS_DITHERENABLE,FALSE},
};
inline constexpr D3DRENDERSTATETYPE bloom_replay_blend_states[]{
    D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
inline constexpr D3DSAMPLERSTATETYPE bloom_replay_sampler_states[]{
    D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,
    D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_MAXANISOTROPY};
// Saved C11c actual s0/s1 records use WRAP for the unused 2D W coordinate.
inline constexpr DWORD bloom_replay_sampler_values[]{D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTADDRESS_WRAP,
    D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_NONE,0,0,0,1};
inline bool bloom_replay_desc(const D3DSURFACE_DESC& d,unsigned target,PostfxResolution scene) noexcept {
    PostfxResolution expected{};
    const auto fmt=target==3||target==6||target==30?D3DFMT_A8R8G8B8:D3DFMT_A16B16G16R16;
    return postfx_target_resolution(target,scene,expected)&&d.Type==D3DRTYPE_SURFACE&&d.Width==expected.width&&d.Height==expected.height&&d.Format==fmt&&
        d.Usage==D3DUSAGE_RENDERTARGET&&d.Pool==D3DPOOL_DEFAULT&&d.MultiSampleType==D3DMULTISAMPLE_NONE&&d.MultiSampleQuality==0;
}
inline bool bloom_replay_declaration(const D3DVERTEXELEMENT9* elements,unsigned count) noexcept {
    if(!elements||count<3||count>16)return false;bool position=false,uv=false;
    for(unsigned i=0;i<count;++i){const auto& e=elements[i];
        if(i==count-1)return e.Stream==0xff&&e.Offset==0&&e.Type==D3DDECLTYPE_UNUSED&&e.Method==0&&e.Usage==0&&e.UsageIndex==0&&position&&uv;
        if(e.Stream!=0||e.Method!=D3DDECLMETHOD_DEFAULT||e.UsageIndex)return false;
        if(e.Usage==D3DDECLUSAGE_POSITION){if(position||e.Offset||!(e.Type==D3DDECLTYPE_FLOAT3||e.Type==D3DDECLTYPE_FLOAT4))return false;position=true;}
        else if(e.Usage==D3DDECLUSAGE_TEXCOORD){if(uv||e.Offset!=20||e.Type!=D3DDECLTYPE_FLOAT2)return false;uv=true;}
        else if(e.Usage==D3DDECLUSAGE_COLOR){if(e.Offset!=16||e.Type!=D3DDECLTYPE_D3DCOLOR)return false;}
        // Pinned shaders never declare NORMAL. Both admitted encodings occupy
        // only the same unused four bytes at the end of the 32-byte vertex.
        else if(e.Usage==D3DDECLUSAGE_NORMAL){if(e.Offset!=28||(e.Type!=D3DDECLTYPE_D3DCOLOR&&e.Type!=D3DDECLTYPE_UBYTE4))return false;}
        else return false;
        for(unsigned k=0;k<i;++k)if(elements[k].Usage==e.Usage)return false;
    }return false;
}
inline bool bloom_replay_bounds(unsigned eye,unsigned w,unsigned h,std::array<float,4>& out) noexcept {
    out={};if(eye>1||!postfx_pyramid_resolution(w,h))return false;
    // Clamp at the inner texel centers. Round lower limits upward and upper
    // limits downward. The packed stereo halves share the same dx/dy.
    const auto inward=[](double exact,bool lower){const float value=static_cast<float>(exact);
        return (lower?static_cast<double>(value)<exact:static_cast<double>(value)>exact)?
            std::nextafter(value,lower?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity()):value;};
    const double dx=.5/static_cast<double>(w),dy=.5/static_cast<double>(h);
    out=eye?std::array<float,4>{inward(.5+dx,true),inward(dy,true),inward(1-dx,false),inward(1-dy,false)}:
            std::array<float,4>{inward(dx,true),inward(dy,true),inward(.5-dx,false),inward(1-dy,false)};
    return true;
}
}
