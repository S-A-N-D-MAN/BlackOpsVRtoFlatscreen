// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/bloom_shader_native.hpp"
#include "t5/auxiliary_postfx.hpp"
#include <cstring>
namespace bovr::t5 {
namespace {
template<class T>void copy(std::uintptr_t p,T& v) noexcept {std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof(T));}
bool name(std::uint32_t p,std::array<char,128>& out) noexcept {
    if(p<0x10000||p>UINT32_MAX-128)return false;
    for(unsigned i=0;i<128;++i){copy(p+i,out[i]);if(!out[i])return i!=0;}return false;
}
bool candidate(std::uintptr_t base) noexcept {
    __try {
        std::uint32_t material{};std::uint8_t target{};copy(base+0x417eec4,material);copy(base+0x417ff28,target);
        constexpr std::array<unsigned,4> targets{25,26,27,3};
        constexpr std::array<std::uint32_t,4> globals{0x356a398,0x356a390,0x356a3a0,0x356a3cc};
        for(unsigned i=0;i<4;++i)if(target==targets[i]){std::uint32_t m{};copy(base+globals[i],m);if(m&&m==material)return true;}
    }__except(EXCEPTION_EXECUTE_HANDLER){}return false;
}
bool once(std::uintptr_t base,const AuxiliaryViewLayout& layout,BloomDrawSnapshot* out,BloomReadStatus& why) noexcept {
    BloomDrawSnapshot s{};why=BloomReadStatus::layout;
    __try {
        copy(layout.buffer+0x16cc38,s.selected);
        AuxiliaryPostfxSnapshot native{};
        if(!read_auxiliary_postfx(base,layout,s.selected,&native))return false;
        s.buffer=static_cast<std::uint32_t>(layout.buffer);s.native_frame=layout.frame;s.view_count=layout.count;
        why=BloomReadStatus::current_view;copy(base+0x3ccbF78,s.source_view); // source.viewParms3D, not outer selected index.
        if(s.source_view==layout.slots[0].address&&s.selected==0)s.eye=0;
        else if(s.source_view==layout.slots[1].address&&s.selected==2)s.eye=1;
        else return false; // Explicitly excludes the auxiliary third view.
        s.view=s.source_view;
        why=BloomReadStatus::scene;const auto& v=native.views[s.eye];if(v.rendering_mode&7||v.full_screen)return false;
        if(v.scene_def!=native.source_scene_def)return false;s.scene=v.scene_def;
        why=BloomReadStatus::pixel_cost;std::uint32_t cost{};copy(base+0x420c124,cost);if(cost)return false;
        why=BloomReadStatus::material;copy(base+0x417eec4,s.material);std::uint8_t type{};copy(base+0x417eec8,type);if(type!=4)return false;
        constexpr std::array<std::uint32_t,4> globals{0x356a398,0x356a390,0x356a3a0,0x356a3cc};
        constexpr std::array<unsigned,4> targets{25,26,27,3};
        s.role=4;for(unsigned i=0;i<4;++i){std::uint32_t m{};copy(base+globals[i],m);if(m&&s.material==m&&native.source_target==targets[i]){s.role=i;break;}}
        if(s.role==4||s.material<0x10000||s.material>UINT32_MAX-0xc0)return false;
        s.target=targets[s.role];copy(base+0x41eb1e8+20*s.target,s.native_target);
        // Native scene target 3 can be a surface-only shared backbuffer. Its
        // color still has to match the device-returned RT0 lease; sampled input
        // images remain mandatory below. Mip outputs retain their image gate.
        if((!s.native_target.image&&!(s.role==3&&s.target==3))||!s.native_target.color||!s.native_target.width||!s.native_target.height||s.native_target.width>5120||s.native_target.height>4096)return false;
        why=BloomReadStatus::technique;copy(s.material+0xb0,s.techset);if(s.techset<0x10000||s.techset>UINT32_MAX-0x210)return false;
        copy(s.techset+0x18,s.technique);std::uint32_t current{};copy(base+0x417eecc,current);if(s.technique!=current||s.technique<0x10000||s.technique>UINT32_MAX-88)return false;
        copy(s.technique+4,s.technique_flags);copy(s.technique+6,s.pass_count);copy(base+0x417eed4,s.pass_index);
        if(!s.pass_count||s.pass_count>4||s.pass_index>=s.pass_count)return false;
        s.pass=s.technique+8+20*s.pass_index;copy(base+0x417eed0,current);if(s.pass!=current)return false;
        copy(s.pass+4,s.vertex_record);copy(s.pass+8,s.pixel_record);
        if(s.vertex_record<0x10000||s.pixel_record<0x10000||s.vertex_record>UINT32_MAX-16||s.pixel_record>UINT32_MAX-16)return false;
        copy(s.vertex_record+4,s.vertex_shader);copy(s.pixel_record+4,s.pixel_shader);if(!s.vertex_shader||!s.pixel_shader)return false;
        why=BloomReadStatus::arguments;copy(s.pass+12,s.argument_counts);copy(s.pass+15,s.custom_sampler_flags);copy(s.pass+16,s.args_pointer);
        const unsigned count=s.argument_counts[0]+s.argument_counts[1]+s.argument_counts[2];
        if(count>64||(count&&(s.args_pointer<0x10000||s.args_pointer>UINT32_MAX-count*8)))return false;
        for(unsigned i=0;i<count;++i){auto& arg=s.arguments[i];copy(s.args_pointer+8*i,arg);if(!bloom_argument_shape(arg))return false;
            if((arg.type==3||arg.type==5)&&(arg.value&0xffff)==93){
                if(s.constant_count>=s.constants.size())return false;s.constants[s.constant_count++]={arg.type,arg.destination};}
            if(arg.type==4&&arg.value>=34&&arg.value<=36){
                if(s.sampler_count>=s.samplers.size())return false;auto& a=s.samplers[s.sampler_count++];a.code=arg.value;a.destination=arg.destination;
                copy(base+0x3ccb9c0+4*a.code,a.image);copy(base+0x3ccba6c+a.code,a.sampler);
                copy(base+0x3ccba98+8*a.code,a.control);copy(base+0x3ccba9c+8*a.code,a.control_value);
                if(a.control&0x100||a.image<0x10000||a.image>UINT32_MAX-4)return false;copy(a.image,a.texture);if(!a.texture)return false;}
        }
        why=BloomReadStatus::names;const std::array<std::uint32_t,4> names{s.material,s.technique,s.vertex_record,s.pixel_record};
        for(unsigned i=0;i<4;++i){std::uint32_t p{};copy(names[i],p);if(!name(p,s.names[i]))return false;}
        why=BloomReadStatus::source;copy(base+0x3ccbfa4,s.source_mode);copy(base+0x3ccbfec,s.selector);copy(base+0x3ccbff0,s.width);copy(base+0x3ccbff4,s.height);
        if(s.source_mode!=2||s.selector!=(s.role==3?0u:1u)||s.width!=s.native_target.width||s.height!=s.native_target.height)return false;
        copy(base+0x3ccb340,s.viewport_constant);s.scene_viewport=native.source_viewport;copy(base+0x3ccbfcc,s.scissor);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=s;return true;
}
}
bool read_bloom_shader(std::uintptr_t base,const AuxiliaryViewLayout& layout,BloomDrawSnapshot* out,BloomReadStatus* status) noexcept {
    const auto error=GetLastError();bool ok=false;BloomReadStatus why=BloomReadStatus::unrelated;if(out)*out={};
    if(out&&base&&base<=UINT32_MAX-auxiliary_read_extent&&candidate(base)){BloomDrawSnapshot a{},b{};ok=once(base,layout,&a,why)&&once(base,layout,&b,why);if(ok){why=BloomReadStatus::changed;ok=a==b;}if(ok){*out=b;why=BloomReadStatus::accepted;}}
    if(status)*status=why;SetLastError(error);return ok;
}
#include "t5/bloom_shader_guards.inc"
}
