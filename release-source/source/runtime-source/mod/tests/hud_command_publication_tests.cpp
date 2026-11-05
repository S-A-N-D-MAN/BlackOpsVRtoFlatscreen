// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hud_command_publication.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace {
using namespace bovr::t5;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
struct Fixture {
    std::uintptr_t base{},bytes{},buffer{},array{},views{};
    std::uint32_t frame{42};unsigned slot{};
    Fixture() {
        base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4300000,MEM_RESERVE,PAGE_READWRITE));
        bytes=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x18000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        check(base&&bytes,"isolated synthetic native address ranges reserved");reset(0);
    }
    ~Fixture(){VirtualFree(reinterpret_cast<void*>(bytes),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void store_bytes(std::uintptr_t address,const void* data,std::size_t size) {
        const auto first=address&~std::uintptr_t{4095};const auto last=(address+size+4095)&~std::uintptr_t{4095};
        if(address>=base&&address<base+0x4300000)check(VirtualAlloc(reinterpret_cast<void*>(first),last-first,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"synthetic metadata page committed");
        std::memcpy(reinterpret_cast<void*>(address),data,size);
    }
    template<class T>void store(std::uintptr_t address,T value){store_bytes(address,&value,sizeof(value));}
    void reset(unsigned smp) {
        slot=smp;buffer=base+0x3740100+slot*0x186300;array=base+0x3737090+slot*24;views=base+0x420f730+slot*0xe380;
        store(base+0x373708c,buffer);store(base+0x3a5b5f8,buffer);store(base+0x37370c0,array);
        store(base+0x356a480,1);store(buffer+0x15d7a0,frame);store(buffer+0x16cc34,array);store(buffer+0x16cc3c,2u);store(buffer+0x16cc40,views);
        store(base+0x356a641,std::uint8_t{1});store(buffer+0x16cd24,bytes+68);
        store(array,bytes);store(array+4,80);store(array+8,80);store(array+12,static_cast<std::uint32_t>(bytes+76));store(array+16,0x18000);store(array+20,0x12000);
        for(unsigned i=0;i<2;++i) {
            const auto v=views+i*0x38e0;store(v+0x2220,i?std::uintptr_t{}:bytes+16);
            bovr::render::HudEyeViewport viewport{static_cast<int>(i*640),0,640,720};
            store(v+0x140,viewport);store(v+0x150,viewport);store(v+0x38c4,std::uint8_t{});
        }
        std::memset(reinterpret_cast<void*>(bytes),0,0x18000);
        store(bytes+16,std::uint16_t{48});store(bytes+18,std::uint8_t{10});store(bytes+20,0x12345678u);
        store(bytes+24,100.0f);store(bytes+28,200.0f);store(bytes+32,1.0f);store(bytes+36,200.0f);store(bytes+40,100.0f);
        store(bytes+64,std::uint16_t{4});store(bytes+68,std::uint16_t{8});store(bytes+70,std::uint8_t{27});store(bytes+76,std::uint16_t{4});
    }
    void reset_native_counters(){store(array+4,0);store(array+8,0);store(array+12,0u);}
};
}
int main() {
    Fixture f;
    for(unsigned slot=0;slot<2;++slot) {
        f.reset(slot);HudCommandPublication publication{};
        check(capture_native_hud_publication(f.base,&publication),"post-terminator captures either exact native frontend slot");
        check(publication.bytes.size()==80&&publication.native.capacity==0x18000,"only finalized used extent copied, never allocation capacity");
        const auto list=published_hud_list(publication,0);
        check(list.size()==52&&list[2]==10&&list[50]==0,"per-view list extracted through its own native ID0 within published extent");
        check(published_hud_list(publication,1).empty()&&published_hud_list(publication,4).empty(),"null/right or out-of-range view cannot invent a list");
        check(match_native_hud_backend(f.base,publication,0),"matching native backend/storage/views/source bytes accepted");
        NativeHudFrame begin{};
        check(read_native_command_frame(f.base,true,&begin)&&begin==publication.native&&begin.renderer_active==1,"stable full source snapshot includes renderer state and shared pointer");
        check(published_shared_list(publication).size()==12&&match_native_shared_backend(f.base,publication),"owned final shared list and exact full native allocation match");
        f.reset_native_counters();
        check(match_native_hud_backend(f.base,publication,0),"documented backend counter reset does not erase finalized publication evidence");
        check(match_native_shared_backend(f.base,publication),"menu publication also survives documented zero counters");
        f.store(f.bytes,std::uint32_t{42});
        check(!match_native_shared_backend(f.base,publication),"even changed bytes outside shared list reject full allocation identity");
        f.store(f.bytes,std::uint32_t{0});f.store(f.buffer+0x16cd24,f.bytes+64);
        check(!match_native_shared_backend(f.base,publication),"changed shared command pointer rejects same-frame byte reuse");
        f.store(f.buffer+0x16cd24,f.bytes+68);
        HudCommandPublication missing{};check(!capture_native_hud_publication(f.base,&missing)&&missing.bytes.empty(),"pre-reset publication cannot be reconstructed from zero counters");
        f.store(f.bytes+24,101.0f);
        check(!match_native_hud_backend(f.base,publication,0)&&publication.bytes[24]==0,"changed native source rejects while owned published bytes remain immutable");
        f.store(f.bytes+24,100.0f);f.store(f.buffer+0x15d7a0,f.frame+1);
        check(!match_native_hud_backend(f.base,publication,0),"reused backend allocation with newer native frame rejects old publication");
        f.store(f.buffer+0x15d7a0,f.frame);f.store(f.views+0x2220,f.bytes+20);
        check(!match_native_hud_backend(f.base,publication,0),"same buffer/frame with changed view-list identity rejected");
        f.store(f.views+0x2220,f.bytes+16);f.store(f.views+0x150,bovr::render::HudEyeViewport{0,0,800,720});
        check(!match_native_hud_backend(f.base,publication,0),"same source bytes with changed display viewport rejected");
    }
    auto rejected=[&](const char* why){HudCommandPublication p;check(!capture_native_hud_publication(f.base,&p)&&p.bytes.empty(),why);};
    f.reset(0);f.store(f.array+12,0u);rejected("allocation-failed final terminator rejected by exact last-command identity");
    f.reset(0);f.store(f.bytes+78,std::uint8_t{10});rejected("last allocation record must really be ID0");
    f.reset(0);f.store(f.bytes+76,std::uint16_t{8});rejected("last ID0 must have the exact native four-byte form");
    f.reset(0);f.store(f.array+4,0x18004);rejected("used extent beyond native capacity rejected");
    f.reset(0);f.store(f.array+8,84);rejected("critical extent beyond used rejected");
    f.reset(0);f.store(f.array+8,77);rejected("critical byte extent must preserve native four-byte alignment");
    f.reset(0);f.store(f.array+4,79);f.store(f.array+8,76);f.store(f.array+12,static_cast<std::uint32_t>(f.bytes+75));
    f.store(f.bytes+75,std::uint16_t{4});f.store(f.bytes+77,std::uint8_t{0});rejected("odd finalized used extent rejected even with matching last pointer and apparent ID0");
    f.reset(0);f.store(f.array+16,0x20000);rejected("capacity must match exact configured native allocation formula");
    f.reset(0);f.store(f.base+0x37370c0,f.array+24);rejected("frontend current array must match this native slot");
    f.reset(0);f.store(f.buffer+0x16cc40,f.views+4);rejected("view-array ownership must match pinned native slot");
    f.reset(0);f.store(f.views+0x2220,f.bytes+0x18000);rejected("per-view source cannot point past its owned allocation");
    f.reset(0);f.store(f.array,std::uint32_t{0xfffffff0});rejected("overflowing allocation range rejected before dereference");
    f.reset(0);DWORD previous{};
    check(VirtualProtect(reinterpret_cast<void*>(f.bytes),0x18000,PAGE_NOACCESS,&previous)!=FALSE,"synthetic command storage protected");
    rejected("unreadable command extent fails closed without an access crash");
    DWORD unused{};VirtualProtect(reinterpret_cast<void*>(f.bytes),0x18000,previous,&unused);
    f.reset(0);HudCommandPublication publication{};check(capture_native_hud_publication(f.base,&publication),"capture recovers after isolated protection test");
    auto corrupt=publication;corrupt.bytes.pop_back();check(published_hud_list(corrupt,0).empty(),"owned copy length must match finalized metadata");
    corrupt=publication;corrupt.native.views[0].commands=corrupt.native.bytes+78;check(published_hud_list(corrupt,0).empty(),"unaligned owned list start rejected");
    corrupt=publication;corrupt.bytes[18]=29;check(published_hud_list(corrupt,0).empty(),"unknown native ID prevents list extraction");
    corrupt=publication;corrupt.native.shared_commands=corrupt.native.bytes+79;
    check(published_shared_list(corrupt).empty(),"unaligned/past-end shared pointer rejected");
    corrupt=publication;corrupt.bytes[70]=29;check(published_shared_list(corrupt).empty(),"unknown shared native ID rejected");
    corrupt=publication;corrupt.bytes[68]=6;check(published_shared_list(corrupt).empty(),"unaligned shared command length rejected");
    corrupt=publication;corrupt.bytes[78]=27;check(published_shared_list(corrupt).empty(),"missing shared terminator rejected within used boundary");
    corrupt=publication;corrupt.bytes[70]=25; // Native resource/callback class is legal for opaque pixels, never replayed here.
    check(published_shared_list(corrupt).size()==12,"opaque ownership parser is separate from HUD replay whitelist");
    check(!read_native_command_frame(f.base,true,nullptr),"null metadata output rejected");
    check(!capture_native_hud_publication(f.base,nullptr),"null output rejected");
    std::printf("hud_publication_checks=%u failures=0 native_calls=0 game_launches=0\n",checks);return 0;
}
