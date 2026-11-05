// SPDX-License-Identifier: GPL-3.0-only
// Actual production reader over sparse owned SP-layout bytes; no game or native calls.
#include "t5/helicopter_context.hpp"
#include <cstdio>
#include <map>
#include <vector>
#include <functional>
#include <limits>

namespace hc=bovr::t5::helicopter;
using bovr::t5::CommandContext;
using bovr::t5::PreCommandSnapshot;
using U32=std::uint32_t;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
struct Memory {
    static constexpr U32 base=0x400000,cg=0x20000000,ps=cg+0x8a364,server=0x21000000;
    static constexpr U32 player=0x1a796f8,vehicle=player+505*0x34c,scr=0x22000000,info=0x1cc1480+3*0x1dc8;
    static constexpr U32 map=0x23000000,map_name=map+0x80,map_value=map+0x100;
    static constexpr U32 huey_vehicle=player+257*0x34c,huey_info=0x1cc1480+5*0x1dc8;
    struct Read {std::uintptr_t at{};std::size_t size{};};
    std::map<std::uintptr_t,unsigned char> data;
    std::vector<Read> reads;
    PreCommandSnapshot pre{};
    DWORD thread=42;
    std::uint64_t now=1000;
    unsigned pre_reads{};
    std::size_t fail_read{},mutate_read{};
    std::function<void(Memory&)> mutate;
    bool change_last_pre{},fail_pre{};
    template<class T>void put(std::uintptr_t at,T value){
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)data[at+i]=bytes[i];
    }
    void player_fields(U32 at){
        put<int>(at+4,1);put<U32>(at+0xc,0);put<U32>(at+0x10,0);put<U32>(at+0xe0,0x84000);
        put<U32>(at+0xe4,0);put<std::uint8_t>(at+0x130,0);put<std::uint8_t>(at+0x144,0);
        put<int>(at+0x1c4,100);put<U32>(at+0x440,0);put<std::int16_t>(at+0x444,505);
        put<int>(at+0x448,0);put<int>(at+0x44c,6);
    }
    Memory(int type=6){
        pre.context={};pre.context.valid=true;pre.context.local_client=0;pre.context.native_thread=thread;
        pre.context.command_number=17;pre.context.native_frame_time=9000;pre.context.connection_state=10;
        pre.context.viewangles[0]=-.8f;pre.context.viewangles[1]=75.9f;
        pre.sequence=25;pre.observed_ms=1000;pre.gameplay_epoch=9;pre.native_cg_identity=cg;
        pre.native_game_time=449318;pre.native_health=100;pre.native_pm_type=1;pre.native_entity_flags=0x84000;
        pre.native_gameplay_fields_valid=true;
        put<U32>(0x2ff5354,cg);put<U32>(0x1c03140,server);put<U32>(player+0x13c,server);put<int>(player,0);
        player_fields(ps);player_fields(server);put<int>(vehicle,505);put<U32>(vehicle+0x148,scr);
        put<std::int16_t>(scr+0x1ec,3);put<U32>(scr+0x1f0,1);put<int>(scr+0x620,0);put<std::int16_t>(info+0x40,6);
        for(U32 offset:{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780})put<int>(cg+offset,0);
        put<int>(cg+0xa4680,2);put<int>(cg+0x8a348,pre.native_game_time);
        put<int>(0x2993850,17);put<int>(0x2481764,9000);put<int>(0x2910160,0);put<int>(0x2910164,10);put<int>(0x2911e2c,0);
        put<int>(ps+0x44c,type);put<int>(server+0x44c,type);put<std::int16_t>(info+0x40,static_cast<std::int16_t>(type));
        if(type==4) {
            // Recorded river PBR driver state, including predicted/server flag difference.
            pre.native_pm_flags=0x08080000;pre.native_entity_flags=0xa4000;pre.native_entity_flags2=0x20000000;
            put<U32>(ps+0xc,pre.native_pm_flags);put<U32>(server+0xc,pre.native_pm_flags);
            put<U32>(ps+0xe0,pre.native_entity_flags);
            put<U32>(ps+0xe4,pre.native_entity_flags2);put<U32>(server+0xe4,pre.native_entity_flags2);
        }
    }
    void huey(bool scripted_path=true) {
        // Recorded Redemption driver graph; catcher is cleared for gameplay.
        pre.native_weapon_flags=0xa0;pre.native_entity_flags=0xa4000;
        put<U32>(ps+0x10,0xa0);put<U32>(server+0x10,0xa0);
        put<U32>(ps+0xe0,0xa4000);put<U32>(server+0xe0,0x84000);
        for(auto role:{ps,server}) {
            put<std::int16_t>(role+0x444,257);put<int>(role+0x44c,6);
        }
        put<int>(huey_vehicle,257);put<U32>(huey_vehicle+0x148,scr);
        put<std::int16_t>(scr+0x1ec,5);put<std::int16_t>(huey_info+0x40,6);
        put<U32>(scr+0x1f0,scripted_path?0x281:0x201);
        put<U32>(0x2899cec,map);put<U32>(map,map_name);put<U32>(map+0xc,0x44);
        put<U32>(map+0x10,7);put<U32>(map+0x18,map_value);
        put(map_name,std::array<char,8>{'m','a','p','n','a','m','e',0});
        put(map_value,std::array<char,15>{'u','n','d','e','r','w','a','t','e','r','b','a','s','e',0});
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(opaque);m.reads.push_back({at,size});
        if(m.fail_read==m.reads.size())return false;
        auto* bytes=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){const auto it=m.data.find(at+i);if(it==m.data.end())return false;bytes[i]=it->second;}
        if(m.mutate&&m.mutate_read==m.reads.size())m.mutate(m);
        return true;
    }
    static bool publication(void* opaque,PreCommandSnapshot* out) noexcept {
        auto& m=*static_cast<Memory*>(opaque);++m.pre_reads;
        if(m.fail_pre)return false;
        *out=m.pre;if(m.change_last_pre&&m.pre_reads>1)++out->sequence;
        return true;
    }
    static DWORD owner(void* opaque) noexcept {return static_cast<Memory*>(opaque)->thread;}
    hc::Host host(){return {this,copy,publication,owner};}
    bool read(hc::Facts* out){
        reads.clear();pre_reads=0;const auto request=pre;const auto command=pre.context;
        return hc::copy_current(host(),base,request,command,now,out);
    }
};
void baseline_and_lifetime(){
    Memory m;hc::Facts first{};
    check(m.read(&first)&&hc::eligible(first),"recorded pilot graph accepted by actual reader");
    check(first.predicted.ps==Memory::ps&&first.authoritative.ps==Memory::server&&first.vehicle_entity==Memory::vehicle&&
          first.scr_vehicle==Memory::scr&&first.info==Memory::info&&first.entity_index==505&&first.info_index==3,
          "exact SP roots seat0 and helicopter6 copied");
    check(first.authoritative.vehicle_pos==0&&first.authoritative.vehicle_type==6&&first.occupant==0,
          "pilot is local server entity0, not an arbitrary linked occupant");
    // Native player command timestamps are not part of this reader's graph.
    m.put<int>(Memory::ps,123);m.put<int>(Memory::server,456);
    hc::Facts current{};check(m.read(&current)&&hc::same_vehicle(first,current),"asynchronous player-state command times are not compared");
    ++m.pre.context.command_number;m.put<int>(0x2993850,m.pre.context.command_number);
    ++m.pre.context.native_frame_time;m.put<int>(0x2481764,m.pre.context.native_frame_time);
    ++m.pre.native_game_time;m.put<int>(Memory::cg+0x8a348,m.pre.native_game_time);
    ++m.pre.sequence;m.pre.native_health=99;m.put<int>(Memory::ps+0x1c4,99);m.put<int>(Memory::server+0x1c4,98);
    m.put<U32>(Memory::scr+0x1f0,0x41);
    check(m.read(&current)&&hc::same_vehicle(first,current),"same vehicle survives valid clock health and unrelated flag progression");
    m.put<int>(Memory::cg+0x8a360,2);
    check(m.read(&current)&&hc::same_vehicle(first,current),"native first-person shadow mode retains vehicle identity");
    // Native AdjustAngles may update globals after the original pre context.
    m.put<float>(0x2911e20,33.f);m.put<float>(0x2911e24,44.f);
    check(m.read(&current),"post-copy uses retained pre angles, not legitimate post-native global view angle changes");
    auto wrong=current;wrong.vehicle_entity+=0x34c;check(!hc::same_vehicle(current,wrong),"changed vehicle is not same current owner");
    wrong=current;wrong.vehicle_flags|=2;check(!hc::same_vehicle(current,wrong),"AI ownership transition invalidates same vehicle");
}
void identity_refusals(){
    const std::vector<std::pair<std::function<void(Memory&)>,const char*>> cases{
        {[](Memory& m){m.put<U32>(0x2ff5354,0);},"missing cg"},
        {[](Memory& m){m.put<U32>(0x2ff5354,0xfffffff0U);},"cg offset overflow"},
        {[](Memory& m){m.put<U32>(0x1c03140,Memory::ps);m.put<U32>(Memory::player+0x13c,Memory::ps);},"predicted and server alias"},
        {[](Memory& m){m.put<U32>(Memory::player+0x13c,Memory::server+4);},"local entity server pointer differs"},
        {[](Memory& m){m.put<int>(Memory::player,1);},"local player entity number differs"},
        {[](Memory& m){m.put<int>(Memory::vehicle,504);},"vehicle self number differs"},
        {[](Memory& m){m.put<U32>(Memory::vehicle+0x148,0);},"missing scr vehicle"},
        {[](Memory& m){m.put<U32>(Memory::vehicle+0x148,Memory::server);},"vehicle state aliases player state"},
        {[](Memory& m){m.put<std::int16_t>(Memory::scr+0x1ec,-1);},"negative info index"},
        {[](Memory& m){m.put<std::int16_t>(Memory::scr+0x1ec,32);},"out-of-bound info index"},
        {[](Memory& m){m.put<std::int16_t>(Memory::info+0x40,5);},"native info is not a helicopter"},
        {[](Memory& m){m.put<int>(Memory::scr+0x620,1);},"another pilot occupies driver seat"},
        {[](Memory& m){m.put<int>(Memory::scr+0x620,1023);},"empty native driver seat"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0);},"pilot flag missing"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x81);},"path control owns helicopter"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,3);},"AI control owns helicopter"},
        {[](Memory& m){m.put<U32>(Memory::scr+0x1f0,0x101);},"path drive owns helicopter"},
        {[](Memory& m){m.put<int>(Memory::cg+0x8a360,1);},"third-person view"},
        {[](Memory& m){m.put<int>(Memory::cg+0xa4680,5);},"turret camera is not pilot camera"},
        {[](Memory& m){m.put<int>(Memory::cg+0x14,3);},"cinematic camera override"},
        {[](Memory& m){m.put<int>(Memory::cg+0xc,1);},"cubemap"},
        {[](Memory& m){m.put<int>(Memory::cg+0xa9d44,1);},"draw suppression"},
        {[](Memory& m){m.put<int>(Memory::cg+0x1c,1);},"special view camera"},
        {[](Memory& m){m.put<int>(Memory::cg+0xa4780,1);},"camera selector override"},
        {[](Memory& m){m.put<int>(Memory::cg+0x8a348,m.pre.native_game_time+1);},"pre snapshot game time changed"},
        {[](Memory& m){m.put<int>(0x2993850,18);},"native command changed"},
        {[](Memory& m){m.put<int>(0x2481764,9001);},"native input clock changed"},
        {[](Memory& m){m.put<int>(0x2910160,0x10);},"native menu opened"},
        {[](Memory& m){m.put<int>(0x2910164,8);},"native connection lost"},
        {[](Memory& m){m.put<int>(0x2911e2c,1);},"native script command override"}
    };
    for(int type:{4,6})for(const auto& [change,label]:cases){Memory m(type);hc::Facts out{};change(m);out.cg=1;
        check(!m.read(&out)&&out==hc::Facts{},label);}
    for(const auto ps:{Memory::ps,Memory::server}){
        for(const auto pm:{0,2,9}){Memory m;m.put<int>(ps+4,pm);hc::Facts out{};check(!m.read(&out),"ordinary frozen or dead movement mode rejected");}
        for(U32 flags:{0x400U,0x800U}){Memory m;m.put<U32>(ps+0xc,flags);hc::Facts out{};check(!m.read(&out),"native flight freeze bits rejected in either role");}
        for(U32 flags:{0x80U,0x100U}){Memory m;m.put<U32>(ps+0x10,flags);hc::Facts out{};check(!m.read(&out),"native weapon disable rejected in either role");}
        for(U32 flags:{0U,0x84100U,0x84200U}){Memory m;m.put<U32>(ps+0xe0,flags);hc::Facts out{};check(!m.read(&out),"missing vehicle or turret flag rejected");}
        for(U32 flags:{0x10000000U,0x40000000U,0x40000U}){Memory m;m.put<U32>(ps+0xe4,flags);hc::Facts out{};check(!m.read(&out),"remote missile or freeze-look state rejected");}
        for(const auto offset:{0x130U,0x144U}){Memory m;m.put<std::uint8_t>(ps+offset,1);hc::Facts out{};check(!m.read(&out),"foreign local client or handheld primary rejected");}
        for(const auto offset:{0x440U,0x448U}){Memory m;m.put<int>(ps+offset,1);hc::Facts out{};check(!m.read(&out),"viewlocked turret or passenger seat rejected");}
        {Memory m;m.put<int>(ps+0x1c4,0);hc::Facts out{};check(!m.read(&out),"dead pilot role rejected");}
        {Memory m;m.put<int>(ps+0x44c,3);hc::Facts out{};check(!m.read(&out),"wrong role vehicle type rejected");}
        for(std::int16_t ent:{std::int16_t(-1),std::int16_t(504),std::int16_t(1022)}){Memory m;m.put<std::int16_t>(ps+0x444,ent);hc::Facts out{};check(!m.read(&out),"vehicle link mismatch or sentinel rejected");}
    }
}
void boat_identity(){
    Memory boat(4),hind;hc::Facts b{},h{};
    check(boat.read(&b)&&hc::eligible(b)&&b.info_type==4,"recorded PBR driver accepted");
    check(hind.read(&h)&&!hc::same_vehicle(b,h),"same pointers with different vehicle type revoke ownership");
    for(auto role:{Memory::ps,Memory::server}) {
        Memory m(4);m.put<int>(role+0x44c,6);hc::Facts out{};
        check(!m.read(&out),"mixed boat and helicopter player roles reject");
        Memory passenger(4);passenger.put<int>(role+0x448,1);
        check(!passenger.read(&out),"boat passenger is not admitted as driver");
    }
    Memory mismatch(4);mismatch.put<std::int16_t>(Memory::info+0x40,6);
    check(!mismatch.read(&b),"boat roles with helicopter info reject");
}
void command_and_changes(){
    Memory baseline;hc::Facts out{};check(baseline.read(&out),"copy sequence baseline");
    const auto calls=baseline.reads;
    // Each required native copy participates in fail-closed admission.
    for(std::size_t i=1;i<=calls.size();++i){Memory m;m.fail_read=i;out=hc::Facts{};out.cg=1;
        if(calls[i-1].at==0x2899cec)
            check(m.read(&out)&&!out.scripted_huey,"ordinary Hind admission does not require optional mission metadata");
        else check(!m.read(&out)&&out==hc::Facts{},"required copied field unavailable rejects empty");}
    auto first_read=[&](std::uintptr_t at){for(std::size_t i=0;i<calls.size();++i)if(calls[i].at==at)return i+1;return std::size_t(0);};
    for(const auto at:{std::uintptr_t(0x2ff5354),std::uintptr_t(0x1c03140),std::uintptr_t(Memory::vehicle+0x148),std::uintptr_t(Memory::scr+0x620)}){
        Memory m;m.mutate_read=first_read(at);check(m.mutate_read!=0,"graph mutation point participates in copy");
        m.mutate=[at](Memory& target){target.put<U32>(at,at==Memory::scr+0x620?1U:0U);};out.cg=1;
        check(!m.read(&out)&&out==hc::Facts{},"graph replaced after first copy refuses second copy");
    }
    {Memory m;constexpr U32 replacement=Memory::scr+0x10000;
        for(const auto& [at,byte]:m.data)if(at>=Memory::scr&&at<Memory::scr+0x624)
            m.put<unsigned char>(replacement+(at-Memory::scr),byte);
        m.put<U32>(Memory::vehicle+0x148,replacement);
        check(m.read(&out)&&out.scr_vehicle==replacement,"replacement vehicle state is independently eligible");
        m.put<U32>(Memory::vehicle+0x148,Memory::scr);
        m.mutate_read=first_read(Memory::vehicle+0x148);
        m.mutate=[](Memory& target){target.put<U32>(Memory::vehicle+0x148,replacement);};
        check(!m.read(&out)&&out==hc::Facts{},"valid identical-content graph replacement between copies rejects");
    }
    {Memory m;m.change_last_pre=true;check(!m.read(&out)&&out==hc::Facts{},"new pre occurrence during copy rejected");}
    {Memory m;m.fail_pre=true;check(!m.read(&out)&&m.reads.empty(),"missing pre refuses before native graph reads");}
    {Memory m;++m.thread;check(!m.read(&out)&&m.reads.empty(),"foreign callback thread refuses without graph reads");}
    {Memory m;m.mutate_read=first_read(Memory::scr+0x620);m.mutate=[](Memory& target){++target.thread;};check(!m.read(&out),"owner changes during read rejected");}
    for(const auto age:{std::uint64_t(100),std::uint64_t(101)}){Memory m;m.now+=age;check(m.read(&out)==(age==100),"100ms maximum request age enforced");}
    {Memory m;m.now=999;check(!m.read(&out),"clock regression rejected");}
    for(const auto slot:{0U,1U,2U,3U,4U,5U}){Memory m;
        if(slot<3)m.pre.context.viewangles[slot]=std::numeric_limits<float>::quiet_NaN();
        else m.pre.context.kickangles[slot-3]=std::numeric_limits<float>::infinity();
        check(!m.read(&out)&&m.reads.empty(),"nonfinite original command angle rejected");}
    {Memory m;auto c=m.pre.context;++c.command_number;check(!hc::copy_current(m.host(),Memory::base,m.pre,c,m.now,&out),"passed command must join pre occurrence");}
    {Memory m;auto c=m.pre.context;c.viewangles[0]=1;check(!hc::copy_current(m.host(),Memory::base,m.pre,c,m.now,&out),"passed command angle must be original pre angle");}
    {Memory m;check(!hc::copy_current(m.host(),Memory::base+1,m.pre,m.pre.context,m.now,&out),"unsupported image base rejected");}
    for(unsigned fault=0;fault<8;++fault){Memory m;
        switch(fault){case 0:m.pre.sequence=0;break;case 1:m.pre.gameplay_epoch=0;break;case 2:m.pre.native_gameplay_fields_valid=false;break;
        case 3:m.pre.context.key_catcher=16;break;case 4:m.pre.context.native_override=true;break;case 5:m.pre.context.valid=false;break;
        case 6:m.pre.context.local_client=1;break;default:m.pre.observed_ms=0;break;}
        check(!m.read(&out)&&m.reads.empty(),"invalid pre context refuses before native reads");
    }
}
void redemption_identity(){
    Memory m;m.huey();hc::Facts path{},flight{},enabled{};
    check(m.read(&path)&&hc::eligible(path)&&path.scripted_huey&&path.entity_index==257&&path.info_index==5,
          "recorded Redemption disabled-handheld driver graph admits script controls");
    check(path.predicted.weapon_flags==0xa0&&path.authoritative.weapon_flags==0xa0&&path.vehicle_flags==0x281&&
          !hc::flight_control_allowed(path),"scripted flight retains native path authority");
    m.put<U32>(Memory::scr+0x1f0,0x201);
    check(m.read(&flight)&&flight.scripted_huey&&hc::flight_control_allowed(flight),
          "ReturnPlayerControl admits flight without changing handheld weapon flags");
    check(!hc::same_vehicle(path,flight),"native path authority transition revokes prior command ownership");
    m.pre.native_weapon_flags=0;m.put<U32>(Memory::ps+0x10,0);m.put<U32>(Memory::server+0x10,0);
    check(m.read(&enabled)&&enabled.scripted_huey&&hc::flight_control_allowed(enabled),
          "mission identity persists after native EnableWeapons and ReturnPlayerControl");
    check(hc::same_vehicle(flight,enabled),"native handheld enable does not discard unchanged Huey role identity");
    auto ordinary=enabled;ordinary.scripted_huey=false;ordinary.map={};
    check(hc::eligible(ordinary)&&!hc::same_vehicle(enabled,ordinary),"changing scripted mission role revokes an otherwise valid driver");
    auto moved=enabled;++moved.map.current;
    check(hc::eligible(moved)&&!hc::same_vehicle(enabled,moved),"equivalent text in replaced map storage is a new ownership identity");
    auto invalid=path;invalid.map.value_text.back()='x';
    check(!hc::eligible(invalid)&&!hc::flight_control_allowed(invalid),"forged exception cannot bypass exact map termination");
    for(unsigned fault=0;fault<20;++fault) {
        Memory bad;bad.huey();hc::Facts out{};
        switch(fault) {
        case 0:bad.put<U32>(0x2899cec,0);break;
        case 1:bad.put<U32>(Memory::map,0);break;
        case 2:bad.put<U32>(Memory::map+0x18,0xfffffff8U);break;
        case 3:bad.put<U32>(Memory::map+0xc,0x40);break;
        case 4:bad.put<U32>(Memory::map+0x10,6);break;
        case 5:bad.put<char>(Memory::map_name+6,'X');break;
        case 6:bad.put<char>(Memory::map_name+7,'X');break;
        case 7:bad.put<char>(Memory::map_value,'X');break;
        case 8:bad.put<char>(Memory::map_value+14,'X');break;
        case 9:bad.put<int>(Memory::scr+0x620,1);break;
        case 10:bad.put<U32>(Memory::player+0x13c,Memory::server+4);break;
        case 11:bad.put<U32>(Memory::scr+0x1f0,0x283);break;
        case 12:bad.put<U32>(Memory::scr+0x1f0,0x381);break;
        case 13:bad.put<U32>(Memory::scr+0x1f0,0x280);break;
        case 14:bad.put<int>(Memory::cg+0xa4680,5);break;
        case 15:bad.put<int>(Memory::cg+0x8a360,1);break;
        case 16:bad.put<int>(0x2910160,16);bad.pre.context.key_catcher=16;break;
        case 17:bad.put<U32>(Memory::huey_vehicle+0x148,Memory::server);break;
        case 18:for(auto role:{Memory::ps,Memory::server})bad.put<int>(role+0x44c,4);
                bad.put<std::int16_t>(Memory::huey_info+0x40,4);break;
        case 19:bad.put<int>(Memory::huey_vehicle,258);break;
        }
        check(!bad.read(&out)&&out==hc::Facts{},"exception rejects other maps malformed metadata ownership flight role and boat state");
    }
    for(auto role:{Memory::ps,Memory::server})for(unsigned fault=0;fault<10;++fault) {
        Memory bad;bad.huey();hc::Facts out{};
        switch(fault) {
        case 0:bad.put<U32>(role+0x10,0x1a0);break;
        case 1:bad.put<U32>(role+0xc,0x400);break;
        case 2:bad.put<U32>(role+0xc,0x800);break;
        case 3:bad.put<int>(role+0x1c4,0);break;
        case 4:bad.put<int>(role+4,0);break;
        case 5:bad.put<std::uint8_t>(role+0x144,4);break;
        case 6:bad.put<int>(role+0x448,1);break;
        case 7:bad.put<U32>(role+0x440,1);break;
        case 8:bad.put<U32>(role+0xe4,0x40000);break;
        case 9:bad.put<std::int16_t>(role+0x444,258);break;
        }
        check(!bad.read(&out)&&out==hc::Facts{},"both Huey roles retain weapon freeze health seat viewlock and entity protections");
    }
    Memory baseline;baseline.huey();hc::Facts out{};check(baseline.read(&out),"scripted Huey copy baseline");
    const auto calls=baseline.reads;
    for(std::size_t i=1;i<=calls.size();++i) {
        Memory bad;bad.huey();bad.fail_read=i;out=hc::Facts{};out.cg=1;
        check(!bad.read(&out)&&out==hc::Facts{},"every exception graph and map copy is required and fails closed");
    }
    auto first_read=[&](std::uintptr_t at){for(std::size_t i=0;i<calls.size();++i)if(calls[i].at==at)return i+1;return std::size_t(0);};
    for(auto at:{U32{0x2899cec},Memory::map,Memory::map+0xc,Memory::map+0x10,Memory::map+0x18,
                 Memory::map_name,Memory::map_value,Memory::scr+0x1f0}) {
        Memory bad;bad.huey();bad.mutate_read=first_read(at);
        check(bad.mutate_read!=0,"exception identity mutation is sampled by production reader");
        bad.mutate=[at](Memory& target){target.put<U32>(at,at==Memory::scr+0x1f0?0x201:0);};
        check(!bad.read(&out)&&out==hc::Facts{},"map or flight authority drift between two native graph copies rejects");
    }
    Memory stable;stable.huey();stable.now+=101;
    check(!stable.read(&out),"Huey mission exception preserves maximum publication age");
}
}
int main(){baseline_and_lifetime();boat_identity();identity_refusals();command_and_changes();redemption_identity();
    std::printf("helicopter context: %u checks, %u failures\n",checks,failures);return failures?1:0;}
