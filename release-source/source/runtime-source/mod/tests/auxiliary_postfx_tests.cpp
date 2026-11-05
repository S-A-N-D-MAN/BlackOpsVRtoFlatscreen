// SPDX-License-Identifier: GPL-3.0-only
#include "t5/auxiliary_postfx.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks{},failures{},lines{},truncated{};
std::array<std::array<char,1536>,200> logs{};
void check(bool b,const char* why){++checks;if(!b){++failures;std::printf("FAIL %s\n",why);}}
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
AuxiliaryViewLayout fill(Memory& m,unsigned slot){
    AuxiliaryViewLayout s{};s.buffer=m.base+0x3740100+slot*0x186300;s.view_array=m.base+0x420f730+slot*0xe380;s.frame=42;s.count=3;s.selected=2;
    m.write(m.base+0x3a5b5f8,s.buffer);m.write(s.buffer+0x15d7a0,s.frame);m.write(s.buffer+0x16cc38,s.selected);m.write(s.buffer+0x16cc3c,s.count);m.write(s.buffer+0x16cc40,s.view_array);
    for(unsigned i=0;i<3;++i){auto& v=s.slots[i];v.address=s.view_array+i*0x38e0;v.scene=v.display=v.scissor=i<2?bovr::render::HudEyeViewport{static_cast<int>(i)*640,0,640,720}:bovr::render::HudEyeViewport{0,0,768,432};
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
}
namespace bovr {
void trace(const char*,const char* fmt,...) noexcept {
    if(lines>=logs.size()){++truncated;return;}va_list args;va_start(args,fmt);
    const auto n=std::vsnprintf(logs[lines].data(),logs[lines].size(),fmt,args);va_end(args);
    if(n<0||static_cast<unsigned>(n)>=logs[lines].size())++truncated;++lines;SetLastError(0x9999);
}
}
int main(){
    {AuxiliaryPostfxSnapshot s{},out{};s.saved_times[0]=100;
        unsigned reads=0;check(copy_stable_auxiliary_postfx([&](auto* p){++reads;*p=s;return true;},&out)&&reads==2&&out==s,"actual reader join requires two equal owned copies");
        for(unsigned change=0;change<8;++change){reads=0;check(!copy_stable_auxiliary_postfx([&](auto* p){*p=s;if(++reads==2)switch(change){
            case 0:p->saved_times[0]++;break;case 1:p->saved_params[19]++;break;case 2:p->targets[0].image++;break;case 3:p->views[1].effects[20]++;break;
            case 4:p->source_scene_def[0]++;break;case 5:p->layout.frame++;break;case 6:p->source_target++;break;case 7:return false;}return true;},&out)&&out==AuxiliaryPostfxSnapshot{},"in-copy history/parameters/target/time/layout transitions or partial second copy reject without stale output");}}
    {AuxiliaryPostfxBudget b;check(!b.claim(0),"zero clock cannot acquire diagnostic attempt");
        for(unsigned i=0;i<16;++i){const auto now=1000+i*250;check(b.claim(now)==i+1,"only16 spaced attempts");
            check(!b.claim(now)&&!b.claim(now+249)&&!b.claim(now-1),"duplicates near timestamps and backwards clocks cannot spend/rearm");}
        for(unsigned i=0;i<100;++i)check(!b.claim(5000+i*1000),"finite budget remains exhausted across later frames");}
    for(unsigned timer:{0u,1u,2u,3u,4u,0xffffffffu})for(unsigned enabled=0;enabled<8;++enabled){AuxiliaryPostfxView v{};
        v.effects[4]=v.effects[11]=v.effects[19]=timer;v.effects[5]=enabled&1;v.effects[12]=(enabled>>1)&1;v.effects[20]=(enabled>>2)&1;
        auto parsed=auxiliary_postfx_effects(v);check(parsed.timer_outside_copied_history_mask==(timer<4?0:enabled),"timer interpretation bounded to copied history, no native indexing");}
    for(unsigned mode=0;mode<32;++mode)for(unsigned full:{0u,1u,0xffffffffu}){AuxiliaryPostfxView v{};v.rendering_mode=mode;v.full_screen=full;
        check(auxiliary_postfx_effects(v).full_filter==((mode&7)!=0||full!=0),"literal native full/split condition reproduced");}
    Memory m;check(m.base!=0,"fixture owns sparse address reservation");if(!m.base)return 1;
    for(unsigned slot=0;slot<2;++slot){auto layout=fill(m,slot);AuxiliaryPostfxSnapshot initial{};
        SetLastError(0x12345);check(read_auxiliary_postfx(m.base,layout,2,&initial)&&GetLastError()==0x12345,"protected reader copies exact slot and preserves LastError");
        check(initial.views[0].scene_def[0]==1000&&initial.views[1].scene_def[0]==1001&&initial.source_scene_def[0]==998,"native view and source times stay distinct");
        check(initial.views[1].effects[3]==0x2103&&initial.saved_params[19]==0x30000013&&initial.targets[0].image==0x11110001&&initial.targets[2].image==0x11110006,"both raw effect records, history tail and target1/6 copied exactly");
        auto snap=initial;m.write(m.base+0x356a32c+4,1234u);check(read_auxiliary_postfx(m.base,layout,2,&snap)&&snap.saved_times[1]==1234&&snap!=initial,"expected across-call history change is separate accepted observation");
        m.write(layout.buffer+0x16cc38,0u);check(read_auxiliary_postfx(m.base,layout,0,&snap)&&snap.layout.selected==0,"final selected0 stage retains same exact auxiliary ownership");
        check(!read_auxiliary_postfx(m.base,layout,2,&snap)&&snap==AuxiliaryPostfxSnapshot{},"wrong stage index clears output");m.write(layout.buffer+0x16cc38,2u);
        for(unsigned why=0;why<7;++why){auto expected=layout;switch(why){case 0:expected.frame++;break;case 1:expected.view_array+=4;break;
            case 2:expected.count=2;break;case 3:expected.slots[1].extra=1;break;case 4:expected.slots[0].scene.width--;break;
            case 5:expected.targets[0].image++;break;case 6:expected.buffer+=4;break;}
            check(!read_auxiliary_postfx(m.base,expected,2,&snap)&&snap==AuxiliaryPostfxSnapshot{},"stale native owner/layout/target cannot yield copied diagnostic");}
        const std::array<std::uintptr_t,7> faults{layout.slots[0].address+0x21cc,layout.slots[1].address+0x21cc,m.base+0x356a32c,
            m.base+0x3ccbfa8,m.base+0x417ff28,m.base+0x41eb1e8,layout.slots[2].address+0x38c4};
        for(auto address:faults){const auto page=address&~std::uintptr_t(4095);DWORD old{};check(VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old)!=0,"owned copied field page protected");
            SetLastError(0x8765);check(!read_auxiliary_postfx(m.base,layout,2,&snap)&&snap==AuxiliaryPostfxSnapshot{}&&GetLastError()==0x8765,"partial copy fault is contained and no prior data reused");
            DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored);check(read_auxiliary_postfx(m.base,layout,2,&snap),"reader recovers after owned page restored");}
        // Actual helper emits separate stage records around the exact plan.
        lines=0;truncated=0;const AuxiliaryPostfxKey key{1,123,2,44,GetCurrentThreadId()};unsigned calls=0,copies=0,stage=0;bool started=false;
        trace_auxiliary_postfx(m.base,layout,key,0);
        check(!execute_stereo_draw_plan(auxiliary_draw_plan(),[&](std::uint32_t v){m.write(layout.buffer+0x16cc38,v);},
            [&]{++calls;m.write(m.base+0x356a32c,1000+calls);trace_auxiliary_postfx(m.base,layout,key,++stage);},
            [&](unsigned){++copies;return false;},&started),"native copy failure remains failure with diagnostics enabled");
        // Copy failure returns false while both originals and their metadata
        // observations still run. Assert separately below; no rendering proof.
        check(calls==2&&copies==1&&stage==2&&lines==9&&!truncated,"failed first eye copy does not skip second native or diagnostic stage");
        unsigned index{};std::memcpy(&index,reinterpret_cast<void*>(layout.buffer+0x16cc38),4);check(index==2,"native selected index restored by existing plan");
        check(std::strstr(logs[0].data(),"stage=0")&&std::strstr(logs[3].data(),"stage=1")&&std::strstr(logs[6].data(),"stage=2"),"three distinct stage identities saved");
        const auto old_lines=lines;auto foreign=key;foreign.owner++;trace_auxiliary_postfx(m.base,layout,foreign,0);foreign=key;foreign.attempt=17;trace_auxiliary_postfx(m.base,layout,foreign,0);
        trace_auxiliary_postfx(m.base,layout,key,3);check(lines==old_lines,"foreign owner out-of-budget and invalid stage produce no diagnostic");
        SetLastError(0x45678);trace_auxiliary_postfx(m.base,layout,key,0);check(GetLastError()==0x45678,"whole trace helper preserves native LastError through logging");
        auto maximum=key;maximum.attempt=16;maximum.producer=maximum.generation=maximum.xr_frame=UINT64_MAX;
        trace_auxiliary_postfx(m.base,layout,maximum,0);check(!truncated,"maximum64-bit keys and full raw payloads remain below trace capacity");
        const auto page=(layout.slots[1].address+0x21cc)&~std::uintptr_t(4095);DWORD old{};VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old);
        const auto before=lines;trace_auxiliary_postfx(m.base,layout,key,0);check(lines==before+1&&std::strstr(logs[before].data(),"copied=0"),"failed snapshot reports failure without pretending zero fields are valid");
        DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored);
        calls=0;stage=0;try{execute_stereo_draw_plan(auxiliary_draw_plan(),[&](std::uint32_t v){m.write(layout.buffer+0x16cc38,v);},
            [&]{++calls;if(calls==1)throw std::runtime_error("original native fixture");trace_auxiliary_postfx(m.base,layout,key,++stage);},
            [](unsigned){return true;},&started);check(false,"original failure must propagate");}
        catch(const std::runtime_error&){std::memcpy(&index,reinterpret_cast<void*>(layout.buffer+0x16cc38),4);
            check(calls==1&&stage==0&&index==2,"original C++ failure forwards, no fake after record or second call, existing index restore retained");}
    }
    AuxiliaryPostfxSnapshot out{};auto layout=fill(m,0);
    check(!read_auxiliary_postfx(0,layout,2,&out)&&!read_auxiliary_postfx(UINT32_MAX-auxiliary_read_extent+1,layout,2,&out)&&!read_auxiliary_postfx(m.base,layout,1,&out)&&!read_auxiliary_postfx(m.base,layout,2,nullptr),"null overflow unknown stage and output reject without reads");
    check(!validate_auxiliary_postfx_guards(nullptr)&&!validate_auxiliary_postfx_guards(GetModuleHandleW(nullptr)),"synthetic fixture host never passes pinned native guards");
    std::printf("auxiliary_postfx_checks=%u failures=%u native_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;
}
