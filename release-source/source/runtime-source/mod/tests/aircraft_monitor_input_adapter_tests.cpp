// SPDX-License-Identifier: GPL-3.0-only
// Actual protected reader and before/after callbacks over owned CPU storage.
// No retail game, OS keyboard events, executable edits or XR runtime calls.
#define main legacy_aircraft_input_tests_main
#include "native_command_aim_adapter_tests.cpp"
#undef main

namespace {
namespace monitor=t5::aircraft_monitor;
struct MonitorFixture {
    Fixture native;
    t5::Adapter controls{0,false,true,false,true};
    std::uintptr_t map{},server{},predicted{};
    MonitorFixture() {
        controls.base=native.aim.base;controls.execute=sink;controls.cod4_controls=true;
        controls.router=input::ActionRouter(false,true);
        map=native.aim.base+0x9000;server=native.aim.base+0x1000;
        predicted=reinterpret_cast<std::uintptr_t>(native.cg+0x8a364);
        store(native.aim.base+0x2499cec,map);store(map,map+0x80);
        store(map+0xc,std::uint32_t{0x44});store(map+0x10,std::uint32_t{7});store(map+0x18,map+0x90);
        std::memcpy(reinterpret_cast<void*>(map+0x80),"mapname",8);
        std::memcpy(reinterpret_cast<void*>(map+0x90),"wmd_sr71",9);
        store(native.aim.base+0x1803140,server);
        for(auto ps:{predicted,server}) {
            store(ps+4,1);store(ps+0x1c4,100);store(ps+0x10,std::uint32_t{0x80});
            store(ps+0xe0,ps==predicted?std::uint32_t{0xa0002}:std::uint32_t{0x80002});
            store(ps+0xe4,std::uint32_t{});store(ps+0x144,std::uint8_t{});
            store(ps+0x24,std::array<float,3>{-71148,61286,19283});
            store(ps+0x1a8,std::array<float,2>{0,0});
        }
        actions.menu.active=true;
        for(auto& hand:actions.hands) {
            hand.grip.active=hand.grip.orientation_valid=hand.grip.position_valid=true;
            hand.grip.orientation_tracked=hand.grip.position_tracked=true;
            hand.trigger.active=hand.stick.active=hand.squeeze.active=true;
            hand.primary.active=hand.secondary.active=hand.stick_click.active=true;
        }
        refresh();
    }
    void refresh() {
        aim_now+=16;++actions.sequence;
        store(native.aim.base+0x2593850,native.context().command_number+1);
        store(native.aim.base+0x2081764,native.context().native_frame_time+16);
        t5::before_command(0);
        rig.frame.actions=actions;rig.published_ms=aim_now;
    }
    void step(bool fresh_xr=true){
        const auto previous_sequence=actions.sequence;refresh();
        if(!fresh_xr){actions.sequence=previous_sequence;rig.frame.actions=actions;}
        t5::before_native(&controls,native.context());
    }
    void press_all(bool held) {
        actions.hands[0].trigger.current=actions.hands[1].trigger.current=held?1.f:0.f;
        actions.hands[0].primary.current=actions.hands[1].primary.current=held;
        actions.hands[1].secondary.current=held;
    }
    unsigned count(const char* prefix) const {
        return unsigned(std::count_if(dispatches.begin(),dispatches.end(),
            [&](const auto& value){return value.starts_with(prefix);}));
    }
    bool current() const {return monitor::current(native.aim.base,t5::pre_snapshot);}
};
void reader_checks() {
    {
        MonitorFixture f;check(f.current(),"recorded local SR71 monitor native graph accepted");
        monitor::Facts facts{};check(monitor::copy_once(f.native.aim.base,t5::pre_snapshot,facts),"protected graph copier accepts valid owned storage");
        check(facts.predicted.entity_flags!=facts.authoritative.entity_flags,
              "legitimate predicted/server eFlag differences do not disqualify monitor");
        check(facts.map_name==std::array<char,8>{'m','a','p','n','a','m','e',0}&&facts.predicted.primary==0,
              "copied map and weapon identities are exact");
    }
    for(unsigned fault=0;fault<12;++fault) {
        MonitorFixture f;
        switch(fault) {
        case 0:store(f.native.aim.base+0x2499cec,std::uintptr_t{});break;
        case 1:store(f.native.aim.base+0x2499cec,std::uintptr_t{1});break;
        case 2:store(f.map,std::uintptr_t{});break;
        case 3:store(f.map+0x18,std::uintptr_t{0xfffffffdu});break;
        case 4:store(f.map+0xc,std::uint32_t{0x40});break;
        case 5:store(f.map+0x10,std::uint32_t{6});break;
        case 6:std::memcpy(reinterpret_cast<void*>(f.map+0x80),"mapnamX",8);break;
        case 7:std::memcpy(reinterpret_cast<void*>(f.map+0x90),"wmd_sr7X",9);break;
        case 8:store(f.map+0x98,char{'x'});break;
        case 9:store(f.native.aim.base+0x1803140,std::uintptr_t{});break;
        case 10:++t5::pre_snapshot.native_cg_identity;break;
        case 11:t5::pre_snapshot.native_gameplay_fields_valid=false;break;
        }
        check(!f.current(),"unknown map name type flags pointers or pre identity refuses monitor role");
    }
    for(bool authoritative:{false,true})for(unsigned fault=0;fault<15;++fault) {
        MonitorFixture f;const auto ps=authoritative?f.server:f.predicted;
        switch(fault) {
        case 0:store(ps+4,0);break;
        case 1:store(ps+4,5);break;
        case 2:store(ps+0x1c4,0);break;
        case 3:store(ps+0x144,std::uint8_t{4});break;
        case 4:store(ps+0x10,std::uint32_t{});break;
        case 5:store(ps+0xe0,std::uint32_t{0x4000});break;
        case 6:store(ps+0xe0,std::uint32_t{0x100});break;
        case 7:store(ps+0xe4,std::uint32_t{0x40000});break;
        case 8:store(ps+0xe4,std::uint32_t{0x40000000});break;
        case 9:store(ps+0x2c,10000.f);break;
        case 10:store(ps+0x24,std::numeric_limits<float>::quiet_NaN());break;
        case 11:store(ps+0x2c,std::numeric_limits<float>::infinity());break;
        case 12:store(ps+0x1a8,1.f);break;
        case 13:store(ps+0x1ac,std::numeric_limits<float>::quiet_NaN());break;
        case 14:store(ps+0xc,std::uint32_t{0x800});break;
        }
        if(authoritative&&fault==9)
            check(f.current(),"native server altitude does not override the current predicted cockpit role");
        else check(!f.current(),"each native player copy excludes infantry boarding weapons vehicles guided views and invalid clamps");
    }
    for(unsigned field=0;field<7;++field) {
        MonitorFixture f;
        switch(field) {
        case 0:++t5::pre_snapshot.native_pm_type;break;
        case 1:--t5::pre_snapshot.native_health;break;
        case 2:t5::pre_snapshot.native_weapon_flags^=1;break;
        case 3:t5::pre_snapshot.native_entity_flags^=2;break;
        case 4:t5::pre_snapshot.native_entity_flags2^=1;break;
        case 5:t5::pre_snapshot.native_cg_identity=0;break;
        case 6:t5::pre_snapshot.native_pm_flags^=1;break;
        }
        check(!f.current(),"copied role must join the exact pre-command player facts");
    }
    for(unsigned region=0;region<3;++region) {
        MonitorFixture f;DWORD old{};
        const auto at=region==0?f.map:region==1?f.server:(f.predicted&~std::uintptr_t{4095});
        const bool protected_ok=VirtualProtect(reinterpret_cast<void*>(at),4096,PAGE_NOACCESS,&old)!=FALSE;
        check(protected_ok,"owned reader fault region protected");
        if(protected_ok) {
            check(!f.current(),"unreadable map or player storage safely rejects");
            DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(at),4096,old,&ignored);
        }
    }
    {
        MonitorFixture f;monitor::Facts first{},second{};
        check(monitor::copy_once(f.native.aim.base,t5::pre_snapshot,first),"first captured cockpit graph copied");
        store(f.server+0x24,std::array<float,3>{-100999.789f,95946.3906f,8.125f});
        check(monitor::copy_once(f.native.aim.base,t5::pre_snapshot,second)&&monitor::same_read(first,second),
              "paired graph comparison permits observed finite server relocation only");
        for(unsigned field=0;field<13;++field) {
            auto changed=second;
            switch(field) {
            case 0:++changed.server;break;
            case 1:++changed.cg;break;
            case 2:++changed.map;break;
            case 3:changed.predicted.origin[0]+=1;break;
            case 4:--changed.authoritative.health;break;
            case 5:changed.authoritative.pm_flags^=1;break;
            case 6:changed.authoritative.weapon_flags^=1;break;
            case 7:changed.authoritative.entity_flags^=2;break;
            case 8:changed.authoritative.entity_flags2^=1;break;
            case 9:changed.authoritative.primary=4;break;
            case 10:changed.authoritative.clamp_range[0]=1;break;
            case 11:changed.authoritative.origin[0]=std::numeric_limits<float>::quiet_NaN();break;
            case 12:changed.authoritative.origin[1]=std::numeric_limits<float>::infinity();break;
            }
            check(!monitor::same_read(first,changed),"origin exception preserves every other graph and finite-storage requirement");
        }
    }
}
void return_from_ground_regression() {
    constexpr std::array<float,3> cockpit{-71148.7109f,61286.1211f,19283.0762f};
    constexpr std::array<float,3> ground{-100999.789f,95946.3906f,8.125f};
    MonitorFixture f;
    for(auto ps:{f.predicted,f.server}) {
        store(ps+4,0);store(ps+0x144,std::uint8_t{4});store(ps+0x10,std::uint32_t{});store(ps+0x24,ground);
    }
    f.step();check(!f.controls.monitor_active,"ground infantry begins outside monitor context");
    for(auto ps:{f.predicted,f.server}) {
        store(ps+4,1);store(ps+0x144,std::uint8_t{});store(ps+0x10,std::uint32_t{0x80});
        store(ps+0xc,std::uint32_t{0x9840000});
    }
    store(f.predicted+0x24,cockpit);store(f.server+0x24,cockpit);f.step();
    check(f.controls.monitor_active,"return to native cockpit selects monitor after ground section");
    dispatches.clear();f.press_all(true);actions.hands[1].stick.current={.6f,.8f};f.step();
    check(dispatches.size()==5,"all five native commands acquire one held lease on return");
    for(unsigned command_index=0;command_index<18;++command_index) {
        store(f.server+0x24,command_index%2?cockpit:ground);
        f.step(command_index%3==0);
        check(f.controls.monitor_active&&dispatches.size()==5,
              "intermittent server relocation and reused XR sequence cannot release or repeat held monitor commands");
        store(f.server+0x24,command_index%2?ground:cockpit);
        auto command=f.native.bytes(),original=command;
        t5::after_native(&f.controls,f.native.context(),command.data());
        check(command[0x1b]==102&&command[0x1c]==76,
              "pan movement survives native server relocation between pre and post");
        bool other_bytes_preserved=true;
        for(unsigned i=0;i<command.size();++i)if(i!=0x1b&&i!=0x1c)
            other_bytes_preserved=other_bytes_preserved&&command[i]==original[i];
        check(other_bytes_preserved,"return monitor pan preserves all other native command fields");
    }
    f.press_all(false);actions.hands[1].stick.current={};f.step();
    check(dispatches.size()==10&&f.count("-gostand 28676 ")==1&&f.count("-melee 28679 ")==1&&
          f.count("-activate 28674 ")==1&&f.count("-attack 28672 ")==1&&f.count("-speed_throw 28673 ")==1,
          "deliberate release balances each returned monitor lease exactly once");
    for(unsigned exit=0;exit<8;++exit) {
        MonitorFixture e;e.step();e.press_all(true);e.step();
        switch(exit) {
        case 0:store(e.predicted+0x24,ground);break;
        case 1:store(e.predicted+4,0);break;
        case 2:store(e.server+0xc,std::uint32_t{0x800});break;
        case 3:std::memcpy(reinterpret_cast<void*>(e.map+0x90),"cuba\0\0\0\0",9);break;
        case 4:store(e.predicted+0x144,std::uint8_t{4});break;
        case 5:store(e.server+0x24,std::numeric_limits<float>::quiet_NaN());break;
        case 6:store(e.server+0x2c,std::numeric_limits<float>::infinity());break;
        case 7:store(e.predicted+0x24,std::numeric_limits<float>::quiet_NaN());break;
        }
        e.step(false);
        check(!e.controls.monitor_active&&e.count("-gostand 28676 ")==1&&e.count("-melee 28679 ")==1&&
              e.count("-activate 28674 ")==1&&e.count("-attack 28672 ")==1&&e.count("-speed_throw 28673 ")==1,
              "true role exit freeze map weapon or invalid origin releases held monitor commands");
    }
}
void callback_checks() {
    {
        MonitorFixture f;f.step();f.press_all(true);f.step();
        check(f.count("+gostand 28676 ")==1&&f.count("+melee 28679 ")==1&&
              f.count("+activate 28674 ")==1&&f.count("+attack 28672 ")==1&&f.count("+speed_throw 28673 ")==1,
              "actual callback delivers all five native monitor commands with distinct keys");
        check(dispatches.size()==5,"monitor callback emits no reload cycle stance or grenade request");
        f.step();check(dispatches.size()==5,"held monitor controls never repeat native presses");
        f.press_all(false);f.step();
        check(f.count("-gostand 28676 ")==1&&f.count("-melee 28679 ")==1&&
              f.count("-activate 28674 ")==1&&f.count("-attack 28672 ")==1&&f.count("-speed_throw 28673 ")==1,
              "actual callback balances every monitor key");
        actions.hands[0].stick.current={.6f,.8f};f.step();
        check(std::fabs(f.controls.controls.forward-.8f)<.00001f&&std::fabs(f.controls.controls.right-.6f)<.00001f,
              "actual monitor callback bypasses locomotion orientation despite unavailable head pose");
        auto command=f.native.bytes(),original=command;
        t5::after_native(&f.controls,f.native.context(),command.data());
        check(command[0x1b]==102&&command[0x1c]==76,"native forward right bytes receive monitor navigation");
        for(unsigned i=0;i<command.size();++i)if(i!=0x1b&&i!=0x1c)
            check(command[i]==original[i],"monitor navigation preserves every unrelated command byte");
    }
    {
        MonitorFixture f;f.step();actions.hands[1].stick.current={.6f,.8f};f.step();
        check(f.controls.monitor_active&&f.controls.controls.turn==0,
              "camera motion is owned by current monitor occurrence without body turning");
        auto command=f.native.bytes(),original=command;
        t5::after_native(&f.controls,f.native.context(),command.data());
        check(static_cast<std::int8_t>(command[0x1b])==102&&static_cast<std::int8_t>(command[0x1c])==76,
              "actual post callback delivers movement bytes consumed by wmd_rts GetNormalizedMovement");
        for(unsigned i=0;i<command.size();++i)if(i!=0x1b&&i!=0x1c)
            check(command[i]==original[i],"camera control preserves native buttons weapon view and camera-look bytes");
        check(!f.controls.monitor_active,"camera occurrence consumed once");
        command=original;t5::after_native(&f.controls,f.native.context(),command.data());
        check(command==original,"duplicate post cannot replay old monitor camera axes");
        actions.hands[1].stick.current={};f.step();command=original;
        t5::after_native(&f.controls,f.native.context(),command.data());
        check(command==original,"neutral camera stick preserves native desktop camera values");
    }
    for(unsigned change=0;change<5;++change) {
        MonitorFixture f;f.step();actions.hands[1].stick.current={.6f,.8f};
        actions.hands[0].stick.current={.6f,.8f};f.step();
        auto command=f.native.bytes(),original=command;auto context=f.native.context();
        switch(change) {
        case 0:store(f.predicted+0xc,std::uint32_t{0x800});break;
        case 1:store(f.server+0xc,std::uint32_t{0x800});break;
        case 2:store(f.server+0x144,std::uint8_t{4});break;
        case 3:++context.command_number;break;
        case 4:std::memcpy(reinterpret_cast<void*>(f.map+0x90),"cuba\0\0\0\0",9);break;
        }
        t5::after_native(&f.controls,context,command.data());
        check(command==original,"freeze role map or command change before post refuses both monitor axis groups");
    }
    for(unsigned fault=0;fault<6;++fault) {
        MonitorFixture f;f.step();
        switch(fault) {
        case 0:std::memcpy(reinterpret_cast<void*>(f.map+0x90),"cuba\0\0\0\0",9);break;
        case 1:store(f.native.aim.base+0x2499cec,std::uintptr_t{});break;
        case 2:store(f.predicted+0x2c,150.f);store(f.server+0x2c,150.f);break;
        case 3:store(f.predicted+0x144,std::uint8_t{4});store(f.server+0x144,std::uint8_t{4});break;
        case 4:store(f.server+0x1c4,0);break;
        case 5:store(f.server+0x1a8,10.f);break;
        }
        f.step();actions.hands[1].primary.current=true;f.step();
        check(f.count("+gostand ")==0,"unknown map car boarding weapon or mismatched server role never gains monitor command button");
    }
    {
        MonitorFixture f;std::memcpy(reinterpret_cast<void*>(f.map+0x90),"cuba\0\0\0\0",9);
        f.step();actions.hands[0].trigger.current=1;f.step();
        check(f.count("+speed_throw 28673 ")==1&&f.count("+gostand ")==0,
              "Cuba linked car retains right mouse hold without a simultaneous troop command");
    }
    for(unsigned loss=0;loss<3;++loss) {
        MonitorFixture f;f.step();f.press_all(true);f.step();
        if(loss==0){actions.menu.current=true;f.step();}
        if(loss==1){actions.focused=false;f.step();}
        if(loss==2){store(f.native.aim.base+0x2510160,16);f.step();}
        check(f.count("-gostand 28676 ")==1&&f.count("-melee 28679 ")==1&&f.count("-attack 28672 ")==1,
              "pause or focus loss releases actual command and zoom leases");
        actions.menu.current=false;actions.focused=true;store(f.native.aim.base+0x2510160,0);f.step();
        check(f.count("+gostand ")==1,"resume while held cannot replay troop order");
    }
}
}
int main() {
    reader_checks();callback_checks();return_from_ground_regression();
    std::printf("aircraft_monitor_native_checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
