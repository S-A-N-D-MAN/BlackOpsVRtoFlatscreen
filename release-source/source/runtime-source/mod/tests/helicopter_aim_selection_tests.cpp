// SPDX-License-Identifier: GPL-3.0-only
// Actual production selector over owned sparse native bytes. No game or writes.
#include "t5/helicopter_aim_selection.hpp"
#include "xr_math.h"
#include <cstdio>
#include <map>
#include <vector>
#include <functional>
#include <limits>

namespace {
namespace xr=wawvr::xr;
namespace hc=bovr::t5::helicopter;
namespace aim=bovr::t5::helicopter_aim;
using U32=std::uint32_t;
using bovr::t5::PreCommandSnapshot;
unsigned checks{},failures{};
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
bool close(float a,float b,float tolerance=.002F){return std::fabs(a-b)<tolerance;}
bool close(xr::Vec3f a,xr::Vec3f b){return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z);}
bool empty(const aim::Plan& p){
    const xr::EnginePose identity{};
    return p.owner==hc::Facts{}&&p.view.owner==hc::Facts{}&&!p.view.refdef&&!p.view.epoch&&!p.view.sequence&&!p.view.native_time&&
        !p.view.rig.xr_generation&&!p.view.rig.anchor_generation&&!p.view.rig.published_ms&&!p.view.rig.frame.frame_id&&
        !p.pre.sequence&&!p.pre.gameplay_epoch&&!p.pre.observed_ms&&!p.pre.native_cg_identity&&!p.pre.context.valid&&
        close(p.head.position,{})&&close(p.head.axis.forward,identity.axis.forward)&&
        close(p.head.axis.left,identity.axis.left)&&close(p.head.axis.up,identity.axis.up)&&
        close(p.old_start,{})&&close(p.old_end,{})&&close(p.start,{})&&close(p.end,{})&&close(p.direction,{})&&
        !p.start_address&&!p.end_address&&!p.target_address;
}
struct Memory {
    static constexpr U32 cg=0x20000000,ps=cg+0x8a364,server=0x21000000;
    static constexpr U32 player=0x1a796f8,vehicle=player+505*0x34c,scr=0x22000000,info=0x1cc1480+3*0x1dc8;
    static constexpr U32 stack=0x12010000;
    struct Read{std::uintptr_t at{};std::size_t size{};};
    std::map<std::uintptr_t,unsigned char> data;
    std::vector<Read> reads;
    PreCommandSnapshot pre{};
    bovr::t5::HelicopterSceneView view{};
    aim::Call call{0x81141c,vehicle,scr,info,player,stack,stack-0x10000,stack+0x10000,42};
    xr::Vec3f native_start{130,240,75},native_end{10370,240,75};
    std::uint64_t now=1000;
    std::size_t fail_read{},mutate_read{};
    std::function<void(Memory&)> mutate;
    bool missing_copy{};
    template<class T>void put(std::uintptr_t at,const T& value){
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)data[at+i]=bytes[i];
    }
    void player_fields(U32 at){
        put<int>(at+4,1);put<U32>(at+0xc,0);put<U32>(at+0x10,0);put<U32>(at+0xe0,0x84000);
        put<U32>(at+0xe4,0);put<std::uint8_t>(at+0x130,0);put<std::uint8_t>(at+0x144,0);
        put<int>(at+0x1c4,100);put<U32>(at+0x440,0);put<std::int16_t>(at+0x444,505);
        put<int>(at+0x448,0);put<int>(at+0x44c,6);
    }
    void scripted_huey(U32 flags=0x281){
        constexpr U32 dvar=0x24000000,name=dvar+0x100,map=dvar+0x200;
        put<U32>(0x2899cec,dvar);put<U32>(dvar,name);put<U32>(dvar+0xc,0x44);
        put<U32>(dvar+0x10,7);put<U32>(dvar+0x18,map);
        const std::array<char,8> name_text{'m','a','p','n','a','m','e',0};
        const std::array<char,15> map_text{'u','n','d','e','r','w','a','t','e','r','b','a','s','e',0};
        put(name,name_text);put(map,map_text);
        for(auto at:{ps,server})put<U32>(at+0x10,0xa0);
        put<U32>(scr+0x1f0,flags);
        put<U32>(scr+0x418,0); // Native gunner-only Huey has no main turret target.
        hc::detail::read_graph({this,copy,nullptr,nullptr},view.owner);reads.clear();
    }
    Memory(){
        pre.context.valid=true;pre.context.native_thread=42;pre.context.command_number=17;
        pre.context.native_frame_time=9000;pre.context.connection_state=10;
        pre.context.viewangles[0]=-.8F;pre.context.viewangles[1]=75.9F;
        pre.sequence=25;pre.observed_ms=1000;pre.gameplay_epoch=9;pre.native_cg_identity=cg;
        pre.native_game_time=449318;pre.native_health=100;pre.native_pm_type=1;
        pre.native_entity_flags=0x84000;pre.native_gameplay_fields_valid=true;
        put<U32>(0x2ff5354,cg);put<U32>(0x1c03140,server);put<U32>(player+0x13c,server);put<int>(player,0);
        player_fields(ps);player_fields(server);put<int>(vehicle,505);put<U32>(vehicle+0x148,scr);
        put<std::int16_t>(scr+0x1ec,3);put<U32>(scr+0x1f0,1);put<int>(scr+0x620,0);put<std::int16_t>(info+0x40,6);
        for(U32 offset:{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780})put<int>(cg+offset,0);
        put<int>(cg+0xa4680,2);put<int>(cg+0x8a348,pre.native_game_time);
        put<int>(0x2993850,17);put<int>(0x2481764,9000);put<int>(0x2910160,0);put<int>(0x2910164,10);put<int>(0x2911e2c,0);
        put<DWORD>(0x251ca98,42);put<DWORD>(0x251cac0,43);
        const std::array<U32,8> args{0x81141c,stack+0xa8,stack+0x30,stack+0x48,0,0x280e091,0,0};
        put(stack,args);put(stack+0x30,native_start);put(stack+0x48,native_end);
        put<U32>(scr+0x418,1);put<U32>(scr+0x42c,1023);put<std::uint16_t>(vehicle+0x134,1);put(scr+0x434,native_end);
        // These R50 inputs are not selector inputs and must remain untouched.
        put<float>(0x2911e20,-12.5F);put<float>(0x2911e24,123.25F);
        const std::array<std::int8_t,4> flight{100,-80,51,-44};put(0x2300001b,flight);
        view.refdef=cg+0x8c100;view.epoch=pre.gameplay_epoch;view.sequence=pre.sequence;view.native_time=pre.native_game_time;
        view.stock={{150,250,80},{}};
        auto& r=view.rig;r.xr_generation=r.anchor_generation=7;r.published_ms=990;
        auto& f=r.frame;f.frame_id=23;f.predicted_display_time=100000;f.predicted_display_period=11111111;
        f.should_render=f.views_valid=true;f.actions.focused=true;f.actions.sequence=29;
        r.tracking_anchor.position={0,1.6F,0};f.head_center=r.tracking_anchor;
        // Head yaw 90 degrees differs from the original aircraft-forward trace.
        const float half=std::sqrt(.5F);f.head_center.orientation={0,half,0,half};
        for(unsigned i=0;i<2;++i){auto& e=f.eyes[i];e.pose=f.head_center;
            const auto v=xr::Rotate(e.pose.orientation,{i?.032F:-.032F,0,0});
            e.pose.position.x+=v.x;e.pose.position.y+=v.y;e.pose.position.z+=v.z;e.fov={-.72F,.78F,.76F,-.70F};}
        // Simulate the earlier renderer publication using its actual copied
        // native owner shape. Later changes never recapture this source owner.
        hc::detail::read_graph({this,copy,nullptr,nullptr},view.owner);reads.clear();
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(opaque);m.reads.push_back({at,size});
        if(m.fail_read==m.reads.size())return false;
        auto* bytes=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){const auto it=m.data.find(at+i);if(it==m.data.end())return false;bytes[i]=it->second;}
        if(m.mutate&&m.mutate_read==m.reads.size())m.mutate(m);
        return true;
    }
    bool run(aim::Plan* out){
        reads.clear();return aim::propose({this,missing_copy?nullptr:copy,nullptr,nullptr},call,view,pre,now,out);
    }
    bool refuses(){
        aim::Plan out{};out.owner.cg=1;out.view=view;out.pre=pre;out.head.position={7,8,9};
        out.old_start=out.old_end=out.start=out.end=out.direction={7,8,9};
        out.start_address=out.end_address=out.target_address=1;
        return !run(&out)&&empty(out);
    }
};
void accepted_and_immutable(){
    {
        Memory boat;aim::Plan plan{};xr::Vec3f angles{};
        const hc::Host host{&boat,Memory::copy,nullptr,nullptr};
        for(auto ps:{Memory::ps,Memory::server})boat.put<int>(ps+0x44c,4);
        boat.put<std::int16_t>(Memory::info+0x40,4);boat.put<U32>(Memory::scr+0x418,0);
        hc::detail::read_graph(host,boat.view.owner);
        auto& hand=boat.view.rig.frame.actions.hands[1].aim;
        hand.active=hand.orientation_valid=hand.orientation_tracked=true;
        // HMD looks 90 degrees left while the right hand points 30 right.
        hand.pose.orientation={0,-std::sin(.261799388F),0,std::cos(.261799388F)};
        const auto original=boat.data;
        check(boat.run(&plan),"boat driver trace accepts native no-turret target flag");
        check(close(plan.head.axis.forward,{std::cos(.523598776F),-.5F,0}),
              "boat machine-gun target follows right hand independently of HMD");
        check(aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now,&angles)&&
              close(angles.x,0.f)&&close(angles.y,-30.f),"boat rocket crew gets right-controller direction");
        const auto hand_direction=plan.head.axis.forward;
        boat.view.rig.frame.head_center.orientation={};
        check(boat.run(&plan)&&close(plan.head.axis.forward,hand_direction)&&
              aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now,&angles)&&close(angles.y,-30.f),
              "turning only the headset does not redirect either boat weapon");
        hand.pose.orientation={std::sin(.174532925F),0,0,std::cos(.174532925F)};
        check(boat.run(&plan)&&close(plan.head.axis.forward,{std::cos(.34906585F),0,std::sin(.34906585F)})&&
              aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now,&angles)&&
              close(angles.x,-20.f)&&close(angles.y,0.f),"both boat weapons follow controller pitch");
        for(int invalid=0;invalid<4;++invalid){
            const auto good=hand;
            if(invalid==0)hand.active=false;
            if(invalid==1)hand.orientation_valid=false;
            if(invalid==2)hand.orientation_tracked=false;
            if(invalid==3)hand.pose.orientation={0,0,0,0};
            check(boat.refuses()&&!aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now,&angles),
                  "invalid right aim refuses boat targeting without headset fallback");
            hand=good;
        }
        check(boat.data==original,"boat targeting leaves native camera and player state untouched");
        check(!aim::vehicle_script_angles(host,1,43,boat.view,boat.pre,boat.now,&angles),"NPC script angles remain native");
        check(!aim::vehicle_script_angles(host,0,44,boat.view,boat.pre,boat.now,&angles),"foreign script thread refused");
        check(!aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now+101,&angles),"stale boat script aim refused");
        boat.pre.context.key_catcher=16;
        check(!aim::vehicle_script_angles(host,0,43,boat.view,boat.pre,boat.now,&angles),"paused boat script aim refused");
    }
    Memory m;aim::Plan p{};const auto before=m.data;const auto pre=m.pre;const auto view=m.view;
    check(m.run(&p),"actual selector admits exact local pilot trace call");
    check(p.owner.vehicle_entity==Memory::vehicle&&p.owner.scr_vehicle==Memory::scr&&
        p.owner.server==Memory::server&&p.pre.sequence==25&&p.view.sequence==25,
        "plan retains exact player vehicle and rendered-scene provenance");
    check(close(p.start,{150,250,80})&&close(p.head.axis.forward,{0,1,0})&&close(p.end,{150,10490,80}),
        "trace plan follows HMD ray instead of old aircraft-forward endpoint");
    check(close(p.old_start,m.native_start)&&close(p.old_end,m.native_end)&&
        p.start_address==Memory::stack+0x30&&p.end_address==Memory::stack+0x48&&p.target_address==Memory::scr+0x434,
        "native vectors and exact permitted destinations retained for separate guarded commit");
    const double dx=double(p.end.x)-m.native_start.x,dy=double(p.end.y)-m.native_start.y,dz=double(p.end.z)-m.native_start.z;
    const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
    check(close(p.direction,{float(dx/length),float(dy/length),float(dz/length)}),
        "muzzle convergence direction reaches common HMD endpoint");
    check(m.data==before&&!std::memcmp(&m.pre,&pre,sizeof(pre))&&!std::memcmp(&m.view,&view,sizeof(view)),
        "selection does not mutate flags flight look native stack vectors or source publications");
    m.call.thread=43;check(m.run(&p),"native server thread admits retained main-thread scene and pre snapshot");
    m.call.thread=42;m.view.sequence=24;check(m.run(&p),"prior completed scene sequence may precede latest native pre");
    m.call.stack_low=m.call.stack;m.call.stack_high=m.call.stack+0x180;
    check(m.run(&p),"exact complete caller-frame span is sufficient");
    m.put<int>(Memory::cg+0x8a348,m.view.native_time+100);
    check(m.run(&p),"100ms native simulation-to-rendered-view lag allowed independently of command clock");
    m.put<int>(Memory::cg+0x8a348,m.view.native_time+101);check(m.refuses(),"101ms native rendered-view lag refused");
    m=Memory{};m.pre.observed_ms=900;m.view.rig.published_ms=900;
    check(m.run(&p),"100ms pre and original XR prediction boundaries accepted");
    m.now=1001;check(m.refuses(),"unchanged original publications expire without timestamp refresh");
    m=Memory{};m.view.rig.frame.head_center.position.x=.2F;
    check(m.run(&p)&&close(p.start,{150,250-.2F*xr::kIwUnitsPerMeter,80}),
        "positional HMD shift changes trace origin in rendered world coordinates");
    m=Memory{};check(!m.run(nullptr),"null plan refused");
}
void scripted_huey_aiming(){
    for(const U32 flags:{0x281u,0x201u}){
        Memory m;m.scripted_huey(flags);aim::Plan plan{};xr::Vec3f angles{};
        const hc::Host host{&m,Memory::copy,nullptr,nullptr};const auto original=m.data;
        check(m.view.owner.scripted_huey,"exact Redemption Huey owner is captured");
        check(m.run(&plan)&&close(plan.head.axis.forward,{0,1,0}),
              "gunner-only Huey cannon uses HMD ray during authored path and after player control returns");
        check(aim::vehicle_script_angles(host,0,43,m.view,m.pre,m.now,&angles)&&
              close(angles,{0,90,0}),"Huey rockets use HMD direction without a tracked right hand");
        auto& hand=m.view.rig.frame.actions.hands[1].aim;
        hand.active=hand.orientation_valid=hand.orientation_tracked=true;
        hand.pose.orientation={0,-std::sin(.261799388F),0,std::cos(.261799388F)};
        check(aim::vehicle_script_angles(host,0,43,m.view,m.pre,m.now,&angles)&&
              close(angles,{0,90,0}),"right hand movement does not redirect Huey rockets");
        m.view.rig.frame.head_center.orientation={std::sin(.174532925F),0,0,std::cos(.174532925F)};
        check(m.run(&plan)&&close(plan.head.axis.forward,{std::cos(.34906585F),0,std::sin(.34906585F)})&&
              aim::vehicle_script_angles(host,0,42,m.view,m.pre,m.now,&angles)&&close(angles,{-20,0,0}),
              "Huey cannon and rockets share headset pitch on either native thread");
        check(m.data==original,"Huey aiming leaves script path and weapon readiness flags untouched");
        check(hc::flight_control_allowed(m.view.owner)==(flags==0x201),
              "aim availability does not grant flight control during the native authored path");
        m.put<U32>(Memory::scr+0x418,1);
        check(m.run(&plan),"Huey native active-target flag remains accepted");
    }
    {
        Memory hind;xr::Vec3f angles{};const hc::Host host{&hind,Memory::copy,nullptr,nullptr};
        check(!aim::vehicle_script_angles(host,0,43,hind.view,hind.pre,hind.now,&angles),
              "ordinary Hind script GetPlayerAngles remains native");
        hind.put<U32>(Memory::scr+0x418,0);
        check(hind.refuses(),"ordinary Hind cannot borrow the gunner-only Huey target exception");
    }
    const std::vector<std::pair<std::function<void(Memory&)>,const char*>> cases{
        {[](Memory& m){m.put<char>(0x24000200,'x');},"Huey script aim refuses another map"},
        {[](Memory& m){m.put<U32>(0x2400000c,0);},"Huey script aim requires verified map dvar shape"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x283);},"Huey AI control remains protected"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x381);},"Huey suspended control remains protected"},
        {[](Memory& m){m.put<U32>(Memory::server+0x10,0x1a0);},"Huey extra weapon-disable bit remains protected"},
        {[](Memory& m){m.put<int>(Memory::server+0x448,1);},"Huey passenger cannot claim pilot aim"},
        {[](Memory& m){m.view.owner.scripted_huey=false;m.view.owner.map={};},"stale non-Huey scene cannot grant script aim"},
        {[](Memory& m){m.pre.context.key_catcher=16;},"paused Huey script aim refused"},
        {[](Memory& m){++m.view.epoch;},"previous Huey gameplay epoch refused"},
        {[](Memory& m){m.pre.observed_ms=899;},"stale Huey command publication refused"},
        {[](Memory& m){m.view.rig.published_ms=899;},"stale Huey headset publication refused"},
        {[](Memory& m){m.view.rig.frame.head_center.orientation={0,0,0,0};},"invalid Huey HMD pose refused"}
    };
    for(const auto& [change,label]:cases){
        Memory m;m.scripted_huey();change(m);xr::Vec3f angles{7,8,9};
        const hc::Host host{&m,Memory::copy,nullptr,nullptr};
        check(m.refuses()&&!aim::vehicle_script_angles(host,0,43,m.view,m.pre,m.now,&angles)&&
              close(angles,{7,8,9}),label);
    }
    for(U32 invalid:{2u,3u,UINT32_MAX}){
        Memory m;m.scripted_huey();m.put<U32>(Memory::scr+0x418,invalid);
        check(m.refuses(),"Huey target mode exception accepts zero only");
    }
    {
        Memory m;m.scripted_huey();m.put<U32>(Memory::scr+0x42c,4);
        check(m.refuses(),"gunner-only Huey never overrides native entity targeting");
    }
    {
        Memory m;m.scripted_huey();m.put<std::uint16_t>(Memory::vehicle+0x134,2);
        check(m.refuses(),"gunner-only Huey requires current local owner handle");
    }
    Memory baseline;baseline.scripted_huey();xr::Vec3f angles{};
    const hc::Host host{&baseline,Memory::copy,nullptr,nullptr};
    check(aim::vehicle_script_angles(host,0,43,baseline.view,baseline.pre,baseline.now,&angles),
          "Huey script baseline for map copy drift check");
    for(std::size_t i=0;i<baseline.reads.size();++i)if(baseline.reads[i].at==0x24000200){
        Memory m;m.scripted_huey();m.mutate_read=i+1;
        m.mutate=[](Memory& next){next.put<char>(0x24000200,'x');};
        const hc::Host mutable_host{&m,Memory::copy,nullptr,nullptr};
        check(!aim::vehicle_script_angles(mutable_host,0,43,m.view,m.pre,m.now,&angles),
              "map transition between Huey owner copies refuses script-angle override");
        break;
    }
}
void call_and_context_refusals(){
    const std::vector<std::pair<std::function<void(Memory&)>,const char*>> cases{
        {[](Memory& m){++m.call.returned;},"wrong native return site"},
        {[](Memory& m){m.call.vehicle+=0x34c;},"wrong vehicle register"},
        {[](Memory& m){m.call.scr+=4;},"wrong script-vehicle register"},
        {[](Memory& m){m.call.info+=0x1dc8;},"wrong vehicle-info register"},
        {[](Memory& m){m.call.player+=0x34c;},"wrong local-player register"},
        {[](Memory& m){m.call.thread=0;},"zero native thread"},
        {[](Memory& m){m.call.thread=44;},"foreign native thread"},
        {[](Memory& m){m.put<DWORD>(0x251ca98,44);},"actual main-thread owner differs"},
        {[](Memory& m){m.missing_copy=true;},"missing native copy capability"},
        {[](Memory& m){++m.call.stack;},"unaligned trace stack"},
        {[](Memory& m){m.call.stack_low=m.call.stack+4;},"entry below owned stack range"},
        {[](Memory& m){m.call.stack_high=m.call.stack-4;},"entry beyond stack high bound"},
        {[](Memory& m){m.call.stack_high=m.call.stack+0x17f;},"insufficient owned caller-frame extent"},
        {[](Memory& m){m.call.stack=0xfffffff0U;m.call.stack_low=m.call.stack;m.call.stack_high=UINT32_MAX;},"trace span overflows native address space"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x418,0);},"native player target inactive"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x418,2);},"unexpected target mode"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x42c,4);},"native entity targeting owns turret"},
        {[](Memory& m){m.put<std::uint16_t>(Memory::vehicle+0x134,2);},"vehicle owner handle is another player"},
        {[](Memory& m){m.put(Memory::scr+0x434,xr::Vec3f{1,2,3});},"native target does not equal copied trace endpoint"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,3);},"AI ownership"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x81);},"scripted path ownership"},
        {[](Memory& m){m.put<int>(Memory::scr+0x620,1);},"foreign pilot occupant"},
        {[](Memory& m){m.put<int>(Memory::ps+0x1c4,0);},"dead predicted pilot"},
        {[](Memory& m){m.put<int>(Memory::server+0x1c4,0);},"dead authoritative pilot"},
        {[](Memory& m){m.put<int>(Memory::server+0x448,1);},"passenger role"},
        {[](Memory& m){m.put<int>(0x2910160,16);},"current native menu"},
        {[](Memory& m){m.put<int>(0x2910164,8);},"current native connection lost"},
        {[](Memory& m){m.put<int>(0x2911e2c,1);},"current native override"},
        {[](Memory& m){m.pre.context.key_catcher=16;},"pre menu occurrence"},
        {[](Memory& m){m.pre.context.native_override=true;},"pre script override"},
        {[](Memory& m){m.pre.context.valid=false;},"invalid pre context"},
        {[](Memory& m){m.pre.context.local_client=1;},"foreign pre client"},
        {[](Memory& m){m.pre.context.connection_state=8;},"pre connection lost"},
        {[](Memory& m){m.pre.sequence=0;},"missing pre sequence"},
        {[](Memory& m){m.pre.gameplay_epoch=0;},"missing native epoch"},
        {[](Memory& m){m.pre.native_gameplay_fields_valid=false;},"native pre fields unavailable"},
        {[](Memory& m){m.pre.native_cg_identity+=4;},"native cg differs from pre"},
        {[](Memory& m){m.pre.observed_ms=0;},"missing pre publication clock"},
        {[](Memory& m){m.pre.observed_ms=899;},"stale pre snapshot"},
        {[](Memory& m){m.now=999;},"backward wall clock"},
        {[](Memory& m){++m.view.epoch;},"rendered scene belongs to another native epoch"},
        {[](Memory& m){m.view.sequence=0;},"missing rendered scene sequence"},
        {[](Memory& m){m.view.sequence=m.pre.sequence+1;},"rendered scene newer than selected pre"},
        {[](Memory& m){m.view.owner={};},"rendered scene has no admitted helicopter owner"},
        {[](Memory& m){m.view.owner.predicted.pm_type=0;},"pre-boarding rendered camera cannot be reused in helicopter"},
        {[](Memory& m){m.view.owner.authoritative.vehicle_pos=1;},"previous passenger camera cannot become pilot aim"},
        {[](Memory& m){m.view.owner.vehicle_flags=3;},"previous AI-controlled camera cannot become pilot aim"},
        {[](Memory& m){auto& o=m.view.owner;--o.entity_index;--o.vehicle_number;
            --o.predicted.vehicle_entity_index;--o.authoritative.vehicle_entity_index;o.vehicle_entity-=0x34c;},
            "otherwise eligible rendered camera from another vehicle is refused"},
        {[](Memory& m){m.view.refdef+=4;},"rendered stock camera belongs to another refdef"},
        {[](Memory& m){m.view.native_time=0;},"missing native scene time"},
        {[](Memory& m){++m.view.native_time;},"scene time ahead of current native graph"},
        {[](Memory& m){m.view.rig.xr_generation=0;},"missing XR generation"},
        {[](Memory& m){++m.view.rig.anchor_generation;},"mismatched tracking-anchor generation"},
        {[](Memory& m){m.view.rig.published_ms=899;},"stale rendered XR sample"},
        {[](Memory& m){m.view.rig.frame.actions.focused=false;},"rendered XR sample unfocused"},
        {[](Memory& m){m.view.rig.frame.views_valid=false;},"invalid headset views"},
        {[](Memory& m){m.view.rig.frame.head_center.orientation={0,0,0,0};},"invalid HMD orientation"},
        {[](Memory& m){m.view.stock.axis.left={0,-1,0};},"reflected native body basis"}
    };
    for(const auto& [change,label]:cases){Memory m;change(m);check(m.refuses(),label);}
    for(unsigned arg=0;arg<8;++arg){Memory m;
        const std::array<U32,8> bad{0x811420,Memory::stack+0xac,Memory::stack+0x34,Memory::stack+0x4c,1,0x280e090,1,1};
        m.put<U32>(Memory::stack+arg*4,bad[arg]);check(m.refuses(),"each exact trace argument and mask is required");}
    for(const auto address:{Memory::stack+0x30,Memory::stack+0x48}){Memory m;
        const xr::Vec3f bad{std::numeric_limits<float>::quiet_NaN(),0,0};m.put(address,bad);
        if(address==Memory::stack+0x48)m.put(Memory::scr+0x434,bad);
        check(m.refuses(),"nonfinite native trace vector refused");}
}
void copy_failures_and_drift(){
    Memory baseline;aim::Plan p{};check(baseline.run(&p),"sparse-read baseline for targeted failure injection");
    const auto reads=baseline.reads;
    const auto first=[&](std::uintptr_t at){for(std::size_t i=0;i<reads.size();++i)if(reads[i].at==at)return i+1;return std::size_t{};};
    // Representative distinct admission stages, including the second graph.
    for(const auto at:{std::uintptr_t(Memory::stack),std::uintptr_t(0x251ca98),std::uintptr_t(0x2ff5354),
        std::uintptr_t(Memory::server+0x1c4),std::uintptr_t(Memory::scr+0x620),
        std::uintptr_t(Memory::vehicle+0x134),std::uintptr_t(Memory::stack+0x30),std::uintptr_t(Memory::scr+0x434)}){
        Memory m;m.fail_read=first(at);check(m.fail_read!=0&&m.refuses(),"required native read failure leaves no plan");}
    {Memory m;m.fail_read=reads.size();check(m.refuses(),"last second-graph read failure leaves no plan");}
    for(const auto at:{std::uintptr_t(0x2ff5354),std::uintptr_t(Memory::vehicle+0x148),
        std::uintptr_t(Memory::scr+0x1f0),std::uintptr_t(Memory::scr+0x620),std::uintptr_t(Memory::cg+0x8a348)}){
        Memory m;m.mutate_read=first(at);
        m.mutate=[at](Memory& next){
            if(at==Memory::cg+0x8a348)next.put<int>(at,next.pre.native_game_time+1);
            else next.put<U32>(at,at==Memory::scr+0x1f0?3U:at==Memory::scr+0x620?1U:0U);
        };
        check(m.mutate_read!=0&&m.refuses(),"mid-copy graph ownership or native-time drift rejected by exact second copy");
    }
    // A benign mutable field still belongs to the exact copied graph, so its
    // change must not silently turn the first snapshot into current authority.
    {Memory m;m.mutate_read=first(Memory::server+0x1c4);
        m.mutate=[](Memory& next){next.put<int>(Memory::server+0x1c4,99);};
        check(m.refuses(),"still-alive health drift between graph copies rejects");}
}
}
int main(){accepted_and_immutable();scripted_huey_aiming();call_and_context_refusals();copy_failures_and_drift();
    std::printf("helicopter aim selection: %u checks, %u failures\n",checks,failures);return failures?1:0;}
