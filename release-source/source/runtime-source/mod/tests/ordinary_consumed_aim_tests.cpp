// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_consumed_aim.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include <cstdio>
#include <vector>
namespace {
bovr::t5::CommandAimPublication publication{};bool lookup=true,change_second{};unsigned lookups{};
}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool read_native_command_aim_for_shot(const input::NativeCommandBytes& bytes,std::uintptr_t cg,
                                    CommandAimPublication* out,bool allow) noexcept {
    ++lookups;if(!lookup||!allow||bytes!=publication.command.bytes||cg!=publication.cg_identity)return false;
    *out=publication;if(change_second&&lookups==2)++out->command.publication_serial;return true;
}
}
#include "../src/t5/ordinary_consumed_aim.cpp"
#define BOVR_TEST_NATIVE_GUARDS 1
#define BOVR_TEST_ORDINARY_CONSUMED_INTEGRATION 1
#include "../src/t5/consumed_command_probe.cpp"
namespace {
namespace aim=bovr::t5::ordinary_consumed_aim;
using bovr::t5::NativeConsumedCommand;
unsigned checks{},failures{};
void check(bool yes,const char* why){++checks;if(!yes){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
template<class T>T get(std::uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof v);return v;}
constexpr std::uintptr_t cg=0x10000000,client=0x12000000,server=0x13000000,
    variant=0x14000000,definition=0x15000000,dvar=0x16000000,inactivity=0x16000100,entity=0x1a796f8;
NativeConsumedCommand n{};std::array<unsigned char,52> source{};
const aim::Angles original{12.f,-27.f};
constexpr unsigned packed=0x40002000;
void fixture(){
    n={};n.image_base=0x400000;n.server_client_identity=server;n.entity_identity=entity;
    n.game_client_identity=client;n.source_command_identity=reinterpret_cast<std::uintptr_t>(source.data());
    n.scope_serial=42;n.observed_ms=123;n.native_thread=GetCurrentThreadId();n.server_state=4;n.command_time=2000;
    n.readable=n.local_identity=n.active=true;put<unsigned>(reinterpret_cast<std::uintptr_t>(n.command.data()),2000);
    n.command[0x18]=4;put<unsigned>(reinterpret_cast<std::uintptr_t>(n.command.data()+0x20),packed);source=n.command;
    std::memcpy(reinterpret_cast<void*>(server+0x10f38),source.data(),52);
    for(auto p:{cg+0x8a364,client}){put<int>(p+4,0);put<int>(p+0x1c4,100);put<unsigned>(p+0xc,0);put<unsigned>(p+0x10,0);put<unsigned>(p+0xe0,0);put<unsigned>(p+0xe4,0);put<unsigned char>(p+0x130,0);put<unsigned char>(p+0x144,4);put<int>(p+0x158,0);put<int>(p+0x15c,0);}
    put<unsigned>(0x2ff5354,cg);put<unsigned>(entity+0x13c,client);put<unsigned>(0x1c03140,client);put<unsigned>(0x286d01c,server);
    put<int>(server,4);put<int>(entity,0);put<unsigned char>(entity+0xd4,4);put<unsigned>(0xbe19a8+4*4,variant);put<unsigned>(variant+8,definition);
    put<int>(definition+0x1c,0);put<int>(definition+0x20,4);put<int>(definition+0x30,1);put<unsigned char>(definition+0x56a,0);
    put<int>(cg+0x8a360,0);put<int>(0x2910160,0);put<int>(0x2910164,10);put<int>(0x2911e2c,0);put<int>(cg+0xc,0);put<int>(cg+0xa9d44,0);
    put<int>(entity+0x184,100);put<int>(client+0x1a2c,2);put<int>(client+0x1a18,0);put<unsigned>(client+0x1c0c,0);put<unsigned>(entity+0x298,0);
    put<unsigned>(0xbcd150,dvar);put<int>(dvar+0x18,0);put<unsigned>(entity+0x330,7);put<unsigned>(entity+0x74,9);put<int>(entity+0x340,100);
    put<unsigned>(0x3023b00,inactivity);put<int>(inactivity+0x18,0);put<int>(0x1c04890,2000);
    put<int>(client,1900);put<aim::Angles>(client+0x1c34,original);
    publication={};publication.cg_identity=cg;publication.weapon=4;publication.variant_identity=variant;publication.definition_identity=definition;
    publication.command.bytes=n.command;publication.command.publication_serial=123;
    lookup=true;change_second=false;lookups=0;SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",L"correct");
}
void complete(bool returned=true){auto after=n;after.native_returned=returned;if(!returned)after.mismatches=bovr::t5::consumed_native_unwind;aim::after(after);}
void reject(void(*mutate)(),const char* reason){fixture();mutate();aim::before(n);check(get<aim::Angles>(client+0x1c34)==original,reason);complete(false);}
unsigned native_calls{},native_depth{};bool nested{},raise_native{},change_native{},retire_native{},native_saw_preload{},nested_saw_original{};
constexpr DWORD synthetic_exception=0xe042b041;
void invoke_integration();
void __cdecl fake_native(unsigned index){
    ++native_calls;++native_depth;check(index==0,"real bridge forwards original client index");
    if(native_depth==1)native_saw_preload=get<aim::Angles>(client+0x1c34)==aim::decode(packed);
    else nested_saw_original=get<aim::Angles>(client+0x1c34)==original;
    if(nested&&native_depth==1){invoke_integration();check(get<aim::Angles>(client+0x1c34)==original,"outer preload remains retired after nested return");}
    if(change_native)put<aim::Angles>(client+0x1c34,{32,64});
    if(retire_native){put<int>(client+4,9);put<int>(client+0x1c4,0);put<int>(client,1999);put<unsigned char>(client+0x144,5);}
    --native_depth;if(raise_native)RaiseException(synthetic_exception,0,0,nullptr);
    SetLastError(4321);
}
void invoke_integration(){
    std::uint32_t saved[19]{};alignas(16) unsigned char fx[512]{};
    saved[9]=0x513e5d;saved[12]=n.command_time;saved[17]=server;saved[18]=static_cast<unsigned>(n.source_command_identity);
    __asm {fxsave fx}
    bovr::t5::dispatch_preserved(saved,fx);
}
bool integration_exception(){
    __try {invoke_integration();}__except(GetExceptionCode()==synthetic_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
void integration_fixture(){fixture();native_calls=native_depth=0;nested=raise_native=change_native=retire_native=false;native_saw_preload=nested_saw_original=false;
    bovr::t5::consumed_base=0x400000;bovr::t5::consumed_original=reinterpret_cast<void*>(&fake_native);}
}
int main(){
    std::vector<void*> allocated;
    for(auto span:{std::pair<std::uintptr_t,std::size_t>{0x1a70000,0x10000},{0x1c00000,0x10000},{0x2860000,0x10000},{0x2910000,0x20000},{0x2ff0000,0x10000},{0x3020000,0x10000},{0xbc0000,0x10000},{0xbe0000,0x10000},{cg,0xb0000},{client,0x3000},{server,0x20000},{variant,0x1000},{definition,0x1000},{dvar,0x1000}}){
        void* p=VirtualAlloc(reinterpret_cast<void*>(span.first),span.second,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(p!=reinterpret_cast<void*>(span.first)){std::printf("fixture allocation failed %p\n",reinterpret_cast<void*>(span.first));return 2;}allocated.push_back(p);}
    fixture();aim::before(n);check(get<aim::Angles>(client+0x1c34)==original,"disabled before validated coordinator enable");aim::enable();
    check(aim::decode(0x40002000)==aim::Angles{45,90},"native SHORT2ANGLE quadrant");
    check(aim::decode(0x8000c000)==aim::Angles{-90,-180},"native signed angle wrap");
    fixture();aim::before(n);check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"exact consumed command preloads only gun angles");
    check(get<int>(client)==1900&&get<unsigned char>(client+0x144)==4&&get<int>(entity+0x184)==100,"command time weapon health untouched");
    put<int>(client,n.command_time);complete();check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"native normal decoded result retained");
    fixture();aim::before(n);put<aim::Angles>(client+0x1c34,{23,67});complete(false);check(get<aim::Angles>(client+0x1c34)==aim::Angles{23,67},"native changed fields survive exceptional return");
    fixture();aim::before(n);complete(false);check(get<aim::Angles>(client+0x1c34)==original,"unchanged exact preload rolls back on native unwind");
    fixture();aim::before(n);put<int>(client+4,9);put<int>(client+0x1c4,0);put<int>(client,1999);put<unsigned char>(client+0x144,5);complete();check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"normal native return wins even with changed post-native lifecycle and same angle bits");
    fixture();aim::before(n);put<float>(client+0x1c34,32);complete(false);check(get<aim::Angles>(client+0x1c34)==aim::Angles{32,original.yaw},"abnormal cancellation preserves changed native field and restores other owned field");
    fixture();aim::Owner owner{};check(aim::copy(n,&owner),"partial-store fixture proves real owner");
    aim::pending={owner,n,aim::decode(packed),123,0,true};put<float>(client+0x1c38,64);
    check(!aim::write_if_current(),"second-field CAS mismatch reports partial store");complete(false);
    check(get<aim::Angles>(client+0x1c34)==aim::Angles{original.pitch,64}&&!aim::pending.active,"partial store cancels only owned field and retires transaction");
    fixture();check(aim::copy(n,&owner),"same-selected failed CAS fixture proves owner");aim::pending={owner,n,aim::decode(packed),123,0,true};put<float>(client+0x1c38,aim::decode(packed).yaw);
    check(!aim::write_if_current(),"second field already selected by another writer is not owned");complete(false);
    check(get<aim::Angles>(client+0x1c34)==aim::Angles{original.pitch,aim::decode(packed).yaw},"failed same-selected CAS never restores an unowned field");
    fixture();aim::before(n);put<unsigned>(entity+0x330,8);complete(false);check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"retired incarnation is never written during cleanup");
    fixture();SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",L"observe");aim::before(n);check(get<aim::Angles>(client+0x1c34)==original,"observe performs no native writes");complete();
    reject([]{SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",L"off");},"off preserves native values");
    reject([]{n.active=false;},"inactive consumed scope rejected");
    reject([]{n.mismatches=1;},"scope mismatch rejected");
    reject([]{n.native_thread=0;},"wrong native thread rejected");
    reject([]{put<int>(client+4,9);},"death before entry rejected");
    reject([]{put<int>(entity+0x184,0);},"dead native entity rejected");
    reject([]{put<unsigned>(entity+0x298,0x17000000);},"linked native body rejected");
    reject([]{put<int>(0x2910160,1);},"menu ownership rejected");
    reject([]{put<unsigned char>(definition+0x56a,1);},"dual pistol excluded");
    reject([]{put<int>(definition+0x1c,2);},"projectile excluded");
    reject([]{put<int>(definition+0x30,5);},"stacked fan excluded");
    reject([]{put<int>(dvar+0x18,1);},"native forced-view branch excluded");
    reject([]{put<int>(inactivity+0x18,1);},"enabled native inactivity early return excluded");
    reject([]{put<int>(client,2000);},"duplicate equal command time rejected before preload");
    reject([]{put<int>(client,2001);},"older command time rejected before preload");
    reject([]{put<int>(0x1c04890,3001);},"native lower time clamp rejected before preload");
    reject([]{put<int>(0x1c04890,1799);},"native upper time clamp rejected before preload");
    for(int level:{1800,3000}){fixture();put<int>(0x1c04890,level);aim::before(n);check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"inclusive native time clamp boundary admitted");complete(false);}
    reject([]{source[7]^=1;},"exact command source reread required");
    reject([]{lookup=false;},"no retained publication rejected");
    reject([]{publication.definition_identity++;},"changed definition receipt rejected");
    reject([]{change_second=true;},"publication changes during recheck rejected");
    integration_fixture();invoke_integration();check(native_calls==1&&native_saw_preload,"actual consumed wrapper exposes exact preload to original once");
    check(get<aim::Angles>(client+0x1c34)==aim::decode(packed)&&!aim::pending.active&&!bovr::t5::scope.depth&&GetLastError()==4321,"normal integrated completion preserves native result and LastError and retires scope");
    integration_fixture();retire_native=true;invoke_integration();check(get<aim::Angles>(client+0x1c34)==aim::decode(packed),"integrated normal death/switch does not undo same-bit native result");
    integration_fixture();nested=true;invoke_integration();check(native_calls==2&&native_saw_preload&&nested_saw_original&&!aim::pending.active&&!bovr::t5::scope.depth,"nested native sees cancelled outer preload and neither scope rearms it");
    integration_fixture();raise_native=true;check(integration_exception(),"original native exception propagates unchanged through actual wrapper");
    check(native_calls==1&&native_saw_preload&&get<aim::Angles>(client+0x1c34)==original&&!aim::pending.active&&!bovr::t5::scope.depth,"actual compiler finally cancels owned preload and retires once on unwind");
    integration_fixture();raise_native=change_native=true;check(integration_exception(),"changed-value native exception propagates unchanged");check(get<aim::Angles>(client+0x1c34)==aim::Angles{32,64},"integrated unwind preserves native changed fields");
    integration_fixture();SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",L"observe");invoke_integration();check(native_calls==1&&!native_saw_preload&&get<aim::Angles>(client+0x1c34)==original,"integrated observe calls original once without writing native angles");
    integration_fixture();put<int>(client,2000);invoke_integration();check(native_calls==1&&!native_saw_preload&&get<aim::Angles>(client+0x1c34)==original,"integrated duplicate command reaches native unchanged");
    integration_fixture();invoke_integration();check(native_saw_preload&&!aim::pending.active&&!bovr::t5::scope.depth,"fresh command works after native faults and nested retirement");
    SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",nullptr);
    for(auto p:allocated)VirtualFree(p,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
