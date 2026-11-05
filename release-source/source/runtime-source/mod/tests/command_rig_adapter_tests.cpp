// SPDX-License-Identifier: GPL-3.0-only
// Exact adapter and existing input callbacks with copied CPU fixtures. No native
// command hook, game image, graphics factory or OpenXR runtime is invoked.
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
static std::uint64_t fixture_now=1000;
static ULONGLONG rig_fixture_clock(){return fixture_now;}
#define GetTickCount64 rig_fixture_clock
#include "../src/t5/command_rig_adapter.cpp"
#include "../src/t5/input_adapter.cpp"
#undef GetTickCount64
namespace {
using namespace bovr;
unsigned checks{},failures{},action_reads{},rig_reads{},scope_reads{},callback_registrations{};
t5::PreCommandSnapshot fixture_native{};
wawvr::xr::ActionSnapshot fixture_actions{};
input::CommandRigSample fixture_rig{};
std::uint64_t fixture_generation{7};
bool fixture_native_readable=true,fixture_actions_readable=true,fixture_scope_active=false;
t5::NativeConsumedCommand fixture_scope{};
std::vector<std::string> sent_commands;
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
void __cdecl native_sink(int,int,const char* text){sent_commands.emplace_back(text);}
void prepare(std::uint64_t sequence=1,int command=100,std::uint64_t epoch=3) {
    fixture_native={};fixture_native.context.valid=true;fixture_native.context.local_client=0;
    fixture_native.context.native_thread=GetCurrentThreadId();fixture_native.context.command_number=command;
    fixture_native.context.connection_state=10;fixture_native.context.native_frame_time=4000+command;
    fixture_native.sequence=sequence;fixture_native.observed_ms=fixture_now;fixture_native.gameplay_epoch=epoch;
    fixture_native.native_cg_identity=0x123400;fixture_native.native_game_time=5000+command;
    fixture_native.native_health=100;fixture_native.native_gameplay_fields_valid=fixture_native.gameplay_active=true;
    fixture_generation=7;fixture_actions={};fixture_actions.focused=true;fixture_actions.sequence=sequence;
    for(auto& hand:fixture_actions.hands) {
        hand.grip.active=hand.grip.position_valid=hand.grip.orientation_valid=true;
        hand.grip.position_tracked=hand.grip.orientation_tracked=true;
        hand.grip.pose.position={1,2,3};hand.trigger.active=true;hand.stick.active=true;
    }
    fixture_rig={};fixture_rig.frame.actions=fixture_actions;fixture_rig.xr_generation=fixture_rig.anchor_generation=7;
    fixture_rig.published_ms=fixture_now-2;fixture_rig.frame.frame_id=sequence+200;
    fixture_rig.frame.predicted_display_time=987654321;fixture_rig.frame.should_render=fixture_rig.frame.views_valid=true;
    fixture_rig.tracking_anchor.position={4,5,6};fixture_rig.frame.head_center.position={7,8,9};
    fixture_native_readable=fixture_actions_readable=true;fixture_scope_active=false;fixture_scope={};
}
input::NativeCommandBytes native_bytes(int time=5100) {
    input::NativeCommandBytes bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i*3+1);
    std::memcpy(bytes.data(),&time,4);return bytes;
}
t5::NativeConsumedCommand consumed(const input::NativeCommandBytes& bytes,std::uint64_t serial=1) {
    t5::NativeConsumedCommand value{};value.command=bytes;value.scope_serial=serial;value.observed_ms=fixture_now;
    value.native_thread=GetCurrentThreadId();value.image_base=0x400000;
    value.active=value.local_identity=value.readable=true;value.native_returned=true;return value;
}
struct RigFixture {
    t5::RigAdapter state;
    RigFixture(){prepare();t5::rig_adapter.store(&state);}
    ~RigFixture(){t5::rig_adapter.store(nullptr);fixture_scope_active=false;}
    void begin(){t5::begin_command_rig(fixture_native.context,fixture_actions,fixture_generation,fixture_rig,fixture_actions_readable);}
    void finish(const input::NativeCommandBytes& bytes){t5::finish_command_rig(fixture_native.context,bytes.data());}
    void observe(const input::NativeCommandBytes& bytes,std::uint64_t serial=1){fixture_scope=consumed(bytes,serial);t5::consumed_rig_before(&state,fixture_scope);}
};
}
namespace bovr::t5 {
bool read_pre_command_snapshot(PreCommandSnapshot* p) noexcept {if(!p)return false;*p=fixture_native;return fixture_native_readable;}
bool native_command_dual_current(const CommandContext&) noexcept {return false;}
bool native_command_hitscan_current(const CommandContext&) noexcept {return false;}
bool native_command_ordinary_attack_inhibited(const CommandContext&) noexcept {return false;}
bool read_vr_input_snapshot(wawvr::xr::ActionSnapshot* a,std::uint64_t* g) noexcept {
    ++action_reads;*a=fixture_actions;*g=fixture_generation;return fixture_actions_readable;
}
bool read_vr_command_input_snapshot(wawvr::xr::ActionSnapshot* a,std::uint64_t* g,input::CommandRigSample* r) noexcept {
    ++rig_reads;*a=fixture_actions;*g=fixture_generation;*r=fixture_rig;return fixture_actions_readable;
}
bool read_consumed_command_scope(NativeConsumedCommand* p) noexcept {
    ++scope_reads;if(!fixture_scope_active)return false;*p=fixture_scope;return true;
}
bool set_consumed_command_callbacks(const ConsumedCommandCallbacks&) noexcept {++callback_registrations;return true;}
bool set_command_callbacks(const CommandCallbacks&) noexcept {++callback_registrations;return true;}
}
int trace_io_child_mode(int argc,char** argv);
void run_trace_io_tests();
int main(int argc,char** argv){
    if(const int child=trace_io_child_mode(argc,argv);child>=0)return child;
    run_trace_io_tests();
    using namespace bovr;using input::CommandRigStatus;
    SetEnvironmentVariableW(L"BOVR_COMMAND_RIG_PROBE",nullptr);
    check(!t5::configure_command_rig_adapter(GetModuleHandleW(nullptr),true,true)&&!t5::command_rig_enabled()&&!callback_registrations,"default off registers no callback or native observer");
    {
        prepare();t5::Adapter controls(0,false);controls.execute=native_sink;
        const auto old_actions=action_reads,old_rigs=rig_reads;
        t5::before_native(&controls,fixture_native.context);auto command=native_bytes();const auto baseline=command;
        t5::after_native(&controls,fixture_native.context,command.data());
        check(action_reads==old_actions+1&&rig_reads==old_rigs,"default input still uses exactly its old action cache API");
        check(command==baseline,"default neutral input preserves all native bytes");
    }
    {
        RigFixture f;t5::Adapter controls(0,false);controls.execute=native_sink;
        const auto old_actions=action_reads,old_rigs=rig_reads;
        t5::before_native(&controls,fixture_native.context);auto command=native_bytes();const auto baseline=command;
        const auto original=fixture_rig;
        // A newer owner publication between before/after cannot replace the rig.
        fixture_actions.sequence=999;fixture_rig.frame.frame_id=999;fixture_rig.published_ms=fixture_now;
        fixture_rig.tracking_anchor.position.x=99;
        t5::after_native(&controls,fixture_native.context,command.data());
        check(rig_reads==old_rigs+1&&action_reads==old_actions,"opt-in uses one atomic action-plus-rig read and no post pose read");
        check(command==baseline&&f.state.last_publish==CommandRigStatus::accepted,"neutral native result still publishes exact final52 without edits");
        fixture_actions=original.frame.actions;fixture_rig=original;f.observe(command);
        check(f.state.current.valid&&f.state.current.produced.bytes==baseline,"actual consumed fingerprint joins neutral command");
        check(f.state.current.produced.rig.frame.frame_id==original.frame.frame_id&&f.state.current.produced.rig.published_ms==original.published_ms&&f.state.current.produced.rig.tracking_anchor.position.x==4,"joined copy retains original XR frame publication and anchor despite newer cache");
        input::CommandRigMatch output{};const auto no_scope=scope_reads;
        check(!t5::read_current_command_rig(&output)&&!output.valid&&scope_reads==no_scope+1,"copied callback match is hidden before native ClientThink scope");
        fixture_scope_active=true;check(t5::read_current_command_rig(&output)&&output.valid,"same current native scope exposes copied association");
        fixture_scope.native_thread+=1;check(!t5::read_current_command_rig(&output)&&!output.valid,"different native thread cannot read scope association");fixture_scope.native_thread-=1;
        fixture_scope.mismatches=t5::consumed_nested;check(!t5::read_current_command_rig(&output),"nested poisoned scope never exposes rig");fixture_scope.mismatches=0;
        t5::consumed_rig_after(&f.state,fixture_scope);check(!t5::read_current_command_rig(&output)&&!f.state.current.valid,"after callback retires current rig even if a stale test scope remains visible");
    }
    {
        RigFixture f;t5::Adapter controls(0,false);controls.execute=native_sink;
        t5::before_native(&controls,fixture_native.context);auto neutral=native_bytes();t5::after_native(&controls,fixture_native.context,neutral.data());
        ++fixture_now;prepare(2,101);fixture_actions.hands[0].stick.current={-.6f,.8f};fixture_actions.hands[1].trigger.current=.8f;
        fixture_rig.frame.actions=fixture_actions;t5::before_native(&controls,fixture_native.context);
        auto command=native_bytes(5101);auto expected=command;expected[0x1b]=102;expected[0x1c]=static_cast<unsigned char>(static_cast<std::int8_t>(-76));
        t5::after_native(&controls,fixture_native.context,command.data());
        check(command==expected&&std::fabs(controls.controls.forward-.8f)<0.00001f,"existing input router writes intended two analog bytes before publication");
        f.observe(command);check(f.state.current.valid&&f.state.current.produced.bytes==expected,"ledger fingerprint includes final post-analog bytes");
        check(f.state.current.produced.rig.frame.actions.hands[0].stick.current.x==-.6f&&f.state.current.produced.rig.frame.actions.hands[1].trigger.current==.8f,"movement and attack controls share the recorded original action sample");
    }
    {
        RigFixture f;fixture_native.observed_ms=fixture_now-4;fixture_rig.published_ms=fixture_now-2;
        f.begin();const auto command=native_bytes();f.finish(command);f.observe(command);
        check(f.state.current.valid&&f.state.current.produced.observed_ms==fixture_now&&
              f.state.current.produced.rig.published_ms==fixture_now-2,
              "XR publication after native pre-context but before actual input capture remains valid without refreshing original publication");
    }
    for(unsigned byte=0;byte<52;++byte) {
        RigFixture f;f.begin();auto command=native_bytes();f.finish(command);auto changed=command;changed[byte]^=1;
        f.observe(changed);check(!f.state.current.valid,"any native consumed byte difference rejects instead of canonicalizing or guessing");
        check(command==native_bytes(),"passive comparison never modifies producer source bytes");
    }
    for(unsigned invalid=0;invalid<15;++invalid) {
        RigFixture f;
        switch(invalid) {
        case 0:fixture_native_readable=false;break;case 1:fixture_native.context.valid=false;break;
        case 2:fixture_native.context.local_client=1;break;case 3:fixture_native.gameplay_active=false;break;
        case 4:fixture_native.native_gameplay_fields_valid=false;break;case 5:fixture_native.context.native_override=true;break;
        case 6:fixture_native.context.key_catcher=0x10;break;case 7:fixture_native.observed_ms=fixture_now-101;break;
        case 8:fixture_actions_readable=false;break;case 9:fixture_actions.focused=false;break;
        case 10:fixture_rig.anchor_generation=0;break;case 11:fixture_rig.xr_generation=8;break;
        case 12:fixture_rig.published_ms=fixture_now-101;break;case 13:fixture_rig.published_ms=fixture_now+1;break;
        case 14:fixture_rig.frame.actions.sequence++;break;
        }
        f.begin();const auto command=native_bytes();f.finish(command);
        check(!f.state.pending.present&&f.state.publications==0,"invalid pre-command/context/rig is never retained or published");
    }
    for(unsigned invalid=0;invalid<9;++invalid) {
        RigFixture f;f.begin();auto after=fixture_native.context;const auto command=native_bytes();
        switch(invalid) {
        case 0:after.valid=false;break;case 1:after.command_number++;break;case 2:after.native_frame_time++;break;
        case 3:after.native_thread++;break;case 4:fixture_native.sequence++;break;case 5:fixture_native.gameplay_epoch++;break;
        case 6:fixture_native.gameplay_active=false;break;case 7:fixture_now+=101;break;case 8:fixture_native_readable=false;break;
        }
        t5::finish_command_rig(after,command.data());check(f.state.last_publish!=CommandRigStatus::accepted&&!f.state.pending.present,"post mismatch or elapsed original publication rejects and retires pending copy");
    }
    {
        RigFixture f;f.begin();t5::finish_command_rig(fixture_native.context,reinterpret_cast<const unsigned char*>(1));
        check(f.state.last_publish==CommandRigStatus::invalid&&!f.state.pending.present,"unreadable final result is contained without native memory writes");
    }
    {
        RigFixture f;f.begin();++fixture_native.sequence;f.begin();check(f.state.overlapping&&!f.state.pending.present,"overlapping or unpaired begin poisons ambiguous producer identity");
        f.finish(native_bytes());++fixture_native.sequence;f.begin();f.finish(native_bytes());
        check(!f.state.publications,"same epoch cannot revive an outer return with a newer pending sample");
        ++fixture_native.gameplay_epoch;++fixture_native.sequence;f.begin();f.finish(native_bytes());
        check(!f.state.overlapping&&f.state.last_publish==CommandRigStatus::accepted&&f.state.producer_serial==4,"actual native epoch permits rearm without resetting process serial");
    }
    {
        RigFixture f;f.begin();const auto command=native_bytes();f.finish(command);
        std::thread server([&]{f.observe(command);fixture_scope_active=true;input::CommandRigMatch match{};
            check(t5::read_current_command_rig(&match)&&match.server_thread==GetCurrentThreadId()&&match.produced.native_thread!=match.server_thread,"server and producer threads may differ while actual scope identity remains exact");
            fixture_scope.native_returned=false;t5::consumed_rig_after(&f.state,fixture_scope);
            check(!t5::read_current_command_rig(&match),"native unwind after notification invalidates association");});server.join();
    }
    for(unsigned invalid=0;invalid<7;++invalid) {
        RigFixture f;f.begin();auto command=native_bytes();f.finish(command);fixture_scope=consumed(command);
        switch(invalid) {
        case 0:fixture_scope.mismatches=t5::consumed_source_changed;break;case 1:fixture_scope.active=false;break;
        case 2:fixture_native.gameplay_epoch++;break;case 3:fixture_generation++;break;
        case 4:fixture_actions.focused=false;break;case 5:fixture_native.observed_ms=fixture_now-101;break;
        case 6:fixture_scope.observed_ms=fixture_now+1;break;
        }
        t5::consumed_rig_before(&f.state,fixture_scope);check(!f.state.current.valid,"consumption requires current epoch focus generation native ownership and original observation freshness");
    }
    {
        RigFixture f;f.begin();auto command=native_bytes();f.finish(command);f.observe(command);
        fixture_scope_active=true;input::CommandRigMatch match{};fixture_now+=101;
        check(!t5::read_current_command_rig(&match),"association getter does not refresh old publication time");
        t5::cancel_command_rig("fixture");check(!f.state.current.valid&&!f.state.pending.present,"explicit cancellation clears association and pending values");
    }
    {
        RigFixture f;f.begin();auto command=native_bytes();f.finish(command);f.observe(command);
        fixture_scope_active=true;t5::NativeLocalShot shot{};
        shot.serial=91;shot.observed_ms=fixture_now;shot.native_thread=GetCurrentThreadId();
        shot.local_shot_confirmed=true;shot.weapon_index=command[0x18];
        const auto original=f.state.current.produced;
        SetLastError(0x12345001);
        check(t5::trace_native_shot_command_rig(shot)&&GetLastError()==0x12345001,"actual-scope shot diagnostic returns association and preserves LastError");
        check(f.state.current.produced.bytes==original.bytes&&f.state.current.produced.rig.published_ms==original.rig.published_ms&&
              shot.serial==91&&shot.weapon_index==command[0x18],"shot diagnostic never changes command source time or shot values");
        ++shot.native_thread;check(!t5::trace_native_shot_command_rig(shot),"foreign shot thread cannot borrow active scope");
        shot.native_thread=GetCurrentThreadId();shot.local_shot_confirmed=false;
        check(!t5::trace_native_shot_command_rig(shot),"unconfirmed native shot has no association diagnostic");
        shot.local_shot_confirmed=true;fixture_scope_active=false;
        check(!t5::trace_native_shot_command_rig(shot),"shot outside actual ClientThink scope cannot acquire recorded rig");
    }
    t5::rig_adapter.store(nullptr);
    check(callback_registrations==0,"CPU test never installs native callbacks or touches real command hook");
    std::printf("%s command_rig_adapter_checks=%u failures=%u retail_calls=0 xr_calls=0 native_shot_writes=0\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
