// SPDX-License-Identifier: GPL-3.0-only
// Actual private-limit selector over owned sparse native bytes. No game access.
#include "t5/helicopter_turret_limit_selection.hpp"
#include "xr_math.h"
#include <cstdio>
#include <map>
#include <vector>
#include <functional>
#include <limits>

namespace {
namespace xr=wawvr::xr;
namespace hc=bovr::t5::helicopter;
namespace limits=bovr::t5::helicopter_turret_limits;
using U32=std::uint32_t;
using bovr::t5::PreCommandSnapshot;
unsigned checks{},failures{};
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
struct Memory {
    static constexpr U32 cg=0x20000000,ps=cg+0x8a364,server=0x21000000;
    static constexpr U32 player=0x1a796f8,vehicle=player+505*0x34c,scr=0x22000000,info=0x1cc1480+3*0x1dc8;
    static constexpr U32 stack=0x12010000;
    struct Read{std::uintptr_t at{};std::size_t size{};};
    std::map<std::uintptr_t,unsigned char> data;
    std::vector<Read> reads;
    PreCommandSnapshot pre{};
    bovr::t5::HelicopterSceneView view{};
    limits::Call call{0x80c49f,vehicle,scr,info,player,stack,stack-0x10000,stack+0x10000,42};
    xr::Vec3f native_start{130,240,75},native_end{10370,240,75};
    std::uint64_t now=1000;
    limits::TargetAdmission target{};
    limits::Info native_info{};
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
    Memory(){
        native_info.fill(0xa5);
        const std::int16_t native_type=6;std::memcpy(native_info.data()+0x40,&native_type,sizeof native_type);
        const std::array<float,5> native_limits{35,35,10,10,60};
        std::memcpy(native_info.data()+0x1ec,native_limits.data(),sizeof native_limits);
        put(info,native_info);
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
        put<U32>(stack,0x80c49f);
        const std::array<float,4> args{25,24,60,.05f};put(stack+4,args);put<float>(stack+0x60,25);
        put<int>(0x1c04890,10000);
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
        target={view.owner,pre.gameplay_epoch,view.rig.xr_generation,990,10000,call.thread,true};
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(opaque);m.reads.push_back({at,size});
        if(m.fail_read==m.reads.size())return false;
        auto* bytes=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){const auto it=m.data.find(at+i);if(it==m.data.end())return false;bytes[i]=it->second;}
        if(m.mutate&&m.mutate_read==m.reads.size())m.mutate(m);
        return true;
    }
    bool run(limits::Info* out){
        reads.clear();return limits::select({this,missing_copy?nullptr:copy,nullptr,nullptr},call,view,pre,target,now,out);
    }
    bool refuses(){
        limits::Info out{};out.fill(0x5c);const auto before=out;
        return !run(&out)&&out==before;
    }
};
void accepted_and_private(){
    Memory m;limits::Info out{};const auto before=m.data;const auto pre=m.pre;const auto view=m.view;const auto target=m.target;
    check(m.run(&out),"exact pilot HMD ticket and native aim call admit private limits");
    for(std::size_t i=0;i<out.size();++i)if(i<0x1ec||i>=0x1fc)
        check(out[i]==m.native_info[i],"all non-limit vehicle info bytes remain exact");
    const std::array<float,4> expected_limits{179,179,89,89};
    check(!std::memcmp(out.data()+0x1ec,expected_limits.data(),sizeof expected_limits),"only four copied clamp scalars widen");
    check(m.data==before&&!std::memcmp(&m.pre,&pre,sizeof pre)&&!std::memcmp(&m.view,&view,sizeof view)&&
        !std::memcmp(&m.target,&target,sizeof target),"selector leaves native graph stack controls and admission unchanged");
    check(m.target.level_time!=m.pre.native_game_time&&m.target.level_time!=m.pre.context.native_frame_time,
        "level time is independent of cg and input-clock origins");
    m.call.thread=m.target.thread=43;check(m.run(&out),"same admitted native server thread accepted");
    m=Memory{};m.view.sequence=24;check(m.run(&out),"earlier current rendered sequence accepted");
    m.call.stack_low=m.call.stack;m.call.stack_high=m.call.stack+0x80;
    check(m.run(&out),"exact protected stack extent accepted");
    m=Memory{};m.target.published_ms=m.pre.observed_ms=m.view.rig.published_ms=900;
    check(m.run(&out),"original 100ms ticket pre and rig boundary accepted");
    m.now=1001;check(m.refuses(),"original publication age is never refreshed");
    m=Memory{};m.put<int>(Memory::cg+0x8a348,m.view.native_time+100);
    check(m.run(&out),"bounded native camera age accepted without clock-domain comparison");
    m.put<int>(Memory::cg+0x8a348,m.view.native_time+101);check(m.refuses(),"native camera age above100 refused");
    m=Memory{};check(!m.run(nullptr),"null private output refused");
}
void admission_refusals(){
    const std::vector<std::pair<std::function<void(Memory&)>,const char*>> cases{
        {[](Memory& m){++m.call.returned;},"wrong return site"},
        {[](Memory& m){m.put<U32>(Memory::stack,0x80c4a0);},"stack return differs from bridge"},
        {[](Memory& m){m.call.vehicle+=0x34c;},"foreign vehicle register"},
        {[](Memory& m){m.call.scr+=4;},"foreign vehicle storage register"},
        {[](Memory& m){m.call.info+=0x1dc8;},"foreign info register"},
        {[](Memory& m){m.call.pilot+=0x34c;},"foreign pilot register"},
        {[](Memory& m){m.call.thread=0;},"zero thread"},
        {[](Memory& m){++m.call.thread;},"current thread differs from successful trace"},
        {[](Memory& m){++m.target.thread;},"ticket belongs to another native thread"},
        {[](Memory& m){m.target.valid=false;},"missing or consumed successful trace admission"},
        {[](Memory& m){m.target.owner={};},"ticket has no pilot owner"},
        {[](Memory& m){m.target.owner.vehicle_entity+=0x34c;},"ticket belongs to another vehicle"},
        {[](Memory& m){++m.target.epoch;},"ticket belongs to another gameplay epoch"},
        {[](Memory& m){++m.target.generation;},"ticket belongs to another XR generation"},
        {[](Memory& m){m.target.published_ms=0;},"ticket has no original wall stamp"},
        {[](Memory& m){m.target.published_ms=899;},"ticket older than100ms"},
        {[](Memory& m){m.target.published_ms=1001;},"ticket from future wall clock"},
        {[](Memory& m){++m.target.level_time;},"same vehicle ticket from another level tick"},
        {[](Memory& m){m.put<int>(0x1c04890,0);m.target.level_time=0;},"zero level time despite equal ticket"},
        {[](Memory& m){m.missing_copy=true;},"missing copy capability"},
        {[](Memory& m){++m.call.stack;},"unaligned native stack"},
        {[](Memory& m){m.call.stack_low=m.call.stack+4;},"stack below thread bounds"},
        {[](Memory& m){m.call.stack_high=m.call.stack-4;},"stack above thread bounds"},
        {[](Memory& m){m.call.stack_high=m.call.stack+0x7f;},"truncated native stack frame"},
        {[](Memory& m){m.call.stack=0xfffffff0U;m.call.stack_low=m.call.stack;m.call.stack_high=UINT32_MAX;},"stack overflow"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x418,0);},"native target is not active"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x418,2);},"unexpected native target mode"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x42c,4);},"script-owned entity target"},
        {[](Memory& m){m.put<std::uint16_t>(Memory::vehicle+0x134,2);},"foreign native owner handle"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,3);},"AI vehicle"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x81);},"script path vehicle"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x101);},"additional native path control"},
        {[](Memory& m){m.put<int>(Memory::scr+0x620,1);},"foreign seat0 occupant"},
        {[](Memory& m){m.put<U32>(Memory::vehicle+0x148,Memory::server);},"aliased native vehicle storage refused"},
        {[](Memory& m){m.put<int>(Memory::ps+0x1c4,0);},"dead predicted pilot"},
        {[](Memory& m){m.put<int>(Memory::server+0x1c4,0);},"dead authoritative pilot"},
        {[](Memory& m){m.put<int>(Memory::server+0x448,1);},"passenger"},
        {[](Memory& m){m.put<U32>(Memory::server+0xc,0x800);},"frozen native pilot"},
        {[](Memory& m){m.put<int>(0x2910160,16);},"native menu"},
        {[](Memory& m){m.put<int>(0x2910164,8);},"native connection loss"},
        {[](Memory& m){m.put<int>(0x2911e2c,1);},"native override"},
        {[](Memory& m){m.pre.context.key_catcher=16;},"pre menu"},
        {[](Memory& m){m.pre.context.native_override=true;},"pre override"},
        {[](Memory& m){m.pre.context.valid=false;},"invalid pre"},
        {[](Memory& m){m.pre.context.local_client=1;},"foreign pre local client"},
        {[](Memory& m){m.pre.context.connection_state=8;},"pre connection loss"},
        {[](Memory& m){m.pre.sequence=0;},"missing pre sequence"},
        {[](Memory& m){m.pre.gameplay_epoch=0;},"missing pre epoch"},
        {[](Memory& m){m.pre.native_gameplay_fields_valid=false;},"unavailable native pre fields"},
        {[](Memory& m){m.pre.native_cg_identity+=4;},"different native cg"},
        {[](Memory& m){m.pre.observed_ms=0;},"missing pre stamp"},
        {[](Memory& m){m.pre.observed_ms=899;},"stale pre"},
        {[](Memory& m){m.now=989;},"wall clock rollback"},
        {[](Memory& m){++m.view.epoch;},"different rendered epoch"},
        {[](Memory& m){m.view.sequence=0;},"missing rendered sequence"},
        {[](Memory& m){m.view.sequence=m.pre.sequence+1;},"future rendered sequence"},
        {[](Memory& m){m.view.owner={};},"missing rendered owner"},
        {[](Memory& m){m.view.owner.predicted.pm_type=0;},"preboarding rendered owner"},
        {[](Memory& m){m.view.owner.vehicle_entity+=0x34c;},"another rendered vehicle"},
        {[](Memory& m){m.view.refdef+=4;},"another native refdef"},
        {[](Memory& m){m.view.native_time=0;},"missing rendered native time"},
        {[](Memory& m){++m.view.native_time;},"future rendered native time"},
        {[](Memory& m){m.view.rig.xr_generation=0;},"missing XR generation"},
        {[](Memory& m){++m.view.rig.anchor_generation;},"anchor generation drift"},
        {[](Memory& m){m.view.rig.published_ms=899;},"stale original XR pose"},
        {[](Memory& m){m.view.rig.frame.frame_id=0;},"missing XR frame"},
        {[](Memory& m){m.view.rig.frame.actions.focused=false;},"unfocused XR pose"},
        {[](Memory& m){m.view.rig.frame.views_valid=false;},"invalid XR view pair"},
        {[](Memory& m){m.view.rig.frame.should_render=false;},"nonrendering XR prediction"},
        {[](Memory& m){m.view.rig.frame.head_center.orientation={0,0,0,0};},"invalid headset quaternion"},
        {[](Memory& m){m.view.stock.axis.left={0,-1,0};},"reflected native basis"}
    };
    for(const auto& [change,label]:cases){Memory m;change(m);check(m.refuses(),label);}
    for(unsigned i=0;i<4;++i){Memory m;m.put<float>(Memory::stack+4+i*4,std::numeric_limits<float>::quiet_NaN());
        check(m.refuses(),"nonfinite native LinearTrackAngle argument refused");}
    for(const auto [offset,value]:std::array<std::pair<unsigned,float>,8>{{{4,3601.F},{8,-3601.F},{12,-1.F},{12,36001.F},
        {16,.049F},{0x60,3601.F},{0x60,-3601.F},{0x60,std::numeric_limits<float>::infinity()}}}){
        Memory m;m.put<float>(Memory::stack+offset,value);check(m.refuses(),"native argument or normalization slot outside exact ABI bounds");}
    for(const auto offset:{0x1ecu,0x1f0u,0x1f4u,0x1f8u})for(const auto bad:{0.F,-1.F,181.F,std::numeric_limits<float>::quiet_NaN()}){
        Memory m;m.put<float>(Memory::info+offset,bad);check(m.refuses(),"invalid source clamp scalar preserves output");}
}
void read_failure_drift_and_alias(){
    Memory baseline;limits::Info out{};check(baseline.run(&out),"baseline read trace is available");
    const auto reads=baseline.reads;
    const auto first=[&](std::uintptr_t at,std::size_t bytes=0){
        for(std::size_t i=0;i<reads.size();++i)if(reads[i].at==at&&(!bytes||reads[i].size==bytes))return i+1;
        return std::size_t{};
    };
    for(const auto at:{std::uintptr_t(Memory::stack),std::uintptr_t(Memory::stack+4),std::uintptr_t(Memory::stack+0x60),
        std::uintptr_t(0x1c04890),std::uintptr_t(0x2ff5354),std::uintptr_t(Memory::server+0x1c4),
        std::uintptr_t(Memory::vehicle+0x134),std::uintptr_t(Memory::scr+0x418),std::uintptr_t(Memory::scr+0x42c)}){
        Memory m;m.fail_read=first(at);check(m.fail_read&&m.refuses(),"required admission read fails without output mutation");}
    for(std::size_t i=0;i<reads.size();++i)if(reads[i].size==sizeof(limits::Info)||i+1==reads.size()){
        Memory m;m.fail_read=i+1;check(m.refuses(),"each full info copy and final level recheck is required");}
    {Memory m;m.mutate_read=first(Memory::info,sizeof(limits::Info));
        m.mutate=[](Memory& n){n.put<std::uint8_t>(Memory::info+0x150,0x1b);};
        check(m.mutate_read&&m.refuses(),"non-limit info byte changes between copies reject complete clone");}
    for(const auto at:{std::uintptr_t(0x1c04890),std::uintptr_t(Memory::vehicle+0x148),
        std::uintptr_t(Memory::scr+0x1f0),std::uintptr_t(Memory::scr+0x620),std::uintptr_t(Memory::cg+0x8a348),
        std::uintptr_t(Memory::server+0x1c4)}){
        Memory m;m.mutate_read=first(at);m.mutate=[at](Memory& n){
            if(at==0x1c04890)n.put<int>(at,10001);
            else if(at==Memory::cg+0x8a348)n.put<int>(at,n.pre.native_game_time+1);
            else if(at==Memory::server+0x1c4)n.put<int>(at,99);
            else n.put<U32>(at,at==Memory::scr+0x1f0?3U:at==Memory::scr+0x620?1U:0U);
        };check(m.mutate_read&&m.refuses(),"between-copy identity health or native-level drift rejects");}
    // Exercise the production clone's independent-copy and alias guarantees;
    // select itself supplies separate local Info values and a caller-owned out.
    limits::Info a=baseline.native_info,b=a,untouched{};untouched.fill(0x6d);auto before=untouched;
    check(!limits::clone(a,a,&untouched)&&untouched==before,"one copy cannot stand in for repeated observations");
    before=a;check(!limits::clone(a,b,&a)&&a==before,"output cannot alias first input copy");
    before=b;check(!limits::clone(a,b,&b)&&b==before,"output cannot alias second input copy");
}
}
int main(){accepted_and_private();admission_refusals();read_failure_drift_and_alias();
    std::printf("helicopter turret limit selection: %u checks, %u failures\n",checks,failures);return failures?1:0;}
