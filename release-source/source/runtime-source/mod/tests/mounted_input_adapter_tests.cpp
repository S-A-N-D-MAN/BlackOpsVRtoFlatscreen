// Actual command/aim/input callbacks plus the actual protected mounted reader.
// Both existing fixture bodies remain separate executable tests. No retail/GPU.
#define main legacy_mounted_aim_main
#include "native_command_aim_adapter_tests.cpp"
#undef main
#define main legacy_mounted_reader_main
#define Fixture MountedMemory
#define check mounted_reader_check
#define checks mounted_reader_checks
#define failures mounted_reader_failures
#define read_pre_command_snapshot mounted_reader_unused_pre
#include "mounted_look_context_tests.cpp"
#undef read_pre_command_snapshot
#undef failures
#undef checks
#undef check
#undef Fixture
#undef main
#include <stdexcept>

namespace {
struct MountedFixture;
MountedFixture* mounted_fixture{};
unsigned delivery_effect{};
unsigned slingshot_writes{};
float slingshot_pitch{},slingshot_yaw{};
void __cdecl mounted_sink(int,int,const char*);
bool capture_slingshot_look(std::uintptr_t,float pitch,float yaw) noexcept {
    ++slingshot_writes;slingshot_pitch=pitch;slingshot_yaw=yaw;return true;
}
struct MountedFixture {
    MountedMemory memory;
    Fixture native;
    t5::Adapter controls;
    MountedFixture():native(true,true),controls(native.aim.base,false,true,true,true,memory.host()) {
        controls.execute=mounted_sink;native.aim.exact_profile=true;native.aim.two_hand_support=true;
        actions.menu.active=true;
        for(auto& h:actions.hands){
            h.grip.active=h.grip.position_valid=h.grip.position_tracked=h.grip.orientation_valid=h.grip.orientation_tracked=true;
            h.stick.active=h.trigger.active=h.primary.active=h.secondary.active=h.stick_click.active=true;
        }
        delivery_effect=0;mounted_fixture=this;sync();
        check(controls.mounted_source.configuration()==ml::Status::accepted,"actual mounted startup reader accepts pinned fixture guards");
    }
    ~MountedFixture(){mounted_fixture=nullptr;}
    void sync() {
        memory.sync();memory.pre.context=memory.request.command;memory.pre.sequence=memory.request.pre_sequence;
        memory.pre.gameplay_epoch=memory.request.gameplay_epoch;memory.pre.observed_ms=aim_now;memory.ticks=aim_now;
        memory.put<int>(0x2993850,memory.request.command.command_number);
        memory.put<int>(0x2481764,memory.request.command.native_frame_time);
        t5::pre_snapshot=memory.pre;rig.frame.actions=actions;memory.reset();
    }
    void prepare(float y,bool fresh=true) {
        ++memory.request.pre_sequence;++memory.request.command.command_number;memory.request.command.native_frame_time+=16;
        memory.put<int>(MountedMemory::cg+0x8a348,memory.get<int>(MountedMemory::cg+0x8a348)+16);
        if(fresh)++actions.sequence;actions.hands[1].stick.current={0,y};sync();
    }
    void invoke() {t5::before_native(&controls,memory.request.command);}
    void step(float y,bool fresh=true) {prepare(y,fresh);invoke();}
    void preserved_post() {
        auto command=native.bytes(),saved=command;t5::after_native(&controls,memory.request.command,command.data());
        check(command==saved&&!native.aim.admission_serial.load(),"mounted post preserves all52 native bytes and no tracked-aim admission");
        check(*reinterpret_cast<float*>(native.image+t5::yaw_rva)==0,"mounted input never writes direct native yaw");
    }
    std::size_t count(const char* prefix)const {
        return static_cast<std::size_t>(std::count_if(dispatches.begin(),dispatches.end(),[&](const auto& s){return s.starts_with(prefix);}));
    }
};
void __cdecl mounted_sink(int,int,const char* command) {
    dispatches.emplace_back(command);
    const unsigned effect=delivery_effect;delivery_effect=0;
    auto& f=*mounted_fixture;
    if(effect==1) { // Ordinary activate exited the turret during delivery.
        f.memory.put<U32>(MountedMemory::ps+0xe0,0);f.memory.put<U32>(MountedMemory::ps+0x440,0);f.memory.sync();
    }
    if(effect==2)t5::before_native(&f.controls,f.memory.request.command);
    if(effect==3){std::thread foreign([&]{auto c=f.memory.request.command;c.native_thread=GetCurrentThreadId();
        t5::before_native(&f.controls,c);auto bytes=f.native.bytes(),saved=bytes;t5::after_native(&f.controls,c,bytes.data());
        check(bytes==saved,"foreign mounted post preserves52 bytes");});foreign.join();}
    if(effect==4)throw std::runtime_error("native-like side effect then throw");
}
void normal_delivery() {
    MountedFixture f;f.step(0);f.step(1);
    check(dispatches.size()==1&&dispatches[0]=="+lookup 28680 9032","up uses exact balanced native key and absolute command clock");
    f.preserved_post();f.step(1,false);f.step(1);
    check(dispatches.size()==1&&f.controls.mounted_owned_keys[8],"duplicate XR and fresh held samples never repeat or release up");
    f.step(-1);check(f.count("-lookup ")==1&&f.count("+lookdown ")==0,"direct reversal releases without down press");
    f.step(-1);check(f.count("+lookdown ")==0,"held reversal cannot rearm");
    f.step(0);f.step(-1);check(f.count("+lookdown 28681 ")==1,"fresh neutral then down uses separate owner key");
    actions.hands[0].stick.current={1,1};actions.hands[0].trigger.current=1;
    actions.hands[0].primary.current=actions.hands[0].secondary.current=actions.hands[0].stick_click.current=true;
    actions.hands[1].secondary.current=true;f.step(-1);f.preserved_post();
    check(dispatches.size()==3&&f.controls.cycle_requests==0,"mounted suppresses movement turn cycle and non-fire/use ordinary actions");
    actions.hands[1].trigger.current=1;f.step(-1);check(f.count("+attack 28672 ")==1,"mounted fire retained");
    actions.hands[1].primary.current=true;f.step(-1);check(f.count("+activate 28674 ")==1,"mounted use retained");
    actions.menu.current=true;f.step(-1);check(f.count("-lookdown ")==1,"menu request releases directional hold");
}
void controller_owned_mounted_delivery() {
    MountedFixture f;
    f.controls.cod4_controls=true;
    f.controls.router=bovr::input::ActionRouter(false,true);
    f.controls.movement_orientation=bovr::input::MovementOrientation::hmd;
    f.step(0);
    // Simultaneous head/stick motion must not regain camera or turret steering
    // while the controller-owned mounted route retains fire/use delivery.
    rig.frame.head_center.orientation={0,std::sin(.6f),0,std::cos(.6f)};
    rig.frame.head_center.position={1,2,3};
    actions.hands[0].stick.current={1,1};
    actions.hands[1].trigger.current=1;
    actions.hands[0].primary.current=true;
    for(float direction:{1.f,-1.f,0.f}) {
        f.prepare(direction);
        actions.hands[1].stick.current.x=direction;
        f.sync();f.invoke();
        check(f.controls.mounted_active&&!f.controls.gameplay&&
              !f.controls.pending_body_yaw.valid&&f.controls.controls.turn==0&&
              f.controls.controls.forward==0&&f.controls.controls.right==0,
              "controller-owned mount suppresses stick turning, movement and HMD body-yaw transfer");
        check(f.count("+lookup ")==0&&f.count("+lookdown ")==0&&
              !f.controls.mounted_owned_keys[8]&&!f.controls.mounted_owned_keys[9],
              "controller-owned mount never grants stick look key ownership");
        f.preserved_post();
    }
    check(f.count("+attack 28672 ")==1&&f.count("+activate 28674 ")==1&&
          dispatches.size()==2,
          "controller-owned mounted route retains right-trigger fire and left-X use only");
    actions.hands[1].trigger.current=0;actions.hands[0].primary.current=false;
    f.step(1);f.preserved_post();
    check(f.count("-attack 28672 ")==1&&f.count("-activate 28674 ")==1&&
          dispatches.size()==4,
          "controller-owned mounted fire/use releases balance without directional look edges");
}
void mounted_fire_jitter_continuity() {
    for(bool cod4:{false,true}) {
        MountedFixture f;f.controls.cod4_controls=cod4;
        f.controls.router=bovr::input::ActionRouter(false,cod4);f.step(0);
        actions.hands[1].trigger.current=1;f.step(0);
        // Retail 0x819D16 sets viewlock2 after every shot; 0x81C581 returns
        // it to1 later. Reproduce those actual callback-to-callback states.
        for(U32 lock:{2U,2U,1U,1U,2U,1U}) {
            f.memory.put<U32>(MountedMemory::ps+0x440,lock);f.step(0);f.step(0,false);
            check(f.controls.mounted_active&&f.controls.mounted_owned_keys[0]&&
                  !f.controls.mounted_release_pending&&f.count("+attack 28672 ")==1&&f.count("-attack 28672 ")==0,
                  "held mounted fire survives native full/jitter transitions without duplicate press or forced release");
            f.preserved_post();
        }
        actions.hands[1].trigger.current=0;f.step(0);
        check(f.count("+attack 28672 ")==1&&f.count("-attack 28672 ")==1&&!f.controls.mounted_owned_keys[0],
              "actual trigger release balances sustained mounted fire once");
    }
    for(U32 invalid_lock:{0U,3U}) {
        MountedFixture f;f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);f.step(0);
        actions.hands[1].trigger.current=1;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0x440,invalid_lock);f.step(0);
        check(!f.controls.mounted_active&&f.count("-attack 28672 ")==1&&!f.controls.mounted_owned_keys[0],
              "viewlock none or unsupported mode still releases mounted fire immediately");
        f.memory.put<U32>(MountedMemory::ps+0x440,1);f.step(0);
        check(f.count("+attack 28672 ")==1,"invalid viewlock recovery still requires fresh trigger neutral");
    }
}
void scoped_tow_fire_continuity() {
    for(bool cod4:{false,true}) {
        MountedFixture f;f.memory.scoped_tow();f.controls.cod4_controls=cod4;
        f.controls.router=bovr::input::ActionRouter(false,cod4);f.step(0);
        actions.hands[1].trigger.current=1;f.step(0);
        check(f.controls.mounted_active&&f.controls.mounted_owned_keys[0]&&
              f.count("+attack 28672 ")==1,"scoped truck TOW acquires native primary attack despite hidden held knife");
        // The moving truck updates its clamp centre every command, and native
        // ADS toggles EF_ADS during targeting. Neither is a mount replacement.
        for(unsigned sample=0;sample<8;++sample) {
            f.memory.put<float>(MountedMemory::ps+0x1a0,10.f+.37f*sample);
            f.memory.put<float>(MountedMemory::ps+0x1a4,120.f-.81f*sample);
            f.memory.put<U32>(MountedMemory::ps+0xe0,(sample&1)?0x80302U:0xa0302U);
            f.memory.put<U32>(MountedMemory::ps+0x440,(sample&2)?2U:1U);
            f.step(0);f.step(0,false);
            check(f.controls.mounted_active&&f.controls.mounted_owned_keys[0]&&
                  !f.controls.mounted_release_pending&&f.count("+attack 28672 ")==1&&f.count("-attack 28672 ")==0,
                  "moving TOW clamp centre and ADS jitter retain one held attack lease across fresh and duplicate samples");
            f.preserved_post();
        }
        actions.hands[1].trigger.current=0;f.step(0);
        check(f.count("-attack 28672 ")==1&&!f.controls.mounted_owned_keys[0],
              "TOW trigger release balances the uninterrupted native attack exactly once");
    }
    // A valid copied mount can still change identity or mechanical limits.
    // Those changes must release fire, even when the trigger remains held.
    for(unsigned fault=0;fault<6;++fault) {
        MountedFixture f;f.memory.scoped_tow();f.controls.cod4_controls=true;
        f.controls.router=bovr::input::ActionRouter(false,true);f.step(0);
        actions.hands[1].trigger.current=1;f.step(0);
        switch(fault) {
        case 0:f.memory.put<float>(MountedMemory::ps+0x1a8,44);break;
        case 1:f.memory.put<float>(MountedMemory::ps+0x1ac,59);break;
        case 2:f.memory.put<U32>(MountedMemory::ps+0xe0,0xa0300);break;
        case 3:f.memory.put<U32>(MountedMemory::ps+0xc,0x1000001);break;
        case 4:f.memory.put<U32>(MountedMemory::tow_variant+0x8c,0x7730100);break;
        case 5:f.memory.put<U8>(MountedMemory::tow_name,'!');break;
        }
        f.step(0);f.step(0);
        check(f.count("+attack 28672 ")==1&&f.count("-attack 28672 ")==1&&!f.controls.mounted_owned_keys[0],
              "TOW limit identity or non-ADS state change releases held fire and cannot rearm without neutral");
    }
    for(unsigned change=0;change<3;++change) {
        MountedFixture f;f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);f.step(0);
        actions.hands[1].trigger.current=1;f.step(0);
        if(change==0)f.memory.put<float>(MountedMemory::ps+0x1a0,11);
        if(change==1)f.memory.put<float>(MountedMemory::ps+0x1a4,121);
        if(change==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0x20100);
        f.step(0);f.step(0);
        check(f.count("+attack 28672 ")==1&&f.count("-attack 28672 ")==1&&!f.controls.mounted_owned_keys[0],
              "ordinary turrets do not inherit TOW clamp-centre or ADS continuity exceptions");
    }
}
void refusals() {
    for(unsigned fault=0;fault<13;++fault) {
        MountedFixture f;f.step(0);f.step(1);f.prepare(1);
        switch(fault) {
        case 0:f.memory.put<U32>(MountedMemory::ps+0xe0,0);break;
        case 1:f.memory.put<std::int16_t>(MountedMemory::ps+0x444,43);break;
        case 2:f.memory.put<int>(MountedMemory::ps+0x1c4,0);break;
        case 3:actions.focused=false;break;
        case 4:actions.hands[0].grip.position_tracked=false;break;
        case 5:++rig.xr_generation;break;
        case 6:f.memory.request.command.key_catcher=16;f.memory.put<int>(0x2910160,16);break;
        case 7:f.memory.command_ok=false;break;
        case 8:f.memory.request.pre_sequence=f.controls.mounted_copy.pre.sequence;break;
        case 9:f.memory.request.command.command_number=f.controls.mounted_copy.pre.command.number;break;
        case 10:f.memory.request.command.native_frame_time=f.controls.mounted_copy.pre.command.clock-1;break;
        case 11:f.memory.put<int>(MountedMemory::cg+0x8a348,f.controls.mounted_copy.facts.game_time-1);break;
        case 12:f.memory.put<float>(MountedMemory::ps+0x1a8,46);break;
        }
        f.sync();f.invoke();f.preserved_post();
        check(f.count("+lookup ")==1&&f.count("-lookup ")==1&&!f.controls.mounted_owned_keys[8],"invalidated context or occurrence releases owned up exactly once");
        f.step(1);check(f.count("+lookup ")==1,"held input cannot recover after invalidation without neutral");
    }
    {MountedFixture f;f.step(0);f.prepare(1);f.memory.fail_at=47;f.invoke();
        check(dispatches.empty(),"failed second native fact pass cannot dispatch look");}
}
void interruption() {
    for(unsigned effect=1;effect<=4;++effect) {
        MountedFixture f;f.step(0);f.prepare(1);actions.hands[1].primary.current=true;f.sync();delivery_effect=effect;
        try{f.invoke();}catch(const std::runtime_error&){}
        check(f.count("+activate ")==1&&f.count("+lookup ")==0,"native use exit reentry foreign callback or throw cancels queued look press");
        f.preserved_post();f.step(1);
        check(f.count("+activate ")==1&&f.count("-activate ")==1,"interrupted mounted use is released without repeated press");
    }
    {MountedFixture f;f.step(0);f.prepare(1);delivery_effect=4;
        try{f.invoke();}catch(const std::runtime_error&){}
        check(f.count("+lookup ")==1&&f.controls.mounted_owned_keys[8],"uncertain native press retains its key ownership");
        f.prepare(1);delivery_effect=4;try{f.invoke();}catch(const std::runtime_error&){}
        check(f.count("-lookup ")==1&&f.controls.mounted_owned_keys[8],"throwing release keeps the key scheduled for owner cleanup");
        f.step(1);check(f.count("+lookup ")==1&&f.count("-lookup ")==2&&!f.controls.mounted_owned_keys[8],"fresh owner retries only idempotent keyed release and never the press");
    }
}
void carry_state(MountedFixture& f) {
    f.memory.put<int>(MountedMemory::ps+4,1);
    f.memory.put<U32>(MountedMemory::ps+0x10,128);
    f.memory.put<U32>(MountedMemory::ps+0xe0,0x80000);
    f.memory.put<U32>(MountedMemory::ps+0x440,0);
    f.memory.put<std::int16_t>(MountedMemory::ps+0x444,1023);
    f.memory.put<int>(MountedMemory::cg+0xa4680,1);
    f.memory.pre.gameplay_active=false;f.sync();
}
void carry_post(MountedFixture& f,bool moving) {
    auto bytes=f.native.bytes(),expected=bytes;
    if(moving){expected[0x1b]=127;expected[0x1c]=0;}
    t5::after_native(&f.controls,f.memory.request.command,bytes.data());
    check(bytes==expected,"carry changes only the two native movement bytes when admitted");
    check(!f.native.aim.admission_serial.load()&&f.controls.cycle_requests==0&&f.count("weapnext")==0&&
          f.count("+lookup ")==0&&f.count("+lookdown ")==0,"carry never grants mounted look cycle or exact weapon aim");
    check(*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==0,"linked carry never uses direct snap-turn");
}
void linked_input() {
    for(bool usable_rig:{true,false}) {
        MountedFixture f;carry_state(f);if(!usable_rig)rig={};f.step(0);
        actions.hands[0].stick.current={0,1};actions.hands[1].primary.current=true;
        f.step(1);carry_post(f,true);
        check(f.count("+activate 28674 ")==1&&!f.controls.mounted_active,"living linked carry retains native use and movement with or without aim rig");
        // A lateral stick still cannot bypass native linked-camera clamps.
        f.prepare(0);actions.hands[1].stick.current={1,0};f.sync();f.invoke();carry_post(f,true);
    }
    for(unsigned fault=0;fault<8;++fault) {
        MountedFixture f;carry_state(f);f.step(0);f.prepare(1);
        actions.hands[0].stick.current={0,1};actions.hands[1].primary.current=true;
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,2);
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0x80100);
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe0,0x84000);
        f.sync();
        if(fault==4)t5::pre_snapshot.observed_ms=aim_now-101;
        if(fault==5)++t5::pre_snapshot.context.command_number;
        if(fault==6)t5::pre_snapshot.native_gameplay_fields_valid=false;
        if(fault==7){std::thread foreign([&]{auto c=f.memory.request.command;c.native_thread=GetCurrentThreadId();
            t5::before_native(&f.controls,c);auto bytes=f.native.bytes(),saved=bytes;
            t5::after_native(&f.controls,c,bytes.data());check(bytes==saved,"foreign carry callbacks preserve native command");});foreign.join();}
        else f.invoke();
        carry_post(f,false);check(dispatches.empty(),"invalid carry context cannot fabricate basic input permission");
    }
    {MountedFixture f;f.step(0);f.step(1);check(f.count("+lookup ")==1,"transition starts with owned mounted up");
        carry_state(f);actions.hands[0].stick.current={0,1};f.step(1);
        auto bytes=f.native.bytes(),saved=bytes;t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        check(f.count("-lookup ")==1&&!f.controls.mounted_owned_keys[8]&&bytes==saved,"turret to carry releases old look and suppresses already-held movement");
        f.step(1);bytes=f.native.bytes();saved=bytes;t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        check(bytes==saved,"continued held carry movement remains inhibited until neutral");
        actions.hands[0].stick.current={0,0};f.step(0);
        actions.hands[0].stick.current={0,1};f.step(0);bytes=f.native.bytes();saved=bytes;saved[0x1b]=127;saved[0x1c]=0;
        t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        check(bytes==saved&&f.count("+lookup ")==1&&!f.native.aim.admission_serial.load(),"fresh neutral restores only basic carry movement after mounted exit");
    }
}
void struggle_state(MountedFixture& f) {
    // Exact alive linked/frozen flags captured in the user's Khe Sanh struggle.
    carry_state(f);f.memory.put<U32>(MountedMemory::ps+0xc,0x4000800);
    f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);f.sync();
}
void scripted_use_input() {
    {
        MountedFixture f;struggle_state(f);f.step(0);
        for(unsigned tap=0;tap<6;++tap) {
            actions.hands[0].primary.current=true;actions.hands[0].stick.current={1,1};
            actions.hands[1].trigger.current=0;actions.hands[0].trigger.current=0;
            actions.hands[1].primary.current=actions.hands[1].secondary.current=true;
            actions.hands[0].secondary.current=true;f.step(0);
            check(f.count("+activate 28674 ")==tap+1&&f.count("-activate 28674 ")==tap&&
                  dispatches.size()==tap*2+1,"frozen campaign X tap delivers exactly one native activate press");
            f.preserved_post();f.step(0,false);f.step(0);
            check(dispatches.size()==tap*2+1,"frozen use held and duplicate samples cannot fabricate repeated taps");
            actions.hands[0].primary.current=false;f.step(0);
            check(f.count("-activate 28674 ")==tap+1&&dispatches.size()==tap*2+2,
                  "frozen campaign X release balances the dedicated native use key");
            f.preserved_post();
        }
        actions.hands[0].primary.current=true;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0xc,0);f.step(0);f.preserved_post();
        check(f.count("-activate 28674 ")==7&&dispatches.size()==14,
              "leaving frozen struggle releases use and requires fresh neutral for held gameplay controls");
    }
    for(unsigned fault=0;fault<12;++fault) {
        MountedFixture f;struggle_state(f);f.step(0);f.prepare(1);actions.hands[0].primary.current=true;actions.hands[0].trigger.current=1;
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,2);
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0x80100);
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe4,0x40000000);
        if(fault==4)f.memory.request.command.key_catcher=16;
        if(fault==5)f.memory.request.command.native_override=true;
        if(fault==6)f.memory.request.command.connection_state=8;
        if(fault==7)actions.focused=false;
        if(fault==8)actions.hands[0].grip.position_tracked=false;
        f.sync();
        if(fault==9)t5::pre_snapshot.observed_ms=aim_now-101;
        if(fault==10)++t5::pre_snapshot.context.command_number;
        if(fault==11)t5::pre_snapshot.native_gameplay_fields_valid=false;
        f.invoke();f.preserved_post();check(dispatches.empty(),"invalid frozen context cannot fabricate use or jump permission");
    }
}
void scripted_jump_input() {
    for(bool stick_jump:{false,true}) {
        MountedFixture f;struggle_state(f);
        // Exact Vorkuta train prompt state: linked, weapons disabled and frozen.
        f.memory.put<U32>(MountedMemory::ps+0xc,0x4000800);
        f.memory.put<U32>(MountedMemory::ps+0xe0,0x80002);f.sync();f.step(0);
        const auto press=[&]{actions.hands[0].trigger.current=stick_jump?0.f:1.f;f.step(stick_jump?1.f:0.f);};
        const auto neutral=[&]{actions.hands[0].trigger.current=0;f.step(0);};
        actions.hands[0].stick.current={1,1};actions.hands[1].trigger.current=0;
        press();f.preserved_post();
        check(f.count("+gostand 28676 ")==1&&dispatches.size()==1,
              "either VR jump control reaches the exact native Space command while frozen, without fire or movement");
        f.step(stick_jump?1.f:0.f,false);press();
        check(dispatches.size()==1,"held and duplicate jump samples do not manufacture repeated presses");
        neutral();f.preserved_post();
        check(f.count("-gostand 28676 ")==1&&dispatches.size()==2,"release balances the frozen scene jump key");
        press();f.memory.request.command.key_catcher=16;f.step(stick_jump?1.f:0.f);
        check(f.count("-gostand 28676 ")==2,"pause releases the scene jump key");
        f.memory.request.command.key_catcher=0;press();
        check(f.count("+gostand 28676 ")==2,"held jump cannot reacquire after pause without neutral");
        neutral();press();check(f.count("+gostand 28676 ")==3,"fresh jump press after pause is delivered");
        f.memory.put<U32>(MountedMemory::ps+0xc,0);f.step(stick_jump?1.f:0.f);f.preserved_post();
        check(f.count("-gostand 28676 ")==3,"leaving frozen prompt releases jump without leaking into linked gameplay");
    }
}
void rebirth_ladder_melee_state(MountedFixture& f) {
    struggle_state(f);
    // Captured R105 Rebirth ladder prompt, with the pause catcher removed by
    // the next ordinary unpaused command. Native weapons remain disabled.
    f.memory.put<U32>(MountedMemory::ps+0xc,0x04202800);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x80);
    f.memory.put<U32>(MountedMemory::ps+0xe0,0x80002);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0x20000000);
    f.memory.put<U8>(MountedMemory::ps+0x144,0);
    f.memory.put<int>(MountedMemory::ps+0x158,49);
    f.memory.put<int>(MountedMemory::cg+0xa9d44,1);f.sync();
}
void scripted_frozen_melee_input() {
    for(bool left_handed:{false,true}) {
        MountedFixture f;rebirth_ladder_melee_state(f);
        f.controls.left_handed=left_handed;f.controls.router=input::ActionRouter(left_handed,true);
        const unsigned hand=left_handed?0u:1u;
        actions.hands[hand].stick_click.current=true;f.step(0);
        check(f.count("+melee ")==0,"held-on-entry frozen ladder melee must first pass neutral in either hand layout");
        actions.hands[hand].stick_click.current=false;f.step(0);
        for(unsigned tap=0;tap<3;++tap) {
            actions.hands[hand].stick_click.current=true;
            actions.hands[hand].trigger.current=0;
            actions.hands[0].stick.current={1,1};f.prepare(0);
            actions.hands[1].stick.current={1,0};f.sync();f.invoke();
            check(f.count("+melee 28679 ")==tap+1&&f.count("+attack ")==0&&
                  !f.controls.gameplay&&!f.controls.controls.forward&&!f.controls.controls.right&&!f.controls.controls.turn,
                  "frozen ladder thumbstick click delivers native V melee without unfreezing movement, firing or turning");
            f.preserved_post();f.step(0,false);f.step(0);
            check(f.count("+melee ")==tap+1,"held and duplicate frozen ladder clicks cannot manufacture another melee press");
            actions.hands[hand].stick_click.current=false;f.step(0);
            check(f.count("-melee 28679 ")==tap+1,"frozen ladder melee releases its dedicated native key once");
        }
        actions.hands[hand].stick_click.current=true;f.step(0);
        f.memory.request.command.key_catcher=16;f.step(0);
        check(f.count("+melee ")==4&&f.count("-melee ")==4,"pause balances frozen ladder melee");
        f.memory.request.command.key_catcher=0;f.step(0);
        check(f.count("+melee ")==4,"resuming a held ladder click cannot manufacture a new melee press");
        actions.hands[hand].stick_click.current=false;f.step(0);
        actions.hands[hand].stick_click.current=true;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0xc,0);f.step(0);
        check(f.count("+melee ")==5&&f.count("-melee ")==5,
              "ending the freeze releases melee before the linked route requires fresh neutral");
        f.preserved_post();
    }
    for(unsigned fault=0;fault<14;++fault) {
        MountedFixture f;rebirth_ladder_melee_state(f);f.step(0);f.prepare(0);
        actions.hands[1].stick_click.current=true;
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,2);
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0x80102);
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe4,0x40000000);
        if(fault==4)f.memory.request.command.key_catcher=16;
        if(fault==5)f.memory.request.command.native_override=true;
        if(fault==6)f.memory.request.command.connection_state=8;
        if(fault==7)actions.focused=false;
        if(fault==8)actions.hands[0].grip.position_tracked=false;
        if(fault==9)actions.hands[1].stick_click.active=false;
        f.sync();
        if(fault==10)t5::pre_snapshot.observed_ms=aim_now-101;
        if(fault==11)++t5::pre_snapshot.context.command_number;
        if(fault==12)t5::pre_snapshot.native_gameplay_fields_valid=false;
        if(fault==13){std::thread foreign([&]{auto c=f.memory.request.command;c.native_thread=GetCurrentThreadId();
            t5::before_native(&f.controls,c);});foreign.join();}
        else f.invoke();
        f.preserved_post();check(dispatches.empty(),"invalid frozen ladder owner, native state or tracking cannot gain melee permission");
    }
}
void dragovich_state(MountedFixture& f) {
    struggle_state(f);
    // Both native player roles at the reported final-mission grab prompt.
    f.memory.put<U32>(MountedMemory::ps+0xc,0x04000800);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x02008080);
    f.memory.put<U8>(MountedMemory::ps+0x144,0);
    f.memory.put<int>(MountedMemory::ps+0x158,0);f.sync();
}
void scripted_frozen_attack_input() {
    for(bool left_handed:{false,true}) {
        MountedFixture f;dragovich_state(f);
        f.controls.left_handed=left_handed;f.controls.router=input::ActionRouter(left_handed,true);
        const unsigned hand=left_handed?0u:1u;
        actions.hands[hand].trigger.current=1;f.step(0);
        check(dispatches.empty(),"Dragovich trigger held on entry needs neutral in either handed layout");
        actions.hands[hand].trigger.current=0;f.step(0);
        for(unsigned tap=0;tap<3;++tap) {
            actions.hands[hand].trigger.current=1;
            actions.hands[0].stick.current={1,1};f.prepare(0);
            actions.hands[1].stick.current={1,0};f.sync();f.invoke();
            check(f.count("+attack 28672 ")==tap+1&&f.count("-attack 28672 ")==tap&&
                  dispatches.size()==tap*2+1&&!f.controls.gameplay&&
                  !f.controls.controls.forward&&!f.controls.controls.right&&!f.controls.controls.turn,
                  "Dragovich trigger dispatches one native left-mouse attack without weapon or movement permission");
            f.preserved_post();f.step(0,false);f.step(0);
            check(f.count("+attack ")==tap+1,"held and duplicate grab inputs do not fabricate extra presses");
            actions.hands[hand].trigger.current=0;f.step(0);
            check(f.count("-attack 28672 ")==tap+1,"grab trigger release balances its native key");
        }
        actions.hands[hand].trigger.current=1;f.step(0);
        f.memory.request.command.key_catcher=16;f.step(0);
        check(f.count("+attack ")==4&&f.count("-attack ")==4,"pause balances Dragovich attack");
        f.memory.request.command.key_catcher=0;f.step(0);
        check(f.count("+attack ")==4,"resume cannot reuse a trigger held across pause");
        actions.hands[hand].trigger.current=0;f.step(0);
        actions.hands[hand].trigger.current=1;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0x10,0);f.step(0);
        check(f.count("+attack ")==5&&f.count("-attack ")==5,
              "native weapon enable withdraws the frozen attack exception and balances its key");
        f.preserved_post();
        check(f.memory.get<U32>(MountedMemory::ps+0xc)==0x04000800&&
              f.memory.get<U8>(MountedMemory::ps+0x144)==0,
              "script button routing never changes native freeze or weapon selection");
    }
    for(unsigned fault=0;fault<17;++fault) {
        MountedFixture f;dragovich_state(f);f.step(0);f.prepare(0);
        actions.hands[1].trigger.current=1;
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,0); // Frozen but not linked.
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0x10,0x02008000); // Weapons enabled.
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe0,0x80102);
        if(fault==4)f.memory.put<U32>(MountedMemory::ps+0xe4,0x40000000);
        if(fault==5)f.memory.request.command.key_catcher=16;
        if(fault==6)f.memory.request.command.native_override=true;
        if(fault==7)f.memory.request.command.connection_state=8;
        if(fault==8)actions.focused=false;
        if(fault==9)actions.hands[0].grip.position_tracked=false;
        if(fault==10)actions.hands[1].grip.orientation_tracked=false;
        if(fault==11)actions.hands[1].trigger.active=false;
        f.sync();
        if(fault==12)t5::pre_snapshot.observed_ms=aim_now-101;
        if(fault==13)++t5::pre_snapshot.context.command_number;
        if(fault==14)t5::pre_snapshot.native_gameplay_fields_valid=false;
        if(fault==15)t5::pre_snapshot.gameplay_epoch=0;
        if(fault==16){std::thread foreign([&]{auto c=f.memory.request.command;c.native_thread=GetCurrentThreadId();
            t5::before_native(&f.controls,c);});foreign.join();}
        else f.invoke();
        f.preserved_post();check(dispatches.empty(),
            "nonlinked, weapon-enabled, stale, untracked or invalid frozen contexts cannot gain attack");
    }
}
void car_state(MountedFixture& f) {
    carry_state(f);
    // Live paused Cuba car copy user-car-context-01.json, with the pause catcher
    // removed by a normal unpaused command. These incidental flags are evidence,
    // not extra vehicle-selector requirements.
    f.memory.put<U32>(MountedMemory::ps+0xc,0x1810000);
    f.memory.put<U32>(MountedMemory::ps+0xe0,0xa0002);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0);
    f.memory.put<int>(MountedMemory::ps+0x1c4,40);
    f.memory.request.command.connection_state=10;
    f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);f.sync();
}
void scripted_linked_input() {
    {
        MountedFixture f;car_state(f);
        f.memory.put<U32>(MountedMemory::ps+0xc,2);
        f.memory.put<U32>(MountedMemory::ps+0x10,0x02000080);
        f.memory.put<U32>(MountedMemory::ps+0xe0,0xa0006);
        f.memory.put<U32>(MountedMemory::ps+0xe4,0x20000000);
        actions.hands[1].stick_click.current=true;f.step(0);
        check(f.count("+melee ")==0,"held-on-entry tunnel melee requires neutral before dispatch");
        actions.hands[1].stick_click.current=false;f.step(0);
        for(unsigned tap=0;tap<3;++tap){
            actions.hands[1].stick_click.current=true;f.step(0);
            check(f.count("+melee 28679 ")==tap+1&&f.count("+attack ")==0,
                "each fresh tunnel right-stick click reaches native V melee without firearm attack");
            f.step(0,false);f.step(0);check(f.count("+melee ")==tap+1,
                "duplicate and held tunnel melee do not manufacture extra dig taps");
            actions.hands[1].stick_click.current=false;f.step(0);
            check(f.count("-melee 28679 ")==tap+1,"tunnel melee key lease releases exactly once");
            f.preserved_post();
        }
        actions.hands[1].stick_click.current=true;f.step(0);
        f.memory.request.command.key_catcher=16;f.step(0);
        check(f.count("+melee ")==4&&f.count("-melee ")==4,"pause balances held tunnel melee");
        f.memory.request.command.key_catcher=0;f.step(0);
        check(f.count("+melee ")==4,"resuming with melee held cannot dig until neutral");
        actions.hands[1].stick_click.current=false;f.step(0);
        actions.hands[1].stick_click.current=true;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0xc,0x800);f.step(0);
        check(f.count("-melee ")==5,"native frozen context still withdraws linked melee permission");
    }
    {
        MountedFixture f;car_state(f);f.step(0);
        actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;
        actions.hands[0].primary.current=true;
        actions.hands[0].secondary.current=actions.hands[1].primary.current=actions.hands[1].secondary.current=true;
        actions.hands[0].stick_click.current=true;
        f.step(1);f.preserved_post();
        check(f.count("+attack 28672 ")==1&&f.count("+speed_throw 28673 ")==1&&
              f.count("+activate 28674 ")==1&&dispatches.size()==3,
              "recorded Cuba context maps both held native mouse buttons and X only");
        f.step(1,false);f.step(1);f.preserved_post();
        check(dispatches.size()==3,"scripted reverse stays held across duplicate and fresh samples without pulsing");
        actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;actions.hands[0].primary.current=false;
        f.step(1);f.preserved_post();
        check(f.count("-attack 28672 ")==1&&f.count("-speed_throw 28673 ")==1&&
              f.count("-activate 28674 ")==1&&dispatches.size()==6,
              "scripted trigger and use releases balance their distinct native keys");
    }
    {
        MountedFixture f;carry_state(f);f.controls.cod4_controls=true;
        f.controls.router=bovr::input::ActionRouter(false,true);f.step(0);
        actions.hands[0].stick.current={0,1};actions.hands[0].primary.current=true;
        f.step(1);carry_post(f,true);
        check(f.count("+activate 28674 ")==1,"COD4 linked disabled-weapon carry retains X and analog movement");
    }
    {
        MountedFixture f;car_state(f);f.step(0);actions.hands[0].trigger.current=1;f.step(0);
        f.memory.put<int>(MountedMemory::ps+4,0);f.memory.put<U32>(MountedMemory::ps+0x10,0);f.step(0);
        check(f.count("-speed_throw 28673 ")==1&&f.count("+gostand ")==0,
              "leaving linked context releases reverse and cannot turn held trigger into jump");
        f.step(0);check(f.count("+gostand ")==0,"held trigger cannot rearm ordinary jump after linked exit");
        actions.hands[0].trigger.current=0;f.step(0);actions.hands[0].trigger.current=1;f.step(0);
        check(f.count("+gostand 28676 ")==1,"fresh neutral restores ordinary COD4 left-trigger jump");
    }
    for(unsigned fault=0;fault<15;++fault) {
        MountedFixture f;car_state(f);f.step(0);actions.hands[0].trigger.current=1;f.step(0);
        f.prepare(0);
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,2);
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0xa0102);
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe4,0x40000000);
        if(fault==4)f.memory.request.command.key_catcher=16;
        if(fault==5)f.memory.request.command.native_override=true;
        if(fault==6)f.memory.request.command.connection_state=8;
        if(fault==7)actions.focused=false;
        if(fault==8)actions.hands[0].grip.position_tracked=false;
        if(fault==9)f.memory.put<U32>(MountedMemory::ps+0xc,0x1810800);
        if(fault==10)f.memory.put<U32>(MountedMemory::ps+0x10,0);
        f.sync();
        if(fault==11)t5::pre_snapshot.observed_ms=aim_now-101;
        if(fault==12)++t5::pre_snapshot.context.command_number;
        if(fault==13)t5::pre_snapshot.native_gameplay_fields_valid=false;
        if(fault==14){f.memory.request.command.native_thread+=1;t5::pre_snapshot.context=f.memory.request.command;}
        f.invoke();f.preserved_post();
        if(fault==14) {
            check(f.count("-speed_throw ")==0,"foreign callback cannot dispatch a native release on another thread");
            f.memory.request.command.native_thread=GetCurrentThreadId();actions.hands[0].trigger.current=1;f.step(0);
            check(f.count("+speed_throw ")==1&&f.count("-speed_throw ")==0,
                  "ignored foreign callback cannot duplicate or cancel the current owner's held reverse");
            actions.hands[0].trigger.current=0;f.step(0);
            check(f.count("-speed_throw ")==1,"owner trigger release balances reverse after an ignored foreign callback");
            continue;
        }
        check(f.count("+speed_throw ")==1&&f.count("-speed_throw ")==1&&f.count("+gostand ")==0,
              "linked refusal or context loss releases reverse without converting a held trigger");
        f.step(0);check(f.count("+speed_throw ")==1,"restored context with still-held trigger requires neutral");
    }
    for(unsigned selector=0;selector<3;++selector) {
        MountedFixture f;car_state(f);
        if(selector==0)f.memory.put<int>(MountedMemory::ps+4,0);
        if(selector==1)f.memory.put<U32>(MountedMemory::ps+0x10,0);
        if(selector==2){f.controls.cod4_controls=false;f.controls.router=bovr::input::ActionRouter(false,false);}
        f.step(0);
        if(selector<2) {
            actions.hands[0].trigger.current=1;f.step(0);
            check(f.count("+speed_throw ")==0&&f.count("+gostand ")==1,
                  "ordinary PM or enabled weapon context keeps COD4 left-trigger jump");
        } else {
            actions.hands[0].primary.current=true;f.step(0);
            check(f.count("+activate ")==0&&f.count("+gostand ")==1,
                  "non-COD4 controls keep their existing support-button mapping");
        }
    }
}
void slingshot_input() {
    MountedFixture f;
    f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);
    f.controls.write_helicopter_look=capture_slingshot_look;
    f.memory.put<int>(MountedMemory::ps+4,1);
    f.memory.put<U32>(MountedMemory::ps+0xc,0x00200000);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x02000080);
    f.memory.put<U32>(MountedMemory::ps+0xe0,0x000a0002);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0);
    f.memory.put<int>(MountedMemory::cg+0x8a348,5000);
    f.memory.pre.gameplay_active=false;
    f.memory.pre.native_cg_identity=reinterpret_cast<std::uintptr_t>(f.native.cg);
    const auto ps=reinterpret_cast<std::uintptr_t>(f.native.cg+0x8a364);
    store(ps+0x144,static_cast<unsigned char>(0));store(ps+0x134,13);
    store(ps+0x84,2.252197f);store(ps+0x88,-52.025757f);store(ps+0x8c,-22.236328f);
    store(ps+0x1a0,355.f);store(ps+0x1a4,186.000015f);
    store(ps+0x1a8,15.f);store(ps+0x1ac,25.f);
    store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8a348),5000);
    store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8c158),5000);
    const xr::Vec3f origin{-2654.4563f,4569.2900f,1482.0892f};
    const xr::Basis3f axis{};
    store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8c120),origin);
    store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8c134),axis);
    actions.hands[1].aim.pose.orientation={0,0.2588190451f,0,0.9659258263f};
    rig.frame.actions=actions;rig.frame.head_center.orientation.w=1;rig.tracking_anchor.orientation.w=1;
    slingshot_writes=0;slingshot_pitch=slingshot_yaw=0;
    const auto step=[&](float right_y){
        f.prepare(right_y);const auto now=f.memory.pre.native_game_time;
        store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8a348),now);
        store(reinterpret_cast<std::uintptr_t>(f.native.cg+0x8c158),now);f.invoke();
    };
    f.sync();step(0);
    check(slingshot_writes==1&&std::fabs(slingshot_pitch+7.252197f)<.02f&&std::fabs(slingshot_yaw+121.974228f)<.02f,
          "Vorkuta slingshot calibrates the first right-controller pose to the native aim centre and compensates delta angles");
    actions.hands[1].aim.pose.orientation={0,0.3420201433f,0,0.9396926208f};
    actions.hands[0].stick.current={0,-1};step(0);
    auto bytes=f.native.bytes(),expected=bytes;expected[0x1b]=static_cast<unsigned char>(-127);expected[0x1c]=0;
    t5::after_native(&f.controls,f.memory.request.command,bytes.data());
    check(bytes==expected&&slingshot_writes==2&&std::fabs(slingshot_yaw+111.974228f)<.03f,
          "slingshot keeps left-stick down as unrotated native pull while relative controller aim updates continuously");
    f.memory.put<U32>(MountedMemory::ps+0xe0,2);step(0);
    check(slingshot_writes==2&&!f.controls.slingshot_active,
          "leaving the exact slingshot state stops native view writes immediately");
}
void vehicle_weapon_state(MountedFixture& f) {
    // Exact unpaused semantics from the user's Cuba anti-air incident:
    // PM_NORMAL_LINKED, EF_VEHICLE_ACTIVE, CAM_VEHICLE and no ordinary weapon.
    carry_state(f);
    f.memory.put<U32>(MountedMemory::ps+0xc,0);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x02000000);
    f.memory.put<U32>(MountedMemory::ps+0xe0,0x000a4000);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0);
    f.memory.put<U32>(MountedMemory::ps+0x440,0);
    f.memory.put<std::int16_t>(MountedMemory::ps+0x444,223);
    f.memory.put<int>(MountedMemory::cg+0xa4680,2);
    f.memory.request.command.connection_state=10;
    f.controls.cod4_controls=true;f.controls.router=bovr::input::ActionRouter(false,true);f.sync();
}
void vehicle_weapon_input() {
    {
        MountedFixture f;vehicle_weapon_state(f);f.step(0);
        actions.hands[1].trigger.current=1;actions.hands[0].trigger.current=1;
        actions.hands[0].stick.current={1,1};actions.hands[1].stick.current={1,1};
        actions.hands[0].primary.current=actions.hands[0].secondary.current=true;
        actions.hands[1].primary.current=actions.hands[1].secondary.current=true;
        f.step(1);f.preserved_post();
        check(f.count("+vehicleattack 28720 ")==1&&f.count("+attack ")==0&&
              f.count("+activate 28674 ")==1&&dispatches.size()==2&&f.controls.vehicle_attack_owned,
              "captured anti-air context maps right trigger to native vehicleattack only");
        f.step(1,false);f.step(1);f.preserved_post();
        check(dispatches.size()==2,"held and duplicate anti-air trigger never repeat fire");
        actions.hands[1].trigger.current=0;actions.hands[0].primary.current=false;f.step(1);f.preserved_post();
        check(f.count("-vehicleattack 28720 ")==1&&f.count("-activate 28674 ")==1&&
              dispatches.size()==4&&!f.controls.vehicle_attack_owned,
              "anti-air trigger and use releases balance their owned keys");
    }
    {
        MountedFixture f;vehicle_weapon_state(f);f.step(0);actions.hands[1].trigger.current=1;f.step(0);
        f.memory.put<U32>(MountedMemory::ps+0xe0,0x000a0000);f.step(0);
        check(f.count("+vehicleattack 28720 ")==1&&f.count("-vehicleattack 28720 ")==1&&
              !f.controls.vehicle_attack_owned&&!f.controls.vehicle_weapon_active,
              "leaving vehicle weapon mode releases the owned native fire key exactly once");
        f.step(0);check(f.count("+attack ")==0,"held trigger cannot leak into ordinary fire after vehicle exit");
        actions.hands[1].trigger.current=0;f.step(0);actions.hands[1].trigger.current=1;f.step(0);
        check(f.count("+attack 28672 ")==1,"fresh neutral restores ordinary right-trigger fire");
    }
    for(unsigned fault=0;fault<10;++fault) {
        MountedFixture f;vehicle_weapon_state(f);f.step(0);f.prepare(0);actions.hands[1].trigger.current=1;
        if(fault==0)f.memory.put<int>(MountedMemory::ps+0x1c4,0);
        if(fault==1)f.memory.put<int>(MountedMemory::ps+4,0);
        if(fault==2)f.memory.put<U32>(MountedMemory::ps+0xe0,0x000a0000);
        if(fault==3)f.memory.put<U32>(MountedMemory::ps+0xe0,0x000a4100);
        if(fault==4)f.memory.put<U32>(MountedMemory::ps+0xe4,0x40000000);
        if(fault==5)f.memory.put<U32>(MountedMemory::ps+0xc,0x800);
        if(fault==6)f.memory.request.command.key_catcher=16;
        if(fault==7)f.memory.request.command.native_override=true;
        if(fault==8)f.memory.request.command.connection_state=8;
        f.sync();if(fault==9)t5::pre_snapshot.observed_ms=aim_now-101;
        f.invoke();f.preserved_post();
        check(f.count("+vehicleattack ")==0,"invalid vehicle tuple cannot fabricate anti-air fire");
    }
}
float ladder_test_delta{};
bool ladder_test_delta_available=true;
unsigned ladder_test_delta_effect{};
bool ladder_delta_fixture(std::uintptr_t,const t5::PreCommandSnapshot&,float* out) noexcept {
    if(!ladder_test_delta_available||!out)return false;*out=ladder_test_delta;
    if(ladder_test_delta_effect==1)aim_now+=101;
    if(ladder_test_delta_effect==2)++t5::pre_snapshot.sequence;
    if(ladder_test_delta_effect==3)generation_current=false;
    return true;
}
void ladder_test_state(MountedFixture& f,bool attached,input::MovementOrientation orientation) {
    ladder_test_delta=0;ladder_test_delta_available=true;ladder_test_delta_effect=0;f.controls.copy_ladder_delta=ladder_delta_fixture;
    f.memory.put<int>(MountedMemory::ps+4,0);
    f.memory.put<U32>(MountedMemory::ps+0xc,attached?8U:0U);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x2000000);
    f.memory.put<U32>(MountedMemory::ps+0xe0,2);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0);
    f.memory.put<U32>(MountedMemory::ps+0x440,0);
    f.memory.put<int>(MountedMemory::cg+0xa4680,0);
    f.memory.pre.gameplay_active=true;
    f.controls.cod4_controls=true;f.controls.router=input::ActionRouter(false,true);
    f.controls.movement_orientation=orientation;
    constexpr float half_pi=.7853981633974483f;
    const xr::Quaternionf quarter_turn{0,std::sin(half_pi),0,std::cos(half_pi)};
    rig.frame.head_center.position={0,1.7f,0};rig.frame.head_center.orientation=quarter_turn;
    for(auto& h:actions.hands) {
        h.aim.active=h.aim.orientation_valid=h.aim.orientation_tracked=true;
        h.aim.pose.orientation=quarter_turn;h.grip.pose.orientation=quarter_turn;
    }
    f.sync();f.step(0); // Fresh neutral arms the existing router.
}
void ladder_test_prepare(MountedFixture& f,float direction) {
    f.prepare(0);actions.hands[0].stick.current={0,direction};f.sync();
}
void ladder_test_post(MountedFixture& f,int forward,int right) {
    auto bytes=f.native.bytes(),expected=bytes;
    const bool expect_pitch=f.controls.ladder_occurrence&&forward!=0;
    if(expect_pitch) {
        const auto pitch=t5::ladder_command_pitch(ladder_test_delta);
        memcpy(expected.data()+0xc,&pitch,sizeof(pitch));
    }
    if(forward||right) {
        expected[0x1b]=static_cast<unsigned char>(static_cast<std::int8_t>(forward));
        expected[0x1c]=static_cast<unsigned char>(static_cast<std::int8_t>(right));
    }
    t5::after_native(&f.controls,f.memory.request.command,bytes.data());
    check(bytes==expected,"ladder callback changes only admitted axes and nonzero climb pitch; preserves every other native byte");
}
void ladder_input() {
    for(auto orientation:{input::MovementOrientation::hmd,input::MovementOrientation::controller}) {
        for(bool attached:{false,true})for(float direction:{1.f,-1.f}) {
            MountedFixture f;ladder_test_state(f,attached,orientation);ladder_test_prepare(f,direction);f.invoke();
            check(f.controls.ladder_occurrence==attached,"only current PMF_LADDER selects raw ladder movement");
            check(!attached||!f.controls.pending_body_yaw.valid,"ladder look direction never schedules HMD body-yaw transfer");
            if(!attached&&orientation==input::MovementOrientation::hmd)
                check(f.controls.pending_body_yaw.valid,"ordinary HMD movement still schedules body-yaw transfer");
            ladder_test_post(f,attached?static_cast<int>(127*direction):0,
                               attached?0:static_cast<int>(-127*direction));
            if(attached) {
                check(!f.controls.ladder_occurrence&&f.controls.controls.forward==0&&f.controls.controls.right==0,
                      "ladder post consumes both occurrence and axes");
                ladder_test_post(f,0,0);
                f.memory.put<U32>(MountedMemory::ps+0xc,0);ladder_test_prepare(f,direction);f.invoke();
                check(!f.controls.ladder_occurrence,"leaving ladder cannot retain its raw-axis route");
                ladder_test_post(f,0,static_cast<int>(-127*direction));
            }
        }
    }
    {MountedFixture f;ladder_test_state(f,true,input::MovementOrientation::hmd);
        ladder_test_prepare(f,0);actions.hands[0].stick.current={1,0};f.sync();f.invoke();
        ladder_test_post(f,0,127);
        check(!f.controls.pending_body_yaw.valid,"ladder strafe retains no HMD body-yaw transfer");}
    for(unsigned fault=0;fault<13;++fault) {
        MountedFixture f;ladder_test_state(f,true,input::MovementOrientation::hmd);ladder_test_prepare(f,1);
        switch(fault) {
        case 0:t5::pre_snapshot.observed_ms=aim_now-101;break;
        case 1:++t5::pre_snapshot.context.command_number;break;
        case 2:f.memory.request.command.key_catcher=16;t5::pre_snapshot.context.key_catcher=16;break;
        case 3:f.memory.request.command.native_override=true;t5::pre_snapshot.context.native_override=true;break;
        case 4:t5::pre_snapshot.native_health=0;break;
        case 5:t5::pre_snapshot.native_pm_flags|=0x800;break;
        case 6:rig.published_ms=aim_now-101;break;
        case 7:rig.published_ms=aim_now+1;break;
        case 8:++rig.xr_generation;break;
        case 9:++rig.frame.actions.sequence;break;
        case 10:actions.focused=false;rig.frame.actions=actions;break;
        case 11:generation_current=false;break;
        case 12:rig_live=false;break;
        }
        f.invoke();check(!f.controls.pending_body_yaw.valid,"invalid ladder context or rig never grants body-yaw transfer");
        ladder_test_post(f,0,0);
    }
    for(unsigned fault=0;fault<11;++fault) {
        MountedFixture f;ladder_test_state(f,true,input::MovementOrientation::hmd);ladder_test_prepare(f,1);f.invoke();
        check(f.controls.ladder_occurrence,"post refusal fixture starts with admitted ladder occurrence");
        switch(fault) {
        case 0:aim_now+=101;break;
        case 1:++t5::pre_snapshot.sequence;break;
        case 2:++t5::pre_snapshot.gameplay_epoch;break;
        case 3:t5::pre_snapshot.native_pm_flags=0;break;
        case 4:generation_current=false;break;
        case 5:f.memory.request.command.key_catcher=16;t5::pre_snapshot.context.key_catcher=16;break;
        case 6:++t5::pre_snapshot.native_game_time;break;
        case 7:++t5::pre_snapshot.native_cg_identity;break;
        // Matching current/post contexts are insufficient: the occurrence was
        // captured for the original command, frame and view input.
        case 8:++f.memory.request.command.command_number;
            t5::pre_snapshot.context=f.memory.request.command;break;
        case 9:++f.memory.request.command.native_frame_time;
            t5::pre_snapshot.context=f.memory.request.command;break;
        case 10:f.memory.request.command.viewangles[0]+=1;
            t5::pre_snapshot.context=f.memory.request.command;break;
        }
        ladder_test_post(f,0,0);
        check(!f.controls.ladder_occurrence&&f.controls.controls.forward==0&&f.controls.controls.right==0,
              "invalidated ladder post discards its axes instead of caching them");
    }
    {MountedFixture f;ladder_test_state(f,true,input::MovementOrientation::hmd);ladder_test_prepare(f,1);f.invoke();
        ladder_test_post(f,127,0);
        f.memory.request.command.key_catcher=16;f.sync();f.invoke();
        ladder_test_post(f,0,0);
        check(!f.controls.ladder_occurrence,"menu does not retain preceding ladder input or pitch");}
    for(unsigned fault=0;fault<7;++fault) {
        MountedFixture f;ladder_test_state(f,true,input::MovementOrientation::hmd);ladder_test_prepare(f,1);f.invoke();
        if(fault==0)ladder_test_delta_available=false;
        if(fault==1)ladder_test_delta=std::numeric_limits<float>::infinity();
        if(fault==2)ladder_test_delta=std::numeric_limits<float>::quiet_NaN();
        if(fault==3)ladder_test_delta=360001.f;
        if(fault>=4)ladder_test_delta_effect=fault-3;
        ladder_test_post(f,0,0);
        check(!f.controls.ladder_occurrence,"unreadable or invalid delta consumes ladder sample without partial axis/pitch writes");
    }
}
void ladder_pitch_reader_guards() {
    for(unsigned fault=0;fault<15;++fault) {
        Fixture f;const auto ps=reinterpret_cast<std::uintptr_t>(f.cg+0x8a364);
        store(ps+0xc,8U);t5::pre_snapshot.native_pm_flags=8;store(ps+0x84,31.75f);
        auto pre=t5::pre_snapshot;auto base=f.aim.base;float delta=999.f;DWORD protection{};
        switch(fault) {
        case 1:base=0;break;
        case 2:++pre.native_cg_identity;break;
        case 3:store(reinterpret_cast<std::uintptr_t>(f.cg+0x8a348),pre.native_game_time+1);break;
        case 4:store(ps+4,1);break;
        case 5:store(ps+0xc,0U);break;
        case 6:store(ps+0x1c4,0);break;
        case 7:store(ps+0xe0,pre.native_entity_flags|0x100U);break;
        case 8:store(ps+0xe4,pre.native_entity_flags2|0x40000000U);break;
        case 9:store(ps+0x84,std::numeric_limits<float>::quiet_NaN());break;
        case 10:store(ps+0x84,std::numeric_limits<float>::infinity());break;
        case 11:store(ps+0x84,360001.f);break;
        case 12:pre.observed_ms=aim_now-101;break;
        case 13:pre.context.key_catcher=16;break;
        case 14:check(VirtualProtect(f.cg,0xb0000,PAGE_NOACCESS,&protection)!=0,"ladder reader fixture page guard established");break;
        }
        const bool copied=t5::copy_ladder_delta_pitch(base,pre,&delta);
        if(fault==14){DWORD ignored{};VirtualProtect(f.cg,0xb0000,protection,&ignored);}
        check(copied==(fault==0)&&delta==(fault==0?31.75f:0.f),
              "protected ladder reader requires live matching native player facts and finite accessible delta");
    }
    for(float delta:{0.f,31.75f,-179.9f,179.9f,359.9f,-359.9f,720.f,-720.f}) {
        const auto packed=t5::ladder_command_pitch(delta);
        const auto pitch=std::remainder(packed*(360.f/65536.f)+delta,360.f);
        const float factor=std::clamp((-std::sin(pitch*.0174532925199433f)+.25f)*2.5f,-1.f,1.f);
        check(packed>=0&&packed<=65535&&std::fabs(pitch+18.f)<.006f&&factor>.999f,
              "wrapped positive and negative native deltas preserve upward ladder pitch and full climb factor");
    }
}
void ladder_actual_pitch_command() {
    Fixture f;f.controls.cod4_controls=true;f.controls.router=input::ActionRouter(false,true);
    const auto ps=reinterpret_cast<std::uintptr_t>(f.cg+0x8a364);
    store(ps+0xc,8U);t5::pre_snapshot.native_pm_flags=8;
    // Ladder script holsters the weapon; no weapon definition is required to
    // climb. Native cl_view pitch and player delta remain owned by the engine.
    store(ps+0x144,static_cast<unsigned char>(0));
    constexpr float delta=31.75f,native_pitch=85.f;
    store(ps+0x84,delta);store(f.aim.base+t5::yaw_rva-4,native_pitch);
    t5::pre_snapshot.context.viewangles[0]=native_pitch;
    actions.menu.active=true;
    for(auto& h:actions.hands) {
        h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;
        h.grip.orientation_valid=h.grip.orientation_tracked=h.stick.active=true;
    }
    rig.frame.head_center.position={0,1.7f,0};rig.frame.actions=actions;
    auto neutral=f.bytes();t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),neutral.data());
    f.next();++actions.sequence;actions.hands[0].stick.current={0,1};rig.frame.actions=actions;
    auto bytes=f.bytes(),original=bytes;
    bytes[0x18]=original[0x18]=0;
    t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),bytes.data());
    std::int32_t packed{};memcpy(&packed,bytes.data()+0xc,sizeof(packed));
    const float predicted_pitch=std::remainder(packed*(360.f/65536.f)+delta,360.f);
    const float climb=std::clamp((-std::sin(predicted_pitch*.0174532925199433f)+.25f)*2.5f,-1.f,1.f);
    check(std::fabs(predicted_pitch+18.f)<.006f&&climb>.999f&&bytes[0x1b]==127&&bytes[0x1c]==0,
          "actual reader and input callback make weapon-zero ladder stick-up climb from native pitch+85");
    memcpy(original.data()+0xc,&packed,sizeof(packed));original[0x1b]=127;original[0x1c]=0;
    check(bytes==original,"actual holstered ladder command preserves all bytes except ordinary pitch and movement axes");
    check(*reinterpret_cast<const float*>(ps+0x84)==delta&&
          *reinterpret_cast<const float*>(f.aim.base+t5::yaw_rva-4)==native_pitch&&
          !f.controls.pending_body_yaw.valid,
          "ladder command correction never writes player delta or global camera pitch/yaw");
    for(unsigned phase=0;phase<3;++phase) {
        f.next();++actions.sequence;actions.hands[0].stick.current={phase==1?1.f:0.f,0};rig.frame.actions=actions;
        if(phase==2){store(ps+0xc,0U);t5::pre_snapshot.native_pm_flags=0;}
        auto next=f.bytes();next[0x18]=0;const auto stock=next;
        t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),next.data());
        check(!memcmp(next.data()+0xc,stock.data()+0xc,12),
              "next neutral, strafe-only or ladder-exit command keeps freshly generated native view angles");
    }
}
unsigned swim_copy_effect{};
bool swim_reader_fixture(std::uintptr_t,const t5::PreCommandSnapshot& pre,const t5::CommandContext& context,
                         std::uint64_t now,t5::swimming::Facts* out) noexcept {
    auto& m=mounted_fixture->memory;
    const bool copied=t5::swimming::copy_current({&m,MountedMemory::copy,MountedMemory::command,MountedMemory::thread},
                                                0x400000,pre,context,now,out);
    if(copied&&swim_copy_effect) {
        if(swim_copy_effect==1)t5::pre_snapshot.native_pm_type=1;
        if(swim_copy_effect==2)t5::pre_snapshot.native_pm_flags|=8;
        if(swim_copy_effect==3)t5::pre_snapshot.native_entity_flags2|=0x10000000;
        if(swim_copy_effect==4)--t5::pre_snapshot.native_health;
        swim_copy_effect=0;
    }
    return copied;
}
bool walking_reader_fixture(std::uintptr_t,const t5::PreCommandSnapshot& pre,const t5::CommandContext& context,
                         std::uint64_t now,t5::swimming::Facts* out) noexcept {
    auto& m=mounted_fixture->memory;
    return t5::swimming::copy_walking_current({&m,MountedMemory::copy,MountedMemory::command,MountedMemory::thread},
                                             0x400000,pre,context,now,out);
}
bool tunnel_reader_fixture(std::uintptr_t,const t5::PreCommandSnapshot& pre,const t5::CommandContext& context,
                         std::uint64_t now) noexcept {
    auto& m=mounted_fixture->memory;
    return t5::tunnel_crawl::current({&m,MountedMemory::copy,MountedMemory::command,MountedMemory::thread},
                                    0x400000,pre,context,now);
}
void swim_sync(MountedFixture& f) {
    f.memory.put<int>(MountedMemory::cg+0x8c158,f.memory.get<int>(MountedMemory::cg+0x8a348));f.sync();
}
void swim_state(MountedFixture& f,float head_yaw=0,float head_up=30) {
    swim_copy_effect=0;
    ladder_test_state(f,false,input::MovementOrientation::hmd);
    f.controls.copy_swimming=swim_reader_fixture;
    f.memory.put<int>(MountedMemory::ps+0x55c,5);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0x8000);
    const std::array<float,3> delta{-47.966f,-11.09f,1.148f};
    f.memory.bytes(MountedMemory::ps+0x84,delta.data(),sizeof(delta));
    const xr::Basis3f identity{};f.memory.bytes(MountedMemory::cg+0x8c134,&identity,sizeof(identity));
    constexpr float half_radian=.00872664625997165f;
    rig.frame.head_center.orientation=xr::Multiply(
        xr::Quaternionf{0,std::sin(head_yaw*half_radian),0,std::cos(head_yaw*half_radian)},
        xr::Quaternionf{std::sin(head_up*half_radian),0,0,std::cos(head_up*half_radian)});
    swim_sync(f);f.invoke();auto neutral=f.native.bytes();t5::after_native(&f.controls,f.memory.request.command,neutral.data());
}
void disarmed_walk_input() {
    for(bool smooth:{false,true}){
        MountedFixture f;swim_state(f,0,0);f.controls.copy_walking=walking_reader_fixture;f.controls.smooth_turn=smooth;
        f.memory.put<int>(MountedMemory::ps+0x55c,0);f.memory.put<U32>(MountedMemory::ps+0x10,0x80);
        f.memory.put<int>(MountedMemory::cg+0xa4680,1);f.memory.pre.gameplay_active=false;
        f.prepare(0);swim_sync(f);f.invoke();
        for(unsigned sample=0;sample<3;++sample){aim_now+=20;rig.published_ms=aim_now-1;f.prepare(0);
            actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();}
        const auto yaw=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
        check(f.controls.controls.turn==1&&yaw<0&&!f.controls.swim_occurrence&&!t5::pre_snapshot.gameplay_active,
            "actual disarmed hidden walking callback retains snap and smooth turn independently of weapon permission");
        check(f.memory.get<U32>(MountedMemory::ps+0x10)==0x80&&f.memory.get<int>(MountedMemory::cg+0xa4680)==1,
            "turning leaves native hidden and disabled-weapon state unchanged");
    }
    for(unsigned fault=0;fault<7;++fault){
        MountedFixture f;swim_state(f,45,0);f.controls.copy_walking=walking_reader_fixture;
        f.memory.put<int>(MountedMemory::ps+0x55c,0);f.memory.put<U32>(MountedMemory::ps+0x10,0x80);
        f.memory.put<int>(MountedMemory::cg+0xa4680,1);f.memory.pre.gameplay_active=false;
        f.prepare(0);swim_sync(f);f.invoke();f.prepare(0);
        actions.hands[0].stick.current={0,1};actions.hands[1].stick.current={1,0};
        switch(fault){
        case 0:f.memory.put<int>(MountedMemory::ps+4,1);break;
        case 1:f.memory.put<U32>(MountedMemory::ps+0xc,0x800);break;
        case 2:f.memory.put<U32>(MountedMemory::ps+0xc,8);break;
        case 3:f.memory.put<int>(MountedMemory::cg+0xa4780,1);break;
        case 4:f.memory.request.command.key_catcher=16;break;
        case 5:rig.published_ms=aim_now-101;break;
        case 6:generation_current=false;break;
        }
        swim_sync(f);f.invoke();
        check(f.controls.controls.turn==0&&!f.controls.pending_body_yaw.valid&&
              *reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==0,
            "walking exception cannot turn or transfer body yaw during linked frozen ladder camera pause or stale tracking");
    }
}
void tunnel_crawl_input() {
    MountedFixture f;swim_state(f,90,0);f.controls.copy_tunnel_crawl=tunnel_reader_fixture;
    f.memory.put<int>(MountedMemory::ps+4,1);f.memory.put<U32>(MountedMemory::ps+0xc,2);
    f.memory.put<U32>(MountedMemory::ps+0x10,0x02000080);f.memory.put<U32>(MountedMemory::ps+0xe0,0xa0006);
    f.memory.put<U32>(MountedMemory::ps+0xe4,0x20000000);f.memory.put<U8>(MountedMemory::ps+0x144,0);
    f.memory.put<int>(MountedMemory::cg+0xa4680,1);f.memory.put<float>(MountedMemory::ps+0x1a8,.01f);
    f.memory.put<float>(MountedMemory::ps+0x1ac,.01f);f.memory.pre.gameplay_active=false;
    const auto map=MountedMemory::dvar(4),name=MountedMemory::name(4),value=MountedMemory::name(5);
    f.memory.put<U32>(0x2899cec,map);f.memory.put<U32>(map,name);f.memory.put<U32>(map+0x18,value);
    f.memory.put<U32>(map+0xc,0x44);f.memory.put<U32>(map+0x10,7);
    f.memory.bytes(name,"mapname",8);f.memory.bytes(value,"creek_1",8);
    f.prepare(0);swim_sync(f);f.invoke();
    f.prepare(0);actions.hands[0].stick.current={0,1};actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();
    check(f.controls.controls.forward==1&&f.controls.controls.right==0&&f.controls.controls.turn==0&&
          !f.controls.pending_body_yaw.valid,"90-degree HMD yaw cannot turn tunnel stick-forward into sideways crawl or rotate linked camera");
    auto bytes=f.native.bytes(),expected=bytes;expected[0x1b]=127;expected[0x1c]=0;
    t5::after_native(&f.controls,f.memory.request.command,bytes.data());
    check(bytes==expected&&*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==0,
        "linked tunnel crawl changes only native forward/right command bytes and preserves native camera");
    f.memory.bytes(value,"vorkuta",8);f.prepare(0);swim_sync(f);f.invoke();
    check(std::fabs(f.controls.controls.forward)<.01f&&std::fabs(f.controls.controls.right)>.99f,
        "different map keeps existing HMD-oriented linked movement instead of inheriting tunnel exception");
    f.memory.bytes(value,"creek_1",8);swim_sync(f);
    const auto reader=[&]{return tunnel_reader_fixture(f.native.aim.base,t5::pre_snapshot,f.memory.request.command,aim_now);};
    check(reader(),"complete narrow tunnel graph is accepted independently of input routing");
    const auto read_count=f.memory.reads.size();const auto saved_blocks=f.memory.blocks;
    for(std::size_t fault=1;fault<=read_count;++fault){f.memory.reset();f.memory.fail_at=fault;
        check(!reader(),"every inaccessible tunnel graph getter refuses raw-crawl override");}
    for(unsigned fault=0;fault<10;++fault){f.memory.blocks=saved_blocks;
        switch(fault){
        case 0:f.memory.put<U8>(MountedMemory::ps+0x144,3);break;
        case 1:f.memory.put<int>(MountedMemory::cg+0xa4680,0);break;
        case 2:f.memory.put<float>(MountedMemory::ps+0x1ac,1);break;
        case 3:f.memory.put<int>(MountedMemory::ps+4,0);break;
        case 4:f.memory.bytes(value,"vorkuta",8);break;
        case 5:f.memory.put<U32>(MountedMemory::ps+0x10,0);break;
        case 6:f.memory.put<U32>(MountedMemory::ps+0xc,0x800);break;
        case 7:f.memory.put<int>(MountedMemory::cg+0x8a360,1);break;
        case 8:f.memory.put<U32>(MountedMemory::ps+0x440,1);break;
        case 9:f.memory.put<int>(MountedMemory::ps+0x1c4,0);break;
        }
        swim_sync(f);check(!reader(),"only holstered alive unfrozen linked Creek crawl with tight native clamps gains raw axes");
    }
    f.memory.blocks=saved_blocks;swim_sync(f);f.memory.mutate_at=read_count/2;
    f.memory.mutate_address=MountedMemory::ps+0x1ac;
    check(!reader(),"native crawl clamp changing between complete copies refuses the override");
}
input::NativeCommandBytes swim_native_command(MountedFixture& f,float yaw=0) {
    auto bytes=f.native.bytes();
    for(unsigned i=0;i<3;++i) {
        const float delta=f.memory.get<float>(MountedMemory::ps+0x84+4*i);
        const auto angle=static_cast<std::int32_t>(std::lround(std::remainder((i==0?9.f:(i==1?yaw:0.f))-delta,360.f)*(65536.f/360.f)))&0xffff;
        memcpy(bytes.data()+0xc+4*i,&angle,sizeof(angle));
    }
    return bytes;
}
void swim_prepare(MountedFixture& f,float forward,float right=0) {
    f.prepare(0);actions.hands[0].stick.current={right,forward};swim_sync(f);f.invoke();
}
void swimming_input() {
    for(float yaw:{0.f,60.f,90.f,180.f})for(float up:{-40.f,40.f})for(float direction:{-1.f,1.f}) {
        MountedFixture f;swim_state(f,yaw,up);swim_prepare(f,direction);
        check(f.controls.swim_occurrence&&!f.controls.pending_body_yaw.valid,
              "native underwater state selects head-directed swim without body yaw/anchor transfer");
        auto bytes=swim_native_command(f),original=bytes;
        const auto before_head=rig.frame.head_center;const auto before_anchor=rig.tracking_anchor;
        t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        std::int32_t packed{};memcpy(&packed,bytes.data()+0xc,sizeof(packed));
        const float pitch=std::remainder(packed*(360.f/65536.f)+f.memory.get<float>(MountedMemory::ps+0x84),360.f);
        const float forward=static_cast<std::int8_t>(bytes[0x1b])/127.f,right=static_cast<std::int8_t>(bytes[0x1c])/127.f;
        const float rad=.0174532925199433f;
        const xr::Vec3f actual{forward*std::cos(pitch*rad),-right,-forward*std::sin(pitch*rad)};
        const xr::Vec3f desired{direction*std::cos(up*rad)*std::cos(yaw*rad),
                              direction*std::cos(up*rad)*std::sin(yaw*rad),direction*std::sin(up*rad)};
        const float norm=std::sqrt(actual.x*actual.x+actual.y*actual.y+actual.z*actual.z);
        const float agreement=(actual.x*desired.x+actual.y*desired.y+actual.z*desired.z)/norm;
        check(agreement>std::cos(5.2f*rad),"actual callback plus protected swim reader follow HMD up/down/yaw including reverse and native pitch-limit cases");
        memcpy(original.data()+0xc,&packed,sizeof(packed));original[0x1b]=bytes[0x1b];original[0x1c]=bytes[0x1c];
        check(bytes==original,"swim command preserves native yaw/roll, buttons, weapons and every unrelated byte");
        check(!memcmp(&rig.frame.head_center,&before_head,sizeof(before_head))&&
              !memcmp(&rig.tracking_anchor,&before_anchor,sizeof(before_anchor))&&
              *reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==0,
              "swimming does not modify tracked head, anchor, or native global yaw");
        check(!f.controls.swim_occurrence&&f.controls.controls.forward==0&&f.controls.controls.right==0,
              "swim callback consumes its occurrence and analog axes once");
        auto repeated=swim_native_command(f),saved=repeated;
        t5::after_native(&f.controls,f.memory.request.command,repeated.data());
        check(repeated==saved,"duplicate swim post does not retain or reapply pitch and axes");
        swim_prepare(f,0);auto neutral=swim_native_command(f),stock=neutral;
        t5::after_native(&f.controls,f.memory.request.command,neutral.data());
        check(neutral==stock,"next neutral swim command restores untouched native pitch and buoyancy inputs");
    }
    for(unsigned fault=0;fault<15;++fault) {
        MountedFixture f;swim_state(f);swim_prepare(f,1);
        switch(fault) {
        case 0:aim_now+=101;break;
        case 1:++t5::pre_snapshot.sequence;break;
        case 2:generation_current=false;break;
        case 3:f.memory.put<int>(MountedMemory::ps+0x55c,2);break;
        case 4:f.memory.put<U32>(MountedMemory::ps+0xc,8);break;
        case 5:f.memory.put<float>(MountedMemory::ps+0x84,0);break;
        case 6:f.memory.put<float>(MountedMemory::dvar(2)+0x18,50);break;
        case 7:f.memory.request.command.key_catcher=16;t5::pre_snapshot.context.key_catcher=16;break;
        case 8:++f.memory.request.command.command_number;t5::pre_snapshot.context=f.memory.request.command;break;
        case 9:++f.memory.request.command.native_frame_time;t5::pre_snapshot.context=f.memory.request.command;break;
        case 10:f.memory.request.command.viewangles[0]+=1;t5::pre_snapshot.context=f.memory.request.command;break;
        case 11:case 12:case 13:case 14:swim_copy_effect=fault-10;break;
        }
        auto bytes=swim_native_command(f),saved=bytes;t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        check(bytes==saved&&!f.controls.swim_occurrence&&f.controls.controls.forward==0,
              "stale/mismatched/changed swim state consumes the occurrence without partial command writes");
    }
    {MountedFixture f;swim_state(f);swim_prepare(f,1);auto b=swim_native_command(f);t5::after_native(&f.controls,f.memory.request.command,b.data());
        f.memory.put<int>(MountedMemory::ps+0x55c,2);swim_prepare(f,1);
        check(!f.controls.swim_occurrence,"leaving native swim mode restores ordinary ground movement");
        auto next=swim_native_command(f),stock=next;t5::after_native(&f.controls,f.memory.request.command,next.data());
        check(!memcmp(next.data()+0xc,stock.data()+0xc,12),"swim exit leaves newly generated native view angles unchanged");}
    {MountedFixture f;swim_state(f);f.memory.put<int>(MountedMemory::ps+0x55c,3);swim_prepare(f,1);
        check(f.controls.swim_occurrence,"surface waterlevel3 retains native swimming mode");
        auto b=swim_native_command(f);t5::after_native(&f.controls,f.memory.request.command,b.data());
        check(f.memory.get<int>(MountedMemory::ps+0x55c)==3,"surface level remains native; adapter cannot force an underwater state");}
}
// Keep the protected variant reader real; translate only the synthetic native
// graph address between the existing independent reader and adapter fixtures.
bool swim_primary_reader_fixture(std::uintptr_t base,const t5::PreCommandSnapshot& pre,t5::PrimaryWeaponMode* out) noexcept {
    auto& f=*mounted_fixture;
    if(!t5::swimming::rd::same_pre(pre,f.memory.pre))return false;
    auto native_pre=pre;native_pre.native_cg_identity=reinterpret_cast<std::uintptr_t>(f.native.cg);
    native_pre.native_game_time=*reinterpret_cast<int*>(f.native.cg+0x8a348);
    return t5::copy_primary_weapon_mode(base,native_pre,out);
}
void swim_knife_step(MountedFixture& f,float hand_x) {
    aim_now+=20;rig.published_ms=aim_now-1;f.prepare(0);
    actions.hands[1].grip.pose.position.x=hand_x;swim_sync(f);f.invoke();
}
void swim_knife_state(MountedFixture& f,const char* internal="creek_knife_sp",unsigned flags=0x88) {
    swim_state(f);f.controls.copy_primary_mode=swim_primary_reader_fixture;
    f.memory.put<U32>(MountedMemory::ps+0x10,flags);f.memory.pre.gameplay_active=!(flags&0x80);
    strcpy_s(reinterpret_cast<char*>(f.native.dvar+0x900),64,internal);
    strcpy_s(reinterpret_cast<char*>(f.native.dvar+0xa00),64,"WEAPON_KNIFE");
    store(reinterpret_cast<std::uintptr_t>(f.native.variant),reinterpret_cast<std::uintptr_t>(f.native.dvar+0x900));
    store(reinterpret_cast<std::uintptr_t>(f.native.variant+0xc),reinterpret_cast<std::uintptr_t>(f.native.dvar+0xa00));
    store(reinterpret_cast<std::uintptr_t>(f.native.def+0x1c),0);store(reinterpret_cast<std::uintptr_t>(f.native.def+0x20),0);
    swim_knife_step(f,0);
    check(f.controls.knife_primary_active,"actual protected variant reader recognizes the scripted native knife");
}
void swimming_disabled_weapons_input() {
    {
        MountedFixture f;swim_state(f,60,40);
        f.memory.put<U32>(MountedMemory::ps+0x10,0x88);f.memory.pre.gameplay_active=false;
        swim_prepare(f,1);
        check(f.controls.swim_occurrence&&f.controls.controls.forward==1&&!t5::pre_snapshot.gameplay_active&&
              !f.controls.pending_body_yaw.valid,"DisableWeapons retains fresh native swimming without granting weapon gameplay or body transfer");
        auto bytes=swim_native_command(f),stock=bytes;t5::after_native(&f.controls,f.memory.request.command,bytes.data());
        std::int32_t packed{};memcpy(&packed,bytes.data()+0xc,sizeof(packed));
        const float pitch=std::remainder(packed*(360.f/65536.f)+f.memory.get<float>(MountedMemory::ps+0x84),360.f);
        check(pitch< -40&&static_cast<std::int8_t>(bytes[0x1b])>0,
              "actual callback still encodes upward HMD swimming with native weapon flags0x88");
        memcpy(stock.data()+0xc,&packed,4);stock[0x1b]=bytes[0x1b];stock[0x1c]=bytes[0x1c];
        check(bytes==stock&&f.memory.get<U32>(MountedMemory::ps+0x10)==0x88,
              "disabled-weapon swimming preserves every nonmovement command byte and the script's weapon flags");
        swim_prepare(f,0);f.prepare(0);actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();
        check(f.controls.swim_occurrence&&f.controls.controls.turn==1&&
              *reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==-45,
              "fresh disabled-weapon swimming retains existing right-stick snap turn");
    }
    for(unsigned fault=0;fault<8;++fault) {
        MountedFixture f;swim_state(f);f.memory.put<U32>(MountedMemory::ps+0x10,0x88);f.memory.pre.gameplay_active=false;
        swim_prepare(f,0);f.prepare(0);actions.hands[1].stick.current={1,0};
        switch(fault) {
        case 0:f.memory.put<int>(MountedMemory::ps+0x55c,2);break;
        case 1:f.memory.put<U32>(MountedMemory::ps+0xc,0x800);break;
        case 2:f.memory.put<int>(MountedMemory::ps+4,1);break;
        case 3:f.memory.put<U32>(MountedMemory::ps+0xc,8);break;
        case 4:f.memory.put<U32>(MountedMemory::ps+0xe0,0x4000);break;
        case 5:f.memory.request.command.key_catcher=16;break;
        case 6:rig.published_ms=aim_now-101;break;
        case 7:generation_current=false;break;
        }
        swim_sync(f);f.invoke();
        check(!f.controls.swim_occurrence&&f.controls.controls.turn==0&&
              *reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==0,
              "disabled weapons cannot extend swim/turn permission to ground, frozen, linked, ladder, camera, menu or stale XR states");
    }
    {
        MountedFixture f;swim_knife_state(f);dispatches.clear();actions.hands[1].trigger.current=1;
        swim_knife_step(f,.03f);
        check(f.count("+melee 28679 ")==1&&f.count("+attack ")==0,
              "actual boat knife gesture delivers native MeleeButtonPressed with no attack while trigger is held");
        f.invoke();check(f.count("+melee 28679 ")==1,"duplicate boat gesture cannot repeat native melee acquisition");
        swim_knife_step(f,.031f);
        check(f.count("-melee 28679 ")==1,"next quiet knife sample balances the native melee key");
        actions.hands[1].stick_click.current=true;swim_knife_step(f,.031f);
        check(f.count("+melee 28679 ")==2,"right-stick click remains available for the boat takedown");
        f.memory.request.command.key_catcher=16;swim_knife_step(f,.031f);
        check(f.count("-melee 28679 ")==2&&f.count("+attack ")==0,
              "pausing the actual disabled-weapon swim callback releases held native melee once");
    }
    {
        MountedFixture f;swim_knife_state(f);dispatches.clear();swim_knife_step(f,.03f);
        f.memory.put<U32>(MountedMemory::ps+0x10,8);f.memory.pre.gameplay_active=true;
        swim_knife_step(f,.031f);
        check(f.count("+melee 28679 ")==1&&f.count("-melee 28679 ")==1&&f.count("+attack ")==0,
              "EnableWeapons transition balances scripted melee without creating a new primary knife attack");
    }
    for(bool stale_rig:{false,true}) {
        MountedFixture f;swim_knife_state(f);dispatches.clear();
        const auto step=[&](bool fresh=true){aim_now+=20;rig.published_ms=aim_now-(fresh?1:101);f.prepare(0);
            actions.hands[0].stick.current={0,1};actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();};
        step();check(f.controls.swim_occurrence&&f.controls.controls.forward==1&&f.controls.controls.turn==1,
                     "scripted knife swimming starts with both held locomotion sticks armed");
        const float first_yaw=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
        f.memory.request.command.viewangles[1]=first_yaw;
        if(!stale_rig)f.controls.copy_swimming=nullptr;step(!stale_rig);
        check(!f.controls.swim_occurrence&&f.controls.controls.turn==0,
              "one native swim selector refusal cannot retain its restricted turn permission");
        f.controls.copy_swimming=swim_reader_fixture;step();
        check(f.controls.swim_occurrence&&f.controls.controls.forward==1&&f.controls.controls.turn==1&&dispatches.empty(),
              "actual swim selector recovery restores held movement and turn without a neutral gesture or spurious attack");
        check(first_yaw==-45&&*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==first_yaw,
              "one swim reader or rig refusal cannot turn a held snap stick into a second native snap");
        aim_now+=20;rig.published_ms=aim_now-1;swim_prepare(f,0);step();
        check(*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==first_yaw-45,
              "real centered stick after swim refusal allows exactly one new snap");
    }
    for(unsigned mode=0;mode<3;++mode) {
        MountedFixture f;swim_knife_state(f,mode==0?"vorkuta_knife_sp":"creek_knife_sp",mode==1?8:0x88);
        if(mode==2){f.memory.put<int>(MountedMemory::ps+0x55c,2);swim_knife_step(f,.001f);}
        dispatches.clear();swim_knife_step(f,.031f);
        check(f.count("+melee ")==0&&f.count("+attack 28672 ")==1,
              "ordinary, nonboat and non-swimming primary knives retain their previous gesture-to-attack mapping");
    }
}
void swimming_turn_post_context() {
    for(bool smooth:{false,true}) {
        MountedFixture f;swim_state(f,0,40);f.controls.smooth_turn=smooth;
        f.memory.put<U32>(MountedMemory::ps+0x10,0x88);f.memory.pre.gameplay_active=false;
        swim_prepare(f,0);
        for(unsigned step=0;step<3;++step) {
            // DispatchCallback reads this yaw again after our successful write.
            // Model that real post context, not the original pre-hook context.
            aim_now+=20;rig.published_ms=aim_now-1;f.prepare(0);
            actions.hands[0].stick.current={0,1};actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();
            auto post=f.memory.request.command;
            post.viewangles[1]=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
            check(f.controls.swim_turn_owned&&(smooth?post.viewangles[1]<f.memory.request.command.viewangles[1]:post.viewangles[1]==-45),
                  "actual swim occurrence records only its successfully written snap or smooth yaw");
            auto bytes=swim_native_command(f,post.viewangles[1]),stock=bytes;
            t5::after_native(&f.controls,post,bytes.data());
            std::int32_t packed{};memcpy(&packed,bytes.data()+0xc,4);
            constexpr float rad=.0174532925199433f;
            const float pitch=std::remainder(packed*(360.f/65536.f)+f.memory.get<float>(MountedMemory::ps+0x84),360.f)*rad;
            const float yaw=post.viewangles[1]*rad;
            const float forward=static_cast<std::int8_t>(bytes[0x1b])/127.f,right=static_cast<std::int8_t>(bytes[0x1c])/127.f;
            const xr::Vec3f actual{forward*std::cos(pitch)*std::cos(yaw)+right*std::sin(yaw),
                forward*std::cos(pitch)*std::sin(yaw)-right*std::cos(yaw),-forward*std::sin(pitch)};
            const float length=std::sqrt(actual.x*actual.x+actual.y*actual.y+actual.z*actual.z);
            const float agreement=(actual.x*std::cos(40*rad)+actual.z*std::sin(40*rad))/length;
            check(agreement>std::cos(1.f*rad)&&actual.z>0,
                  "same-command snap and continuous smooth turn preserve head-directed swim using generated command yaw");
            memcpy(stock.data()+0xc,&packed,4);stock[0x1b]=bytes[0x1b];stock[0x1c]=bytes[0x1c];
            check(bytes==stock&&!f.controls.swim_occurrence&&!f.controls.swim_turn_owned,
                  "owned turn allowance preserves generated yaw and all unrelated bytes and is consumed exactly once");
            auto repeated=swim_native_command(f,post.viewangles[1]),saved=repeated;
            t5::after_native(&f.controls,post,repeated.data());check(repeated==saved,"turned swim post cannot replay its prior yaw allowance");
            if(!smooth)break;
            f.memory.request.command.viewangles[1]=post.viewangles[1];
        }
    }
    for(unsigned fault=0;fault<9;++fault) {
        MountedFixture f;swim_state(f);f.memory.put<U32>(MountedMemory::ps+0x10,0x88);f.memory.pre.gameplay_active=false;
        swim_prepare(f,0);f.prepare(0);actions.hands[0].stick.current={0,1};actions.hands[1].stick.current={1,0};swim_sync(f);f.invoke();
        auto post=f.memory.request.command;post.viewangles[1]=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
        auto bytes=swim_native_command(f,post.viewangles[1]),saved=bytes;
        switch(fault) {
        case 0:post.viewangles[1]+=1;break;
        case 1:post.viewangles[0]+=1;break;
        case 2:post.viewangles[2]+=1;break;
        case 3:++post.native_frame_time;break;
        case 4:post.native_thread+=1;break;
        case 5:++post.command_number;break;
        case 6:post.kickangles[1]+=1;break;
        case 7:post.viewangles[1]=f.memory.request.command.viewangles[1];break;
        case 8:swim_prepare(f,0);break;
        }
        t5::after_native(&f.controls,post,bytes.data());
        check(bytes==saved,"owned turn allowance rejects unexpected yaw, pitch, roll, clock, thread, command, kick or a prior occurrence");
    }
}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);normal_delivery();controller_owned_mounted_delivery();mounted_fire_jitter_continuity();scoped_tow_fire_continuity();refusals();interruption();linked_input();scripted_use_input();scripted_jump_input();scripted_frozen_melee_input();scripted_frozen_attack_input();scripted_linked_input();slingshot_input();vehicle_weapon_input();ladder_input();ladder_pitch_reader_guards();ladder_actual_pitch_command();swimming_input();swimming_disabled_weapons_input();swimming_turn_post_context();disarmed_walk_input();tunnel_crawl_input();
    printf("mounted_actual_input_checks=%u failures=%u reader_fixture_failures=%u retail_calls=0 gpu_calls=0\n",checks,failures,mounted_reader_failures);
    return failures||mounted_reader_failures?1:0;}
