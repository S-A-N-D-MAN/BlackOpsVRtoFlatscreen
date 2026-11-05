// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_event_fx.hpp"
#include <atomic>
#if !defined(BOVR_TEST_ORDINARY_EVENT_ABI)
#include "t5/dual_pistol_effects.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/identity.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#endif
#if !defined(_M_IX86)
#error The native event bridges require MSVC x86.
#endif
namespace bovr::t5 { namespace {
void* original_pmove{};void* original_fire{};
#if !defined(BOVR_TEST_ORDINARY_EVENT_ABI)
using namespace ordinary_event_fx;
std::atomic<bool> event_runtime_ready{};
template<class T>T load(std::uintptr_t p)noexcept{T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof v);return v;}
struct Owner {
    std::uintptr_t cg{},ps{},client{},variant{},definition{},snap{};
    unsigned weapon{},use{},network_use{};int birth{},command_time{};
    primary_weapon_tracking::Player player{};primary_weapon_tracking::View view{};
    Angles angles{};friend bool operator==(const Owner&,const Owner&)=default;
};
bool read_owner(Owner* out)noexcept{
    __try {
        Owner o{};o.cg=load<unsigned>(0x2ff5354);
        if(o.cg<0x10000||(o.cg&3)||o.cg>UINT32_MAX-0xcc8b0)return false;o.ps=o.cg+0x8a364;
        o.client=load<unsigned>(0x1a796f8+0x13c);
        if(o.client<0x10000||o.client>UINT32_MAX-0x1c40||o.client!=load<unsigned>(0x1c03140)||
           load<int>(0x1a796f8)||load<unsigned char>(o.ps+0x130)||load<unsigned char>(o.client+0x130))return false;
        o.player={load<int>(o.ps+4),load<int>(o.ps+0x1c4),load<unsigned>(o.ps+0xc),load<unsigned>(o.ps+0x10),load<unsigned>(o.ps+0xe0),load<unsigned>(o.ps+0xe4)};
        o.view={load<int>(o.cg+0x8a360),load<int>(0x2910160),load<int>(0x2910164),load<int>(0x2911e2c)!=0,load<int>(o.cg+0xc)!=0,load<int>(o.cg+0xa9d44)!=0};
        if(primary_weapon_tracking::client_mode(o.player,o.view)!=primary_weapon_tracking::Mode::ordinary)return false;
        o.weapon=load<unsigned char>(o.ps+0x144);if(!o.weapon)return false;
        o.variant=load<unsigned>(0xbe19a8+4*o.weapon);if(o.variant<0x10000||o.variant>UINT32_MAX-0x50)return false;
        o.definition=load<unsigned>(o.variant+8);if(o.definition<0x10000||o.definition>UINT32_MAX-0x598||
           !dual_pistol_effects::ordinary_weapon(load<int>(o.definition+0x1c),load<int>(o.definition+0x20),load<int>(o.definition+0x30),load<unsigned char>(o.definition+0x56a)))return false;
        o.snap=load<unsigned>(o.cg+0x34);if(o.snap<0x10000||o.snap>UINT32_MAX-0x140||!(load<unsigned>(o.snap+0x20)&6)||load<unsigned char>(o.snap+0x13c))return false;
        o.use=load<unsigned>(0x1a796f8+0x330);o.network_use=load<unsigned>(0x1a796f8+0x74);o.birth=load<int>(0x1a796f8+0x340);
        o.command_time=load<int>(o.ps);o.angles=load<Angles>(o.cg+0xa9ce4);
        if(o.command_time<0||!std::isfinite(o.angles.pitch)||!std::isfinite(o.angles.yaw))return false;
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool incarnation(const Owner& o,std::uint64_t epoch)noexcept{
    PreCommandSnapshot p{};if(!read_pre_command_snapshot(&p)||p.gameplay_epoch!=epoch||p.native_cg_identity!=o.cg)return false;
    __try{return load<unsigned>(0x2ff5354)==o.cg&&load<unsigned>(0x1a796f8+0x13c)==o.client&&
        load<unsigned>(0x1c03140)==o.client&&load<unsigned>(0x1a796f8+0x330)==o.use&&
        load<unsigned>(0x1a796f8+0x74)==o.network_use&&load<int>(0x1a796f8+0x340)==o.birth;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool publication(const Owner& o,const CommandAimPublication& p)noexcept{
    return !p.dual_wield&&!p.dual_weapon&&p.weapon==o.weapon&&p.cg_identity==o.cg&&
        p.variant_identity==o.variant&&p.definition_identity==o.definition&&p.command.native_thread==GetCurrentThreadId();
}
thread_local Ledger ledger;
thread_local unsigned prediction_depth{},event_depth{};thread_local bool prediction_poisoned{};
struct Prediction {Owner owner{};Witness witness{};std::uintptr_t pm{};unsigned sequence{};bool admitted{};};
void prediction_before(std::uint32_t* saved,Prediction* p)noexcept{
    ++prediction_depth;if(prediction_depth!=1){prediction_poisoned=true;return;}prediction_poisoned=false;
    if(!event_runtime_ready.load(std::memory_order_acquire)||mode()==Mode::disabled||!exact_profile_aim_enabled())return;
    Owner again{};CommandAimPublication aim{},second{};
    __try {
        const auto entry=reinterpret_cast<std::uintptr_t>(saved)+36;
        if(load<unsigned>(entry)!=0x8908ca||load<unsigned>(entry+4)!=0x2ff7980)return;
        p->pm=0x2ff7980;if(!read_owner(&p->owner)||load<unsigned>(p->pm)!=p->owner.ps)return;
        p->witness.command=load<input::NativeCommandBytes>(p->pm+4);
        p->sequence=load<unsigned short>(p->owner.ps+0xe8);if(p->sequence>255)return;
        if(!read_native_command_aim_for_shot(p->witness.command,p->owner.cg,&aim,true)||!publication(p->owner,aim)||
           !read_owner(&again)||again!=p->owner||!read_native_command_aim_for_shot(p->witness.command,p->owner.cg,&second,true)||
           second.command.publication_serial!=aim.command.publication_serial||second.invalidation_revision!=aim.invalidation_revision)return;
        auto& w=p->witness;w.cg=p->owner.cg;w.client=p->owner.client;w.weapon=p->owner.weapon;w.use=p->owner.use;
        w.network_use=p->owner.network_use;w.birth=p->owner.birth;w.epoch=aim.command.gameplay_epoch;w.revision=aim.invalidation_revision;
        w.publication=aim.command.publication_serial;w.observed=GetTickCount64();p->admitted=true;
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
void prediction_after(Prediction* p,bool abnormal)noexcept{
    if(p->admitted&&!abnormal&&prediction_depth==1&&!prediction_poisoned)__try{
        Owner o{};CommandAimPublication aim{};
        const auto time=load<int>(reinterpret_cast<std::uintptr_t>(p->witness.command.data()));
        if(read_owner(&o)&&o.cg==p->owner.cg&&o.client==p->owner.client&&o.weapon==p->owner.weapon&&
           o.variant==p->owner.variant&&o.definition==p->owner.definition&&o.command_time==time&&
           incarnation(p->owner,p->witness.epoch)&&load<unsigned>(p->pm)==o.ps&&
           !std::memcmp(reinterpret_cast<void*>(p->pm+4),p->witness.command.data(),52)&&
           read_native_command_aim_for_shot(p->witness.command,o.cg,&aim,true)&&publication(o,aim)&&
           aim.command.publication_serial==p->witness.publication&&aim.invalidation_revision==p->witness.revision){
            const auto after=load<unsigned short>(o.ps+0xe8);const auto count=appended(p->sequence,after);
            for(unsigned i=0;i<count;++i){auto w=p->witness;w.sequence=(p->sequence+i)&255u;
                w.event=load<unsigned>(o.ps+0xec+4*(w.sequence&3));w.parm=load<unsigned>(o.ps+0xfc+4*(w.sequence&3));
                if(w.event==29||w.event==30){ledger.add(w);static std::atomic<unsigned> n{};const auto serial=++n;
                    if(serial<=64||serial%120==0)trace("t5_ordinary_predicted_event","count=%u sequence=%u event=%u parm=%u weapon=%u command_time=%d publication=%llu writes=0 native_calls_added=0",serial,w.sequence,w.event,w.parm,w.weapon,time,w.publication);}}
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    if(prediction_depth)--prediction_depth;
}
struct Event {Owner owner{};Witness witness{};Angles selected{};unsigned written{};Cursor cursor{};};
thread_local Event* active_event{};
#if defined(BOVR_TEST_ORDINARY_EVENT_STATE)
void(*event_after_first_write_test)(){};
#endif
unsigned restore(Event* e)noexcept{
    if(!e||!e->written)return 0;const auto written=e->written;e->written=0;unsigned restored{};
    __try{if(incarnation(e->owner,e->witness.epoch)){
        LONG before[2]{},selected[2]{};std::memcpy(before,&e->owner.angles,8);std::memcpy(selected,&e->selected,8);
        for(unsigned i=0;i<2;++i)if(written&(1u<<i))if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(e->owner.cg+0xa9ce4+4*i),before[i],selected[i])==selected[i])restored|=1u<<i;}
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return restored;
}
void event_attempt(std::uint32_t* saved,Event* e,unsigned& reason)noexcept{
    if(!event_runtime_ready.load(std::memory_order_acquire))return;
    const auto selected_mode=mode();if(selected_mode!=Mode::observe&&selected_mode!=Mode::correct)return;
    const auto entry=reinterpret_cast<std::uintptr_t>(saved)+36;reason=1;
    __try {
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        if(entry<low||entry>high-0x138||load<unsigned>(entry)!=0x64b86b||load<unsigned>(entry+4)||load<unsigned>(entry+28)||!read_owner(&e->owner))return;
        const auto& o=e->owner;const auto cent=load<unsigned>(entry+8),event=load<unsigned>(entry+12);
        if(cent!=o.cg+0x8bd7c)return;
        const auto passed=load<unsigned>(entry+20);const auto effective=passed?passed:load<unsigned char>(cent+0x238);
        if(cent!=o.cg+0x8bd7c||load<unsigned>(entry+24)!=o.ps||!load<unsigned short>(entry+16)||
           load<int>(cent+0x164)||effective!=o.weapon)return;
        auto& c=e->cursor;c.fire_return=load<unsigned>(entry);c.entity_return=load<unsigned>(entry+0xec);
        c.check_return=load<unsigned>(entry+0x110);c.transition_return=load<unsigned>(entry+0x128);
        c.ps=load<unsigned>(entry+0x130);c.expected_ps=o.ps;c.ops=load<unsigned>(entry+0x134);c.expected_ops=o.cg+0xcc87c;
        c.cursor=load<int>(entry+0x108);c.previous=load<int>(entry+0x10c);c.sequence=load<unsigned short>(o.ps+0xe8);
        c.event=event;c.parm=load<unsigned short>(cent+0x220);c.ring_event=load<unsigned>(o.ps+0xec+4*(c.cursor&3));c.ring_parm=load<unsigned>(o.ps+0xfc+4*(c.cursor&3));
        reason=2;if(!predicted_cursor(c))return;
        reason=3;if(!ledger.find(o.cg,unsigned(c.cursor)&255u,event,c.ring_parm,o.weapon,GetTickCount64(),&e->witness))return;
        const auto& w=e->witness;
        if(w.client!=o.client||w.use!=o.use||w.network_use!=o.network_use||w.birth!=o.birth)return;
        int time{};std::memcpy(&time,w.command.data(),4);CommandAimPublication aim{},again{};Owner second{};
        reason=4;if(!read_native_command_aim_for_effect(time,o.cg,o.weapon,&aim)||!publication(o,aim)||
           aim.command.bytes!=w.command||aim.command.publication_serial!=w.publication||aim.invalidation_revision!=w.revision||
           !read_owner(&second)||second!=o||!read_native_command_aim_for_effect(time,o.cg,o.weapon,&again)||
           again.command.publication_serial!=w.publication||again.invalidation_revision!=w.revision)return;
        std::uint32_t packed{};std::memcpy(&packed,w.command.data()+0x20,4);e->selected=decode(packed);
        if(selected_mode==Mode::correct){LONG before[2]{},selected[2]{};std::memcpy(before,&o.angles,8);std::memcpy(selected,&e->selected,8);
            for(unsigned i=0;i<2;++i)if(before[i]!=selected[i]){
                const auto observed=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(o.cg+0xa9ce4+4*i),selected[i],before[i]);
                if(observed!=before[i]){reason=6;restore(e);return;}e->written|=1u<<i;
#if defined(BOVR_TEST_ORDINARY_EVENT_STATE)
                if(event_after_first_write_test)event_after_first_write_test();
#endif
            }}
        reason=0;static std::atomic<unsigned> count{};const auto n=++count;
        if(n<=64||n%120==0)trace("t5_ordinary_event_fx","count=%u sequence=%u event=%u parm=%u weapon=%u command_time=%d batch_time=%d publication=%llu packed=0x%08x original=%.6f,%.6f selected=%.6f,%.6f writes=%u native_calls_added=0",
            n,w.sequence,event,w.parm,w.weapon,time,o.command_time,w.publication,packed,o.angles.pitch,o.angles.yaw,e->selected.pitch,e->selected.yaw,4u*((e->written&1)!=0)+4u*((e->written&2)!=0));
    }__except(EXCEPTION_EXECUTE_HANDLER){reason=5;restore(e);}
}
void event_before(std::uint32_t* saved,Event* e)noexcept{
    ++event_depth;if(event_depth!=1){restore(active_event);return;}active_event=e;
    unsigned reason{};event_attempt(saved,e,reason);
    if(reason&&e->owner.cg){static std::atomic<unsigned> counts[7]{};const auto n=++counts[reason];
        if(n<=32||n%120==0)trace("t5_ordinary_event_fx_rejected","phase=%u count=%u weapon=%u batch_time=%d entity_return=%08x check_return=%08x transition_return=%08x cursor=%d previous=%d sequence=%u event=%u ring_event=%u parm=%u ring_parm=%u writes=0",reason,n,e->owner.weapon,e->owner.command_time,
            unsigned(e->cursor.entity_return),unsigned(e->cursor.check_return),unsigned(e->cursor.transition_return),e->cursor.cursor,e->cursor.previous,e->cursor.sequence,e->cursor.event,e->cursor.ring_event,e->cursor.parm,e->cursor.ring_parm);}
}
void event_after(Event* e,bool abnormal)noexcept{const auto owned=e->written;const auto restored=restore(e);
    if(owned){static std::atomic<unsigned> count{};const auto n=++count;if(n<=64||n%120==0||abnormal)
        trace("t5_ordinary_event_fx_complete","count=%u sequence=%u publication=%llu owned_mask=%u restored_mask=%u abnormal=%u native_calls_added=0",n,e->witness.sequence,e->witness.publication,owned,restored,abnormal);}
    if(event_depth)--event_depth;if(!event_depth)active_event=nullptr;}
#else
void(__cdecl* test_before)(unsigned,std::uint32_t*){};
void(__cdecl* test_after)(unsigned,bool){};
struct Prediction{};struct Event{};
void prediction_before(std::uint32_t* s,Prediction*){if(test_before)test_before(1,s);}
void prediction_after(Prediction*,bool a){if(test_after)test_after(1,a);}
void event_before(std::uint32_t* s,Event*){if(test_before)test_before(7,s);}
void event_after(Event*,bool a){if(test_after)test_after(7,a);}
#endif
// Preserve the native input and output register/flag/FX images across the C++
// observers. The compiler-owned dispatch __finally performs real SEH cleanup.
#define BRIDGE_HEAD \
    __asm push ebp __asm push ebx __asm push esi __asm push edi \
    __asm mov eax,[esp+20]
#define BRIDGE_RESTORE_INPUT \
    __asm fxrstor [edx] __asm push dword ptr [eax+32] __asm popfd \
    __asm mov edi,[eax] __asm mov esi,[eax+4] __asm mov ebp,[eax+8] __asm mov ebx,[eax+16] \
    __asm mov edx,[eax+20] __asm mov ecx,[eax+24] __asm mov eax,[eax+28]
#define BRIDGE_TAIL \
    __asm pushfd __asm pushad __asm mov esi,esp __asm mov edi,[esp+56] __asm mov ecx,9 __asm cld __asm rep movsd \
    __asm mov eax,[esp+60] __asm fxsave [eax] __asm lea esp,[esp+36] \
    __asm pop edi __asm pop esi __asm pop ebx __asm pop ebp __asm ret
__declspec(naked) void __cdecl invoke_pmove(std::uint32_t*,void*){
    BRIDGE_HEAD
    __asm push dword ptr [eax+40]
    __asm mov edx,[esp+28]
    BRIDGE_RESTORE_INPUT
    __asm call dword ptr [original_pmove]
    __asm lea esp,[esp+4]
    BRIDGE_TAIL
}
__declspec(naked) void __cdecl invoke_fire(std::uint32_t*,void*){
    BRIDGE_HEAD
    __asm push dword ptr [eax+64]
    __asm push dword ptr [eax+60]
    __asm push dword ptr [eax+56]
    __asm push dword ptr [eax+52]
    __asm push dword ptr [eax+48]
    __asm push dword ptr [eax+44]
    __asm push dword ptr [eax+40]
    __asm mov edx,[esp+52]
    BRIDGE_RESTORE_INPUT
    __asm call dword ptr [original_fire]
    __asm lea esp,[esp+28]
    BRIDGE_TAIL
}
void __cdecl dispatch_pmove(std::uint32_t* saved,void* fx){
    Prediction p{};const auto error=GetLastError();prediction_before(saved,&p);SetLastError(error);
    __try{invoke_pmove(saved,fx);}__finally{const auto after=GetLastError();prediction_after(&p,AbnormalTermination()!=FALSE);SetLastError(after);}
}
void __cdecl dispatch_fire(std::uint32_t* saved,void* fx){
    Event e{};const auto error=GetLastError();event_before(saved,&e);SetLastError(error);
    __try{invoke_fire(saved,fx);}__finally{const auto after=GetLastError();event_after(&e,AbnormalTermination()!=FALSE);SetLastError(after);}
}
#define DETOUR(BODY) \
    __asm pushfd __asm pushad __asm mov ebp,esp __asm sub esp,528 __asm and esp,0xfffffff0 \
    __asm fxsave [esp] __asm cld __asm mov eax,esp __asm push eax __asm push ebp __asm call BODY \
    __asm lea esp,[esp+8] __asm fxrstor [esp] __asm mov esp,ebp __asm popad __asm popfd __asm ret
__declspec(naked) void pmove_detour(){DETOUR(dispatch_pmove)}
__declspec(naked) void fire_detour(){DETOUR(dispatch_fire)}
}
#if defined(BOVR_TEST_ORDINARY_EVENT_ABI)
void* ordinary_event_test_entry(unsigned argc,void* native,void(__cdecl* before)(unsigned,std::uint32_t*),void(__cdecl* after)(unsigned,bool))noexcept{
    test_before=before;test_after=after;if(argc==1){original_pmove=native;return reinterpret_cast<void*>(&pmove_detour);}original_fire=native;return reinterpret_cast<void*>(&fire_detour);
}
#else
namespace {
struct Span{std::uintptr_t address;unsigned bytes;const char* hash;};
constexpr Span spans[]{
 {0x890880,0x63,"d01d3d4e733fb9923c04ddfbd3430d5a67ef8341d60d3d811dac86fd35363b43"},
 {0x464440,0x53,"73f5e4dfc79e3187713d052c40f6ea69baa437199a4007c22249d6759fab425e"},
 {0x656260,0x346,"6f3c00b5d8f8baf0384adb1935f74a7a0b0685276312dade694ebc9f0894cd61"},
 {0x64a8a0,0x58,"4ff840da64a72b7467574b49a28e424b153cc8206a38c51ece5a549c312afcf2"},
 {0x64b7e0,0x8e,"beb2ddb20de6c8c01469b68750a7f5682d81c4a5dfcf63c83fba6d265c54fc65"},
 {0x781ff0,0x156,"fddac8a0222bebcb4e1eb91fc2792c6d5855fc3b9c9e30e5f7618cef19d2b7dd"},
 {0x56e850,0x49,"8237cfbcd5ebbf7bde8c2975d63692c763f93bb75433f0f7832fffe047b9704e"},
 {0x5c38ed,0x17,"e15db13f07dc780b4d2bcb69530b33bf4a17112f656272cfed3b37b9b90f8ea8"},
 {0x799a90,0x286,"7bba148c68b5600be8ab20c6a9aaa03a26f6a4f32c97b5987c826c4a2320884e"}};
std::array<std::array<unsigned char,0x346>,9> copies{};
bool prepared{},installed{};ordinary_event_fx::Mode prepared_mode{};
unsigned char* trampoline{};unsigned char fire_patch[8]{};unsigned char pmove_patch[5]{};
void jump(unsigned char* out,std::uintptr_t from,std::uintptr_t to,unsigned char opcode)noexcept{
    out[0]=opcode;const auto d=static_cast<std::uint32_t>(to-from-5);std::memcpy(out+1,&d,4);
}
}
bool ordinary_event_fx_enabled()noexcept{return ordinary_event_fx::mode()!=ordinary_event_fx::Mode::disabled;}
bool prepare_ordinary_event_fx(HMODULE exe)noexcept{
    if(installed)return true;prepared=false;prepared_mode=ordinary_event_fx::mode();
    if(prepared_mode==ordinary_event_fx::Mode::disabled||exe!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(exe)!=0x400000)return false;
    try{
        std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(exe,reason))return false;
        for(unsigned i=0;i<std::size(spans);++i){const auto& s=spans[i];if(native_guard::compare(reinterpret_cast<void*>(s.address),s.bytes,s.hash))return false;
            std::memcpy(copies[i].data(),reinterpret_cast<void*>(s.address),s.bytes);if(!native_guard::matches(copies[i].data(),s.bytes,s.hash))return false;}
        if(!trampoline){trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,13,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!trampoline)return false;
            std::memcpy(trampoline,copies[2].data(),8);jump(trampoline+8,reinterpret_cast<std::uintptr_t>(trampoline+8),0x656268,0xe9);
            DWORD old{};if(!VirtualProtect(trampoline,13,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),trampoline,13)){
                VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return false;}}
        std::memset(fire_patch,0x90,8);jump(fire_patch,0x656260,reinterpret_cast<std::uintptr_t>(&fire_detour),0xe9);
        jump(pmove_patch,0x8908c5,reinterpret_cast<std::uintptr_t>(&pmove_detour),0xe8);prepared=true;return true;
    }catch(...){return false;}
}
bool commit_ordinary_event_fx()noexcept{
    if(installed)return true;if(!prepared)return false;
    __try{
        for(unsigned i=0;i<std::size(spans);++i)if(std::memcmp(reinterpret_cast<void*>(spans[i].address),copies[i].data(),spans[i].bytes))return false;
        const bool fire=prepared_mode!=ordinary_event_fx::Mode::witness;
        auto* pm=reinterpret_cast<unsigned char*>(0x8908c5);auto* cg=reinterpret_cast<unsigned char*>(0x656260);DWORD pm_old{},cg_old{},unused{};
        if(!VirtualProtect(pm,5,PAGE_EXECUTE_READWRITE,&pm_old))return false;
        if(fire&&!VirtualProtect(cg,8,PAGE_EXECUTE_READWRITE,&cg_old)){VirtualProtect(pm,5,pm_old,&unused);return false;}
        original_pmove=reinterpret_cast<void*>(0x464440);original_fire=trampoline;
        std::memcpy(pm,pmove_patch,5);if(fire)std::memcpy(cg,fire_patch,8);
        bool okay=FlushInstructionCache(GetCurrentProcess(),pm,5)!=FALSE;
        if(fire)okay=(FlushInstructionCache(GetCurrentProcess(),cg,8)!=FALSE)&&okay;
        if(!okay){std::memcpy(pm,copies[0].data()+0x45,5);if(fire)std::memcpy(cg,copies[2].data(),8);FlushInstructionCache(GetCurrentProcess(),pm,5);if(fire)FlushInstructionCache(GetCurrentProcess(),cg,8);}
        const bool rp=VirtualProtect(pm,5,pm_old,&unused)!=FALSE;const bool rc=!fire||VirtualProtect(cg,8,cg_old,&unused)!=FALSE;
        installed=okay&&rp&&rc;
        if(!installed){DWORD writable{};
            if(VirtualProtect(pm,5,PAGE_EXECUTE_READWRITE,&writable)){
                std::memcpy(pm,copies[0].data()+0x45,5);FlushInstructionCache(GetCurrentProcess(),pm,5);VirtualProtect(pm,5,pm_old,&unused);}
            if(fire&&VirtualProtect(cg,8,PAGE_EXECUTE_READWRITE,&writable)){
                std::memcpy(cg,copies[2].data(),8);FlushInstructionCache(GetCurrentProcess(),cg,8);VirtualProtect(cg,8,cg_old,&unused);}
            // A failed installation can leave a bridge reachable only if the
            // OS also refuses rollback. Disable every observer in that case;
            // both bridges still invoke their intact native target exactly once.
            prepared=false;
        }
        event_runtime_ready.store(installed,std::memory_order_release);return installed;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#endif
}
