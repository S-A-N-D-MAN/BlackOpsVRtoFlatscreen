// SPDX-License-Identifier: GPL-3.0-only
#include "t5/motorcycle_context.hpp"
#include <cstdio>
#include <cstring>
#include <map>

namespace {
namespace m=bovr::t5::motorcycle;
unsigned checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
m::Facts admission(){
    m::Facts f{};f.pre.sequence=9;f.pre.gameplay_epoch=4;f.pre.native_gameplay_fields_valid=true;
    f.pre.native_cg_identity=f.cg=0x29100000;f.pre.native_game_time=f.native_time=1442589;
    f.pre.native_health=f.health=100;f.pre.native_pm_type=f.pm_type=0;
    f.pre.native_weapon_flags=f.weapon_flags=0x02000080;f.pre.native_entity_flags=f.entity_flags=2;
    f.pre.observed_ms=1000;f.pre.context.valid=true;f.pre.context.command_number=100;
    f.pre.context.native_frame_time=2000;f.pre.context.native_thread=7;f.pre.context.connection_state=10;
    f.ps=f.cg+0x8a364;f.refdef=f.cg+0x8c100;f.offhand=13;f.vehicle_index=1023;
    f.origin={9706.5723f,5648.6714f,1414.1250f};return f;
}
m::Facts vehicle(const m::Facts& start){
    auto f=start;f.pre.sequence++;f.pre.native_game_time=++f.native_time;
    f.pre.native_pm_type=f.pm_type=1;f.pre.native_weapon_flags=f.weapon_flags=0;
    f.pre.native_entity_flags=f.entity_flags=0x4000;f.primary=7;f.vehicle_index=71;
    f.vehicle_entity=0x1a796f8+71*0x34c;f.script_vehicle=0x24000000;f.info=0x1cc1480+2*0x1dc8;f.info_index=2;
    std::memcpy(f.vehicle_name.data(),"motorcycle_player",sizeof("motorcycle_player"));return f;
}
m::Facts mounting(const m::Facts& start){
    auto f=start;f.pre.sequence++;f.pre.native_game_time=++f.native_time;
    f.pre.native_pm_type=f.pm_type=1;f.pre.native_entity_flags=f.entity_flags=0x80002;
    return f;
}
m::Facts blank(const m::Facts& start){
    auto f=start;f.pre.sequence++;f.pre.native_game_time=++f.native_time;
    f.pre.native_weapon_flags=f.weapon_flags=0;f.pre.native_entity_flags=f.entity_flags=0;
    f.origin={};return f;
}
struct NativeFixture {
    std::map<std::uintptr_t,unsigned char> memory;
    m::Facts f;
    template<class T> void put(std::uintptr_t at,const T& value){
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i)memory[at+i]=bytes[i];
    }
    NativeFixture():f(vehicle(admission())) {
        f.vehicle_index=700;f.vehicle_entity=0x1b09ac8;f.script_vehicle=0x1c95520;
        f.info_index=5;f.info=0x1cca968;f.vehicle_type=1;f.primary=0;
        f.pre.native_weapon_flags=f.weapon_flags=0x02000080;
        f.pre.native_entity_flags=f.entity_flags=0x24000;
        put(0x2ff5354,std::uint32_t(f.cg));put(f.cg+0x8a348,f.native_time);
        put(f.ps+4,f.pm_type);put(f.ps+0xc,f.pm_flags);put(f.ps+0x10,f.weapon_flags);
        put(f.ps+0xe0,f.entity_flags);put(f.ps+0xe4,f.entity_flags2);
        put(f.ps+0x130,f.local);put(f.ps+0x134,f.offhand);put(f.ps+0x144,f.primary);
        put(f.ps+0x1c4,f.health);put(f.ps+0x440,f.viewlock);put(f.ps+0x444,f.vehicle_index);
        put(f.ps+0x448,f.vehicle_pos);put(f.ps+0x44c,f.vehicle_type);put(f.refdef+0x20,f.origin);
        constexpr std::uintptr_t offsets[]{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
        for(unsigned i=0;i<7;++i)put(f.cg+offsets[i],f.camera[i]);
        put(f.vehicle_entity,int(f.vehicle_index));put(f.vehicle_entity+0x148,std::uint32_t(f.script_vehicle));
        // Real paused R79 bytes: signed 16-bit index 5, followed by nonzero padding.
        put(f.script_vehicle+0x1ec,std::uint32_t(0x1ca80005));put(f.info,f.vehicle_name);
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& self=*static_cast<NativeFixture*>(opaque);auto* bytes=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){const auto it=self.memory.find(at+i);if(it==self.memory.end())return false;bytes[i]=it->second;}
        return true;
    }
    static bool pre(void* opaque,bovr::t5::PreCommandSnapshot* out) noexcept {*out=static_cast<NativeFixture*>(opaque)->f.pre;return true;}
    static DWORD thread(void*) noexcept {return 7;}
    bool read(m::Facts& out){return m::copy_current({this,copy,pre,thread},0x400000,f.pre,f.pre.context,1050,&out);}
};
}
int main(){
    {
        NativeFixture fixture;m::Facts copied{};
        check(fixture.read(copied)&&copied.info_index==5&&m::named_vehicle(copied),"real native vehicle index excludes nonzero adjacent padding");
        check(m::eligible(copied,fixture.f.cg,fixture.f.pre.gameplay_epoch)&&m::eligible(copied),"copied named motorcycle can start or resume controls after pause");
        fixture.put(fixture.f.script_vehicle+0x1ee,std::uint16_t(0xffff));
        check(fixture.read(copied)&&copied.info_index==5,"unowned vehicle-index padding cannot invalidate controls");
        fixture.put(fixture.f.script_vehicle+0x1ec,std::int16_t(-1));
        check(!fixture.read(copied),"negative native vehicle index fails closed");
        fixture.put(fixture.f.script_vehicle+0x1ec,std::int16_t(32));
        check(!fixture.read(copied),"out-of-range native vehicle index fails closed");
    }
    auto a=admission();check(m::admission(a)&&m::eligible(a),"captured Vorkuta rooftop motorcycle tuple is admitted");
    auto bad=a;bad.origin.x=9000;check(!m::admission(bad),"matching script flags outside the rooftop are rejected");
    check(m::sentinel_motorcycle(bad)&&m::eligible(bad,a.cg,a.pre.gameplay_epoch),"admitted motorcycle remains selected after driving beyond the rooftop bounds");
    check(!m::eligible(bad),"the travelling sentinel tuple cannot independently start motorcycle controls");
    auto moved=bad;moved.pre.gameplay_epoch++;check(!m::eligible(moved,a.cg,a.pre.gameplay_epoch),"travel retention is revoked by a gameplay epoch change");
    auto reverse=a;reverse.pm_flags=reverse.pre.native_pm_flags=0x20;
    check(!m::admission(reverse)&&m::sentinel_motorcycle(reverse)&&m::eligible(reverse,a.cg,a.pre.gameplay_epoch),"native reverse flag is retained only after rooftop admission");
    bad=a;bad.weapon_flags=bad.pre.native_weapon_flags=0x80;check(!m::admission(bad),"generic linked campaign state is rejected");
    bad=a;bad.camera[0]=1;check(!m::admission(bad),"nonzero native camera mode is rejected at admission");
    auto t=mounting(a);check(m::mounting_vehicle(t),"captured Vorkuta scripted motorcycle mount state is recognized");
    check(!m::eligible(t)&&m::eligible(t,a.cg,a.pre.gameplay_epoch),"mount state requires the admitted campaign occurrence");
    check(m::same_motorcycle(a,t),"admission and scripted mount phases join one motorcycle occurrence");
    auto jump=t;jump.pm_flags=jump.pre.native_pm_flags=0x20;
    check(m::mounting_vehicle(jump)&&m::eligible(jump,a.cg,a.pre.gameplay_epoch),"captured post-window mount plus 0x20 flag remains in the motorcycle occurrence");
    check(m::retained_transition(jump.pre,a.cg,a.pre.gameplay_epoch),"post-window 0x20 mount bridges a protected-copy boundary");
    bad=t;bad.offhand=0;check(!m::mounting_vehicle(bad),"unrelated pm_type 1 state is rejected");
    bad=t;bad.pre.gameplay_epoch++;check(!m::eligible(bad,a.cg,a.pre.gameplay_epoch),"a new epoch revokes the scripted mount latch");
    auto p=t.pre;p.native_entity_flags=0x84000;
    check(m::retained_transition(p,a.cg,a.pre.gameplay_epoch),"stable command snapshot bridges the observed 0x84000 mount rewrite");
    auto pt=m::transition_facts(a,p);check(m::mounting_vehicle(pt)&&m::same_motorcycle(a,pt),"bridged command snapshot remains in the admitted motorcycle occurrence");
    auto landing=t;landing.weapon_flags=landing.pre.native_weapon_flags=0x02000088;
    landing.entity_flags=landing.pre.native_entity_flags=0x00084000;
    check(m::mounting_vehicle(landing),"captured post-landing motorcycle state is recognized");
    check(!m::eligible(landing)&&m::eligible(landing,a.cg,a.pre.gameplay_epoch),"post-landing state requires the admitted campaign occurrence");
    check(m::retained_transition(landing.pre,a.cg,a.pre.gameplay_epoch),"post-landing snapshot bridges a protected-copy boundary");
    check(m::same_motorcycle(t,landing),"pre-jump and post-landing states remain one motorcycle occurrence");
    bad=landing;bad.weapon_flags=bad.pre.native_weapon_flags=0x02000090;
    check(!m::mounting_vehicle(bad)&&!m::retained_transition(bad.pre,a.cg,a.pre.gameplay_epoch),"unobserved mount weapon bits are rejected");
    bad=pt;bad.pre.native_cg_identity++;
    check(!m::retained_transition(bad.pre,a.cg,a.pre.gameplay_epoch),"transition bridge rejects another cg identity");
    check(m::retained_transition(a.pre,a.cg,a.pre.gameplay_epoch),"admitted sentinel snapshot bridges a protected-copy miss");
    auto z=blank(a);check(m::mounting_blank(z),"captured zeroed scripted mount boundary is recognized");
    check(!m::eligible(z)&&m::eligible(z,a.cg,a.pre.gameplay_epoch),"zeroed mount boundary requires the admitted campaign occurrence");
    check(m::same_motorcycle(a,z)&&m::same_motorcycle(z,t),"zeroed boundary joins admission and scripted mount phases");
    check(m::retained_transition(z.pre,a.cg,a.pre.gameplay_epoch),"stable command snapshot bridges the observed zeroed mount rewrite");
    bad=z;bad.health=0;check(!m::mounting_blank(bad),"dead zeroed player state is rejected");
    auto v=vehicle(a);check(m::named_vehicle(v),"native motorcycle_player graph is recognized");
    auto gunner=v;gunner.vehicle_name.fill(0);
    std::memcpy(gunner.vehicle_name.data(),"truck_gaz63_player_single50",sizeof("truck_gaz63_player_single50"));
    gunner.vehicle_pos=1;gunner.vehicle_type=0;gunner.primary=0;
    gunner.weapon_flags=gunner.pre.native_weapon_flags=0x02000090;
    gunner.entity_flags=gunner.pre.native_entity_flags=0xa4000;
    check(m::named_gunner(gunner)&&m::eligible(gunner),"captured escape truck gunner is admitted with scripted weapon-disable flags");
    check(!m::same_motorcycle(v,gunner)&&m::same_motorcycle(gunner,gunner),"transfer from bike to gunner revokes prior driving occurrence");
    bad=gunner;bad.vehicle_pos=0;check(!m::named_gunner(bad),"truck driver cannot acquire gunner routing");
    bad=gunner;bad.vehicle_pos=2;check(!m::named_gunner(bad),"unverified passenger seat cannot acquire gunner routing");
    bad=gunner;bad.health=0;check(!m::named_gunner(bad),"dead truck gunner is rejected");
    bad=gunner;bad.pm_flags=0x800;check(!m::named_gunner(bad),"script freeze cannot acquire truck gunner routing");
    bad=gunner;bad.vehicle_name[0]='x';check(!m::named_gunner(bad),"another truck identity is rejected");
    auto apc=gunner;apc.vehicle_name.fill(0);
    std::memcpy(apc.vehicle_name.data(),"apc_btr60_grenade",sizeof("apc_btr60_grenade"));
    apc.weapon_flags=apc.pre.native_weapon_flags=0x02000000;
    apc.entity_flags=apc.pre.native_entity_flags=0xa4002;
    apc.camera={0,0,0,0,0,0,4};
    check(m::rebirth_apc_gunner(apc)&&m::vehicle_gunner(apc)&&m::eligible(apc),"captured Rebirth BTR seated gunner is independently admitted");
    check(!m::named_gunner(apc)&&!m::named_vehicle(apc),"BTR cannot become Vorkuta truck or motorcycle");
    check(m::same_motorcycle(apc,apc)&&!m::same_motorcycle(apc,gunner),"same-seat APC identity is stable but another gunner name revokes it");
    bad=apc;bad.vehicle_pos=0;check(!m::eligible(bad),"BTR driver cannot acquire seated gunner routing");
    bad=apc;bad.vehicle_pos=2;check(!m::eligible(bad),"scripted BTR grenade seat cannot acquire player routing");
    bad=apc;bad.vehicle_type=1;check(!m::eligible(bad),"different vehicle type cannot inherit BTR routing");
    bad=apc;bad.vehicle_name[0]='x';check(!m::eligible(bad),"another APC identity is rejected");
    bad=apc;bad.health=0;check(!m::eligible(bad),"dead BTR gunner is rejected");
    bad=apc;bad.pm_flags=0x800;check(!m::eligible(bad),"script freeze keeps native ownership");
    bad=apc;bad.weapon_flags|=0x80;check(!m::eligible(bad),"disabled BTR weapons cannot acquire input");
    bad=apc;bad.primary=7;check(!m::eligible(bad),"equipped handheld weapon cannot acquire BTR routing");
    bad=apc;bad.entity_flags|=0x10000;check(!m::eligible(bad),"native gunner firing prohibition remains authoritative");
    bad=apc;bad.viewlock=1;check(!m::eligible(bad),"ordinary mounted turret cannot inherit BTR routing");
    for(unsigned i=0;i<apc.camera.size();++i) {
        bad=apc;++bad.camera[i];check(!m::eligible(bad),"changed BTR camera tuple is rejected");
    }
    {
        NativeFixture fixture;m::Facts copied{};
        fixture.f=apc;
        fixture.put(apc.ps+0x10,apc.weapon_flags);fixture.put(apc.ps+0xe0,apc.entity_flags);
        fixture.put(apc.ps+0x448,apc.vehicle_pos);fixture.put(apc.ps+0x44c,apc.vehicle_type);
        fixture.put(apc.ps+0x144,apc.primary);fixture.put(apc.ps+0x444,apc.vehicle_index);
        fixture.put(apc.cg+0xa4680,4);
        fixture.put(apc.vehicle_entity,int(apc.vehicle_index));
        fixture.put(apc.vehicle_entity+0x148,std::uint32_t(apc.script_vehicle));
        fixture.put(apc.script_vehicle+0x1ec,std::int16_t(2));
        fixture.f.info=0x1cc1480+2*0x1dc8;
        fixture.put(fixture.f.info,apc.vehicle_name);
        check(fixture.read(copied)&&m::rebirth_apc_gunner(copied),"actual coherent native reader admits captured BTR vehicle tuple");
    }
    check(m::eligible(v)&&m::eligible(v,a.cg,a.pre.gameplay_epoch),"verified motorcycle graph admits controls independently of rooftop history");
    check(m::same_motorcycle(a,v),"admission and native vehicle phases join one motorcycle occurrence");
    check(m::same_motorcycle(t,v),"scripted mount and native vehicle phases join one motorcycle occurrence");
    bad=v;std::memcpy(bad.vehicle_name.data(),"jeep_player",sizeof("jeep_player"));
    check(!m::named_vehicle(bad)&&!m::eligible(bad,a.cg,a.pre.gameplay_epoch),"other drivable vehicles cannot inherit the latch");
    bad=v;bad.pre.gameplay_epoch++;check(!m::eligible(bad,a.cg,a.pre.gameplay_epoch),"a new gameplay epoch revokes the motorcycle latch");
    auto v2=v;check(m::same_motorcycle(v,v2),"stable protected copies join one native motorcycle");
    v2.vehicle_entity+=0x34c;check(!m::same_motorcycle(v,v2),"vehicle identity change revokes the occurrence");
    auto next=a.pre.context;++next.command_number;next.native_frame_time+=16;
    check(m::retained_snapshot_gap(a,next,1050),"one current command may retain the proven motorcycle across a bounded snapshot miss");
    check(!m::retained_snapshot_gap(a,next,1101),"snapshot gap expires after 100 ms");
    next.key_catcher=1;check(!m::retained_snapshot_gap(a,next,1050),"menu capture revokes a snapshot gap");
    next=a.pre.context;next.command_number+=33;check(!m::retained_snapshot_gap(a,next,1050),"large command discontinuity revokes a snapshot gap");
    std::printf("motorcycle_context_checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
