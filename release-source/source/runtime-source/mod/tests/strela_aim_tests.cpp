// SPDX-License-Identifier: GPL-3.0-only
// Actual native-first consumer and copied live-owner policy, with bounded owned
// memory. No installed hook, executable-page patch, game or OpenXR runtime.
#include <Windows.h>
static ULONGLONG fixture_clock(){return 1000;}
#define GetTickCount64 fixture_clock
#define strela_aim_detail strela_aim_test_detail
#define configure_strela_aim configure_strela_aim_test_copy
#define ensure_strela_aim ensure_strela_aim_test_copy
#define strela_script_angles strela_script_angles_test_copy
#define read_pre_command_snapshot read_pre_command_snapshot_strela_test_copy
#define read_vr_command_input_snapshot read_vr_command_input_snapshot_strela_test_copy
#define command_input_generation_current command_input_generation_current_strela_test_copy
#define read_latest_native_command_aim_publication read_latest_native_command_aim_publication_strela_test_copy
#include "../src/t5/strela_aim.cpp"
#undef GetTickCount64
#undef strela_aim_detail
#undef configure_strela_aim
#undef ensure_strela_aim
#undef strela_script_angles
#undef read_pre_command_snapshot
#undef read_vr_command_input_snapshot
#undef command_input_generation_current
#undef read_latest_native_command_aim_publication
#include <cstdio>
#include <map>
#include <vector>
#include <functional>
#include <limits>

