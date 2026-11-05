// SPDX-License-Identifier: GPL-3.0-only
// Actual production native-first consumer against bounded owned memory and a
// native stub. No installed hook, game, graphics device or OpenXR runtime.
#include <Windows.h>
static ULONGLONG fixture_clock(){return 1000;}
#define GetTickCount64 fixture_clock
#define native_use_gaze_detail native_use_gaze_test_detail
#define configure_native_use_gaze_adapter configure_native_use_gaze_adapter_test_copy
#define publish_native_use_gaze_scene publish_native_use_gaze_scene_test_copy
#define read_pre_command_snapshot read_pre_command_snapshot_use_test_copy
#define read_vr_command_input_snapshot read_vr_command_input_snapshot_use_test_copy
#define command_input_generation_current command_input_generation_current_use_test_copy
#include "../src/t5/native_use_gaze_adapter.cpp"
#undef GetTickCount64
#undef native_use_gaze_detail
#undef configure_native_use_gaze_adapter
#undef publish_native_use_gaze_scene
#undef read_pre_command_snapshot
#undef read_vr_command_input_snapshot
#undef command_input_generation_current
#include <cstdio>
#include <map>
#include <vector>
#include <functional>

namespace d=bovr::t5::native_use_gaze_test_detail;
namespace n=bovr::t5::native_use_gaze;
namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{},native_calls{},pre_calls{},generation_calls{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
bovr::t5::PreCommandSnapshot pre{};
xr::ActionSnapshot latest{};
bool pre_available=true,actions_available=true,generation_current=true;
bool current_views=true,current_render=true;
std::function<void()> read_effect,generation_effect,native_effect;
std::map<std::uintptr_t,std::vector<std::uint8_t>> memory;
alignas(16) std::array<std::uint8_t,256> caller{};
constexpr std::uintptr_t cg=0x11000000,server=0x12000000,topdown=0x13000000,name=0x13001000;
std::uintptr_t failed_read{};
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
    ++native_calls;check(entity==0x1a796f8,"native entity unchanged");
    check(!right&&!up,"native optional outputs unchanged");
    const xr::Vec3f stock{.514f,.074f,-.854f};std::memcpy(forward,&stock,12);
    if(native_effect)native_effect();SetLastError(312);
}
n::Call call{};
void reset() {
    native_calls=pre_calls=generation_calls=0;failed_read=0;
    pre_available=actions_available=generation_current=current_views=current_render=true;read_effect={};generation_effect={};native_effect={};
    memory.clear();caller.fill(0);
    for(const auto [at,size]:std::initializer_list<std::pair<std::uintptr_t,std::size_t>>{
        {cg,0xaa000},{server,0x2000},{0x1a796f8,0x34c},{0x2ff5354,4},{0x1c03140,4},
        {0x251ca98,4},{0x251cac0,4},{0x2910160,8},{0x2911e2c,4},{0xbcd150,4},{topdown,0x100},{name,64}})
        memory[at]=std::vector<std::uint8_t>(size);
    set<std::uint32_t>(0x2ff5354,cg);set<std::uint32_t>(0x1c03140,server);set<std::uint32_t>(0x1a796f8+0x13c,server);
    set<int>(0x1a796f8+0x184,100);set<int>(server+0x1a2c,2);
    set<DWORD>(0x251ca98,GetCurrentThreadId());set<DWORD>(0x251cac0,GetCurrentThreadId()+1);
    set<int>(0x2910164,10);set<std::uint32_t>(0xbcd150,topdown);set<std::uint32_t>(topdown,name);
    set<int>(topdown+0x10,5);const char text[]="player_topDownCamMode";set(name,text);
    for(const auto ps:{cg+0x8a364,server}){set<int>(ps,5000);set<int>(ps+0x1c4,100);set<std::uint32_t>(ps+0x10,0x2000000);}
    set<int>(cg+0x8a348,5000);set<int>(cg+0x8c158,5000);
    pre={};pre.context.valid=true;pre.context.connection_state=10;pre.context.native_thread=GetCurrentThreadId();
    pre.sequence=10;pre.gameplay_epoch=3;pre.observed_ms=1000;pre.native_cg_identity=cg;pre.native_game_time=5000;
    pre.native_gameplay_fields_valid=pre.gameplay_active=true;
    d::ready.store(true);d::host={nullptr,read};d::original=native;d::scene={};
    auto& scene=d::scene;scene.cg=cg;scene.refdef=cg+0x8c100;scene.epoch=3;scene.sequence=10;scene.native_time=5000;
    scene.stock={{1,2,3},{{1,0,0},{0,1,0},{0,0,1}}};
    auto& rig=scene.rig;rig.xr_generation=rig.anchor_generation=7;rig.published_ms=1000;
    rig.tracking_anchor.orientation.w=1;auto& frame=rig.frame;frame.frame_id=40;frame.actions.sequence=30;
    frame.actions.focused=true;frame.should_render=frame.views_valid=true;
    frame.predicted_display_time=10;frame.predicted_display_period=1;frame.head_center.orientation.w=1;
    for(auto& eye:frame.eyes)eye.pose.orientation.w=1;
    latest=frame.actions;
    const auto stack=reinterpret_cast<std::uintptr_t>(caller.data());
    call={0x817e32,0x1a796f8,stack+0x98,0,0,stack,stack,stack+caller.size(),GetCurrentThreadId()};
    const std::array<std::uint32_t,5> args{0x817e32,0x1a796f8,static_cast<std::uint32_t>(call.forward),0,0};
    std::memcpy(caller.data(),args.data(),sizeof args);
}
xr::Vec3f run() {
    d::invoke(call,reinterpret_cast<float*>(call.forward),nullptr,nullptr);
    xr::Vec3f out{};std::memcpy(&out,reinterpret_cast<const void*>(call.forward),sizeof out);
    check(native_calls==1,"original native direction called exactly once");
    check(GetLastError()==312,"original last error preserved after selection");return out;
}
bool same(xr::Vec3f a,xr::Vec3f b){return !std::memcmp(&a,&b,sizeof a);}
void reject(const char* label,const std::function<void()>& mutate) {
    reset();mutate();const auto before=memory;const auto out=run();
    check(same(out,{.514f,.074f,-.854f}),label);
    if(!read_effect)check(memory==before,"native state unchanged on refusal");
}
}
namespace bovr::t5 {
bool read_pre_command_snapshot_use_test_copy(PreCommandSnapshot* out) noexcept {
    ++pre_calls;if(pre_calls==2&&read_effect)read_effect();*out=pre;return pre_available;
}
bool read_vr_command_input_snapshot_use_test_copy(xr::ActionSnapshot* out,std::uint64_t* generation,input::CommandRigSample* rig) noexcept {
    *out=latest;*generation=7;*rig=d::scene.rig;rig->frame.views_valid=current_views;rig->frame.should_render=current_render;return actions_available;
}
bool command_input_generation_current_use_test_copy(const input::CommandRigSample&) noexcept {
    ++generation_calls;if(generation_calls==2&&generation_effect)generation_effect();return generation_current;
}
}
int main() {
    reset();const auto before=memory;const auto args=caller;const auto out=run();
    check(std::fabs(out.x-1)<.0001f&&std::fabs(out.y)<.0001f&&std::fabs(out.z)<.0001f,
        "level HMD replaces stale down-facing native use direction");
    check(memory==before,"player states camera weapon and trigger fields untouched");
    bool only_forward=true;for(unsigned i=0;i<caller.size();++i)if((i<0x98||i>=0xa4)&&caller[i]!=args[i])only_forward=false;
    check(only_forward,"only twelve caller-owned forward bytes change");
    reject("disabled retains native",[]{d::ready.store(false);});
    reject("wrong callsite retains native",[]{++call.returned;});
    reject("wrong caller stack retains native",[]{call.stack+=4;});
    reject("output outside caller stack retains native",[]{call.high-=0x80;});
    reject("wrong thread retains native",[]{call.thread+=4;});
    reject("dead client retains native",[]{set<int>(cg+0x8a364+0x1c4,0);});
    reject("dead server retains native",[]{set<int>(server+0x1c4,0);});
    reject("linked client retains native",[]{set<int>(cg+0x8a364+4,1);});
    reject("linked server retains native",[]{set<int>(server+4,1);});
    reject("parent link retains native",[]{set<std::uint32_t>(0x1a796f8+0x298,0x14000000);});
    reject("frozen prompt retains native",[]{set<std::uint32_t>(server+0xc,0x800);});
    reject("camera mode retains native",[]{set<int>(cg+0x8a360,4);});
    reject("extra camera rotation retains native",[]{set<float>(server+0x464,1);});
    reject("topdown camera retains native",[]{set<int>(topdown+0x18,1);});
    reject("topdown dvar identity retains native",[]{set<char>(name,'x');});
    reject("native pause retains native",[]{set<int>(0x2910160,16);});
    reject("pre pause retains native",[]{pre.context.key_catcher=16;});
    reject("native override retains native",[]{set<int>(0x2911e2c,1);});
    reject("unreadable owner retains native",[]{failed_read=server+0x1c4;});
    reject("wrong client identity retains native",[]{set<std::uint8_t>(server+0x130,1);});
    reject("native ownership mismatch retains native",[]{set<std::uint32_t>(0x1c03140,server+4);});
    reject("missing pre retains native",[]{pre_available=false;});
    reject("stale pre retains native",[]{pre.observed_ms=749;});
    reject("epoch drift retains native",[]{read_effect=[] {++pre.gameplay_epoch;};});
    reject("owner drift retains native",[]{read_effect=[] {set<int>(server+0x1c4,99);};});
    reject("old completed scene retains native",[]{d::scene.native_time=4899;});
    reject("future completed scene retains native",[]{d::scene.native_time=5001;});
    reject("old scene epoch retains native",[]{++d::scene.epoch;});
    reject("wrong scene refdef retains native",[]{++d::scene.refdef;});
    reject("stale tracking retains native",[]{d::scene.rig.published_ms=899;});
    reject("focus loss retains native",[]{latest.focused=false;});
    reject("new menu edge retains native",[]{latest.menu.active=latest.menu.current=true;});
    reject("missing current actions retains native",[]{actions_available=false;});
    reject("latest views invalid retains native",[]{current_views=false;});
    reject("latest render disabled retains native",[]{current_render=false;});
    reject("recenter retains native",[]{generation_current=false;});
    reject("recenter on recheck retains native",[]{generation_effect=[] {generation_current=false;};});
    reset();const auto published=d::scene;
    bovr::t5::publish_native_use_gaze_scene_test_copy(published.rig,published.stock.position,published.stock.axis,pre,published.refdef);
    check(d::scene.epoch==pre.gameplay_epoch&&d::scene.sequence==pre.sequence,"successful scene copied with exact pre identity");
    pre.gameplay_active=false;
    bovr::t5::publish_native_use_gaze_scene_test_copy(published.rig,published.stock.position,published.stock.axis,pre,published.refdef);
    check(!d::scene.epoch,"nonordinary publication retires previous copied scene");
    // The refusal test intentionally injects read-time drift; verify separately.
    reset();native_effect=[] {pre.context.key_catcher=16;};check(same(run(),{.514f,.074f,-.854f}),"native callback transition precedes admission");
    std::printf("native use consumer: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
