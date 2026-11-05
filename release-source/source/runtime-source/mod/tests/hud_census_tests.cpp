// SPDX-License-Identifier: GPL-3.0-only
#define observe_hud_commands test_observe_hud_commands
#include "../src/t5/hud_census.cpp"
#undef observe_hud_commands
#include <cstdlib>
using namespace bovr::t5;
namespace {unsigned checks{};void check(bool pass,const char* why){++checks;if(!pass){std::printf("FAIL %s\n",why);std::exit(1);}}
void commit(std::uintptr_t base,std::uintptr_t offset,std::size_t count){const auto first=(base+offset)&~std::uintptr_t(4095);const auto end=(base+offset+count+4095)&~std::uintptr_t(4095);
    check(VirtualAlloc(reinterpret_cast<void*>(first),end-first,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"synthetic bounded page committed");}
template<class T>void put(std::uintptr_t address,T value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}}
int main(){
    const auto base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4300000,MEM_RESERVE,PAGE_READWRITE));check(base!=0,"synthetic address range reserved");
    const auto bytes=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(bytes!=0,"synthetic command bytes allocated");
    commit(base,0x3a5b5f8,4);commit(base,0x3737090,48);commit(base,0x356a480,4);
    commit(base,0x3740100+0x15d7a0,4);commit(base,0x3740100+0x16cc34,0x100);
    const auto backend=base+0x3740100,array=base+0x3737090;
    put(base+0x3a5b5f8,backend);put(backend+0x16cc34,array);put(backend+0x15d7a0,std::uint32_t{77});put(backend+0x16cd24,bytes+16);put(base+0x356a480,1);
    Array good{static_cast<std::uint32_t>(bytes),64,8,0,0x18000,0x12000};put(array,good);
    NativeFinalUi ui{};ui.backend=backend;ui.commands=bytes+16;ui.native_frame=77;
    Snapshot out{};check(snapshot(base,ui,&out)&&out.array==array&&out.values==good,"exact backend-owned array accepted without using current frontend array");
    for(unsigned field=0;field<10;++field){auto bad=good;
        switch(field){case 0:bad.used=-1;break;case 1:bad.critical=-1;break;case 2:bad.critical=65;break;
            case 3:bad.used=bad.capacity+1;break;case 4:bad.capacity+=4;break;case 5:bad.bytes=0;break;
            case 6:bad.bytes+=1;break;case 7:bad.warning=bad.capacity+1;break;case 8:bad.bytes=0xfffffff0u;break;
            case 9:bad.capacity=0;break;}
        put(array,bad);check(!snapshot(base,ui,&out),"malformed owner metadata/range rejects before command copy");
        check(out.rejection_stage==4&&out.values==bad,"rejection diagnostics retain copied metadata without attempting command copy");}
    put(array,good);auto wrong=ui;++wrong.native_frame;check(!snapshot(base,wrong,&out),"older frame identity rejected");
    auto retired_counters=good;retired_counters.used=retired_counters.critical=0;retired_counters.last=0;put(array,retired_counters);
    check(snapshot(base,ui,&out)&&out.values.used==0,"native EndFrame-cleared counters still permit allocation-bounded passive census");
    put(array,good);
    wrong=ui;wrong.commands=bytes+static_cast<unsigned>(good.capacity);put(backend+0x16cd24,wrong.commands);
    check(!snapshot(base,wrong,&out),"even a matching published pointer outside allocation rejects");put(backend+0x16cd24,ui.commands);
    wrong=ui;wrong.commands+=4;check(!snapshot(base,wrong,&out),"final pointer must agree with native backend field");
    put(backend+0x16cc34,array+24);check(!snapshot(base,ui,&out),"other SMP slot array cannot masquerade as current backend");
    check(out.rejection_stage==2&&out.array==array+24&&out.expected_array==array&&out.values.bytes==0,
          "wrong-array diagnostic exposes both identities without reading the unknown array");put(backend+0x16cc34,array);
    put(base+0x356a480,0);check(!snapshot(base,ui,&out),"zero configured allocation count rejected");put(base+0x356a480,1);
    const auto views=base+0x420f730;commit(base,0x420f730,0x71c0);
    put(backend+0x16cc40,views);put(backend+0x16cc3c,std::uint32_t{2});
    put(views+0x2220,bytes+128);put(views+0x38e0+0x2220,bytes+512);
    const std::array<std::int32_t,4> scene_rect{0,0,640,720},display_rect{0,0,1280,720};
    put(views+0x140,scene_rect);put(views+0x150,display_rect);
    check(snapshot(base,ui,&out,0)&&out.view==views&&out.observed_commands==bytes+128&&out.scene_viewport[2]==640&&out.display_viewport[2]==1280,
          "per-view census uses verified view stride command pointer and distinct scene/display rectangles");
    check(snapshot(base,ui,&out,1)&&out.view==views+0x38e0&&out.observed_commands==bytes+512,
          "second native view selects its own commands rather than the shared final list");
    check(!snapshot(base,ui,&out,2),"view census rejects index outside published native count");
    put(backend+0x16cc40,views+0x38e0);check(!snapshot(base,ui,&out,0),"wrong view-array identity rejects before native view read");put(backend+0x16cc40,views);
    put(views+0x2220,bytes+static_cast<unsigned>(good.capacity));check(!snapshot(base,ui,&out,0),"view command pointer outside owned allocation rejects");put(views+0x2220,bytes+128);
    DWORD view_protection{};VirtualProtect(reinterpret_cast<void*>((views+0x2220)&~std::uintptr_t(4095)),4096,PAGE_NOACCESS,&view_protection);
    check(!snapshot(base,ui,&out,0),"unreadable view command field is contained");
    VirtualProtect(reinterpret_cast<void*>((views+0x2220)&~std::uintptr_t(4095)),4096,view_protection,&view_protection);
    unsigned char copy[4]{};check(copy_commands(bytes,copy,4),"bounded readable native bytes can be copied");
    DWORD old{};VirtualProtect(reinterpret_cast<void*>(bytes),4096,PAGE_NOACCESS,&old);
    check(!copy_commands(bytes,copy,4),"unreadable command byte copy is contained");VirtualProtect(reinterpret_cast<void*>(bytes),4096,old,&old);
    VirtualFree(reinterpret_cast<void*>(bytes),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::printf("hud_native_census_checks=%u failures=0 native_calls=0 game_writes=0\n",checks);
}
