// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_episode_native.hpp"
#include <algorithm>
#include <vector>
#include <cstring>
namespace episode_fixture {
template<class T>void put(std::uintptr_t address,const T& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));}
using namespace bovr::t5;
struct Memory {
    std::uintptr_t base{};std::vector<std::uintptr_t> pages;
    Memory(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,(auxiliary_read_extent+4095)&~std::size_t(4095),MEM_RESERVE,PAGE_NOACCESS));}
    ~Memory(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void commit(std::uintptr_t address,std::size_t size){for(auto p=address&~std::uintptr_t(4095);p<address+size;p+=4096)
        if(std::find(pages.begin(),pages.end(),p)==pages.end()){check(VirtualAlloc(reinterpret_cast<void*>(p),4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"owned fixture page committed");pages.push_back(p);}}
    template<class T>void write(std::uintptr_t address,const T& value){commit(address,sizeof value);put(address,value);}
};
AuxiliaryViewLayout fill(Memory& m,unsigned slot,unsigned views=3){
    AuxiliaryViewLayout s{};s.buffer=m.base+0x3740100+slot*0x186300;s.view_array=m.base+0x420f730+slot*0xe380;s.frame=42;s.count=views;s.selected=views-1;
    m.write(m.base+0x3a5b5f8,s.buffer);m.write(s.buffer+0x15d7a0,s.frame);m.write(s.buffer+0x16cc38,s.selected);m.write(s.buffer+0x16cc3c,s.count);m.write(s.buffer+0x16cc40,s.view_array);
    for(unsigned i=0;i<views;++i){auto& v=s.slots[i];v.address=s.view_array+i*0x38e0;v.scene=v.display=v.scissor=i<2?bovr::render::HudEyeViewport{static_cast<int>(i)*640,0,640,720}:bovr::render::HudEyeViewport{0,0,768,432};
        v.targets={3,3,0x91,0x92,22,20,19,2};v.extra=i==2?1:0;if(i==2)v.targets[0]=v.targets[1]=22;
        m.write(v.address+0x140,v.scene);m.write(v.address+0x150,v.display);m.write(v.address+0x160,v.scissor);m.write(v.address+0x35f0,v.targets);m.write(v.address+0x38c4,v.extra);
        if(i<2){std::array<std::uint32_t,5> scene{1000+i,0x3f800000,0,0,0};std::array<std::uint32_t,21> effects{};
            for(unsigned word=0;word<effects.size();++word)effects[word]=0x2000+i*0x100+word;
            effects[4]=1;effects[5]=2;effects[11]=1;effects[12]=1;effects[19]=1;effects[20]=1;
            m.write(v.address+0x170,scene);m.write(v.address+0x18c,i);m.write(v.address+0x3620,i*8);m.write(v.address+0x21cc,effects);}}
    for(unsigned id:{1u,3u,6u,22u,23u}){AuxiliaryTarget t{0x11110000+id,0x22220000+id,0x33330000+id,768,432};
        m.write(m.base+0x41eb1e8+id*20,t);if(id>=22)s.targets[id-22]=t;}
    m.write(m.base+0x356a32c,std::array<std::uint32_t,4>{100,200,300,400});
    std::array<std::uint32_t,20> params{};for(unsigned i=0;i<params.size();++i)params[i]=0x30000000+i;m.write(m.base+0x356a33c,params);
    m.write(m.base+0x3ccbfa8,std::array<std::uint32_t,5>{998,0x3f800000,0,0,0});m.write(m.base+0x3ccbfbc,bovr::render::HudEyeViewport{0,0,1280,720});
    m.write(m.base+0x417ff28,std::uint8_t{3});return s;
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
    m.write(l.buffer+0x16cc38,postfx_native_index(l.count,eye));m.write(m.base+0x3ccbf78,ptr(l.slots[eye].address));
    m.write(l.slots[eye].address+0x18c,0u);m.write(l.slots[eye].address+0x3620,0u);
    std::array<std::uint32_t,5> scene{};std::memcpy(&scene,reinterpret_cast<const void*>(l.slots[eye].address+0x170),sizeof scene);m.write(m.base+0x3ccbfa8,scene);
    m.write(m.base+0x417ff28,static_cast<std::uint8_t>(ids[role]));m.write(m.base+0x41eb1e8+20*ids[role],AuxiliaryTarget{ptr(image),0xdead1111,0,640,360});
    m.write(m.base+0x3ccbfa4,2u);m.write(m.base+0x3ccbfec,role==3?0u:1u);m.write(m.base+0x3ccbff0,640u);m.write(m.base+0x3ccbff4,360u);
    m.write(m.base+0x3ccb340,std::array<std::uint32_t,4>{eye?0x3f800000u:0u,0,0x3f800000,0x3f000000});m.write(m.base+0x3ccbfcc,bovr::render::HudEyeViewport{0,0,640,360});
}
void episode_setup(Memory& m,const AuxiliaryViewLayout& l,unsigned eye,unsigned role,unsigned pass,unsigned mask,unsigned first_count=1){
    setup(m,l,eye,0,0);
    for(unsigned e=0;e<2;++e){m.write(l.slots[e].address+0x18c,0u);m.write(l.slots[e].address+0x3620,0u);
        for(unsigned off:{0x3784u,0x3780u,0x377cu,0x37d8u,0x37d4u,0x37d0u})m.write(l.slots[e].address+off,0u);
        if(mask&1)m.write(l.slots[e].address+0x3784,0x3f800000u);if(mask&2)m.write(l.slots[e].address+0x37d8,0x3f800000u);}
    for(unsigned id:{3u,6u,25u,26u,27u,28u,29u,30u}){const auto image=static_cast<std::uint32_t>(m.base+0x80000+id*0x100);
        m.write(image,0xdead2000u+id);const auto w=id<25?1280u:id==25?640u:id==26?320u:160u;const auto h=id<25?720u:id==25?360u:id==26?180u:90u;
        m.write(m.base+0x41eb1e8+20*id,AuxiliaryTarget{id==3?0u:image,0xdead1000u+id,0,static_cast<std::uint16_t>(w),static_cast<std::uint16_t>(h)});}
    for(unsigned r=0;r<13;++r){const unsigned k=r==11?9:r;const auto b=static_cast<std::uint32_t>(m.base+0x30000+k*0x2000);
        const unsigned count=r==0?first_count:1;m.commit(b,0x2000);m.write(m.base+bloom_material_rvas[r],b);
        m.write(b,b+0x1000);std::array<char,128> text{};strcpy_s(text.data(),text.size(),"complete-ldr-copied-fixture");m.write(b+0x1000,text);
        m.write(b+0xb0,b+0x200);m.write(b+0x200+0x18,b+0x500);m.write(b+0x500,b+0x1000);m.write(b+0x504,std::uint16_t{7});m.write(b+0x506,static_cast<std::uint16_t>(count));
        m.write(b+0x800,b+0x1000);m.write(b+0x804,0xdead3000u+k);m.write(b+0x900,b+0x1000);m.write(b+0x904,0xdead4000u+k);
        for(unsigned p=0;p<count;++p){const auto a=b+0xa00+p*0x100;std::array<BloomArgument,5> args{};unsigned n=1;args[0]={3,0,0x040000db};
            for(unsigned code:{10u,34u,35u,36u})if(bloom_code_target(r,mask,code))args[n++]={4,static_cast<std::uint16_t>(n-1),code};
            m.write(a,args);const auto pp=b+0x508+20*p;m.write(pp+4,b+0x800);m.write(pp+8,b+0x900);m.write(pp+12,std::array<std::uint8_t,4>{0,static_cast<std::uint8_t>(n),0,0});m.write(pp+16,a);
        }
    }
    const unsigned key=role==11?9:role;const auto b=static_cast<std::uint32_t>(m.base+0x30000+key*0x2000);
    m.write(m.base+0x417eec4,b);m.write(m.base+0x417eecc,b+0x500);m.write(m.base+0x417eed0,b+0x508+20*pass);m.write(m.base+0x417eed4,pass);
    const unsigned id=bloom_role_target(role,mask);AuxiliaryTarget target{};std::memcpy(&target,reinterpret_cast<void*>(m.base+0x41eb1e8+20*id),sizeof target);
    m.write(m.base+0x417ff28,static_cast<std::uint8_t>(id));m.write(m.base+0x3ccbff0,std::uint32_t(target.width));m.write(m.base+0x3ccbff4,std::uint32_t(target.height));m.write(m.base+0x3ccbfec,id==3?0u:1u);
    for(unsigned code:{10u,34u,35u,36u}){const auto input=bloom_code_target(role,mask,code);if(!input)continue;
        m.write(m.base+0x3ccb9c0+4*code,static_cast<std::uint32_t>(m.base+0x80000+input*0x100));m.write(m.base+0x3ccba6c+code,std::uint8_t{7});m.write(m.base+0x3ccba98+8*code,0u);m.write(m.base+0x3ccba9c+8*code,0u);}
}
}
