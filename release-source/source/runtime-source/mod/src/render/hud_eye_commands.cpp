// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_eye_commands.hpp"
#include "render/hud_commands.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
namespace bovr::render {
namespace {
template<class T>T get(const std::uint8_t* p){T value{};std::memcpy(&value,p,sizeof(T));return value;}
template<class T>void put(std::uint8_t* p,T value){std::memcpy(p,&value,sizeof(T));}
bool permitted(unsigned id) {
    switch(id){case 0:case 1:case 2:case 3:case 4:case 10:case 12:case 15:case 17:case 19:case 20:case 27:return true;default:return false;}
}
bool canvas(std::uint32_t w,std::uint32_t h,HudEyeViewport v) {
    constexpr std::uint32_t limit=16384;
    return w&&h&&w<=limit&&h<=limit&&v.x>=0&&v.y>=0&&v.width>0&&v.height>0&&
        static_cast<std::uint64_t>(v.x)+v.width<=limit&&static_cast<std::uint64_t>(v.y)+v.height<=limit;
}
void header(std::uint8_t* p,std::uint16_t length,std::uint8_t id) {put(p,length);p[2]=id;p[3]=0;}
bool finite_bound(const std::uint8_t* p,std::initializer_list<unsigned> offsets,float bound) {
    for(auto offset:offsets)if(!std::isfinite(get<float>(p+offset))||std::fabs(get<float>(p+offset))>bound)return false;
    return true;
}
bool covers_canvas(const std::uint8_t* p,unsigned id,float w,float h) {
    float left{},top{},right{},bottom{};
    if(id==10||id==12){
        left=get<float>(p+8);top=get<float>(p+12);right=left+get<float>(p+20);bottom=top+get<float>(p+24);
        if(id==12) {
            const float angle=get<float>(p+48)*0.01745329251994329577f;
            const float cx=(left+right)*0.5f,cy=(top+bottom)*0.5f;
            const float hx=(std::fabs(std::cos(angle))*get<float>(p+20)+std::fabs(std::sin(angle))*get<float>(p+24))*0.5f;
            const float hy=(std::fabs(std::sin(angle))*get<float>(p+20)+std::fabs(std::cos(angle))*get<float>(p+24))*0.5f;
            left=cx-hx;right=cx+hx;top=cy-hy;bottom=cy+hy;
        }
    }
    else {
        left=right=get<float>(p+8);top=bottom=get<float>(p+12);
        for(unsigned offset=16;offset<=32;offset+=8) {
            const auto x=get<float>(p+offset),y=get<float>(p+offset+4);
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    return left<=0&&top<=0&&right>=w&&bottom>=h;
}
}
bool common_visible_hud_region(const std::array<HudEyeDestination,2>& destinations,HudVisibleRegion* output) noexcept {
    if(!output)return false;
    float left=0,top=0,right=1,bottom=1;
    for(const auto& d:destinations) {
        if(!std::isfinite(d.x)||!std::isfinite(d.y)||!std::isfinite(d.width)||!std::isfinite(d.height)||
           d.width<=0||d.height<=0||d.width>8||d.height>8||std::fabs(d.x)>8||std::fabs(d.y)>8)return false;
        left=std::max(left,-d.x/d.width);right=std::min(right,(1-d.x)/d.width);
        top=std::max(top,-d.y/d.height);bottom=std::min(bottom,(1-d.y)/d.height);
    }
    const float half_width=std::min(0.5f-left,right-0.5f),half_height=std::min(0.5f-top,bottom-0.5f);
    if(!std::isfinite(half_width)||!std::isfinite(half_height)||half_width<=0||half_height<=0)return false;
    *output={0.5f-half_width,0.5f-half_height,half_width*2,half_height*2};return true;
}
HudEyeCommands fit_hud_commands_to_eye(std::span<const std::uint8_t> source,
    std::uint32_t source_width,std::uint32_t source_height,HudEyeViewport eye,HudVisibleRegion visible,HudMaterialPolicy materials) noexcept {
    HudEyeCommands out{};
    if(!canvas(source_width,source_height,eye)||!std::isfinite(visible.x)||!std::isfinite(visible.y)||
       !std::isfinite(visible.width)||!std::isfinite(visible.height)||visible.x<0||visible.y<0||visible.width<=0||visible.height<=0||
       visible.x+visible.width>1||visible.y+visible.height>1){out.status=HudEyeStatus::invalid_canvas;return out;}
    const auto census=census_hud_commands(source);
    if(census.status!=CommandListStatus::complete||census.consumed_bytes!=source.size()||census.malformed_layout_mask) return out;
    // Bound allocation independently of native caller metadata (four native
    // command buffers at most); native metadata preflight may choose less.
    if(source.size()>4u*0x18000u)return out;
    const auto width=static_cast<float>(source_width),height=static_cast<float>(source_height);
    // Round the hardware clipping rectangle inward. This also clips native
    // primitives extending beyond the logical canvas; merely moving geometry
    // inside the full eye viewport would let their tails escape the safe area.
    const auto start_x=static_cast<std::int32_t>(std::ceil(static_cast<double>(eye.width)*visible.x));
    const auto start_y=static_cast<std::int32_t>(std::ceil(static_cast<double>(eye.height)*visible.y));
    const auto end_x=static_cast<std::int32_t>(std::floor(static_cast<double>(eye.width)*(visible.x+visible.width)));
    const auto end_y=static_cast<std::int32_t>(std::floor(static_cast<double>(eye.height)*(visible.y+visible.height)));
    const HudEyeViewport clipped{eye.x+start_x,eye.y+start_y,end_x-start_x,end_y-start_y};
    if(!canvas(source_width,source_height,clipped)){out.status=HudEyeStatus::invalid_canvas;return out;}
    const float available_width=static_cast<float>(clipped.width),available_height=static_cast<float>(clipped.height);
    const float scale=std::min(available_width/width,available_height/height);
    const float offset_x=(available_width-width*scale)*0.5f;
    const float offset_y=(available_height-height*scale)*0.5f;
    const std::uint8_t* compass_constant=nullptr;
    const std::uint8_t* compass_draw=nullptr;
    auto next_active=[&](std::size_t at) {
        while(at<source.size()&&source[at+2]&&source[at+3]&0x80)at+=get<std::uint16_t>(source.data()+at);
        return at;
    };
    // Whole-list preflight before even constructing an executable native copy.
    for(std::size_t offset=0;offset<source.size();) {
        const auto* p=source.data()+offset;const auto length=get<std::uint16_t>(p);const unsigned id=p[2];
        if(id==0)break;
        if(p[3]&0x80){++out.skipped_tagged;offset+=length;continue;}
        const auto reject=[&](HudEyeStatus status) {
            out.status=status;out.rejected_record=true;
            out.rejected_offset=static_cast<std::uint32_t>(offset);
            out.rejected_id=static_cast<std::uint8_t>(id);out.rejected_flags=p[3];out.rejected_length=length;
            out.rejected_word4=length>=8?get<std::uint32_t>(p+4):0;
            out.rejected_material=(id==10||id==12||id==15)?out.rejected_word4:0;
        };
        if(!permitted(id)){reject(HudEyeStatus::unsupported_command);return out;}
        if(id==1) {
            // Generic183 has other meanings in other shaders. Admit only one
            // adjacent compass draw whose exact current shader proves VPOS
            // XYWH; restore native183 immediately after that draw's flush.
            const auto draw=next_active(offset+length);
            const auto end=draw<source.size()?next_active(draw+get<std::uint16_t>(source.data()+draw)):source.size();
            // Census already proves these record bounds. Copy diagnostic
            // identities only; unknown records never gain material authority.
            out.compass_proof_called=out.compass_proved=false;
            out.compass_next_id=out.compass_after_id=0;out.compass_next_material=0;
            out.compass_next_present=draw<source.size();
            out.compass_after_present=end<source.size();
            if(out.compass_next_present) {
                out.compass_next_id=source[draw+2];
                if(out.compass_next_id==10||out.compass_next_id==12||out.compass_next_id==15)
                    out.compass_next_material=get<std::uint32_t>(source.data()+draw+4);
            }
            if(out.compass_after_present)out.compass_after_id=source[end+2];
            auto& reason=out.compass_rejection;
            if(compass_constant)reason=HudCompassRejection::prior_constant;
            else if(get<std::uint32_t>(p+4)!=183)reason=HudCompassRejection::wrong_index;
            else if(draw>=source.size())reason=HudCompassRejection::missing_draw;
            else if(source[draw+2]!=15)reason=HudCompassRejection::draw_not_quad;
            else if(end>=source.size())reason=HudCompassRejection::missing_after;
            else if(!materials.compass)reason=HudCompassRejection::missing_policy;
            else {
                out.compass_proof_called=true;
                out.compass_proved=materials.compass(materials.context,get<std::uint32_t>(source.data()+draw+4));
                if(!out.compass_proved)reason=HudCompassRejection::material_unproved;
            }
            if(reason!=HudCompassRejection::none) {
                reject(HudEyeStatus::unsupported_command);return out;
            }
            if(!finite_bound(p,{8,12,16,20},16*std::max(width,height))||get<float>(p+16)<0.001f||get<float>(p+20)<0.001f) {
                reject(HudEyeStatus::unsupported_geometry);return out;
            }
            compass_constant=p;
            compass_draw=source.data()+draw;
        } else if(id==10||id==12||id==15) {
            const float w=get<float>(p+(id==15?40:16));
            if((id!=12&&w!=1.0f)||(id==12&&(!std::isnormal(w)||w>1000000.0f||get<float>(p+48)<0||get<float>(p+48)>=360))) {
                reject(HudEyeStatus::unsupported_geometry);return out;
            }
            const auto xy=id!=15?std::initializer_list<unsigned>{8,12,20,24}:std::initializer_list<unsigned>{8,12,16,20,24,28,32,36};
            if(!finite_bound(p,xy,16*std::max(width,height))) {reject(HudEyeStatus::unsupported_geometry);return out;}
            if(covers_canvas(p,id,width,height)){reject(HudEyeStatus::ambiguous_overlay);return out;}
            ++out.draws;
        } else if(id==17) {
            // World-objective text uses the same homogeneous depth as its
            // icon. Native font/glow VS multiplies all clip coordinates by W,
            // leaving pixel XY unchanged after division; retain that value.
            const float w=get<float>(p+12);
            if(!std::isnormal(w)||w<=0||w>1000000.0f){reject(HudEyeStatus::unsupported_geometry);return out;}
            if(!finite_bound(p,{4,8,24,28},16*std::max(width,height))) {reject(HudEyeStatus::unsupported_geometry);return out;}
            ++out.draws;
        }
        offset+=length;
    }
    if(!out.draws){out.status=HudEyeStatus::no_draws;return out;}
    try {
        // Native viewport command invalidates the native projection cache. The
        // following native projection2D command rebuilds it for this eye. All
        // primitive XY remain eye-local: the hardware viewport supplies XY.
        out.bytes.resize(28);header(out.bytes.data(),20,6);
        put(out.bytes.data()+4,clipped.x);put(out.bytes.data()+8,clipped.y);
        put(out.bytes.data()+12,clipped.width);put(out.bytes.data()+16,clipped.height);
        header(out.bytes.data()+20,8,27);put(out.bytes.data()+24,std::uint32_t{});
        for(std::size_t offset=0;offset<source.size();) {
            const auto* p=source.data()+offset;const auto length=get<std::uint16_t>(p);const unsigned id=p[2];
            if(id==0)break;
            if(p[3]&0x80){offset+=length;continue;}
            const auto start=out.bytes.size();out.bytes.insert(out.bytes.end(),p,p+length);auto* copy=out.bytes.data()+start;
            auto x=[&](unsigned at){put(copy+at,get<float>(copy+at)*scale+offset_x);};
            auto y=[&](unsigned at){put(copy+at,get<float>(copy+at)*scale+offset_y);};
            auto size=[&](unsigned at){put(copy+at,get<float>(copy+at)*scale);};
            if(id==1){x(8);y(12);size(16);size(20);
                // VPOS is absolute in the packed native render target, unlike
                // vertex XY. Include the clipped hardware viewport origin.
                put(copy+8,get<float>(copy+8)+static_cast<float>(clipped.x));
                put(copy+12,get<float>(copy+12)+static_cast<float>(clipped.y));
            }
            else if(id==10||id==12){x(8);y(12);size(20);size(24);}
            else if(id==15){for(unsigned at=8;at<=32;at+=8){x(at);y(at+4);}}
            else if(id==17){x(4);y(8);size(24);size(28);}
            if((id==10||id==12||id==15)&&materials.hind_reticle&&
               materials.hind_reticle(materials.context,get<std::uint32_t>(p+4))) {
                float center_x{},center_y{};
                if(id==15) {
                    for(unsigned at=8;at<=32;at+=8){center_x+=get<float>(copy+at)*.25f;center_y+=get<float>(copy+at+4)*.25f;}
                } else {
                    center_x=get<float>(copy+8)+get<float>(copy+20)*.5f;
                    center_y=get<float>(copy+12)+get<float>(copy+24)*.5f;
                }
                // The native eye image has a symmetric projection. Its centre
                // is mapped to each runtime eye's asymmetric optical axis by
                // the existing compositor destination; applying FOV fractions
                // here would apply that asymmetry twice. Hardware clipping has
                // already moved the local origin, so undo only that displacement.
                const float dx=static_cast<float>(eye.width)*.5f-static_cast<float>(start_x)-center_x;
                const float dy=static_cast<float>(eye.height)*.5f-static_cast<float>(start_y)-center_y;
                if(id==15) {
                    for(unsigned at=8;at<=32;at+=8){put(copy+at,get<float>(copy+at)+dx);put(copy+at+4,get<float>(copy+at+4)+dy);}
                } else {put(copy+8,get<float>(copy+8)+dx);put(copy+12,get<float>(copy+12)+dy);}
                ++out.centered_reticles;
            }
            // ID1 flushes the pending compass tessellation before restoring
            // the exact original native value. Later supported commands see
            // the same constant they would have seen in the source list.
            if(p==compass_draw)out.bytes.insert(out.bytes.end(),compass_constant,compass_constant+24);
            offset+=length;
        }
        const auto end=out.bytes.size();out.bytes.resize(end+4);header(out.bytes.data()+end,4,0);
        out.uniform_scale=scale;out.offset_x=offset_x;out.offset_y=offset_y;out.status=HudEyeStatus::accepted;
    } catch(...) {out.bytes.clear();out.status=HudEyeStatus::allocation_failed;}
    return out;
}
} // namespace bovr::render
