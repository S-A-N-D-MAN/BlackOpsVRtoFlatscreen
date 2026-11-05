// Actual Q1 command callbacks and x86 original-command bridge, with the actual
// protected profile reader over owned logical-address fixtures. No game/GPU.
#define main legacy_aim_tests_main
#include "native_command_aim_adapter_tests.cpp"
#undef main
#include "command_profile_fixture.hpp"
#include <stdexcept>
#include <memory>
#include <type_traits>
namespace {
namespace cp=bovr::t5::command_profile;
namespace pa=bovr::input::profile_admission;
struct Q1Fixture:Fixture {
    q1_fixture::Memory memory;
    explicit Q1Fixture(unsigned row=3,bool cycle=false):Fixture(cycle,cycle),memory(row){
        aim.exact_profile=true;aim.two_hand_support=true;aim.olympia=true;aim.profile_host=memory.host();
        actions.menu.active=true;
        for(auto& h:actions.hands){h.squeeze.active=h.trigger.active=true;h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;}
        if(cycle)for(auto& h:actions.hands){h.grip.orientation_valid=h.grip.orientation_tracked=h.stick.active=true;}
        actions.hands[0].grip.pose.position={.15F,.08F,-.4F};sync();
    }
    ~Q1Fixture(){on_rig_read=nullptr;}
    void sync(){
        t5::pre_snapshot.native_cg_identity=q1_fixture::Memory::cg;
        memory.pre=t5::pre_snapshot;memory.request.command=context();memory.request.pre_sequence=memory.pre.sequence;
        memory.request.gameplay_epoch=memory.pre.gameplay_epoch;memory.ticks=aim_now;
        memory.put<int>(0x2993850,context().command_number);memory.put<int>(0x2481764,context().native_frame_time);
        memory.put<int>(q1_fixture::Memory::cg+0x8a348,memory.pre.native_game_time);
        memory.put<int>(q1_fixture::Memory::cg+0x8c158,memory.pre.native_game_time);
        rig.frame.actions=actions;memory.reads.clear();
    }
    void next_command(bool fresh_action=true){Fixture::next();if(fresh_action)++actions.sequence;sync();}
    void replace_profile(unsigned row){q1_fixture::Memory next(row);memory.blocks.swap(next.blocks);memory.exact_bytes.swap(next.exact_bytes);
        memory.source_variant=next.source_variant;memory.runtime_profile_row=next.runtime_profile_row;sync();}
    input::NativeCommandBytes command_bytes(){auto b=Fixture::bytes();b[0x18]=*memory.pointer(q1_fixture::Memory::cg+0x8a364+0x144,1);return b;}
    void run(input::NativeCommandBytes& b){t5::before_native(&controls,context());t5::after_native(&controls,context(),b.data());}
    void arm(){auto b=command_bytes();run(b);check(!aim.admission_serial.load(),"transition command stock");
        next_command();run(b);check(!aim.admission_serial.load(),"fresh neutral command stock");next_command(false);run(b);
        check(aim.admission_serial.load()!=0,"next command admits same neutral action sample");}
    pa::Token token(){return {aim.admission_serial.load(),aim.admission_revision.load()};}
    t5::animation_detail::ClearComparisonKey key(){
        const auto& p=aim.admission_profile;const auto& d=p.profile;const auto& id=p.identity;
        t5::animation_detail::ClearComparisonKey k{};k.command_epoch=p.gameplay_epoch;k.command_sequence=p.pre_sequence;k.thread=p.thread;
        k.weapon=d.weapon;k.profile_row=d.row;k.profile_revision=d.revision;k.native_time=p.native.native_time;k.frame_time=p.native.native_frame_time;
        k.command_number=p.command_number;k.command_clock=p.command_clock;k.weapon_state=p.native.weapon_state;k.other_weapon_state=p.native.other_state;
        k.gun_bone=d.gun;k.flash_bone=d.flash;k.bone_count=d.bones;k.pool_index=id.pool;k.opaque_identity={id.cg,id.owner,id.object,id.variant,id.definition,id.table};return k;
    }
};
Q1Fixture* active_q1{};
Q1Fixture* cycling_q1{};
unsigned cycle_effect{},cycle_target_row{};
bool cycle_effect_on_pair{};
std::vector<unsigned char> control_metadata(const t5::Adapter& a){
    std::vector<unsigned char> bytes;
    const auto add=[&]<class T>(const T& v){static_assert(std::is_trivially_copyable_v<T>);
        const auto* p=reinterpret_cast<const unsigned char*>(&v);bytes.insert(bytes.end(),p,p+sizeof(v));};
    add(a.base);add(a.router);add(a.turn);add(a.execute);add(a.controls);add(a.generation);add(a.samples);add(a.edges);
    add(a.owner);add(a.gameplay);add(a.override_active);add(a.pending_release);add(a.seen_command);add(a.last_command);
    add(a.cycle_enabled);add(a.cycle_exact_requested);add(a.cycle_busy);add(a.cycle_seen_revocation);add(a.cycle_previous);
    add(a.cycle_generation);add(a.cycle_requests);add(a.aim_input_running.test());return bytes;
}
void rejected_combined_callbacks(){
    auto& f=*cycling_q1;auto& a=f.controls;const auto metadata=control_metadata(a);
    const auto cycle=a.cycle_revocations.load();const auto aim=f.aim.invalidation.load();
    const auto reads=rig_reads;const auto calls=dispatches.size();auto b=f.command_bytes(),saved=b;
    t5::before_native(&a,f.context());
    check(control_metadata(a)==metadata&&a.cycle_revocations.load()>cycle&&f.aim.invalidation.load()>aim,
          "both-on rejected before preserves owner metadata and revokes both controls");
    t5::after_native(&a,f.context(),b.data());
    check(control_metadata(a)==metadata&&b==saved&&rig_reads==reads&&dispatches.size()==calls,
          "both-on rejected after preserves52bytes without newer rig or dispatch");
}
void __cdecl combined_cycle_sink(int,int,const char* text){
    dispatches.emplace_back(text);
    const bool selected=cycle_effect_on_pair?std::string(text).starts_with("+attack "):!strcmp(text,"weapnext");
    if(!selected)return;
    auto& f=*cycling_q1;const auto effect=cycle_effect;cycle_effect=0;
    if(!strcmp(text,"weapnext"))check(!f.token().serial,"cycle invalidates pending aim before native-like delivery");
    if(effect==1)f.replace_profile(cycle_target_row);
    if(effect==2)rejected_combined_callbacks();
    if(effect==3){std::thread foreign(rejected_combined_callbacks);foreign.join();}
    if(effect==4)throw std::runtime_error("combined native-like effect already occurred");
}
void synchronize_profile(){if(active_q1)active_q1->sync();}
void startup(){
    const std::array<const wchar_t*,12> flags{L"BOVR_EXACT_PROFILE_AIM_PROBE",L"BOVR_COMMAND_AIM_PROBE",L"BOVR_COMMAND_AIM_PLACEMENT_PROBE",
        L"BOVR_TWO_HAND_SUPPORT_PROBE",L"BOVR_XR_STEREO_PROBE",L"BOVR_INPUT_PROBE",L"BOVR_COMMAND_PROBE",L"BOVR_WEAPON_PROBE",
        L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",L"BOVR_BORROWED_WEAPON_PROBE",L"BOVR_BORROWED_WEAPON_PLACEMENT",L"BOVR_BORROWED_OLYMPIA_PROBE"};
    for(auto f:flags)SetEnvironmentVariableW(f,L"1");SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",L"right");
    xr::Hand hand{};
    for(unsigned mask=0;mask<16;++mask){clear_ready=mask&1;borrow_ready=mask&2;writer_ready=mask&4;renderer_ready=mask&8;
        check((t5::aim_startup_mode(true,&hand)==t5::AimMode::exact_profile)==(mask==15),"Q1 requires actual renderer clear borrowed writer readiness");}
    clear_ready=borrow_ready=writer_ready=renderer_ready=true;
    for(auto f:flags){SetEnvironmentVariableW(f,L"bad");check(t5::aim_startup_mode(true,&hand)!=t5::AimMode::exact_profile,"malformed required Q1 request refuses exact mode");SetEnvironmentVariableW(f,L"1");}
    for(auto f:{L"BOVR_UI_PRODUCER_PROBE",L"BOVR_BLOOM_SHADER_PROBE",L"BOVR_CONVENTIONAL_OFFHAND_PROBE",L"BOVR_CONSUMED_COMMAND_PROBE"}){
        SetEnvironmentVariableW(f,L"1");check(t5::aim_startup_mode(true,&hand)==t5::AimMode::refused,"Q1 production rejects conflicting raw environment");SetEnvironmentVariableW(f,nullptr);}
    check(!t5::configure_native_command_aim(nullptr,true)&&!t5::exact_profile_aim_enabled(),"failed actual configuration publishes no exact mode");
    for(auto f:flags)SetEnvironmentVariableW(f,nullptr);SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",nullptr);
    check(t5::aim_startup_mode(true,&hand)==t5::AimMode::disabled,"default off restored");
}
void acceptance(){
    for(const bool launcher:{true,false})for(unsigned count:{1u,2u,4u,7u}) {Q1Fixture f;
        constexpr auto ps=q1_fixture::Memory::cg+0x8a364;
        f.memory.weapon(launcher?7:9);f.memory.put<unsigned>(ps+0x1b0,count);
        f.memory.put<int>(q1_fixture::Memory::definition+0x1c,launcher?2:0);
        f.memory.put<int>(q1_fixture::Memory::definition+0x20,launcher?6:1);
        f.memory.put<int>(q1_fixture::Memory::definition+0x30,launcher?5:0);
        f.arm();f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(f.token().serial&&f.aim.admission_profile.native.stack_count==count&&b!=stock,
            "exact callback tracks launcher or prior rifle with retained native stacked-fire count");
        for(unsigned i=0;i<b.size();++i)if(i<0x20||i>=0x24)check(b[i]==stock[i],"valid stack count changes no native command buttons movement or selection");
    }
    for(unsigned initial:{0u,1u,4u}) {Q1Fixture f;constexpr auto count=q1_fixture::Memory::cg+0x8a364+0x1b0;
        f.memory.put<unsigned>(count,initial);f.arm();f.next_command();auto b=f.command_bytes(),stock=b;
        t5::before_native(&f.controls,f.context());
        check(f.aim.pending.active&&f.aim.pending.native.stack_count==initial,"exact pending command preserves observed stack count");
        const auto aim_serial=f.aim.serial;
        f.memory.put<unsigned>(count,initial==1?4u:1u);t5::after_native(&f.controls,f.context(),b.data());
        check(f.token().serial&&f.aim.admission_profile.native.stack_count==(initial==1?4u:1u)&&
            f.aim.serial==aim_serial&&b==stock,
            "count change preserves command bytes and permits only separately revalidated current rendering");
    }

    {Q1Fixture f;
        constexpr auto ps=q1_fixture::Memory::cg+0x8a364;
        f.memory.weapon(20); // Captured MP5K index, with an owned synthetic model.
        f.memory.put<int>(q1_fixture::Memory::definition+0x20,2);
        for(auto player:{ps,q1_fixture::Memory::server}) {
            f.memory.put<unsigned>(player+0xc,0x200000);
            f.memory.put<unsigned>(player+0x10,0x100); // Mission disables offhand only.
            f.memory.put<unsigned>(player+0xe0,0x2);
            f.memory.put<int>(player+0x158,49);
        }
        f.arm();auto b=f.command_bytes();
        for(int state:{49,49,50,0,48,49}) {
            f.memory.put<int>(ps+0x158,state);f.next_command();auto stock=b;f.run(b);
            check(f.token().serial&&f.aim.admission_profile.native.weapon_state==state,
                "captured Executive Order low-ready context retains real command/profile admission through transitions");
            for(unsigned i=0;i<b.size();++i)if(i<0x20||i>=0x24)
                check(b[i]==stock[i],"low-ready aim leaves native buttons, movement and weapon selection untouched");
            unsigned flags{};std::memcpy(&flags,f.memory.pointer(ps+0x10,4),4);
            check(flags==0x100,"tracking does not clear mission weapon restrictions");
        }
    }
    {Q1Fixture f(6);f.arm();actions.hands[1].squeeze.current=.8f;actions.hands[0].squeeze.current=.8f;
        actions.hands[1].trigger.current=1;f.next_command();rig.published_ms=aim_now-101;
        auto b=f.command_bytes(),stock=b;f.run(b);
        check(!f.token().serial&&b==stock,"stale prediction has no command write or scene admission");
        f.next_command();rig.published_ms=aim_now;f.run(b);
        check(f.token().serial&&b!=stock,"fresh same-profile prediction recovers while grips and fire remain held");
        f.next_command();rig.published_ms=aim_now-90;t5::before_native(&f.controls,f.context());aim_now+=11;
        stock=b;t5::after_native(&f.controls,f.context(),b.data());
        check(!f.token().serial&&b==stock,"prediction that expires during native command remains unwritten");
        f.next_command();rig.published_ms=aim_now;f.run(b);
        check(f.token().serial,"fresh prediction after native-time hitch also recovers without neutral grips");aim_now=1000;}
    {Q1Fixture f;f.memory.put<int>(q1_fixture::Memory::cg+0x8a360,2);f.arm();f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(f.aim.admission_serial.load()!=0&&b!=stock,"actual Q1 callbacks write admitted aim in first-person shadow mode");}
    for(int mode:{1,-1,3}){Q1Fixture f;f.arm();f.memory.put<int>(q1_fixture::Memory::cg+0x8a360,mode);f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(!f.aim.admission_serial.load()&&b==stock,"actual Q1 callbacks keep command stock for model and unknown cameras");}
    for(unsigned offset:{0xcu,0xa9d44u}){Q1Fixture f;f.memory.put<int>(q1_fixture::Memory::cg+0x8a360,2);f.arm();
        f.memory.put<int>(q1_fixture::Memory::cg+offset,1);f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(!f.aim.admission_serial.load()&&b==stock,"actual Q1 shadow aim cannot bypass other camera exclusions");}
    for(unsigned row=0;row<synthetic_profile_fixture::rows().size();++row){Q1Fixture f(row);f.arm();auto b=f.command_bytes();
        const auto hard=f.aim.invalidation.load();
        for(unsigned i=0;i<12;++i){f.next_command(false);f.run(b);check(f.aim.admission_serial.load()!=0&&f.aim.invalidation.load()==hard,"continuous commands do not require neutral every frame");}
        for(int state:{11,28,29,30,31,32,33,48,49,49,50,0}){f.memory.put<int>(q1_fixture::Memory::cg+0x8a364+0x158,state);f.next_command();f.run(b);
            check(f.aim.admission_serial.load()!=0,"reload/sprint/continuous/low-ready states across commands retain same-model tracking admission without repeated neutral");}
        auto key=f.key();pa::Token token{};cp::Copy copied{};
        check(t5::read_command_profile_admission(key,&token,&copied)&&token==f.token(),"actual current command admission joins camera key");
        aim_now=1090;check(t5::command_profile_admission_current(f.token()),"original rig age100ms remains current");
        aim_now=1091;check(!t5::command_profile_admission_current(f.token()),"original rig age101ms refuses even with newer copied profile");aim_now=1000;
        ++key.command_number;check(t5::read_command_profile_admission(key,&token,&copied),"render command successor is legal");
        ++key.command_number;check(!t5::read_command_profile_admission(key,&token,&copied),"render command beyond successor rejected");
        key=f.key();++key.profile_revision;check(!t5::read_command_profile_admission(key,&token,&copied),"wrong profile revision rejected");
        key=f.key();++key.command_sequence;check(!t5::read_command_profile_admission(key,&token,&copied),"wrong pre sequence rejected");
        const auto old=f.token();f.next_command();t5::before_native(&f.controls,f.context());
        check(!t5::command_profile_admission_current(old),"next before retires previous placement admission before write");
        t5::after_native(&f.controls,f.context(),b.data());check(f.token().serial&&f.token()!=old,"fresh write publishes distinct serial");
        t5::invalidate_native_command_aim();check(!t5::command_profile_admission_witness(f.token()),"hard invalidation atomically rejects token");
    }
}
void linked_command_aim(){
    constexpr auto ps=q1_fixture::Memory::cg+0x8a364;
    constexpr std::uint32_t link_slot=0x1a796f8+0x298,link=0x306ed00;
    const auto configure=[&](Q1Fixture& f,bool frozen=false){
        auto& pre=t5::pre_snapshot;pre.gameplay_active=false;pre.linked_weapon_active=!frozen;
        pre.native_pm_type=1;pre.native_pm_flags=frozen?0x1800800u:0x1800000u;
        pre.native_weapon_flags=0x100;pre.native_entity_flags=0x80002;
        for(const auto player:{ps,q1_fixture::Memory::server}){
            f.memory.put<int>(player+4,1);
            f.memory.put<std::uint32_t>(player+0xc,pre.native_pm_flags);
            f.memory.put<std::uint32_t>(player+0x10,pre.native_weapon_flags);
            f.memory.put<std::uint32_t>(player+0xe0,pre.native_entity_flags);
        }
        f.memory.put<std::uint32_t>(link_slot,link);f.sync();
    };
    {Q1Fixture f(0);configure(f);f.arm();f.next_command();
        actions.hands[1].trigger.current=1;f.sync();auto bytes=f.command_bytes(),stock=bytes;
        const auto serial=f.aim.serial;f.begin();f.finish(bytes);
        check(f.token().serial&&f.aim.serial==serial+1&&bytes!=stock&&
            f.aim.admission_profile.identity.server_link==link&&f.aim.admission_profile.identity.tracking_mode==2,
            "actual linked begin/finish admits native offhand-disabled primary aim");
        auto unrelated=bytes;std::memcpy(unrelated.data()+0x20,stock.data()+0x20,4);
        check(unrelated==stock&&f.aim.publications[(f.aim.serial-1)%128].command.bytes==bytes,
            "linked aim changes only gun DWORD and preserves all48 unrelated command bytes");

        f.next_command();bytes=f.command_bytes();stock=bytes;const auto previous=f.token();
        const auto before_change=f.aim.serial;f.begin();
        check(f.aim.pending.active,"linked replacement starts with a genuinely admitted pending command");
        f.memory.put<std::uint32_t>(link_slot,link+4);f.finish(bytes);
        check(bytes==stock&&!f.token().serial&&f.aim.serial==before_change&&!f.aim.rearm.armed()&&
            !t5::command_profile_admission_witness(previous),
            "mid-command bounded server-link replacement rejects all52 bytes and old admission");

        f.next_command();f.begin();f.finish(bytes);
        check(bytes==stock&&!f.token().serial&&!f.aim.rearm.armed(),
            "fresh link plus held trigger cannot inherit previous linked neutral latch");
        actions.hands[1].trigger.current=0;f.next_command(false);f.begin();f.finish(bytes);
        check(bytes==stock&&!f.token().serial&&!f.aim.rearm.armed(),
            "duplicate action sample cannot supply fresh neutral for replacement link");
        f.next_command();f.begin();f.finish(bytes);
        check(bytes==stock&&!f.token().serial&&f.aim.rearm.armed(),
            "fresh neutral for replacement link remains stock on its observation command");
        f.next_command(false);f.begin();f.finish(bytes);
        check(bytes!=stock&&f.token().serial&&f.aim.serial==before_change+1&&
            f.aim.admission_profile.identity.server_link==link+4,
            "next actual linked command recovers only after fresh neutral with new link identity");
    }
    {Q1Fixture f(0);configure(f,true);auto bytes=f.command_bytes(),stock=bytes;
        f.begin();f.finish(bytes);
        check(bytes==stock&&!f.token().serial&&!f.aim.serial&&!f.aim.pending.active,
            "initial frozen scripted pistol preserves all52 command bytes without admission");
    }
}
void refusal(){
    for(unsigned at:{0,1,2}){Q1Fixture f;f.arm();f.next_command();auto b=f.command_bytes(),original=b;
        if(at==0)f.memory.put<std::uint32_t>(q1_fixture::Memory::model(1)+0x4000,0x7fc00000u);
        t5::before_native(&f.controls,f.context());
        if(at==1)f.memory.put<std::uint32_t>(q1_fixture::Memory::model(1)+0x4000,0x7fc00000u);
        // Exact final reader fault: two initial reads in after, four precommand
        // reads inside each reader. Its first copied byte can invalidate safely.
        if(at==2){const auto baseline=f.memory.reads.size();f.memory.fail_call=baseline+baseline+1;}
        t5::after_native(&f.controls,f.context(),b.data());
        check(b==original&&!f.aim.admission_serial.load(),"before after or final protected profile refusal leaves command stock");
    }
    {Q1Fixture f;f.arm();f.next_command();auto b=f.command_bytes();b[0x18]=1;auto original=b;const auto serial=f.aim.serial;f.run(b);
        check(b==original&&f.aim.admission_serial.load()&&f.aim.serial==serial,"requested mismatch retains fresh rendering without gun-angle write or aim publication");
        actions.hands[1].trigger.current=1;f.next_command();b=f.command_bytes();f.run(b);
        check(f.aim.admission_serial.load()&&f.aim.serial==serial+1,"new exact matching command retains already held intent after native selection");}
    {Q1Fixture f;f.arm();f.next_command();auto b=f.command_bytes(),original=b;
        const auto serial=f.aim.serial;
        t5::before_native(&f.controls,f.context());f.memory.put<int>(q1_fixture::Memory::cg+0x8a364+0x158,11);
        t5::after_native(&f.controls,f.context(),b.data());check(b==original&&f.token().serial&&f.aim.serial==serial,"valid state evolution renders current gun without aiming that occurrence");
        f.next_command();f.run(b);check(f.token().serial!=0,"next stable reload occurrence recovers without neutral loop");}
    {Q1Fixture f;f.arm();f.controls.gameplay=true;f.controls.override_active=false;f.controls.controls.forward=.37F;
        auto original_controls=f.controls.controls;const auto before=f.aim.invalidation.load();
        std::thread foreign([&]{auto b=f.command_bytes(),original=b;t5::before_native(&f.controls,f.context());
            check(!memcmp(&original_controls,&f.controls.controls,sizeof(original_controls))&&f.controls.gameplay&&!f.controls.override_active,"foreign before preserves immediate owner metadata");
            t5::after_native(&f.controls,f.context(),b.data());check(b==original,"foreign after preserves immediate command bytes");});foreign.join();
        check(f.aim.invalidation.load()>before&&!f.token().serial,"foreign entry only revokes copied admission");}
    {Q1Fixture f;f.arm();f.next_command();auto b=f.command_bytes(),original=b;
        t5::before_native(&f.controls,f.context());f.aim.running.test_and_set();t5::finish_native_command_aim(f.context(),b.data());f.aim.running.clear();
        t5::finish_native_command_aim(f.context(),b.data());check(b==original&&!f.token().serial,"nested callback invalidates parent write");}
    {Q1Fixture f;f.arm();f.next_command();auto b=f.command_bytes();actions.hands[0].grip.position_tracked=false;f.sync();f.run(b);
        check(f.token().serial!=0&&!f.aim.publications[(f.aim.serial-1)%128].aim.support.paired,"support tracking loss retains single-hand aiming");}
    {Q1Fixture f;f.arm();f.next_command();f.aim.next_admission=UINT64_MAX-1;auto b=f.command_bytes(),old=b;f.run(b);
        check(b==old&&!f.token().serial,"serial exhaustion refuses before any native gun-word write");}
}
void cycling(){
    for(unsigned starting_row:{3u,5u}){Q1Fixture f(starting_row,true);cycling_q1=&f;f.controls.execute=combined_cycle_sink;cycle_effect_on_pair=false;f.arm();
        const unsigned alternate_row=starting_row==3?0u:4u;
        for(unsigned target:{alternate_row,starting_row}){
            f.next_command();actions.hands[1].stick.current={0,1};f.sync();auto b=f.command_bytes(),saved=b;
            const auto count=dispatches.size(),reads=rig_reads;const auto old_token=f.token();const auto aim_serial=f.aim.serial;
            cycle_effect=1;cycle_target_row=target;f.run(b);
            check(dispatches.size()==count+1&&dispatches.back()=="weapnext"&&rig_reads==reads+1,
                  "both-on one original rig read and exact single native cycle payload");
            check(b==saved&&f.token().serial&&f.token()!=old_token&&!t5::command_profile_admission_witness(old_token)&&f.aim.serial==aim_serial,
                  "native cycle replaces current render token while preserving command and aim publication");
            f.next_command();f.run(b);check(f.token().serial&&b==saved&&f.aim.serial==aim_serial,
                  "old requested weapon renders freshly owned gun but cannot write after profile changed");
            // Exact supported replacement keeps already held grips and fire;
            // held cycle action still cannot repeat its native request.
            actions.hands[0].squeeze.current=actions.hands[1].squeeze.current=.8F;
            actions.hands[1].trigger.current=1;
            b=f.command_bytes();
            for(unsigned i=0;i<4;++i){f.next_command();f.run(b);}
            check(f.token().serial!=0&&f.aim.admission_profile.profile.row==q1_fixture::Memory::row_for(target)&&f.aim.serial>aim_serial&&
                std::count(dispatches.begin(),dispatches.end(),"weapnext")==std::ptrdiff_t(f.controls.cycle_requests),
                  "new supported profile retains exact aim with held grips and fire while held cycle does not repeat");
            actions.hands[1].trigger.current=0;
            f.next_command();actions.hands[1].stick.current={};f.sync();f.run(b);
            check(std::count(dispatches.begin(),dispatches.end(),"weapnext")==std::ptrdiff_t(f.controls.cycle_requests),"neutral does not invent a cycle release");
        }
        check(f.controls.cycle_requests==2&&std::count(dispatches.begin(),dispatches.end(),"weapnext")==2,
              "both-on42 to4 to42 uses exactly two deliberate requests");
    }
    for(bool paired:{false,true})for(unsigned effect:{2u,3u,4u}){
        Q1Fixture f(3,true);cycling_q1=&f;f.controls.execute=combined_cycle_sink;cycle_effect_on_pair=paired;f.arm();
        f.next_command();actions.hands[1].stick.current={0,1};if(paired)actions.hands[1].trigger.current=1;f.sync();
        auto b=f.command_bytes(),saved=b;const auto hard=f.aim.invalidation.load();cycle_effect=effect;bool threw{};
        try{f.run(b);}catch(const std::runtime_error&){threw=true;}
        check(threw==(effect==4)&&!f.controls.cycle_busy&&!f.controls.aim_input_running.test(),
              "both-on native-like exception or reentry retires both outer scopes");
        // A paired dispatch can throw before the explicit cycle invalidation;
        // the command bridge normally owns that exception retirement. Direct
        // callback tests must not manufacture a post callback after that throw.
        if(!threw||!paired)check(!f.token().serial&&f.aim.invalidation.load()>hard,
              "both-on observed refusal invalidates aim authority");
        check(b==saved&&dispatches.size()==1&&f.controls.cycle_requests==1,
              "both-on refused occurrence keeps52bytes and consumes reservation once");
        f.next_command();f.run(b);check(dispatches.size()==1,"both-on held request never retries after effect or rejection");
        actions.hands[1].trigger.current=0;actions.hands[1].stick.current={};
        for(unsigned i=0;i<4;++i){f.next_command();f.run(b);}
        check(f.token().serial!=0,"both-on owner recovers after fresh neutral commands");
        f.next_command();actions.hands[1].stick.current={0,1};f.sync();f.run(b);
        check(f.controls.cycle_requests==2&&dispatches.back()=="weapnext",
              "both-on next deliberate excursion is a new request");
    }
    {Q1Fixture f(3,true);cycling_q1=&f;f.controls.execute=combined_cycle_sink;cycle_effect_on_pair=false;f.arm();
        std::thread foreign(rejected_combined_callbacks);foreign.join();f.next_command();actions.hands[1].stick.current={0,1};f.sync();auto b=f.command_bytes();f.run(b);
        check(dispatches.empty(),"foreign between-command callback revokes cycle neutral latch too");}
    cycling_q1=nullptr;cycle_effect=0;cycle_effect_on_pair=false;
}
void render_only_selection(){
    for(unsigned mode=0;mode<4;++mode){Q1Fixture f(3);f.arm();const auto previous=f.token();const auto previous_key=f.key();
        const auto aim_serial=f.aim.serial;const auto publications=f.aim.publications;const auto hard=f.aim.invalidation.load();
        f.next_command();actions.hands[0].squeeze.current=actions.hands[1].squeeze.current=.8F;
        actions.hands[1].trigger.current=1;f.sync();auto bytes=f.command_bytes();
        if(mode==3)bytes[0x18]=1;
        const auto saved=bytes;t5::before_native(&f.controls,f.context());
        check(!t5::command_profile_admission_witness(previous),"selection occurrence immediately retires previous render permission");
        if(mode==0){const auto error=GetLastError();t5::retire_native_command_aim_for_selection(f.context());
            check(GetLastError()==error&&f.aim.pending.active&&f.aim.pending.render_only,
                "explicit selection keeps original copied rig only and preserves LastError");}
        if(mode==1)f.replace_profile(0);
        if(mode==2)f.memory.put<int>(q1_fixture::Memory::cg+0x8a364+0x158,3);
        t5::after_native(&f.controls,f.context(),bytes.data());
        check(bytes==saved&&f.aim.serial==aim_serial&&!memcmp(publications.data(),f.aim.publications.data(),sizeof(publications)),
            "selection state profile and requested mismatch preserve all52 bytes and every aim publication");
        const auto rendered=f.token();pa::Token copied_token{};cp::Copy copied_profile{};
        check(rendered.serial&&rendered!=previous&&f.aim.invalidation.load()==hard&&
            t5::read_command_profile_admission(f.key(),&copied_token,&copied_profile)&&copied_token==rendered&&
            copied_profile.profile.row==q1_fixture::Memory::row_for(mode==1?0u:3u),"each transition gets a distinct current render permission");
        check(!t5::read_command_profile_admission(previous_key,&copied_token,&copied_profile),
            "fresh transition token cannot authorize a previous model or command key");
        aim_now=1091;check(!t5::command_profile_admission_current(rendered),"render-only admission cannot refresh original rig age");aim_now=1000;
        f.next_command();bytes=f.command_bytes();const auto matching=bytes;f.run(bytes);
        check(f.token().serial&&f.token()!=rendered&&f.aim.serial==aim_serial+1&&bytes!=matching&&
            !t5::command_profile_admission_witness(rendered),"held grips and fire recover fresh exact aiming after selection without neutral or stale token");
    }
    // These are real protected-reader failures and lifecycle changes after an
    // explicitly marked selection, not a synthetic renderer-only permit.
    for(unsigned fault=0;fault<8;++fault){Q1Fixture f(3);f.arm();f.next_command();auto bytes=f.command_bytes(),saved=bytes;
        const auto aim_serial=f.aim.serial;t5::before_native(&f.controls,f.context());
        t5::retire_native_command_aim_for_selection(f.context());
        const auto before_reads=f.memory.reads.size();
        switch(fault){
        case 0:f.memory.fail_call=before_reads+1;break;
        case 1:f.memory.fail_call=before_reads*2+1;break;
        case 2:generation_current=false;break;
        case 3:aim_now=1091;break;
        case 4:{const auto replacement=q1_fixture::Memory::owner+0x1000;
            f.memory.bytes(replacement,f.memory.pointer(q1_fixture::Memory::owner,0x34),0x34);
            f.memory.put<std::uint32_t>(0xc1c6d8,replacement);break;}
        case 5:t5::invalidate_native_command_aim();break;
        case 6:{auto other=f.context();++other.command_number;t5::retire_native_command_aim_for_selection(other);break;}
        case 7:{std::thread foreign([&]{t5::retire_native_command_aim_for_selection(f.context());});foreign.join();break;}
        }
        t5::after_native(&f.controls,f.context(),bytes.data());
        check(bytes==saved&&!f.token().serial&&f.aim.serial==aim_serial,
            "render-only selection cannot bypass reader final-copy ownership generation age or hard retirement failures");
        aim_now=1000; // next fixture captures its pre-command snapshot before resetting this clock
    }
}
void paired_dual_aim(){
    Q1Fixture f(0);f.memory.dual();f.sync();f.arm();f.next_command();
    for(auto& hand:actions.hands){hand.grip.active=hand.grip.orientation_valid=hand.grip.position_valid=
        hand.grip.orientation_tracked=hand.grip.position_tracked=true;hand.squeeze.active=true;hand.squeeze.current=.8F;}
    actions.hands[1].grip.pose.position={.2F,-.3F,-.4F};actions.hands[0].grip.pose.position={.5F,.15F,-.5F};
    rig.frame.actions=actions;auto bytes=f.command_bytes();
    check(t5::weapon_two_hand_support_enabled(),"actual shared foregrip policy is enabled for the dual regression");
    input::CommandAimProposal single{},supported{};
    check(input::propose_native_command_aim(rig,actions,{},xr::Hand::Right,aim_now,&single,false)==input::CommandAimStatus::accepted,
        "dual fixture supplies a valid dominant controller aim");
    check(input::propose_native_command_aim(rig,actions,{},xr::Hand::Right,aim_now,&supported,true)==input::CommandAimStatus::accepted&&
        supported.support.paired&&single.packed!=supported.packed,"dual fixture would visibly change aim if foregrip pairing were applied");
    t5::before_native(&f.controls,f.context());
    check(t5::native_command_dual_current(f.context()),"same pending command exposes freshly validated paired dual control observation");
    check(!t5::native_command_hitscan_current(f.context()),"paired dual pending cannot grant ordinary hitscan ownership");
    auto other=f.context();++other.command_number;check(!t5::native_command_dual_current(other),"dual control observation rejects another command");
    bool foreign=true;std::thread worker([&]{foreign=t5::native_command_dual_current(f.context());});worker.join();
    check(!foreign,"dual control observation rejects foreign thread");
    const auto clock=aim_now;aim_now+=101;check(!t5::native_command_dual_current(f.context()),"dual control observation cannot refresh stale pending input");aim_now=clock;
    t5::after_native(&f.controls,f.context(),bytes.data());
    t5::CommandAimPublication publication{};
    check(f.token().serial&&f.aim.admission_profile.native.dual_wield&&t5::read_native_command_aim_publication(f.aim.serial,&publication)&&
        !publication.aim.support.paired&&publication.aim.packed==single.packed,"actual dual command keeps both authored guns on dominant aim despite held left grip");
    check(!t5::native_command_dual_current(f.context()),"finished command retires provisional dual control observation");
}
void prisoner_refusals(){
    for(unsigned row:{4u,5u})for(bool post:{false,true})for(bool binding:{false,true}){
        Q1Fixture f(row,true);f.arm();f.next_command();auto b=f.command_bytes(),saved=b;const auto old_token=f.token();
        check(f.aim.admission_profile.profile.row==q1_fixture::Memory::row_for(row)&&
              f.aim.admission_profile.profile.revision==bovr::t5::borrowed_profile::runtime_revision&&
              f.aim.admission_profile.profile.gun==72&&f.aim.admission_profile.profile.flash==78,
              "runtime fixture row uses its published revision and assembly positions");
        const auto address=q1_fixture::Memory::model(0)+(binding?0x4000+7*32:0x1000);
        auto* byte=f.memory.pointer(address,4);std::uint32_t original{};memcpy(&original,byte,sizeof(original));
        if(post)t5::before_native(&f.controls,f.context());
        if(binding)f.memory.put<std::uint32_t>(address,0x7fc00000u);else *byte^=1;
        if(post)t5::after_native(&f.controls,f.context(),b.data());else f.run(b);
        check(b==saved&&!f.token().serial&&!t5::command_profile_admission_witness(old_token),
              "unknown prisoner name or bind before/after native boundary leaves52bytes stock");
        memcpy(byte,&original,sizeof(original));actions.hands[1].trigger.current=1;f.next_command();f.run(b);
        check(b==saved&&!f.token().serial,"restored prisoner payload plus held trigger cannot resurrect admission");
        actions.hands[1].trigger.current=0;
        for(unsigned i=0;i<4;++i){f.next_command();f.run(b);}
        check(f.token().serial&&f.aim.admission_profile.profile.row==q1_fixture::Memory::row_for(row),
              "fresh neutral recovers supported runtime row after rejection");
    }
}
void room_scale_rebase(){
    Q1Fixture f;f.arm();actions.hands[0].squeeze.current=1;actions.hands[1].trigger.current=1;
    body_commit_enabled=true;
    for(unsigned i=0;i<30;++i){
        f.next_command();auto b=f.command_bytes();f.run(b);
        const auto before=f.token();const auto serial=f.aim.serial;
        t5::CommandAimPublication original{};
        {std::lock_guard lock(f.aim.publications_lock);original=f.aim.publications[(serial-1)%f.aim.publications.size()];}
        auto anchor=rig.tracking_anchor;anchor.position.x+=.005f;
        const t5::RoomScaleView view{{0,0,60},f.aim.admission_profile.identity.cg,t5::pre_snapshot.gameplay_epoch,false};
        check(t5::commit_native_command_aim_room_scale(f.context(),rig,anchor,view)&&
              f.aim.rearm.armed()&&f.token().serial&&f.token()!=before&&
              t5::command_profile_admission_current(f.token()),
              "continuous room-scale walk and held fire retain fresh exact weapon rendering every command");
        {std::lock_guard lock(f.aim.publications_lock);const auto& retained=f.aim.publications[(serial-1)%f.aim.publications.size()];
            check(!memcmp(&retained,&original,sizeof(original))&&f.aim.serial==serial,
                  "room-scale visual rebase never modifies retained authored shot or command bytes");}
        rig.tracking_anchor=current_anchor;
    }
    const auto old=f.token();t5::invalidate_native_command_aim();auto anchor=rig.tracking_anchor;anchor.position.x+=.005f;
    check(t5::commit_native_command_aim_room_scale(f.context(),rig,anchor,{})&&!f.token().serial&&!f.aim.rearm.armed()&&
          !t5::command_profile_admission_witness(old),"roomscale cannot restore an invalidated firing or render receipt");
}
void body_yaw_rebase(){
    const auto turn=[](){auto anchor=rig.tracking_anchor;anchor.orientation={0,.2164396F,0,.976296F};return anchor;};
    {Q1Fixture f;f.arm();actions.hands[0].squeeze.current=1;actions.hands[1].trigger.current=1;
        f.next_command();auto b=f.command_bytes();t5::before_native(&f.controls,f.context());
        auto& planned=f.controls.pending_body_yaw;planned.rig=rig;planned.desired_anchor=turn();
        planned.expected_yaw=0;planned.desired_yaw=planned.delta=25;planned.native_thread=f.context().native_thread;
        planned.command_number=f.context().command_number;planned.native_frame_time=f.context().native_frame_time;planned.valid=true;
        body_commit_enabled=true;const auto hard=f.aim.invalidation.load();
        t5::after_native(&f.controls,f.context(),b.data());
        const auto old_serial=f.aim.serial;t5::CommandAimPublication old{};
        check(body_commit_calls==1&&f.controls.body_yaw_reports==1&&f.aim.rearm.armed()&&f.aim.invalidation.load()==hard,
            "actual post callback commits owned yaw after aim and preserves held support and firing intent");
        check(!f.token().serial&&!t5::read_native_command_aim_publication(old_serial,&old),
            "successful yaw transfer retires old render and command publications before any new admission");
        rig.tracking_anchor=current_anchor;f.next_command();b=f.command_bytes();const auto stock=b;f.run(b);
        check(f.token().serial&&b!=stock&&f.aim.serial==old_serial+1&&
            !memcmp(&f.aim.admission_rig.tracking_anchor,&current_anchor,sizeof(current_anchor)),
            "next current exact command aims and renders in new anchor while grip and fire remain held");
        for(unsigned i=0;i<b.size();++i)if(i<0x20||i>=0x24)check(b[i]==stock[i],"rebase recovery touches no native buttons, movement or weapon selection");
    }
    {Q1Fixture f;f.arm();const auto old=f.token();const auto revision=f.aim.invalidation.load();
        check(!t5::commit_native_command_aim_body_yaw(f.context(),rig,turn(),0,25)&&f.token()==old&&
            t5::command_profile_admission_current(old)&&f.aim.invalidation.load()==revision&&f.aim.rearm.armed(),
            "refused body commit with unchanged coordinates retains current rendering and armed input");}
    for(unsigned fault=0;fault<5;++fault){Q1Fixture f;f.arm();actions.hands[0].squeeze.current=1;actions.hands[1].trigger.current=1;
        f.next_command();auto b=f.command_bytes();f.run(b);const auto old=f.token();body_commit_enabled=true;
        if(fault==0)t5::invalidate_native_command_aim();
        if(fault==1)f.aim.rearm.revoke(actions.sequence);
        if(fault==2)after_body_commit=[](){t5::invalidate_native_command_aim();};
        if(fault==3)after_body_commit=[](){generation_current=false;};
        if(fault==4){active_q1=&f;after_body_commit=[](){++t5::pre_snapshot.gameplay_epoch;active_q1->sync();};}
        check(t5::commit_native_command_aim_body_yaw(f.context(),rig,turn(),0,25)&&!f.aim.rearm.armed()&&
            !f.token().serial&&!t5::command_profile_admission_witness(old),
            "body movement success cannot carry invalidated, unarmed, lost-generation or changed-epoch aim permission");
        after_body_commit=nullptr;active_q1=nullptr;generation_current=true;rig.tracking_anchor=current_anchor;
        f.next_command();b=f.command_bytes();const auto stock=b;f.run(b);
        check(!f.token().serial&&b==stock,"held trigger after real ownership loss cannot become freshly admitted through rebase");
    }
    {Q1Fixture f;f.arm();actions.hands[1].trigger.current=1;rig.tracking_anchor=turn();current_anchor=rig.tracking_anchor;
        f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(!f.token().serial&&b==stock&&!f.aim.rearm.armed(),"arbitrary anchor replacement still requires fresh neutral");}
    {Q1Fixture f;f.arm();body_commit_enabled=true;const auto before=body_commit_calls;
        std::thread foreign([&]{check(!t5::commit_native_command_aim_body_yaw(f.context(),rig,turn(),0,25),"foreign thread cannot commit owned body rebase");});foreign.join();
        check(body_commit_calls==before&&!f.token().serial,"foreign rebase rejects before native body commit and revokes old render token");}
}
void held_support_recovery(){
    constexpr auto ps=q1_fixture::Memory::cg+0x8a364;
    for(bool linked:{false,true}){Q1Fixture f(0);f.arm();actions.hands[0].squeeze.current=1;
        if(linked){auto& pre=t5::pre_snapshot;pre.gameplay_active=false;pre.linked_weapon_active=true;
            pre.native_pm_type=1;pre.native_pm_flags=0x1800000;pre.native_weapon_flags=0x100;pre.native_entity_flags=0x80002;
            for(auto player:{ps,q1_fixture::Memory::server}){f.memory.put<int>(player+4,1);f.memory.put<unsigned>(player+0xc,pre.native_pm_flags);
                f.memory.put<unsigned>(player+0x10,pre.native_weapon_flags);f.memory.put<unsigned>(player+0xe0,pre.native_entity_flags);}
            f.memory.put<unsigned>(0x1a796f8+0x298,0x306ed00);
        }else{t5::pre_snapshot.context.key_catcher=16;t5::pre_snapshot.gameplay_active=false;
            f.next_command();auto b=f.command_bytes();f.run(b);check(!f.token().serial,"menu entry retires aim before held-support recovery");
            t5::pre_snapshot.context.key_catcher=0;t5::pre_snapshot.gameplay_active=true;}
        f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
        check(!f.token().serial&&b==stock&&!f.aim.rearm.armed(),"menu/link transition sample remains stock with support held");
        f.next_command();f.run(b);check(!f.token().serial&&b==stock&&f.aim.rearm.armed(),"fresh non-firing sample can rearm exact single primary while foregrip stays held");
        f.next_command(false);f.run(b);check(f.token().serial&&b!=stock,"next same neutral action occurrence restores exact gun angles and render admission");
    }
    for(unsigned unsafe=0;unsafe<5;++unsafe){Q1Fixture f(0);f.arm();t5::invalidate_native_command_aim();actions.hands[0].squeeze.current=1;
        if(unsafe==0)actions.hands[0].trigger.current=1;
        if(unsafe==1)actions.hands[1].trigger.current=1;
        if(unsafe==2)actions.hands[1].squeeze.current=1;
        if(unsafe==3)actions.hands[0].squeeze.active=false;
        if(unsafe==4)actions.hands[1].aim.orientation_tracked=false;
        for(unsigned i=0;i<3;++i){f.next_command();auto b=f.command_bytes(),stock=b;f.run(b);
            check(!f.token().serial&&b==stock&&!f.aim.rearm.armed(),"support exception does not bypass trigger, right-grip, inactive support or invalid tracking gates");}
    }
}
void held_dominant_render_recovery(){
    struct RecoveryOption {
        std::wstring saved;bool present{};
        RecoveryOption(){
            constexpr auto name=L"BOVR_RENDER_NEUTRAL_RECOVERY";
            const auto size=GetEnvironmentVariableW(name,nullptr,0);present=size!=0;
            if(size){saved.resize(size);const auto copied=GetEnvironmentVariableW(name,saved.data(),size);saved.resize(copied);}
            SetEnvironmentVariableW(name,L"1");
        }
        ~RecoveryOption(){SetEnvironmentVariableW(L"BOVR_RENDER_NEUTRAL_RECOVERY",present?saved.c_str():nullptr);}
    };
    {
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();RecoveryOption enabled;
        input::ActionRouter router(false,true);
        for(auto& hand:actions.hands)hand.grip.orientation_valid=hand.grip.orientation_tracked=true;
        actions.hands[1].stick_click.active=true;
        const auto edge=[](const input::Output& out,input::Action action,bool pressed){
            for(unsigned i=0;i<out.edge_count;++i)if(out.edges[i].action==action&&out.edges[i].pressed==pressed)return true;
            return false;
        };
        const auto route=[&]{return router.update(actions,input::Context::gameplay,aim_now,
            true,false,false,false,false,false,false,false,false,false,t5::native_command_ordinary_attack_inhibited(f.context()));};
        f.next_command();f.begin();route();auto bytes=f.command_bytes();f.finish(bytes);
        actions.hands[1].trigger.current=1;f.next_command();f.begin();
        check(!t5::native_command_ordinary_attack_inhibited(f.context())&&edge(route(),input::Action::fire,true),
            "fresh ordinary tracked gun starts an actual router fire press");
        bytes=f.command_bytes();f.finish(bytes);
        actions.hands[1].squeeze.current=1;actions.hands[1].stick_click.current=true;f.next_command();f.begin();
        check(t5::native_command_ordinary_attack_inhibited(f.context()),"fresh exact grip and melee anticipates ordinary attack inhibition");
        auto other=f.context();++other.command_number;
        check(!t5::native_command_ordinary_attack_inhibited(other),"ordinary attack inhibition rejects wrong command");
        bool foreign=true;std::thread worker([&]{foreign=t5::native_command_ordinary_attack_inhibited(f.context());});worker.join();
        check(!foreign,"ordinary attack inhibition rejects foreign thread");
        const auto clock=aim_now;aim_now+=101;
        check(!t5::native_command_ordinary_attack_inhibited(f.context()),"ordinary attack inhibition rejects expired current owner");aim_now=clock;
        const auto pre=t5::pre_snapshot;++t5::pre_snapshot.native_cg_identity;
        check(!t5::native_command_ordinary_attack_inhibited(f.context()),"ordinary attack inhibition rejects changed current cg");t5::pre_snapshot=pre;
        ++t5::pre_snapshot.native_game_time;
        check(!t5::native_command_ordinary_attack_inhibited(f.context()),"ordinary attack inhibition rejects changed current game time");t5::pre_snapshot=pre;
        generation_current=false;
        check(!t5::native_command_ordinary_attack_inhibited(f.context()),"ordinary attack inhibition rejects changed XR generation");generation_current=true;
        const auto blocked=route();
        check(edge(blocked,input::Action::fire,false)&&!edge(blocked,input::Action::fire,true)&&edge(blocked,input::Action::melee,true),
            "proved grip-melee anticipation balances held fire and preserves actual melee");
        bytes=f.command_bytes();f.finish(bytes);
        actions.hands[1].stick_click.current=false;f.next_command();f.memory.fail_call=1;f.begin();
        check(!t5::native_command_ordinary_attack_inhibited(f.context())&&!edge(route(),input::Action::fire,true),
            "reader failure supplies no cached inhibition fact and cannot reacquire a held router trigger");
        bytes=f.command_bytes();auto stock=bytes;f.finish(bytes);f.memory.fail_call=0;
        check(bytes==stock,"reader failure preserves native command after balanced fire");
        for(unsigned held=0;held<3;++held){
            f.next_command();f.begin();
            check(t5::native_command_ordinary_attack_inhibited(f.context())&&!edge(route(),input::Action::fire,true),
                "fresh held-trigger visual aim wait keeps ordinary fire disarmed");
            bytes=f.command_bytes();stock=bytes;const auto serial=f.aim.serial;f.finish(bytes);
            check(bytes==stock&&f.aim.serial==serial,"held-trigger aim wait writes no gun word or firing publication");
        }
        actions.hands[1].trigger.current=0;f.next_command();f.begin();
        check(f.aim.rearm.armed()&&t5::native_command_ordinary_attack_inhibited(f.context())&&!edge(route(),input::Action::fire,true),
            "one actual trigger release during neutral-observed aim wait rearms without synthetic fire");
        bytes=f.command_bytes();stock=bytes;f.finish(bytes);check(bytes==stock,"neutral-observed release keeps native command stock");
        actions.hands[1].trigger.current=1;actions.hands[1].aim.pose.orientation={0,.38268343F,0,.92387953F};f.next_command();f.begin();
        check(!t5::native_command_ordinary_attack_inhibited(f.context())&&edge(route(),input::Action::fire,true),
            "first fresh press after one actual release restores ordinary router fire with ready aim");
        input::CommandAimProposal expected{};
        check(input::propose_native_command_aim(rig,actions,{},xr::Hand::Right,aim_now,&expected,true)==input::CommandAimStatus::accepted,
            "restored fire has independently computed tracked aim");
        bytes=f.command_bytes();stock=bytes;f.finish(bytes);std::uint32_t packed{};memcpy(&packed,bytes.data()+0x20,4);
        check(f.token().serial&&bytes!=stock&&packed==expected.packed&&actions.hands[1].squeeze.current==1,
            "fresh fire after grip-melee recovery writes tracked gun angles while grip remains held");
        check(!t5::native_command_ordinary_attack_inhibited(f.context()),"finished occurrence supplies no stale inhibition fact");
    }
    for(bool linked:{false,true}){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;
        if(linked){
            auto& pre=t5::pre_snapshot;pre.gameplay_active=false;pre.linked_weapon_active=true;
            pre.native_pm_type=1;pre.native_pm_flags=0x1800000;pre.native_weapon_flags=0x100;pre.native_entity_flags=0x80002;
            for(auto player:{q1_fixture::Memory::cg+0x8a364,q1_fixture::Memory::server}){
                f.memory.put<int>(player+4,1);f.memory.put<unsigned>(player+0xc,pre.native_pm_flags);
                f.memory.put<unsigned>(player+0x10,pre.native_weapon_flags);f.memory.put<unsigned>(player+0xe0,pre.native_entity_flags);
            }
            f.memory.put<unsigned>(0x1a796f8+0x298,0x306ed00);
        }else f.memory.dual();
        f.sync();f.arm();RecoveryOption enabled;
        actions.hands[1].squeeze.current=1;actions.hands[1].stick_click.active=actions.hands[1].stick_click.current=true;
        f.next_command();f.begin();
        check(f.aim.pending.active&&!t5::native_command_ordinary_attack_inhibited(f.context()),
            "fresh exact dual or linked gun cannot inherit ordinary attack inhibition");
        auto bytes=f.command_bytes();f.finish(bytes);
    }
    for(bool dual:{false,true}){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;if(dual){f.memory.dual();f.sync();}f.arm();
        RecoveryOption enabled;
        const auto before=f.token();const auto serial=f.aim.serial;const auto publications=std::make_unique<decltype(f.aim.publications)>(f.aim.publications);
        actions.hands[1].squeeze.current=1;actions.hands[1].trigger.current=1;
        f.next_command();f.memory.fail_call=1;auto bytes=f.command_bytes(),stock=bytes;f.run(bytes);
        check(!f.token().serial&&!f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial,
            "transient exact-reader failure with held right grip revokes permission without changing command");
        f.memory.fail_call=0;
        for(unsigned held=0;held<3;++held){
            const auto old=f.token();f.next_command();bytes=f.command_bytes();stock=bytes;
            t5::before_native(&f.controls,f.context());
            check(f.aim.pending.active&&f.aim.pending.visual_only_unarmed&&!t5::command_profile_admission_witness(old),
                "fresh held-grip recovery has a visual-only pending occurrence and retires prior render token");
            if(dual)check(!t5::native_command_dual_current(f.context()),
                "unarmed visual dual pending cannot grant paired trigger control authority");
            t5::after_native(&f.controls,f.context(),bytes.data());
            pa::Token token{};cp::Copy profile{};
            check(f.token().serial&&f.token()!=before&&f.token()!=old&&!f.aim.rearm.armed()&&
                t5::read_command_profile_admission(f.key(),&token,&profile)&&token==f.token()&&
                bytes==stock&&f.aim.serial==serial&&!memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
                "fresh exact held-grip rendering preserves every command byte and every firing publication while unarmed");
        }
        const auto rendered=f.token();f.run(bytes);
        check(!f.token().serial&&!t5::command_profile_admission_witness(rendered)&&bytes==stock&&f.aim.serial==serial,
            "duplicate occurrence cannot replay visual-only rendering or publish command aim");
        actions.hands[1].squeeze.current=0;f.next_command();f.run(bytes);
        check(f.token().serial&&!f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial,
            "releasing grip alone while trigger stays held remains rendering-only");
        actions.hands[1].trigger.current=0;f.next_command();f.run(bytes);
        check(f.token().serial&&f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial&&
            !memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
            "real fresh neutral arms the latch but its own command remains rendering-only");
        f.next_command(false);f.run(bytes);
        check(f.token().serial&&f.aim.serial==serial+1&&bytes!=stock,
            "next native occurrence after real neutral restores command aiming");
        for(unsigned i=0;i<bytes.size();++i)if(i<0x20||i>=0x24)
            check(bytes[i]==stock[i],"neutral recovery never changes native buttons movement or selection");
    }
    for(bool melee:{false,true}){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();RecoveryOption enabled;
        const auto serial=f.aim.serial;const auto publications=std::make_unique<decltype(f.aim.publications)>(f.aim.publications);
        constexpr auto ps=q1_fixture::Memory::cg+0x8a364;
        actions.hands[1].squeeze.current=1;
        actions.hands[1].aim.pose.orientation={0,.38268343F,0,.92387953F};
        f.next_command();f.memory.fail_call=1;auto bytes=f.command_bytes(),stock=bytes;f.run(bytes);f.memory.fail_call=0;
        check(!f.token().serial&&!f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial,
            "held-grip idle recovery starts from a real failed exact read with no command write");
        if(melee){
            // Stock melee init/end are 17/19. End19 is deliberately outside
            // the exact model reader's tracking states and must fail closed.
            for(int state:{17,19,0}){
                actions.hands[1].stick_click.current=state==0;
                f.memory.put<int>(ps+0x158,state);
                for(unsigned held=0;held<3;++held){
                    f.next_command();bytes=f.command_bytes();stock=bytes;t5::before_native(&f.controls,f.context());
                    check(!t5::native_command_hitscan_current(f.context()),
                        "melee or held melee button cannot gain provisional ordinary shot authority");
                    t5::after_native(&f.controls,f.context(),bytes.data());
                    check(!f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial&&
                        !memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
                        "melee init end and held stick-click keep held-grip command aim unarmed without writes");
                }
            }
            actions.hands[1].stick_click.current=false;
        }else{
            f.next_command();bytes=f.command_bytes();stock=bytes;f.run(bytes);
            check(f.token().serial&&!f.aim.rearm.armed()&&bytes==stock&&f.aim.serial==serial,
                "first fresh exact owner after transient failure is visual-only even with both triggers neutral");
        }
        f.next_command();bytes=f.command_bytes();stock=bytes;t5::before_native(&f.controls,f.context());
        check(f.aim.pending.active&&f.aim.pending.visual_only_unarmed&&f.aim.rearm.armed()&&
            !t5::native_command_hitscan_current(f.context()),
            "ready gun and released melee button observe neutral while right grip stays held but do not yet authorize shots");
        t5::after_native(&f.controls,f.context(),bytes.data());
        check(f.token().serial&&bytes==stock&&f.aim.serial==serial&&
            !memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
            "held-grip neutral-observed occurrence preserves all command bytes and prior publications");
        f.next_command(false);bytes=f.command_bytes();stock=bytes;
        input::CommandAimProposal expected{};
        check(input::propose_native_command_aim(rig,actions,{},xr::Hand::Right,aim_now,&expected,true)==input::CommandAimStatus::accepted,
            "held-grip recovery supplies a separately computed tracked controller gun direction");
        t5::before_native(&f.controls,f.context());
        check(f.aim.pending.active&&!f.aim.pending.visual_only_unarmed&&t5::native_command_hitscan_current(f.context()),
            "next exact native occurrence authorizes ordinary aim without releasing the right grip");
        auto other=f.context();++other.command_number;
        check(!t5::native_command_hitscan_current(other),"held-grip recovered shot authority rejects a different command");
        bool foreign=true;std::thread worker([&]{foreign=t5::native_command_hitscan_current(f.context());});worker.join();
        check(!foreign,"held-grip recovered shot authority rejects a foreign thread");
        const auto clock=aim_now;aim_now+=101;
        check(!t5::native_command_hitscan_current(f.context()),"held-grip recovered shot authority rejects an expired snapshot");aim_now=clock;
        t5::after_native(&f.controls,f.context(),bytes.data());
        std::uint32_t actual{};memcpy(&actual,bytes.data()+0x20,sizeof(actual));
        t5::CommandAimPublication written{};
        check(f.token().serial&&f.aim.serial==serial+1&&bytes!=stock&&actual==expected.packed&&
            t5::read_native_command_aim_publication(f.aim.serial,&written)&&written.command.bytes==bytes&&
            written.aim.packed==expected.packed&&actions.hands[1].squeeze.current==1,
            "held right grip after recovery writes and publishes the actual tracked gun DWORD");
        for(unsigned i=0;i<bytes.size();++i)if(i<0x20||i>=0x24)
            check(bytes[i]==stock[i],"held-grip aim recovery preserves every unrelated native command byte");
        check(!t5::native_command_hitscan_current(f.context()),"completed held-grip recovery retires provisional shot authority");
    }
    for(unsigned triggers:{1u,2u,3u}){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();RecoveryOption enabled;
        const auto serial=f.aim.serial;const auto publications=std::make_unique<decltype(f.aim.publications)>(f.aim.publications);
        actions.hands[1].squeeze.current=1;
        for(unsigned hand=0;hand<2;++hand)actions.hands[hand].trigger.current=(triggers&(1u<<hand))?1.F:0.F;
        f.next_command();f.memory.fail_call=1;auto bytes=f.command_bytes();f.run(bytes);f.memory.fail_call=0;
        for(unsigned held=0;held<4;++held){
            f.next_command();bytes=f.command_bytes();const auto stock=bytes;t5::before_native(&f.controls,f.context());
            check(f.aim.pending.active&&f.aim.pending.visual_only_unarmed&&!f.aim.rearm.armed()&&
                !t5::native_command_hitscan_current(f.context()),
                "either or both held triggers prevent the right-grip exemption from granting ordinary command aim");
            t5::after_native(&f.controls,f.context(),bytes.data());
            check(f.token().serial&&bytes==stock&&f.aim.serial==serial&&
                !memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
                "held-trigger recovery remains visual-only and preserves the complete firing publication ring");
        }
    }
    for(unsigned fault=0;fault<10;++fault){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();RecoveryOption enabled;
        const auto serial=f.aim.serial;const auto publications=std::make_unique<decltype(f.aim.publications)>(f.aim.publications);
        actions.hands[1].squeeze.current=1;f.next_command();f.memory.fail_call=1;
        auto bytes=f.command_bytes(),stock=bytes;f.run(bytes);f.memory.fail_call=0;
        f.next_command();
        if(fault==0){actions.focused=false;f.sync();}
        if(fault==1){t5::pre_snapshot.context.key_catcher=16;t5::pre_snapshot.gameplay_active=false;f.sync();}
        if(fault==2)generation_current=false;
        if(fault==3)f.memory.put<std::uint32_t>(q1_fixture::Memory::model(1)+0x4000,0x7fc00000u);
        if(fault==6)rig.published_ms=aim_now-101;
        if(fault==8){actions.hands[1].aim.orientation_tracked=false;f.sync();}
        t5::before_native(&f.controls,f.context());
        if(fault==4||fault==5||fault==7||fault==9)
            check(f.aim.pending.active&&f.aim.pending.visual_only_unarmed,
                "post-read recovery rejection begins from a valid visual-only pending occurrence");
        if(fault==4){const auto replacement=q1_fixture::Memory::owner+0x1000;
            f.memory.bytes(replacement,f.memory.pointer(q1_fixture::Memory::owner,0x34),0x34);
            f.memory.put<std::uint32_t>(0xc1c6d8,replacement);}
        if(fault==5)f.memory.fail_call=f.memory.reads.size()*2+1;
        if(fault==7)t5::invalidate_native_command_aim();
        if(fault==9)f.memory.fail_call=f.memory.reads.size()+1;
        t5::after_native(&f.controls,f.context(),bytes.data());
        check(!f.token().serial&&bytes==stock&&f.aim.serial==serial&&
            !memcmp(publications->data(),f.aim.publications.data(),sizeof(*publications)),
            "visual-only recovery cannot bypass focus pause generation profile owner final-copy age invalidation or tracking failure");
    }
}
namespace ca=bovr::t5::client_command_angles;
struct ClientAngleFixtureHost {
    Q1Fixture* fixture{};unsigned calls{},fail_call{};bool read_fail{},invalidate_after_write{},retire_after_first{};
    std::vector<std::uintptr_t> addresses;
    static bool read(void* opaque,std::uintptr_t at,void* out,std::size_t size)noexcept {
        auto& h=*static_cast<ClientAngleFixtureHost*>(opaque);
        return !h.read_fail&&q1_fixture::Memory::copy(&h.fixture->memory,at,out,size);
    }
    static bool cas(void* opaque,std::uintptr_t at,std::uint32_t expected,std::uint32_t desired,std::uint32_t* observed)noexcept {
        auto& h=*static_cast<ClientAngleFixtureHost*>(opaque);++h.calls;h.addresses.push_back(at);
        if(h.calls==h.fail_call||at>UINT32_MAX)return false;
        auto* p=h.fixture->memory.pointer(static_cast<std::uint32_t>(at),4);if(!p)return false;
        std::memcpy(observed,p,4);if(*observed==expected)std::memcpy(p,&desired,4);
        if(h.invalidate_after_write&&h.calls==2)++h.fixture->aim.invalidation;
        if(h.retire_after_first&&h.calls==1)h.fixture->memory.put<std::uint32_t>(0x2ff5354,q1_fixture::Memory::cg+0x100000);
        return true;
    }
    void enable(Q1Fixture& f,ca::Mode mode=ca::Mode::correct){fixture=&f;f.aim.client_angles_ready=true;f.aim.client_angles_mode=mode;
        f.aim.client_angles_host={this,read,cas};
        f.memory.put<float>(q1_fixture::Memory::cg+0xa9ce4,10);f.memory.put<float>(q1_fixture::Memory::cg+0xa9ce8,20);
        f.memory.put<float>(q1_fixture::Memory::server+0x1c34,31);f.memory.put<float>(q1_fixture::Memory::server+0x1c38,42);
    }
    ca::Words angles(){ca::Words w{};std::memcpy(w.data(),fixture->memory.pointer(q1_fixture::Memory::cg+0xa9ce4,8),8);return w;}
};
ca::Words client_angle_words(float pitch,float yaw){return {std::bit_cast<std::uint32_t>(pitch),std::bit_cast<std::uint32_t>(yaw)};}
void client_command_refresh(){
    // Every new large Q1 fixture stays on the heap; the prior stack overflow
    // regression must not be reintroduced by composing these cases.
    for(auto mode:{ca::Mode::disabled,ca::Mode::observe,ca::Mode::correct}){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();ClientAngleFixtureHost host;host.enable(f,mode);
        f.next_command();auto bytes=f.command_bytes(),stock=bytes;const auto memory=f.memory.blocks;
        const auto hooks=t5::dual_hook_ensure_calls;f.begin();f.finish(bytes);
        check(f.aim.serial&&bytes!=stock,"client refresh uses actual successful exact authored-command finish");
        const auto& p=f.aim.publications[(f.aim.serial-1)%f.aim.publications.size()];
        ca::Facts facts{true,true,false,0,1,0,p.weapon,aim_now,p.command.rig.published_ms,p.aim.packed,p.command.bytes};ca::Words selected{};
        check(ca::select(facts,&selected)&&p.command.bytes==bytes,"refresh selects exact publication52 and packed native command word");
        check(host.angles()==(mode==ca::Mode::correct?selected:client_angle_words(10,20))&&host.calls==(mode==ca::Mode::correct?2u:0u),
              "correct refresh touches two words while disabled and observe remain write free");
        auto restored=f.memory.blocks;
        if(mode==ca::Mode::correct){auto& block=restored.at(q1_fixture::Memory::cg);std::memcpy(block.data()+0xa9ce4,memory.at(q1_fixture::Memory::cg).data()+0xa9ce4,8);}
        check(restored==memory,"actual finish refresh changes no fixture byte outside the two cg floats including all server storage");
        std::memcpy(bytes.data()+0x20,stock.data()+0x20,4);
        check(bytes==stock&&t5::dual_hook_ensure_calls==hooks,"client-only option adds no command byte changes or native hook request");
        for(auto address:host.addresses)check(address==q1_fixture::Memory::cg+0xa9ce4||address==q1_fixture::Memory::cg+0xa9ce8,"refresh CAS addresses confined to exact cg fields");
    }
    for(unsigned reason=0;reason<12;++reason){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;
        if(reason==0)f.memory.dual();
        if(reason==1){
            t5::pre_snapshot.gameplay_active=false;t5::pre_snapshot.linked_weapon_active=true;t5::pre_snapshot.native_pm_type=1;
            for(auto player:{q1_fixture::Memory::cg+0x8a364,q1_fixture::Memory::server})f.memory.put<int>(player+4,1);
            f.memory.put<std::uint32_t>(0x1a796f8+0x298,0x306ed00);f.sync();
        }
        f.arm();ClientAngleFixtureHost host;host.enable(f);f.next_command();
        if(reason==2){t5::pre_snapshot.context.key_catcher=16;t5::pre_snapshot.gameplay_active=false;f.sync();}
        if(reason==3)rig.published_ms=aim_now-101;
        if(reason==4)generation_current=false;
        if(reason==5)f.memory.put<int>(q1_fixture::Memory::cg+0x8a360,1);
        if(reason==6)f.memory.put<int>(q1_fixture::Memory::definition+0x1c,2);
        if(reason==7)host.read_fail=true;
        if(reason==8)f.aim.client_angles_ready=false;
        auto bytes=f.command_bytes();f.begin();
        if(reason==9)t5::retire_native_command_aim_for_selection(f.context());
        if(reason==10){check(f.aim.pending.active,"visual-only refusal starts with admitted exact pending");f.aim.pending.visual_only_unarmed=f.aim.pending.render_only=true;}
        if(reason==11)f.memory.thread=0;
        const auto memory=f.memory.blocks;f.finish(bytes);
        check(!host.calls&&host.angles()==client_angle_words(10,20)&&f.memory.blocks==memory,
              "dual linked paused stale generation scripted projectile unreadable unready render-only or wrong-thread finish cannot refresh cg angles");
    }
    for(unsigned fault=0;fault<4;++fault){
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();ClientAngleFixtureHost host;host.enable(f);f.next_command();
        if(fault<2)host.fail_call=fault+1;
        if(fault==2)host.invalidate_after_write=true;
        if(fault==3)host.retire_after_first=true;
        auto bytes=f.command_bytes();f.begin();f.finish(bytes);
        if(fault<3)check(host.angles()==client_angle_words(10,20),"real refresh cancels partial exchange and post-write invalidation without residual owned values");
        else check(host.calls==1&&host.angles()[1]==client_angle_words(10,20)[1],"retired cg blocks the second store and all stale rollback CAS attempts");
        const auto* server=f.memory.pointer(q1_fixture::Memory::server+0x1c34,8);const auto expected=client_angle_words(31,42);
        check(!std::memcmp(server,expected.data(),8),"client refresh failure cleanup never writes authoritative server angles");
    }
    {
        auto owned=std::make_unique<Q1Fixture>(0);auto& f=*owned;f.arm();ClientAngleFixtureHost host;host.enable(f);f.next_command();
        auto bytes=f.command_bytes();active_q1=&f;on_rig_read=synchronize_profile;
        t5::original_create_cmd=reinterpret_cast<void*>(synthetic_native);
        check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"client refresh real native bridge callback registration");
        t5::pre_snapshot.native_cg_identity=reinterpret_cast<std::uintptr_t>(f.cg);
        native_count=0;ReturnState result{};invoke_command(reinterpret_cast<void*>(t5::command_detour),bytes.data(),&result);
        check(native_count==1&&host.calls==2&&host.angles()!=client_angle_words(10,20),"existing production command bridge invokes native once and then actual client refresh");
        check(result.eax==reinterpret_cast<std::uint32_t>(bytes.data())&&!std::memcmp(result.xmm,pattern,16)&&result.x87==1,
              "client refresh preserves original native result and floating register outputs");
        active_q1=nullptr;on_rig_read=nullptr;
    }
}
void failed_combined_configuration(){
    // Requested exact mode is latched in the input adapter. Losing publication
    // of that mode cannot downgrade a configured combined request to C1 alone.
    Q1Fixture f(3,true);f.aim.exact_profile=false;t5::aim_adapter.store(nullptr);
    for(float y:{0.f,1.f,1.f,0.f,1.f}){f.next_command();actions.hands[1].stick.current={0,y};f.sync();auto b=f.command_bytes();f.run(b);}
    check(dispatches.empty()&&f.controls.cycle_requests==0,"failed requested exact configuration cannot become standalone cycle");
    t5::aim_adapter.store(&f.aim);
}
void actual_bridge(bool cycle=false){
    Q1Fixture f(3,cycle);f.arm();f.next_command();auto b=f.command_bytes();active_q1=&f;on_rig_read=synchronize_profile;
    t5::original_create_cmd=reinterpret_cast<void*>(synthetic_native);
    check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"actual Q1 callback registration");
    native_count=0;ReturnState captured{};SetLastError(0x87654321);
    t5::pre_snapshot.native_cg_identity=reinterpret_cast<std::uintptr_t>(f.cg); // owned logical-address alias; avoid a synthetic epoch transition
    invoke_command(reinterpret_cast<void*>(t5::command_detour),b.data(),&captured);
    check(native_count==1&&f.token().serial!=0,"actual Q1 native original once and final write");
    check(captured.eax==reinterpret_cast<std::uint32_t>(b.data())&&(captured.flags&0x8d5)==0x44&&!memcmp(captured.xmm,pattern,16)&&
        captured.x87==1&&GetLastError()==0x87654321,"Q1 real bridge preserves native result flags FX LastError");
    f.next_command();t5::original_create_cmd=reinterpret_cast<void*>(synthetic_fault);native_count=0;
    t5::pre_snapshot.native_cg_identity=reinterpret_cast<std::uintptr_t>(f.cg);
    check(invoke_fault(reinterpret_cast<void*>(t5::command_detour),b.data())==0xe0421973&&native_count==1&&!f.token().serial,
        "Q1 original native fault propagates with no positive admission");
    active_q1=nullptr;on_rig_read=nullptr;
}
}
int main(){startup();acceptance();linked_command_aim();refusal();render_only_selection();paired_dual_aim();body_yaw_rebase();room_scale_rebase();held_support_recovery();held_dominant_render_recovery();client_command_refresh();const auto prisoner_start=checks;prisoner_refusals();printf("q1_prisoner_refusal_checks=%u\n",checks-prisoner_start);
    const auto cycle_start=checks;cycling();failed_combined_configuration();actual_bridge(true);
    printf("q1_cycle_combination_checks=%u\n",checks-cycle_start);actual_bridge();printf("q1_command_admission_checks=%u failures=%u actual_game_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;}
