// SPDX-License-Identifier: GPL-3.0-only
#define main inherited_postfx_main
#include "auxiliary_postfx_tests.cpp"
#undef main
#include "t5/bloom_shader_native.hpp"
namespace {
std::array<std::uint32_t,6> authored_shader_tokens() noexcept {
    // A minimal synthetic SM3 frame. bloom_token_stream validates only the
    // bounded token framing, so the two operand payloads are authored markers.
    std::array<std::uint32_t,6> value{};
    constexpr std::uint32_t vertex_stage=0xfffeu,shader_model=3u;
    constexpr std::uint32_t instruction_length=2u,mov_opcode=1u;
    value[0]=(vertex_stage<<16)|(shader_model<<8);
    value[1]=(instruction_length<<24)|mov_opcode;
    value[2]=0x80000000u|(0x0fu<<16);
    value[3]=0x80000000u|(0xe4u<<16)|(1u<<28);
    value[5]=0xffffu;
    return value;
}
std::pair<std::uintptr_t,std::uint32_t> mutation_case(const Memory& m,const AuxiliaryViewLayout& l,unsigned index) noexcept {
    switch(index) {
    case 0:return {m.base+0x3ccbf78,static_cast<std::uint32_t>(l.slots[2].address)};
    case 1:return {m.base+0x3ccbf78,static_cast<std::uint32_t>(l.slots[0].address)};
    case 2:return {m.base+0x3ccbfa8,77};
    case 3:return {m.base+0x420c124,1};
    case 4:return {m.base+0x417eec8,5};
    case 5:return {m.base+0x417eecc,0};
    case 6:return {m.base+0x417eed0,0};
    case 7:return {m.base+0x417eed4,4};
    case 8:return {m.base+0x3ccbfec,0};
    case 9:return {m.base+0x3ccbff0,639};
    case 10:return {m.base+0x3ccba98+34*8,0x100};
    case 11:return {m.base+0x15000+4,0x0101005d};
    case 12:return {m.base+0x15000+8,0x00100004};
    case 13:return {l.slots[1].address+0x18c,1};
    case 14:return {l.slots[1].address+0x3620,1};
    case 15:return {m.base+0x13000+4,0};
    default:return {};
    }
}
void setup(Memory& m,const AuxiliaryViewLayout& l,unsigned eye,unsigned role,unsigned pass) {
    const auto material=m.base+0x10000,techset=m.base+0x11000,technique=m.base+0x12000,vs=m.base+0x13000,ps=m.base+0x14000,args=m.base+0x15000,names=m.base+0x16000,image=m.base+0x17000;
    const auto ptr=[](std::uintptr_t p){return static_cast<std::uint32_t>(p);};
    constexpr std::array<unsigned,4> ids{25,26,27,3};
    for(auto p:{material,techset,technique,vs,ps,args,names,image})m.commit(p,4096);
    m.write(material,ptr(names));m.write(material+0xb0,ptr(techset));m.write(techset+0x18,ptr(technique));m.write(technique,ptr(names));m.write(technique+4,std::uint16_t{7});m.write(technique+6,std::uint16_t{4});
    std::array<char,128> text{};strcpy_s(text.data(),text.size(),"owned-native-shader-fixture");m.write(names,text);
    m.write(vs,ptr(names));m.write(ps,ptr(names));m.write(vs+4,0xdead0001u);m.write(ps+4,0xdead0002u); // Never callable.
    const auto p=technique+8+20*pass;m.write(p+4,ptr(vs));m.write(p+8,ptr(ps));m.write(p+12,std::array<std::uint8_t,4>{0,2,2,0});m.write(p+16,ptr(args));
    std::array<BloomArgument,4> a{{{3,9,0x0100005d},{4,2,34},{5,6,0x0100005d},{4,5,35}}};m.write(args,a);
    m.write(image,0xdeaf0003u);
    for(unsigned c:{34u,35u,36u}){m.write(m.base+0x3ccb9c0+4*c,ptr(image));m.write(m.base+0x3ccba6c+c,std::uint8_t{7});m.write(m.base+0x3ccba98+8*c,0u);m.write(m.base+0x3ccba9c+8*c,0u);}
    constexpr std::array<unsigned,4> globals{0x356a398,0x356a390,0x356a3a0,0x356a3cc};for(auto g:globals)m.write(m.base+g,ptr(material));
    m.write(m.base+0x420c124,0u);m.write(m.base+0x417eec4,ptr(material));m.write(m.base+0x417eec8,std::uint8_t{4});m.write(m.base+0x417eecc,ptr(technique));m.write(m.base+0x417eed0,ptr(p));m.write(m.base+0x417eed4,pass);
    m.write(l.buffer+0x16cc38,eye?2u:0u);m.write(m.base+0x3ccbf78,ptr(l.slots[eye].address));
    m.write(l.slots[eye].address+0x18c,0u);m.write(l.slots[eye].address+0x3620,0u);
    std::array<std::uint32_t,5> scene{};std::memcpy(&scene,reinterpret_cast<const void*>(l.slots[eye].address+0x170),sizeof scene);m.write(m.base+0x3ccbfa8,scene);
    m.write(m.base+0x417ff28,static_cast<std::uint8_t>(ids[role]));m.write(m.base+0x41eb1e8+20*ids[role],AuxiliaryTarget{ptr(image),0xdead1111,0,640,360});
    m.write(m.base+0x3ccbfa4,2u);m.write(m.base+0x3ccbfec,role==3?0u:1u);m.write(m.base+0x3ccbff0,640u);m.write(m.base+0x3ccbff4,360u);
    m.write(m.base+0x3ccb340,std::array<std::uint32_t,4>{eye?0x3f800000u:0u,0,0x3f800000,0x3f000000});m.write(m.base+0x3ccbfcc,bovr::render::HudEyeViewport{0,0,640,360});
}
}
int main(){
    {const auto tokens=authored_shader_tokens();
        check(bloom_token_stream(tokens.data(),tokens.size(),true),"bounded SM3 framed instruction plus NOP accepted");
        for(unsigned why=0;why<6;++why){auto bad=tokens;if(why==0)bad[0]=0xffff0300;if(why==1)bad[0]=0xfffe0101;
            if(why==2)bad[1]=0x0f000001;if(why==3)bad[1]=0x000afffe;if(why==4)bad[1]=0xffff;if(why==5)bad[5]=0;
            check(!bloom_token_stream(bad.data(),bad.size(),true),"wrong stage/model/truncated instruction/comment/early or absent end rejected");}}
    {BloomByteBudget b;check(!b.debit(0)&&!b.debit(bloom_shader_bytes+1),"zero and excessive shader lengths reject");
        for(unsigned i=0;i<64;++i)check(b.debit(65536),"independent exact shader debit");
        check(b.used()==4194304&&!b.debit(4)&&b.used()==4194304,"hard process bound independent of eyes or dedup");}
    for(unsigned count=1;count<=4;++count){BloomPairSet p;for(unsigned eye:{1u,0u})for(unsigned role=0;role<4;++role)for(unsigned pass=0;pass<count;++pass){BloomDrawSnapshot n{};n.eye=eye;n.role=role;n.pass_index=pass;n.pass_count=static_cast<std::uint16_t>(count);n.material=role+1;check(p.add(n),"bounded complete native pass sequence");}check(p.complete(),"all four roles both eyes complete");}
    for(unsigned changed=0;changed<6;++changed){BloomPairSet pair;for(unsigned eye:{1u,0u})for(unsigned role=0;role<4;++role){BloomDrawSnapshot n{};n.eye=eye;n.role=role;n.pass_count=1;n.material=role+1;
        if(!eye&&!role){if(changed==0)++n.material;if(changed==1)++n.techset;if(changed==2)++n.technique;if(changed==3)++n.pass;if(changed==4)++n.vertex_shader;if(changed==5)++n.pixel_shader;}
        pair.add(n);}check(!pair.complete(),"cross-eye material/pass/shader mismatch never establishes pair identity");}
    {BloomPairSet pair;BloomDrawSnapshot n{};n.eye=1;n.pass_count=1;check(pair.add(n)&&!pair.add(n)&&!pair.complete(),"duplicate native pass cannot complete pair");}
    Memory m;check(!!m.base,"sparse native fixture reservation");if(!m.base)return 1;
    for(unsigned slot=0;slot<2;++slot){auto l=fill(m,slot);
        constexpr AuxiliaryTarget authored_surface{0,0xa1b2c3d4u,0x5e6f7081u,1280,720};
        for(unsigned eye=0;eye<2;++eye)for(unsigned pass=0;pass<4;++pass){
            setup(m,l,eye,3,pass);m.write(m.base+0x41eb1e8+20*3,authored_surface);
            m.write(m.base+0x3ccbff0,1280u);m.write(m.base+0x3ccbff4,720u);BloomDrawSnapshot n{};
            SetLastError(0x87654321);
            check(read_bloom_shader(m.base,l,&n)&&GetLastError()==0x87654321,"actual reader admits authored surface-only target3 for each eye/pass");
            check(n.role==3&&n.target==3&&n.native_target==authored_surface&&n.width==1280&&n.height==720,"authored scene output retains every copied identity and dimension");
        }
        for(unsigned eye=0;eye<2;++eye)for(unsigned role=0;role<4;++role){
            setup(m,l,eye,role,0);constexpr std::array<unsigned,4> ids{25,26,27,3};const auto target=m.base+0x41eb1e8+20*ids[role];
            m.write(target,0u);BloomDrawSnapshot n{};
            check(read_bloom_shader(m.base,l,&n)==(role==3),"null output image is permitted only exact scene role, all mip nulls reject");
            for(unsigned failure=0;failure<8;++failure){setup(m,l,eye,role,0);if(role==3)m.write(target,0u);
                if(failure==0)m.write(target+4,0u);
                if(failure==1)m.write(target+12,std::uint16_t{0});
                if(failure==2)m.write(target+14,std::uint16_t{4097});
                if(failure==3)m.write(m.base+0x417eec8,std::uint8_t{5});
                if(failure==4)m.write(m.base+0x417eec4,0u);
                if(failure==5)m.write(m.base+0x417ff28,std::uint8_t{28});
                if(failure==6)m.write(m.base+0x3ccb9c0+34*4,0u);
                if(failure==7)m.write(m.base+0x17000,0u);
                check(!read_bloom_shader(m.base,l,&n)&&n==BloomDrawSnapshot{},"surface-only exception does not admit bad color/dimensions/type/material/target or absent sampled image/texture");
            }
        }
        for(unsigned eye=0;eye<2;++eye)for(unsigned role=0;role<4;++role)for(unsigned pass=0;pass<4;++pass){setup(m,l,eye,role,pass);BloomDrawSnapshot n{};
            SetLastError(0x87654321);check(read_bloom_shader(m.base,l,&n)&&GetLastError()==0x87654321,"actual protected copied reader accepts current native tuple and preserves LastError");
            check(n.eye==eye&&n.role==role&&n.pass_index==pass&&n.source_view==l.slots[eye].address&&n.vertex_shader==0xdead0001&&n.sampler_count==2&&n.samplers[0].texture==0xdeaf0003&&n.constant_count==2,"native view, pass, compare-only handles and actual argument mappings copied");
        }
        setup(m,l,1,0,0);BloomDrawSnapshot n{};
        for(unsigned i=0;i<16;++i){setup(m,l,1,0,0);auto [p,v]=mutation_case(m,l,i);
            m.write(p,v);check(!read_bloom_shader(m.base,l,&n)&&n==BloomDrawSnapshot{},"extra/wrong view, native state, unsupported arguments or changed pass fail closed");}
        setup(m,l,1,0,0);
        for(auto p:{m.base+0x10000,m.base+0x11000,m.base+0x12000,m.base+0x13000,m.base+0x14000,m.base+0x15000,m.base+0x16000,m.base+0x17000,m.base+0x3ccbf78}){
            const auto page=p&~std::uintptr_t(4095);DWORD old{};check(!!VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old),"protect copied metadata page");
            check(!read_bloom_shader(m.base,l,&n)&&n==BloomDrawSnapshot{},"protected native metadata failure contains fault and clears output");DWORD ignore{};VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignore);
            check(read_bloom_shader(m.base,l,&n),"fresh protected reader recovery after owned fixture page returns");}
    }
    std::printf("bloom shader native: %u checks %u failures\n",checks,failures);return failures?1:0;
}
