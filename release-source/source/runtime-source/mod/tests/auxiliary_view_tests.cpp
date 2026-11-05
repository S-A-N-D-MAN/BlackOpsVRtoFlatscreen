// SPDX-License-Identifier: GPL-3.0-only
#include "t5/auxiliary_view.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <algorithm>
namespace {
unsigned checks{},failures{};void check(bool result,const char* why){++checks;if(!result){++failures;std::printf("FAIL %s\n",why);}}
using namespace bovr::t5;
const std::array<bovr::render::HudEyeViewport,2> eyes{{{0,0,640,720},{640,0,640,720}}};
AuxiliaryViewLayout layout(){AuxiliaryViewLayout a{};a.buffer=0x3cc6400;a.view_array=0x461dab0;a.frame=5868;a.count=3;a.selected=2;
    for(unsigned i=0;i<3;++i){auto& v=a.slots[i];v.address=a.view_array+i*0x38e0;v.scene=v.display=v.scissor=i<2?eyes[i]:bovr::render::HudEyeViewport{0,0,768,432};v.extra=i==2?1:0;v.targets={3,3,0x91,0x92,22,20,19,2};if(i==2)v.targets[0]=v.targets[1]=22;}
    a.targets={AuxiliaryTarget{72813464,1139873952,1156256960,768,432},AuxiliaryTarget{72813516,1139874912,1156263200,768,432}};return a;}
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
void fill_memory(std::uintptr_t base,AuxiliaryViewLayout a,unsigned slot=0){a.buffer=base+0x3740100+slot*0x186300;a.view_array=base+0x420f730+slot*0xe380;
    put(base+0x373708c,a.buffer);put(base+0x3a5b5f8,a.buffer);put(a.buffer+0x15d7a0,a.frame);put(a.buffer+0x16cc38,a.selected);put(a.buffer+0x16cc3c,a.count);put(a.buffer+0x16cc40,a.view_array);
    for(unsigned i=0;i<a.count;++i){const auto p=a.view_array+i*0x38e0;put(p+0x140,a.slots[i].scene);put(p+0x150,a.slots[i].display);put(p+0x160,a.slots[i].scissor);put(p+0x35f0,a.slots[i].targets);put(p+0x38c4,a.slots[i].extra);}
    for(unsigned i=0;i<2;++i){const auto p=base+0x41eb1e8+(22+i)*20;put(p,a.targets[i].image);put(p+4,a.targets[i].color);put(p+8,a.targets[i].depth);put(p+12,a.targets[i].width);put(p+14,a.targets[i].height);}}
}
int main(){const auto original=layout();const std::array<AuxiliaryViewSlot,2> produced{original.slots[0],original.slots[1]};
    check(exact_auxiliary_layout(original,eyes,produced),"observed three-slot layout accepted only with exact produced eye copies");
    for(unsigned i=0;i<3;++i){auto s=original;s.count=i;check(!exact_auxiliary_layout(s,eyes,produced),"zero one or two native views do not select auxiliary path");s=original;s.selected=i;check(exact_auxiliary_layout(s,eyes,produced)==(i==2),"only selected auxiliary slot2 admitted");
        s=original;s.slots[i].address+=4;check(!exact_auxiliary_layout(s,eyes,produced),"unexpected storage pointer rejected");s=original;s.slots[i].extra^=1;check(!exact_auxiliary_layout(s,eyes,produced),"regular and extra native flags are not interchangeable");
        for(unsigned field=0;field<8;++field){s=original;s.slots[i].targets[field]^=1;const bool expected=i==2&&(field==2||field==3);check(exact_auxiliary_layout(s,eyes,produced)==expected,"declared targets and copied regular raw target fields checked; extra unused bytes carry no invented semantics");}
        s=original;s.slots[i].scissor.width--;check(!exact_auxiliary_layout(s,eyes,produced),"native per-view scissor retained and validated");}
    auto fullwidth=original;fullwidth.slots[0].scene.width=1280;check(!exact_auxiliary_layout(fullwidth,eyes,produced),"ordinary fullwidth plus auxiliary is not mistaken for stereo");
    for(unsigned i=0;i<2;++i){for(unsigned field=0;field<3;++field){auto s=original;if(!field)s.targets[i].image=0;else if(field==1)s.targets[i].color=0;else s.targets[i].depth=0;check(!exact_auxiliary_layout(s,eyes,produced),"missing native auxiliary target rejected");}
        auto s=original;s.targets[i].width=0;check(!exact_auxiliary_layout(s,eyes,produced),"zero dimensions rejected");s=original;s.targets[i].width=5121;check(!exact_auxiliary_layout(s,eyes,produced),"bounded dimensions rejected");}
    auto s=original;s.targets[1].color=s.targets[0].color;check(!exact_auxiliary_layout(s,eyes,produced),"distinct native auxiliary and FloatZ target identities required");
    for(bool auxiliary:{false,true}){const auto plan=auxiliary?auxiliary_draw_plan():StereoDrawPlan{1,{0,1},{0,1},false};
        for(int rejected=-1;rejected<2;++rejected){std::uint32_t index=plan.original_index;bool started=false;unsigned calls=0;std::vector<unsigned> drawn,copied;
            const bool accepted=execute_stereo_draw_plan(plan,[&](std::uint32_t v){index=v;},[&]{++calls;if(index==2)drawn.push_back(2);drawn.push_back(index==2?1:index);},[&](unsigned eye){copied.push_back(eye);return static_cast<int>(eye)!=rejected;},&started);
            check(started&&calls==2&&index==plan.original_index,"both original native calls run exactly once and restore native selected index after any copy rejection");
            check(drawn==(auxiliary?std::vector<unsigned>{2,1,0}:std::vector<unsigned>{0,1}),"exact mapped auxiliary2/right1/left0 order and unchanged ordinary0/1 order");
            check(accepted==(rejected<0),"partial copy never reports a completed pair");check(copied.front()==plan.eyes[0]&&(copied.size()==1||copied.back()==plan.eyes[1]),"copy order follows actual produced eye and stops after first rejection");}
        bool started=false;std::uint32_t index=plan.original_index;unsigned calls=0,copies=0;
        check(!execute_stereo_draw_plan(plan,[&](std::uint32_t v){index=v;},[&]{++calls;},[&](unsigned){++copies;return true;},&started,false)&&calls==2&&!copies&&index==plan.original_index,"failed staging begin still runs native rendering and cannot copy or submit");
        calls=0;try{execute_stereo_draw_plan(plan,[&](std::uint32_t v){index=v;},[&]{++calls;throw std::runtime_error("native");},[](unsigned){return true;},&started);check(false,"native fault expected");}catch(const std::runtime_error&){check(calls==1&&index==plan.original_index,"C++ native fault forwards without a second native call and restores index");}}
    // Protected memory fixture: virtual address reservation is not a game/GPU
    // mapping. Sparse pages exactly contain the bounded provider reads.
    constexpr std::size_t extent=(auxiliary_read_extent+4095)&~std::size_t(4095);auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,extent,MEM_RESERVE,PAGE_NOACCESS));check(memory!=nullptr,"owned address reservation created");
    if(memory){const auto base=reinterpret_cast<std::uintptr_t>(memory);std::vector<std::uintptr_t> pages;
        auto commit=[&](std::uintptr_t offset,std::size_t bytes){for(auto p=(base+offset)&~std::uintptr_t(4095);p<base+offset+bytes;p+=4096){if(std::find(pages.begin(),pages.end(),p)==pages.end()){check(VirtualAlloc(reinterpret_cast<void*>(p),4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"only fixed fixture pages committed");pages.push_back(p);}}};
        commit(0x373708c,4);commit(0x3a5b5f8,4);commit(0x41eb1e8+22*20,40);
        for(unsigned slot=0;slot<2;++slot){commit(0x3740100+slot*0x186300+0x15d7a0,4);commit(0x3740100+slot*0x186300+0x16cc38,12);commit(0x420f730+slot*0xe380,3*0x38e0);}
        fill_memory(base,original);AuxiliaryViewLayout out{};check(read_auxiliary_layout(base,true,&out)&&out.frame==original.frame&&out.count==3&&out.slots[2].extra==1,"actual protected reader copies exact frontend layout");
        check(read_auxiliary_layout(base,false,&out)&&out.slots[1].scene==eyes[1],"same bounded backend copy route");
        fill_memory(base,original,1);check(read_auxiliary_layout(base,false,&out)&&out.buffer==base+0x38c6400&&out.slots[2].address==base+0x420f730+0xe380+2*0x38e0&&out.slots[2].extra==1,"second SMP allocation reaches its actual last extra-flag page");fill_memory(base,original);
        put(base+0x3740100+0x16cc3c,4u);check(!read_auxiliary_layout(base,false,&out)&&out==AuxiliaryViewLayout{},"four-view expansion rejected and output cleared");fill_memory(base,original);
        put(base+0x3740100+0x16cc40,base+0x420f734);check(!read_auxiliary_layout(base,false,&out),"noncanonical slot storage rejected before any slot dereference");fill_memory(base,original);
        put(base+0x3a5b5f8,base+0x3740104);check(!read_auxiliary_layout(base,false,&out),"noncanonical backend allocation rejected");fill_memory(base,original);
        DWORD old{};const auto guard=(base+0x420f730+2*0x38e0+0x38c4)&~std::uintptr_t(4095);check(VirtualProtect(reinterpret_cast<void*>(guard),4096,PAGE_NOACCESS,&old)!=0,"owned tail page becomes inaccessible");
        check(!read_auxiliary_layout(base,true,&out)&&out==AuxiliaryViewLayout{},"partial native copy fault yields no copied authority");VirtualProtect(reinterpret_cast<void*>(guard),4096,old,&old);
        check(read_auxiliary_layout(base,true,&out),"same reader recovers after protected copy fault");VirtualFree(memory,0,MEM_RELEASE);}
    check(!read_auxiliary_layout(0,true,&s)&&!read_auxiliary_layout(UINT32_MAX-8,true,&s)&&!read_auxiliary_layout(0x400000,true,nullptr),"null overflow and missing output fail without reads");
    check(!read_auxiliary_layout(UINT32_MAX-auxiliary_read_extent+1,true,&s)&&s==AuxiliaryViewLayout{},"exact second-store maximum read extent enforces arithmetic overflow boundary");
    check(!validate_auxiliary_native_guards(nullptr),"guard validation rejects non-game module");
    std::printf("auxiliary_view_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
