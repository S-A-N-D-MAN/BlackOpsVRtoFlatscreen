// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/bloom_episode_native.hpp"
#include "t5/auxiliary_postfx.hpp"
#include "t5/native_read_recipe.hpp"
#include <cstring>
namespace bovr::t5 {
namespace {
template<class T>void copy(std::uintptr_t p,T& v) noexcept {native_read_copy(p,&v);}
bool name(std::uint32_t p,std::array<char,128>& out) noexcept {
    if(p<0x10000||p>UINT32_MAX-128)return false;
    for(unsigned i=0;i<128;++i){copy(p+i,out[i]);if(!out[i])return i!=0;}return false;
}
bool candidate(std::uintptr_t base) noexcept {
    __try {
        std::uint32_t material{};std::uint8_t target{};copy(base+0x417eec4,material);copy(base+0x417ff28,target);
        for(unsigned role=0;role<bloom_role_count;++role){std::uint32_t m{};copy(base+bloom_material_rvas[role],m);
            if(m&&m==material)for(unsigned mask=0;mask<4;++mask)if(bloom_role_enabled(role,mask)&&target==bloom_role_target(role,mask))return true;}
    }__except(EXCEPTION_EXECUTE_HANDLER){}return false;
}
bool address(std::uint32_t p,unsigned bytes) noexcept {return p>=0x10000&&p<=UINT32_MAX-bytes;}
bool plan_once(std::uintptr_t base,const AuxiliaryViewLayout& layout,BloomEpisodePlan& p,BloomEpisodeRejection& rejection) noexcept {
    __try {
        p.version=1;p.buffer=static_cast<std::uint32_t>(layout.buffer);p.native_frame=layout.frame;p.view_count=layout.count;
        p.fp_control=_mm_getcsr()&0xffc0;
        constexpr std::array<unsigned,6> tint{0x3784,0x3780,0x377c,0x37d8,0x37d4,0x37d0};
        for(unsigned eye=0;eye<2;++eye){p.views[eye]=static_cast<std::uint32_t>(layout.slots[eye].address);
            for(unsigned i=0;i<tint.size();++i)copy(p.views[eye]+tint[i],p.tint_bits[eye][i]);
            unsigned enabled{};if(!bloom_tint_enabled(p.tint_bits[eye],enabled))return false;p.enabled[eye]=enabled;
            for(unsigned role=0;role<bloom_role_count;++role){if(!bloom_role_enabled(role,enabled))continue;
                ++p.role_counts[eye];auto& r=p.roles[eye][role];copy(base+bloom_material_rvas[role],r.material);
                if(!address(r.material,0xb4))return false;copy(r.material+0xb0,r.techset);
                if(!address(r.techset,0x1c))return false;copy(r.techset+0x18,r.technique);
                if(!address(r.technique,88))return false;std::uint16_t count{},flags{};copy(r.technique+6,count);copy(r.technique+4,flags);
                r.pass_count=count;r.technique_flags=flags;if(!count||count>4)return false;p.expected_draws+=count;
                for(unsigned i=0;i<count;++i){auto& a=r.passes[i];a.pass=r.technique+8+20*i;
                    copy(a.pass+4,a.vertex_record);copy(a.pass+8,a.pixel_record);
                    if(!address(a.vertex_record,8)||!address(a.pixel_record,8))return false;
                    copy(a.vertex_record+4,a.vertex_shader);copy(a.pixel_record+4,a.pixel_shader);copy(a.pass+12,a.argument_counts);copy(a.pass+16,a.args_pointer);
                }
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    if(p.expected_draws>32){rejection.reason=BloomEpisodeReject::planned_record_cap;rejection.buffer=p.buffer;rejection.native_frame=p.native_frame;rejection.planned_draws=p.expected_draws;return false;}
    return bloom_plan_valid(p);
}
bool once(std::uintptr_t base,const AuxiliaryViewLayout& layout,const BloomEpisodeCursor& cursor,const BloomEpisodePlan& plan,BloomDrawSnapshot* out,BloomReadStatus& why,BloomEpisodeRejection& rejection) noexcept {
    BloomDrawSnapshot s{};why=BloomReadStatus::layout;
    __try {
        copy(layout.buffer+0x16cc38,s.selected);
        AuxiliaryPostfxSnapshot native{};
        if(!read_stereo_postfx(base,layout,s.selected,&native))return false;
        s.buffer=static_cast<std::uint32_t>(layout.buffer);s.native_frame=layout.frame;s.view_count=layout.count;
        why=BloomReadStatus::current_view;copy(base+0x3ccbF78,s.source_view); // source.viewParms3D, not outer selected index.
        if(s.source_view==layout.slots[0].address&&s.selected==0)s.eye=0;
        else if(s.source_view==layout.slots[1].address&&s.selected==postfx_native_index(layout.count,1))s.eye=1;
        else return false; // Explicitly excludes the auxiliary third view.
        s.view=s.source_view;
        why=BloomReadStatus::scene;const auto& v=native.views[s.eye];if(v.rendering_mode&7||v.full_screen)return false;
        if(v.scene_def!=native.source_scene_def)return false;s.scene=v.scene_def;
        why=BloomReadStatus::pixel_cost;std::uint32_t cost{};copy(base+0x420c124,cost);if(cost)return false;
        why=BloomReadStatus::material;copy(base+0x417eec4,s.material);std::uint8_t type{};copy(base+0x417eec8,type);if(type!=4)return false;
        s.role=cursor.role;
        if(cursor.finished||cursor.eye!=s.eye||s.role>=bloom_role_count||!bloom_role_enabled(s.role,plan.enabled[s.eye]))return false;
        const auto& expected=plan.roles[s.eye][s.role];
        if(s.material!=expected.material||!address(s.material,0xc0))return false;
        s.target=bloom_role_target(s.role,plan.enabled[s.eye]);
        if(native.source_target!=s.target)return false;
        copy(base+0x41eb1e8+20*s.target,s.native_target);
        // Native scene target 3 can be a surface-only shared backbuffer. Its
        // color still has to match the device-returned RT0 lease; sampled input
        // images remain mandatory below. Mip outputs retain their image gate.
        if((!s.native_target.image&&!((s.role==3||s.role==8)&&s.target==3))||!s.native_target.color||!s.native_target.width||!s.native_target.height||s.native_target.width>5120||s.native_target.height>4096)return false;
        why=BloomReadStatus::technique;copy(s.material+0xb0,s.techset);if(s.techset<0x10000||s.techset>UINT32_MAX-0x210)return false;
        copy(s.techset+0x18,s.technique);std::uint32_t current{};copy(base+0x417eecc,current);if(s.technique!=current||s.technique<0x10000||s.technique>UINT32_MAX-88)return false;
        copy(s.technique+4,s.technique_flags);copy(s.technique+6,s.pass_count);copy(base+0x417eed4,s.pass_index);
        if(!s.pass_count||s.pass_count>4||s.pass_index>=s.pass_count)return false;
        s.pass=s.technique+8+20*s.pass_index;copy(base+0x417eed0,current);if(s.pass!=current)return false;
        copy(s.pass+4,s.vertex_record);copy(s.pass+8,s.pixel_record);
        if(s.vertex_record<0x10000||s.pixel_record<0x10000||s.vertex_record>UINT32_MAX-16||s.pixel_record>UINT32_MAX-16)return false;
        copy(s.vertex_record+4,s.vertex_shader);copy(s.pixel_record+4,s.pixel_shader);if(!s.vertex_shader||!s.pixel_shader)return false;
        why=BloomReadStatus::arguments;copy(s.pass+12,s.argument_counts);copy(s.pass+15,s.custom_sampler_flags);copy(s.pass+16,s.args_pointer);
        if(s.custom_sampler_flags){rejection={BloomEpisodeReject::custom_samplers,s.buffer,s.native_frame,s.eye,s.role,s.material,s.pass,UINT32_MAX,plan.expected_draws,{0,0,s.custom_sampler_flags}};return false;}
        const unsigned count=s.argument_counts[0]+s.argument_counts[1]+s.argument_counts[2];
        if(count>64||(count&&(s.args_pointer<0x10000||s.args_pointer>UINT32_MAX-count*8)))return false;
        for(unsigned i=0;i<count;++i){auto& arg=s.arguments[i];copy(s.args_pointer+8*i,arg);
            const auto detail=[&](BloomEpisodeReject reason) noexcept {rejection={reason,s.buffer,s.native_frame,s.eye,s.role,s.material,s.pass,i,plan.expected_draws,arg};return false;};
            if(!bloom_argument_shape(arg))return detail(BloomEpisodeReject::argument_shape);
            // Only the currently mapped code samplers are complete input evidence.
            // Unhandled argument classes conservatively reject this diagnostic.
            if(arg.type!=3&&arg.type!=4&&arg.type!=5)return detail(BloomEpisodeReject::argument_source);
            if(arg.type==4&&(!bloom_code_target(s.role,plan.enabled[s.eye],arg.value)||arg.destination>=16))return detail(BloomEpisodeReject::argument_code);
            if((arg.type==3||arg.type==5)&&(arg.value&0xffff)==93){
                if(s.constant_count>=s.constants.size())return false;s.constants[s.constant_count++]={arg.type,arg.destination};}
            if(arg.type==4){
                if(s.sampler_count>=s.samplers.size())return false;auto& a=s.samplers[s.sampler_count++];a.code=arg.value;a.destination=arg.destination;
                copy(base+0x3ccb9c0+4*a.code,a.image);copy(base+0x3ccba6c+a.code,a.sampler);
                copy(base+0x3ccba98+8*a.code,a.control);copy(base+0x3ccba9c+8*a.code,a.control_value);
                if(a.control&0x100||a.image<0x10000||a.image>UINT32_MAX-4)return false;copy(a.image,a.texture);if(!a.texture)return false;
                AuxiliaryTarget input{};copy(base+0x41eb1e8+20*bloom_code_target(s.role,plan.enabled[s.eye],a.code),input);
                if(input.image!=a.image)return false;}
        }
        why=BloomReadStatus::names;const std::array<std::uint32_t,4> names{s.material,s.technique,s.vertex_record,s.pixel_record};
        for(unsigned i=0;i<4;++i){std::uint32_t p{};copy(names[i],p);if(!name(p,s.names[i]))return false;}
        why=BloomReadStatus::source;copy(base+0x3ccbfa4,s.source_mode);copy(base+0x3ccbfec,s.selector);copy(base+0x3ccbff0,s.width);copy(base+0x3ccbff4,s.height);
        if(s.source_mode!=2||s.selector!=((s.role==3||s.role==8)?0u:1u)||s.width!=s.native_target.width||s.height!=s.native_target.height)return false;
        copy(base+0x3ccb340,s.viewport_constant);s.scene_viewport=native.source_viewport;copy(base+0x3ccbfcc,s.scissor);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    if(!bloom_plan_matches(plan,cursor,s))return false;*out=s;return true;
}
}
bool read_bloom_episode_shader(std::uintptr_t base,const AuxiliaryViewLayout& layout,const BloomEpisodeCursor& cursor,
    BloomDrawSnapshot* out,BloomEpisodePlan* plan,BloomReadStatus* status,BloomEpisodeRejection* detail) noexcept {
    const auto error=GetLastError();bool ok=false;BloomReadStatus why=BloomReadStatus::unrelated;
    if(out)*out={};if(plan)*plan={};if(detail)*detail={};BloomEpisodeRejection rejection{};
    __try {
        if(out&&plan&&base&&base<=UINT32_MAX-auxiliary_read_extent&&!cursor.finished){
            // Non-bloom tail draws from the first eye are outside the episode
            // after that eye closes; ordinary and auxiliary orders differ.
            std::uint32_t current_view{};copy(base+0x3ccbf78,current_view);
            const auto first=postfx_first_eye(layout.count);
            const bool closed_first=cursor.eye==1u-first&&cursor.role==0&&cursor.pass==0&&current_view==layout.slots[first].address;
            if(!closed_first&&candidate(base)){
                BloomEpisodePlan a{},b{};BloomDrawSnapshot x{},y{};why=BloomReadStatus::technique;
                ok=plan_once(base,layout,a,rejection)&&once(base,layout,cursor,a,&x,why,rejection)&&plan_once(base,layout,b,rejection)&&once(base,layout,cursor,b,&y,why,rejection);
                if(ok){why=BloomReadStatus::changed;ok=a==b&&x==y;}
                if(ok){*out=y;*plan=b;why=BloomReadStatus::accepted;}
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){ok=false;why=BloomReadStatus::layout;}
    if(detail&&!ok)*detail=rejection;if(status)*status=why;SetLastError(error);return ok;
}
#include "t5/bloom_episode_guards.inc"
}