namespace d=bovr::t5::strela_aim_test_detail;
namespace s=bovr::t5::strela;
namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{},native_calls{},pre_calls{},generation_calls{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
bovr::t5::PreCommandSnapshot pre{};
bovr::t5::CommandAimPublication publication{};
xr::ActionSnapshot latest{};
bool pre_available=true,actions_available=true,publication_available=true,generation_current=true;
bool current_views=true,current_render=true;
std::function<void()> read_effect,generation_effect,native_effect;
std::map<std::uintptr_t,std::vector<std::uint8_t>> memory;
alignas(16) std::array<std::uint8_t,256> caller{};
constexpr std::uintptr_t cg=0x11000000,server=0x12000000,topdown=0x13000000,name=0x13001000;
constexpr std::uintptr_t variant=0x14000000,definition=0x14001000,weapon_name=0x14002000;
std::uintptr_t failed_read{};
const s::Axes stock{{1,0,0},{0,-1,0},{0,0,1}};
s::Call call{};
template<class T>void set(std::uintptr_t at,const T& value) {
    auto i=memory.upper_bound(at);--i;const auto offset=at-i->first;
    if(offset+sizeof value>i->second.size())std::abort();
    std::memcpy(i->second.data()+offset,&value,sizeof value);
}
bool read(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {
    if(failed_read&&at<=failed_read&&failed_read-at<size)return false;
    const auto stack=reinterpret_cast<std::uintptr_t>(caller.data());
    if(at>=stack&&at-stack+size<=caller.size()){std::memcpy(out,caller.data()+at-stack,size);return true;}
    auto i=memory.upper_bound(at);if(i==memory.begin())return false;--i;
    const auto offset=at-i->first;if(offset>i->second.size()||size>i->second.size()-offset)return false;
    std::memcpy(out,i->second.data()+offset,size);return true;
}
void __cdecl native(std::uintptr_t entity,float* forward,float* right,float* up) {
    ++native_calls;check(entity==call.entity,"native entity passed unchanged");
    check(reinterpret_cast<std::uintptr_t>(forward)==call.forward&&
        reinterpret_cast<std::uintptr_t>(right)==call.right&&reinterpret_cast<std::uintptr_t>(up)==call.up,
        "native ABI output pointers passed unchanged");
    std::memcpy(forward,&stock.forward,12);std::memcpy(right,&stock.right,12);std::memcpy(up,&stock.up,12);
    if(native_effect)native_effect();SetLastError(312);
}
void reset() {
    native_calls=pre_calls=generation_calls=0;failed_read=0;
    pre_available=actions_available=publication_available=generation_current=current_views=current_render=true;
    read_effect={};generation_effect={};native_effect={};memory.clear();caller.fill(0xa5);
    for(const auto [at,size]:std::initializer_list<std::pair<std::uintptr_t,std::size_t>>{
        {cg,0xaa000},{server,0x2000},{0x1a796f8,0x34c},{0x2ff5354,4},{0x1c03140,4},
        {0x251ca98,4},{0x251cac0,4},{0x2910160,8},{0x2911e2c,4},{0xbcd150,4},{topdown,0x100},{name,64},
        {0xbe19a8+30*4,4},{variant,0x100},{definition,0x700},{weapon_name,64}})
        memory[at]=std::vector<std::uint8_t>(size);
    set<std::uint32_t>(0x2ff5354,cg);set<std::uint32_t>(0x1c03140,server);set<std::uint32_t>(0x1a796f8+0x13c,server);
    set<int>(0x1a796f8+0x184,100);set<int>(server+0x1a2c,2);
    set<DWORD>(0x251ca98,GetCurrentThreadId());set<DWORD>(0x251cac0,GetCurrentThreadId()+1);
    set<int>(0x2910164,10);set<std::uint32_t>(0xbcd150,topdown);set<std::uint32_t>(topdown,name);
    set<int>(topdown+0x10,5);const char text[]="player_topDownCamMode";set(name,text);
    for(const auto ps:{cg+0x8a364,server}){
        set<int>(ps,5000);set<int>(ps+0x1c4,100);set<std::uint32_t>(ps+0x10,0x2000000);
        set<std::uint32_t>(ps+0xc,0x4000);set<std::uint32_t>(ps+0xe0,0x42);set<int>(ps+0x178,6);
        set<std::uint8_t>(ps+0x144,30);
    }
    set<int>(cg+0x8a348,5000);set<int>(cg+0x8c158,5000);
    set<std::uint32_t>(0xbe19a8+30*4,variant);set<std::uint32_t>(variant,weapon_name);
    set<std::uint32_t>(variant+8,definition);set<int>(variant+0x20,1);
    const char strela[]="strela_sp";set(weapon_name,strela);
    set<int>(definition+0x1c,2);set<int>(definition+0x20,6);set<int>(definition+0x474,35);
    set<int>(definition+0x478,2500);set<std::uint8_t>(definition+0x47c,1);
    pre={};pre.context.valid=true;pre.context.connection_state=10;pre.context.native_thread=GetCurrentThreadId();
    pre.sequence=10;pre.gameplay_epoch=3;pre.observed_ms=1000;pre.native_cg_identity=cg;pre.native_game_time=5000;
    pre.native_gameplay_fields_valid=pre.gameplay_active=true;
    d::ready.store(true);d::host={nullptr,read};d::original=native;d::selecting=false;
    publication={};publication.cg_identity=cg;publication.refdef_identity=cg+0x8c100;
    publication.command.bytes[0x18]=30;publication.command.gameplay_epoch=3;
    publication.command.gameplay_active=true;publication.command.publication_serial=1;
    auto& rig=publication.command.rig;rig.xr_generation=rig.anchor_generation=7;rig.published_ms=1000;
    rig.tracking_anchor.orientation.w=1;auto& frame=rig.frame;frame.frame_id=40;frame.actions.sequence=30;
    frame.actions.focused=true;frame.should_render=frame.views_valid=true;
    frame.predicted_display_time=10;frame.predicted_display_period=1;frame.head_center.orientation.w=1;
    for(auto& eye:frame.eyes)eye.pose.orientation.w=1;
    for(auto& hand:frame.actions.hands){
        hand.aim.active=hand.aim.orientation_valid=hand.aim.orientation_tracked=true;
        hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;
        hand.grip.orientation_valid=hand.grip.orientation_tracked=true;
    }
    publication.aim.pitch_degrees=-35;publication.aim.yaw_degrees=60;
    s::Axes expected{};s::axes(-35,60,&expected);publication.aim.forward=expected.forward;
    latest=frame.actions;
    const auto stack=reinterpret_cast<std::uintptr_t>(caller.data());
    call={0x812c3c,0x1a796f8,stack+0x20,stack+0x2c,stack+0x38,stack,stack,stack+caller.size(),GetCurrentThreadId()};
    const std::array<std::uint32_t,5> args{0x812c3c,0x1a796f8,static_cast<std::uint32_t>(call.forward),
        static_cast<std::uint32_t>(call.right),static_cast<std::uint32_t>(call.up)};
    std::memcpy(caller.data(),args.data(),sizeof args);
    const std::uint32_t parent=0x812ef4;std::memcpy(caller.data()+0x44,&parent,4);
}
s::Axes run() {
    d::invoke(call,reinterpret_cast<float*>(call.forward),reinterpret_cast<float*>(call.right),reinterpret_cast<float*>(call.up));
    s::Axes out{};std::memcpy(&out,reinterpret_cast<const void*>(call.forward),sizeof out);
    check(native_calls==1,"original native direction called exactly once");
    check(GetLastError()==312,"original last error preserved after selection");return out;
}
bool same(s::Axes a,s::Axes b){return !std::memcmp(&a,&b,sizeof a);}
void reject(const char* label,const std::function<void()>& mutate) {
    reset();mutate();const auto before=memory;const auto out=run();check(same(out,stock),label);
    if(!read_effect)check(memory==before,"native state unchanged on refusal");
}
}
namespace bovr::t5 {
bool read_pre_command_snapshot_strela_test_copy(PreCommandSnapshot* out) noexcept {
    ++pre_calls;if(pre_calls%2==0&&read_effect)read_effect();*out=pre;return pre_available;
}
bool read_latest_native_command_aim_publication_strela_test_copy(CommandAimPublication* out) noexcept {
    *out=publication;return publication_available;
}
bool read_vr_command_input_snapshot_strela_test_copy(xr::ActionSnapshot* out,std::uint64_t* generation,input::CommandRigSample* rig) noexcept {
    *out=latest;*generation=7;*rig=publication.command.rig;
    rig->frame.views_valid=current_views;rig->frame.should_render=current_render;return actions_available;
}
bool command_input_generation_current_strela_test_copy(const input::CommandRigSample&) noexcept {
    ++generation_calls;if(generation_calls==2&&generation_effect)generation_effect();return generation_current;
}
}
int main() {
    reset();d::bridge(call.entity,reinterpret_cast<float*>(call.forward),reinterpret_cast<float*>(call.right),reinterpret_cast<float*>(call.up));
    s::Axes bridge_output{};std::memcpy(&bridge_output,reinterpret_cast<const void*>(call.forward),sizeof bridge_output);
    check(native_calls==1&&same(bridge_output,stock)&&GetLastError()==312,
        "actual cdecl bridge forwards native ABI once and rejects foreign caller before reading owner");
    reset();const auto before=memory;const auto args=caller;const auto out=run();s::Axes wanted{};s::axes(-35,60,&wanted);
    check(same(out,wanted),"fresh paired Strela uses admitted controller axes in native target projection");
    check(memory==before,"native player states lock flags target radius ammo and health untouched");
    bool only_axes=true;for(unsigned i=0;i<caller.size();++i)if((i<0x20||i>=0x44)&&caller[i]!=args[i])only_axes=false;
    check(only_axes,"only thirty-six validated caller-owned axis bytes change");
    xr::Vec3f angles{};check(bovr::t5::strela_script_angles_test_copy(0,&angles)&&angles.x==-35&&angles.y==60&&angles.z==0,
        "script clearance uses same fresh controller angles");
    check(!bovr::t5::strela_script_angles_test_copy(1,&angles),"nonlocal script entity retains native angles");
    reset();const char flashpoint[]="flashpoint_strela_sp";set(weapon_name,flashpoint);
    check(same(run(),wanted),"exact flashpoint Strela variant supported");
    reset();publication.command.rig.published_ms=900;check(same(run(),wanted),"one hundred millisecond publication age accepted");
    reject("disabled retains native",[]{d::ready.store(false);});
    reject("nested callback retains native",[]{d::selecting=true;});
    reject("wrong return retains native",[]{++call.returned;});
    reject("wrong parent retains native",[]{caller[0x44]=0;});
    reject("wrong stack arguments retain native",[]{caller[4]=0;});
    reject("wrong stack range retains native",[]{call.high=call.stack+0x44;});
    reject("wrong thread retains native",[]{++call.thread;});
    reject("wrong weapon name retains native",[]{const char weapon[]="rpg_sp";set(weapon_name,weapon);});
    reject("wrong weapon type retains native",[]{set<int>(definition+0x1c,0);});
    reject("wrong weapon class retains native",[]{set<int>(definition+0x20,5);});
    reject("wrong firing mode retains native",[]{set<int>(definition+0x30,1);});
    reject("wrong capacity retains native",[]{set<int>(variant+0x20,2);});
    reject("nonlocking launcher retains native",[]{set<std::uint8_t>(definition+0x47c,0);});
    reject("bad lock radius retains native",[]{set<int>(definition+0x474,0);});
    reject("bad lock duration retains native",[]{set<int>(definition+0x478,-1);});
    reject("unterminated weapon retains native",[]{const std::array<char,24> bad{'x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x','x'};set(weapon_name,bad);});
    reject("client and server weapons differ retains native",[]{set<std::uint8_t>(server+0x144,31);});
    reject("dead client retains native",[]{set<int>(cg+0x8a364+0x1c4,0);});
    reject("dead server retains native",[]{set<int>(server+0x1c4,0);});
    reject("linked client retains native",[]{set<int>(cg+0x8a364+4,1);});
    reject("linked server retains native",[]{set<int>(server+4,1);});
    reject("parent link retains native",[]{set<std::uint32_t>(0x1a796f8+0x298,0x14000000);});
    reject("frozen prompt retains native",[]{set<std::uint32_t>(server+0xc,0x800);});
    reject("camera mode retains native",[]{set<int>(cg+0x8a360,4);});
    reject("extra camera rotation retains native",[]{set<float>(server+0x464,1);});
    reject("topdown camera retains native",[]{set<int>(topdown+0x18,1);});
    reject("native pause retains native",[]{set<int>(0x2910160,16);});
    reject("pre pause retains native",[]{pre.context.key_catcher=16;});
    reject("native override retains native",[]{set<int>(0x2911e2c,1);});
    reject("unreadable owner retains native",[]{failed_read=server+0x1c4;});
    reject("wrong client identity retains native",[]{set<std::uint8_t>(server+0x130,1);});
    reject("native ownership mismatch retains native",[]{set<std::uint32_t>(0x1c03140,server+4);});
    reject("missing pre retains native",[]{pre_available=false;});
    reject("stale pre retains native",[]{pre.observed_ms=749;});
    reject("epoch drift retains native",[]{read_effect=[] {++pre.gameplay_epoch;};});
    reject("persistent owner drift retains native",[]{read_effect=[] {static int health=99;set<int>(server+0x1c4,health--);};});
    reject("mismatched scene time retains native",[]{set<int>(cg+0x8a348,4999);});
    reject("missing publication retains native",[]{publication_available=false;});
    reject("wrong published weapon retains native",[]{publication.command.bytes[0x18]=29;});
    reject("old publication epoch retains native",[]{++publication.command.gameplay_epoch;});
    reject("wrong cg retains native",[]{++publication.cg_identity;});
    reject("wrong refdef retains native",[]{++publication.refdef_identity;});
    reject("stale tracking retains native",[]{publication.command.rig.published_ms=899;});
    reject("invalid direction retains native",[]{publication.aim.pitch_degrees=std::numeric_limits<float>::quiet_NaN();});
    reject("forward mismatch retains native",[]{publication.aim.forward.x=0;});
    reject("focus loss retains native",[]{latest.focused=false;});
    reject("new menu edge retains native",[]{latest.menu.active=latest.menu.current=true;});
    reject("missing current actions retains native",[]{actions_available=false;});
    reject("latest views invalid retains native",[]{current_views=false;});
    reject("latest render disabled retains native",[]{current_render=false;});
    reject("current right aim inactive retains native",[]{latest.hands[1].aim.active=false;});
    reject("current right aim invalid retains native",[]{latest.hands[1].aim.orientation_valid=false;});
    reject("current right aim tracking lost retains native",[]{latest.hands[1].aim.orientation_tracked=false;});
    reject("current right grip tracking lost retains native",[]{latest.hands[1].grip.position_tracked=false;});
    reject("current left grip inactive retains native",[]{latest.hands[0].grip.active=false;});
    reject("current left grip position invalid retains native",[]{latest.hands[0].grip.position_valid=false;});
    reject("current left grip position tracking lost retains native",[]{latest.hands[0].grip.position_tracked=false;});
    reject("current left grip orientation invalid retains native",[]{latest.hands[0].grip.orientation_valid=false;});
    reject("current left grip orientation tracking lost retains native",[]{latest.hands[0].grip.orientation_tracked=false;});
    reject("unjoined current action sample retains native",[]{++latest.sequence;});
    reject("recenter retains native",[]{generation_current=false;});
    reject("recenter on recheck retains native",[]{generation_effect=[] {generation_current=false;};});
    reset();native_effect=[] {pre.context.key_catcher=16;};check(same(run(),stock),"native callback transition precedes admission");
    reset();s::Descriptor descriptor{};
    check(s::read_descriptor(read,nullptr,30,&descriptor)&&descriptor.weapon==30,"descriptor double-copy accepts exact asset");
    failed_read=definition+0x47c;
    check(!s::read_descriptor(read,nullptr,30,&descriptor)&&descriptor.weapon==0,"failed descriptor clears previous identity");
    for(float pitch:{-80.f,-35.f,0.f,50.f,89.f})for(float yaw:{-179.f,-60.f,0.f,60.f,179.f}){
        s::Axes axes{};check(s::axes(pitch,yaw,&axes),"bounded controller angles accepted");
        const auto dot=[](xr::Vec3f a,xr::Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        check(std::fabs(dot(axes.forward,axes.right))<.0001f&&std::fabs(dot(axes.forward,axes.up))<.0001f&&
            std::fabs(dot(axes.right,axes.up))<.0001f&&std::fabs(dot(axes.forward,axes.forward)-1)<.0001f&&
            std::fabs(dot(axes.right,axes.right)-1)<.0001f&&std::fabs(dot(axes.up,axes.up)-1)<.0001f,
            "native target projection receives orthonormal camera basis");
    }
    std::printf("Strela script aim: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
