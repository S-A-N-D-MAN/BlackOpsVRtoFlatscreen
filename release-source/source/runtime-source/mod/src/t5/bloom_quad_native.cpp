// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/bloom_quad_native.hpp"
#include "t5/postfx_resolution.hpp"
#include <bit>
#include <cstring>
#include <immintrin.h>
namespace bovr::t5 {
namespace {
constexpr std::uint32_t source_rva=0x3cca570,state_rva=0x417ee00;
constexpr std::array<std::uint32_t,16> make_identity() noexcept {std::array<std::uint32_t,16> value{};
    for(unsigned i=0;i<4;++i)value[5*i]=0x3f800000;return value;}
constexpr auto identity=make_identity();
constexpr std::array<std::uint16_t,6> quad_indices{3,0,2,2,0,1};
constexpr bool blur_role(unsigned role) noexcept {return role==5||role==6;}
bool protected_copy(void*,std::uintptr_t p,void* out,std::size_t bytes) noexcept {
    __try {std::memcpy(out,reinterpret_cast<const void*>(p),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Reader {
    const BloomQuadMemory& memory;
    template<class T>bool get(std::uintptr_t p,T& out) const noexcept {
        if(p<0x10000||p>UINT32_MAX-sizeof(T))return false;
        return memory.copy(memory.context,p,&out,sizeof(T));
    }
};
// Each operation is explicitly binary32 under the admitted MXCSR control.
// No double approximation, contraction, or reassociation of the native recipe.
float div32(float a,float b) noexcept {return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float mul32(float a,float b) noexcept {return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float add32(float a,float b) noexcept {return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float sub32(float a,float b) noexcept {return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a),_mm_set_ss(b)));}
std::uint32_t bits(float f) noexcept {return std::bit_cast<std::uint32_t>(f);}
bool input(std::uintptr_t base,const BloomDrawSnapshot& n,const BloomDrawCall& d) noexcept {
    if(base<0x10000||base>UINT32_MAX-auxiliary_read_extent||n.eye>1||!postfx_view_count(n.view_count)||
       n.selected!=postfx_native_index(n.view_count,n.eye)||n.source_view!=n.view||n.source_mode!=2||n.role>=13||
       d.caller!=base+0x30b5d3||d.type!=4||d.base_vertex||d.min_vertex||d.num_vertices!=4||d.primitive_count!=2||
       n.pass_index||n.pass_count!=1||n.custom_sampler_flags||!n.material||!n.technique||!n.pass)return false;
    const bool final=n.role==3||n.role==8;
    if(n.selector!=(final?0u:1u)||n.width!=n.native_target.width||n.height!=n.native_target.height)return false;
    PostfxResolution scene{},target{};
    if(!postfx_scene_resolution(n.eye,n.scene_viewport,scene)||!postfx_target_resolution(n.target,scene,target)||
       n.width!=target.width||n.height!=target.height||(final?n.target!=3:n.target<25))return false;
    const unsigned count=n.argument_counts[0]+n.argument_counts[1]+n.argument_counts[2];
    if(!count||count>n.arguments.size())return false;
    unsigned matrix_args{},uv0_args{},uv1_args{};
    for(unsigned i=0;i<count;++i){const auto& a=n.arguments[i];
        if(a.type!=3)continue;
        if(a.destination==0&&a.value==0x040000db)++matrix_args;
        else if(blur_role(n.role)&&a.destination==23&&a.value==0x01000079)++uv0_args;
        else if(blur_role(n.role)&&a.destination==36&&a.value==0x0100007a)++uv1_args;
        else return false;}
    return matrix_args==1&&uv0_args==(blur_role(n.role)?1u:0u)&&uv1_args==(blur_role(n.role)?1u:0u);
}
bool copy_header(const Reader& r,std::uintptr_t base,const BloomDrawSnapshot& n,BloomQuadSnapshot& q,BloomQuadStatus& why) noexcept {
    why=BloomQuadStatus::copy;
    if(!r.get(base+0x747880,q.source)||!r.get(base+0x747884,q.state)||q.source!=base+source_rva||q.state!=base+state_rva)return false;
    if(!r.get(base+0x3a5b5f8,q.buffer)||q.buffer!=n.buffer)return false;
    const unsigned slot=q.buffer==base+0x3740100?0:q.buffer==base+0x38c6400?1:2;
    if(slot>1)return false;
    if(!r.get(q.buffer+0x15d7a0,q.native_frame)||!r.get(q.buffer+0x16cc38,q.selected)||
       !r.get(q.buffer+0x16cc3c,q.view_count)||!r.get(q.buffer+0x16cc40,q.view_array))return false;
    if(q.view_array!=base+0x420f730+slot*0xe380)return false;
    if(!r.get(q.source+0x1a08,q.view)||q.view!=q.view_array+n.eye*0x38e0||q.view!=n.view)return false;
    if(!r.get(q.view+0x18c,q.full_screen)||!r.get(q.view+0x3620,q.rendering_mode)||
       !r.get(q.view+0x150,q.display)||!r.get(q.source+0x1a4c,q.source_viewport)||
       !r.get(q.source+0x1a34,q.source_mode)||!r.get(q.source+0x1a7c,q.selector)||
       !r.get(q.source+0x1a80,q.width)||!r.get(q.source+0x1a84,q.height)||!r.get(q.source+0x1a38,q.scene))return false;
    std::array<std::uint32_t,5> scene{};std::uint8_t target{},type{};
    if(!r.get(q.view+0x170,scene)||!r.get(q.state+0x1128,target)||!r.get(q.state+0xc8,type)||
       !r.get(q.state+0xc4,q.material)||!r.get(q.state+0xcc,q.technique)||!r.get(q.state+0xd0,q.pass)||!r.get(q.state+0xd4,q.pass_index))return false;
    q.target=target;
    why=BloomQuadStatus::context;
    if(q.native_frame!=n.native_frame||q.view_count!=n.view_count||q.selected!=n.selected||q.full_screen||(q.rendering_mode&7)||
       scene!=n.scene||q.scene!=n.scene||q.display!=n.scene_viewport||q.source_viewport!=q.display||
       q.source_mode!=n.source_mode||q.selector!=n.selector||q.width!=n.width||q.height!=n.height||
       q.target!=n.target||type!=4||q.material!=n.material||q.technique!=n.technique||q.pass!=n.pass||q.pass_index!=n.pass_index)return false;
    why=BloomQuadStatus::copy;
    if(!r.get(base+0x3c89254,q.vertex_count)||!r.get(base+0x3c89250,q.index_count)||!r.get(base+0x420c124,q.pixel_cost)||
       !r.get(q.source+0x1a0c,q.depth_hack)||!r.get(q.source+0x19f0,q.eye_offset))return false;
    constexpr std::array<unsigned,5> version_offsets{0x199a,0x19da,0x19c2,0x19e4,0x19c6};
    for(unsigned i=0;i<version_offsets.size();++i){std::uint16_t v{};if(!r.get(q.source+version_offsets[i],v))return false;q.versions[i]=v;}
    if(!r.get(q.state+0xe8,q.matrix_bind_cache)||!r.get(base+0x3d70760,q.vertex_pool_pointer)||
       !r.get(base+0x3d70728,q.index_pool_pointer))return false;
    if(blur_role(n.role)&&(!r.get(q.source+0x1902,q.uv_versions[0])||!r.get(q.source+0x1904,q.uv_versions[1])||
       !r.get(q.state+0x1a0,q.uv_bind_cache[0])||!r.get(q.state+0x208,q.uv_bind_cache[1])))return false;
    if(q.vertex_pool_pointer!=base+0x3d70750||q.index_pool_pointer!=base+0x3d70718)return false;
    if(!r.get(q.vertex_pool_pointer,q.vertex_pool)||!r.get(q.index_pool_pointer,q.index_pool)||
       !r.get(q.state+0x90,q.device)||!r.get(q.state+0x94,q.index_buffer)||!r.get(q.state+0x98,q.declaration_type)||
       !r.get(q.state+0x9c,q.streams)||!r.get(q.state+0xc0,q.declaration))return false;
    q.stream_stride=q.streams[0];q.stream_buffer=q.streams[1];q.stream_offset=q.streams[2];
    return true;
}
bool copy_once(const Reader& r,std::uintptr_t base,const BloomDrawSnapshot& n,BloomQuadSnapshot& q,BloomQuadStatus& why) noexcept {
    if(!copy_header(r,base,n,q,why))return false;const auto header=q;
    why=BloomQuadStatus::copy;
    if(!r.get(base+0x3a5e908,q.vertices)||!r.get(base+0x3a89248,q.indices)||!r.get(q.source,q.world)||
       !r.get(q.source+0x1690,q.view_matrix)||!r.get(q.source+0x16d0,q.projection)||!r.get(q.source+0x1710,q.view_projection)||
       !r.get(q.source+0x500,q.world_view_projection)||!r.get(q.source+0x580,q.matrix))return false;
    if(blur_role(n.role)&&(!r.get(q.source+0xf90,q.uv_constants[0])||!r.get(q.source+0xfa0,q.uv_constants[1])))return false;
    BloomQuadSnapshot late{};if(!copy_header(r,base,n,late,why))return false;
    why=BloomQuadStatus::changed;return header==late;
}
bool recipe(BloomQuadSnapshot& q,const BloomDrawCall& d,bool blur,BloomQuadStatus& why) noexcept {
    why=BloomQuadStatus::geometry;
    if(q.vertex_count!=4||q.index_count!=6||q.pixel_cost||q.indices!=quad_indices)return false;
    const float w=static_cast<float>(q.display.width),h=static_cast<float>(q.display.height);
    const float sx=div32(1.f,static_cast<float>(q.width)),sy=div32(1.f,static_cast<float>(q.height));
    const float u0=mul32(sx,static_cast<float>(q.display.x)),v0=mul32(sy,static_cast<float>(q.display.y));
    const float u1=add32(mul32(sx,w),u0),v1=add32(mul32(sy,h),v0);
    const std::array<std::array<float,4>,4> corners{{{0,0,u0,v0},{w,0,u1,v0},{w,h,u1,v1},{0,h,u0,v1}}};
    for(unsigned i=0;i<4;++i){const auto& c=corners[i];const std::array<std::uint32_t,8> expected{bits(c[0]),bits(c[1]),0,0x3f800000,0xffffffff,bits(c[2]),bits(c[3]),0x3ffe7f7f};
        for(unsigned j=0;j<8;++j)if(q.vertices[i*8+j]!=expected[j])return false;}
    why=BloomQuadStatus::matrix;
    if(q.depth_hack==2||q.eye_offset!=std::array<std::uint32_t,4>{0,0,0,0x3f800000}||q.world!=identity||q.view_matrix!=identity||
       q.versions[0]!=q.versions[1]||q.versions[2]!=q.versions[3]||q.versions[4]!=q.versions[3])return false;
    const float iw=div32(1.f,q.selector?static_cast<float>(q.width):w),ih=div32(1.f,q.selector?static_cast<float>(q.height):h);
    std::array<std::uint32_t,16> projection{};
    projection[0]=bits(mul32(iw,2.f));projection[5]=bits(mul32(ih,-2.f));
    projection[12]=bits(sub32(-1.f,iw));projection[13]=bits(add32(ih,1.f));projection[14]=projection[15]=0x3f800000;
    if(q.projection!=projection||q.view_projection!=projection||q.world_view_projection!=projection)return false;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)if(q.matrix[4*row+col]!=projection[4*col+row])return false;
    if(q.matrix_bind_cache[0]!=q.versions[3]||q.matrix_bind_cache[1]!=0x040000db)return false;
    for(unsigned i=2;i<8;++i)if(q.matrix_bind_cache[i]!=UINT32_MAX)return false;
    if(blur&&(q.uv_bind_cache[0]!=std::array<std::uint32_t,2>{q.uv_versions[0],0x01000079}||
              q.uv_bind_cache[1]!=std::array<std::uint32_t,2>{q.uv_versions[1],0x0100007a}))return false;
    why=BloomQuadStatus::binding;
    if(!q.device||!q.declaration||!q.stream_buffer||!q.index_buffer||q.stream_stride!=32||q.vertex_pool[2]!=q.stream_buffer||q.index_pool[2]!=q.index_buffer||
       q.stream_offset>UINT32_MAX-128||q.vertex_pool[0]!=q.stream_offset+128||q.vertex_pool[0]>q.vertex_pool[1]||q.vertex_pool[3]||
       d.start_index>UINT32_MAX-6||q.index_pool[0]!=d.start_index+6||q.index_pool[0]>q.index_pool[1])return false;
    for(unsigned i=3;i<q.streams.size();++i)if(q.streams[i])return false;
    return true;
}
}
bool read_bloom_quad_copied(std::uintptr_t base,const BloomDrawSnapshot& n,const BloomDrawCall& d,BloomQuadSnapshot* out,const BloomQuadMemory& memory,BloomQuadStatus* status) noexcept {
    alignas(16) unsigned char fp[512];_fxsave(fp);const auto error=GetLastError();const auto control=_mm_getcsr()&0xffc0;
    bool ok=false;BloomQuadStatus why=BloomQuadStatus::input;if(out)*out={};
    if(out&&memory.copy&&control==0x1f80&&input(base,n,d)){
        const Reader r{memory};BloomQuadSnapshot a{},b{};
        ok=copy_once(r,base,n,a,why)&&recipe(a,d,blur_role(n.role),why)&&copy_once(r,base,n,b,why)&&recipe(b,d,blur_role(n.role),why);
        if(ok){why=BloomQuadStatus::changed;ok=a==b;}
        if(ok){b.fp_control=control;*out=b;why=BloomQuadStatus::accepted;}
    }
    if(status)*status=why;SetLastError(error);_fxrstor(fp);return ok;
}
bool read_bloom_quad(std::uintptr_t base,const BloomDrawSnapshot& n,const BloomDrawCall& d,BloomQuadSnapshot* out,BloomQuadStatus* status) noexcept {
    return read_bloom_quad_copied(base,n,d,out,BloomQuadMemory{nullptr,&protected_copy},status);
}
#include "t5/bloom_quad_guards.inc"
}
