// SPDX-License-Identifier: GPL-3.0-only
#define BOVR_TEST_ORDINARY_EVENT_STATE 1
#include "../src/t5/ordinary_event_fx.cpp"
#include <cstdio>
namespace bovr {void trace(const char*,const char*,...)noexcept{}}
namespace bovr::t5 {
CommandAimPublication fixture_aim{};PreCommandSnapshot fixture_pre{};bool fixture_lookup=true;
bool exact_profile_aim_enabled()noexcept{return true;}
bool read_pre_command_snapshot(PreCommandSnapshot* p)noexcept{*p=fixture_pre;return true;}
bool read_native_command_aim_for_shot(const input::NativeCommandBytes& c,std::uintptr_t cg,CommandAimPublication* p,bool)noexcept{
    if(!fixture_lookup||c!=fixture_aim.command.bytes||cg!=fixture_aim.cg_identity)return false;*p=fixture_aim;return true;}
bool read_native_command_aim_for_effect(int time,std::uintptr_t cg,unsigned weapon,CommandAimPublication* p,EffectAimDiagnostic*)noexcept{
    int expected{};std::memcpy(&expected,fixture_aim.command.bytes.data(),4);
    if(!fixture_lookup||time!=expected||cg!=fixture_aim.cg_identity||weapon!=fixture_aim.weapon)return false;*p=fixture_aim;return true;}
}
namespace {
using namespace bovr::t5;using namespace bovr::t5::ordinary_event_fx;
unsigned checks{},failures{};constexpr std::uintptr_t cg=0x10000000,ps=cg+0x8a364,client=0x11000000,variant=0x12000000,def=0x12100000,snap=0x12200000;
template<class T>void put(std::uintptr_t at,T v){std::memcpy(reinterpret_cast<void*>(at),&v,sizeof v);}
void check(bool b,const char* why){++checks;if(!b){++failures;printf("FAIL %s\n",why);}}
bool map(std::uintptr_t a,std::size_t n){return VirtualAlloc(reinterpret_cast<void*>(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reinterpret_cast<void*>(a);}
void initialize(){
    event_runtime_ready=true;fixture_lookup=true;ledger={};prediction_depth=event_depth=0;active_event=nullptr;event_after_first_write_test=nullptr;
    SetEnvironmentVariableW(L"BOVR_ORDINARY_EVENT_FX",L"correct");
    put<unsigned>(0x2ff5354,cg);put<unsigned>(0x1a796f8+0x13c,client);put<unsigned>(0x1c03140,client);
    put<int>(0x2910164,9);put<int>(ps+0x1c4,100);put<unsigned char>(ps+0x144,3);
    put<unsigned>(0xbe19a8+12,variant);put<unsigned>(variant+8,def);put<int>(def+0x1c,0);put<int>(def+0x20,4);put<int>(def+0x30,0);put<unsigned char>(def+0x56a,0);
    put<unsigned>(cg+0x34,snap);put<unsigned>(snap+0x20,6);put<int>(ps,1000);put<unsigned short>(ps+0xe8,17);
    put<unsigned>(0x1a796f8+0x330,7);put<unsigned>(0x1a796f8+0x74,8);put<int>(0x1a796f8+0x340,9);
    put<Angles>(cg+0xa9ce4,{10,20});put<unsigned char>(cg+0x8bd7c+0x238,3);put<unsigned short>(cg+0x8bd7c+0x220,12);
    fixture_aim={};fixture_aim.cg_identity=cg;fixture_aim.weapon=3;fixture_aim.variant_identity=variant;fixture_aim.definition_identity=def;
    fixture_aim.command.native_thread=GetCurrentThreadId();fixture_aim.command.publication_serial=9;fixture_aim.command.gameplay_epoch=10;fixture_aim.invalidation_revision=11;
    const int time=1020;std::memcpy(fixture_aim.command.bytes.data(),&time,4);fixture_aim.command.bytes[0x18]=3;
    const unsigned packed=0x40002000;std::memcpy(fixture_aim.command.bytes.data()+0x20,&packed,4);
    fixture_pre={};fixture_pre.native_cg_identity=cg;fixture_pre.gameplay_epoch=10;
    put<unsigned>(0x2ff7980,ps);std::memcpy(reinterpret_cast<void*>(0x2ff7984),fixture_aim.command.bytes.data(),52);
}
void make_prediction(){
    std::uint32_t frame[16]{};frame[9]=0x8908ca;frame[10]=0x2ff7980;Prediction p{};prediction_before(frame,&p);
    check(p.admitted,"real prediction_before admits exact native command");
    put<int>(ps,1020);put<unsigned short>(ps+0xe8,18);put<unsigned>(ps+0xec+4,29);put<unsigned>(ps+0xfc+4,12);
    prediction_after(&p,false);Witness w{};check(ledger.find(cg,17,29,12,3,GetTickCount64(),&w)&&w.publication==9,"real prediction_after records only appended exact command event");
}
void fire_frame(std::uint32_t* saved){
    const auto b=reinterpret_cast<std::uintptr_t>(saved)+36;
    put<unsigned>(b,0x64b86b);put<unsigned>(b+4,0);put<unsigned>(b+8,cg+0x8bd7c);put<unsigned>(b+12,29);put<unsigned>(b+16,123);put<unsigned>(b+20,0);put<unsigned>(b+24,ps);put<unsigned>(b+28,0);
    put<unsigned>(b+0xec,0x78206f);put<unsigned>(b+0x110,0x56e892);put<unsigned>(b+0x128,0x5c3901);
    put<unsigned>(b+0x130,ps);put<unsigned>(b+0x134,cg+0xcc87c);put<int>(b+0x108,17);put<int>(b+0x10c,17);
}
void raise_between_fields(){RaiseException(0xe042bf02,0,0,nullptr);}
unsigned fixture_native_calls{},fixture_selected_calls{},fixture_stock_calls{},fixture_pmove_calls{};
bool fixture_nested{},fixture_raise{};
void capture_fx(void* p){__asm mov eax,p __asm fxsave [eax]}
void __cdecl fixture_fire(unsigned local,unsigned cent,unsigned event,unsigned tag,unsigned weapon,unsigned player,unsigned left){
    ++fixture_native_calls;check(local==0&&cent==cg+0x8bd7c&&event==29&&tag==123&&!weapon&&player==ps&&!left,"production dispatch keeps seven native args");
    const auto a=load<Angles>(cg+0xa9ce4);fixture_selected_calls+=a==Angles{45,90};fixture_stock_calls+=a==Angles{10,20};
    if(fixture_raise)RaiseException(0xe042bf03,0,0,nullptr);
    if(fixture_nested){fixture_nested=false;std::uint32_t nested[128]{};fire_frame(nested);alignas(16) unsigned char fx[512]{};capture_fx(fx);dispatch_fire(nested,fx);}
}
void __cdecl fixture_pmove(unsigned pm){++fixture_pmove_calls;check(pm==0x2ff7980,"production dispatch keeps pmove pointer");
    put<int>(ps,1020);put<unsigned short>(ps+0xe8,18);put<unsigned>(ps+0xec+4,29);put<unsigned>(ps+0xfc+4,12);}
bool run_fire_dispatch(std::uint32_t* frame,void* fx){__try{dispatch_fire(frame,fx);return false;}
    __except(GetExceptionCode()==0xe042bf03?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}}
}
int main(){
    if(!map(0x1a00000,0x1700000)||!map(0xbe0000,0x10000)||!map(cg,0x100000)||!map(client,0x10000)||
       !map(variant,0x10000)||!map(def,0x10000)||!map(snap,0x10000)){printf("fixed fixture allocation failed\n");return 2;}
    initialize();make_prediction();std::uint32_t frame[128]{};fire_frame(frame);Event e{};
    event_before(frame,&e);check(e.written==3&&load<Angles>(cg+0xa9ce4)==Angles{45,90},"real event scope selects witnessed packed aim");
    event_after(&e,false);check(load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!e.written&&!event_depth,"real normal exit restores both owned fields");
    put<int>(ps,1040);Event batch{};event_before(frame,&batch);check(batch.written==3&&batch.witness.command==fixture_aim.command.bytes,"batch final time does not replace authored command");event_after(&batch,false);
    Event outer{},nested{};event_before(frame,&outer);event_before(frame,&nested);
    check(load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!outer.written&&!nested.written,"nested entry cancels outer borrow before native event");event_after(&nested,false);event_after(&outer,false);
    Event changed{};event_before(frame,&changed);put<float>(cg+0xa9ce8,123);event_after(&changed,false);
    check(load<Angles>(cg+0xa9ce4)==Angles{10,123},"native changed yaw retained and still-owned pitch restored");put<Angles>(cg+0xa9ce4,{10,20});
    Event fault{};event_after_first_write_test=&raise_between_fields;event_before(frame,&fault);
    check(load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!fault.written,"fault after first CAS cancels partial borrow before original");event_after(&fault,false);event_after_first_write_test=nullptr;
    for(unsigned bad=0;bad<6;++bad){initialize();make_prediction();fire_frame(frame);
        if(bad==0)put<unsigned>(reinterpret_cast<std::uintptr_t>(frame)+36+0xec,0x7820e3);
        if(bad==1)put<unsigned>(ps+0xfc+4,13);
        if(bad==2)fixture_lookup=false;
        if(bad==3)++fixture_aim.invalidation_revision;
        if(bad==4)put<unsigned>(0x1a796f8+0x330,99);
        if(bad==5)put<unsigned char>(def+0x56a,1);
        Event denied{};event_before(frame,&denied);check(!denied.written&&load<Angles>(cg+0xa9ce4)==Angles{10,20},"unknown/changed event owner retains native fields");event_after(&denied,false);
    }
    initialize();make_prediction();fire_frame(frame);SetEnvironmentVariableW(L"BOVR_ORDINARY_EVENT_FX",L"observe");Event observed{};event_before(frame,&observed);
    check(!observed.written&&observed.witness.publication==9&&load<Angles>(cg+0xa9ce4)==Angles{10,20},"observe production path correlates with zero writes");event_after(&observed,false);
    initialize();std::uint32_t pframe[16]{};pframe[9]=0x8908ca;pframe[10]=0x2ff7980;Prediction first{},second{};
    prediction_before(pframe,&first);prediction_before(pframe,&second);prediction_after(&second,false);
    put<int>(ps,1020);put<unsigned short>(ps+0xe8,18);put<unsigned>(ps+0xec+4,29);put<unsigned>(ps+0xfc+4,12);prediction_after(&first,false);
    Witness none{};check(!ledger.find(cg,17,29,12,3,GetTickCount64(),&none),"nested prediction cannot publish outer aggregate as one command");
    initialize();alignas(16) unsigned char fx[512]{};capture_fx(fx);original_pmove=reinterpret_cast<void*>(&fixture_pmove);
    dispatch_pmove(pframe,fx);Witness joined{};
    check(fixture_pmove_calls==1&&ledger.find(cg,17,29,12,3,GetTickCount64(),&joined)&&!prediction_depth,"real before/native/after Pmove composition publishes witnessed event once");
    original_fire=reinterpret_cast<void*>(&fixture_fire);fire_frame(frame);capture_fx(fx);run_fire_dispatch(frame,fx);
    check(fixture_native_calls==1&&fixture_selected_calls==1&&load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!event_depth,"production dispatch calls native once with borrowed angles then restores");
    fixture_nested=true;fire_frame(frame);capture_fx(fx);run_fire_dispatch(frame,fx);
    check(fixture_native_calls==3&&fixture_selected_calls==2&&fixture_stock_calls==1&&load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!event_depth,"production nested dispatch cancels before inner native event and restores outer lifetime");
    fixture_raise=true;fire_frame(frame);capture_fx(fx);check(run_fire_dispatch(frame,fx),"production native SEH propagates to caller");
    check(fixture_native_calls==4&&fixture_selected_calls==3&&load<Angles>(cg+0xa9ce4)==Angles{10,20}&&!event_depth&&!active_event,"production finally restores on native SEH without replay");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
