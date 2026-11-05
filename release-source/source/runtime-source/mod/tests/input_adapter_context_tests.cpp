// SPDX-License-Identifier: GPL-3.0-only
// White-box boundary regression: no adapter configuration, XR start, executable
// identity acceptance or native command dispatch. Include this isolated TU to
// exercise the actual pre/post callbacks without exposing them as production API.
#define configure_input_adapter configure_input_adapter_context_test_copy
#define read_vr_input_snapshot read_vr_input_snapshot_context_test_copy
#define read_pre_command_snapshot read_pre_command_snapshot_context_test_copy
#define manual_reload_uses_offhand manual_reload_uses_offhand_context_test_copy
#define manual_grenade_consumes_trigger manual_grenade_consumes_trigger_context_test_copy
#define cancel_manual_reload_for_selection cancel_manual_reload_for_selection_context_test_copy
#define exact_profile_aim_enabled exact_profile_aim_enabled_context_test_copy
#define retire_native_command_aim_for_selection retire_native_command_aim_for_selection_context_test_copy
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/input_adapter.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#undef configure_input_adapter
#undef read_vr_input_snapshot
#undef read_pre_command_snapshot
#undef manual_reload_uses_offhand
#undef manual_grenade_consumes_trigger
#undef cancel_manual_reload_for_selection
#undef exact_profile_aim_enabled
#undef retire_native_command_aim_for_selection
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
wawvr::xr::ActionSnapshot test_snapshot{};
bool test_snapshot_live{};
std::vector<std::string> test_native_commands;
std::uint64_t test_generation=7;
bovr::t5::PreCommandSnapshot test_pre{};
bool test_pre_live{};
unsigned test_pre_reads{};
void (*test_sink_effect)(){};
void (*test_pre_effect)(){};
bovr::t5::AlternateSlots test_alternate{};
unsigned test_alternate_reads{},test_alternate_fault{};
bovr::t5::SupportSelection test_support{};
bool test_support_available{};
unsigned test_support_reads{},test_support_fault{};
bool test_reload_busy{},test_grenade_busy{},test_cancel_allowed=true,test_exact_aim{};
unsigned test_cancel_calls{},test_retire_calls{};
void (*test_cancel_effect)(){};
std::vector<std::string> test_selection_events;
bool copy_support_fixture(std::uintptr_t,const bovr::t5::PreCommandSnapshot&,bovr::t5::SupportSelection* out) noexcept {
    ++test_support_reads;*out=test_support;
    if(!test_support_available||test_support_fault==1)return false;
    if(test_support_reads==2&&test_support_fault==2)++out->descriptor.variant;
    if(test_support_reads>=3&&test_support_fault==3)++out->held;
    if(test_support_reads==1) {
        if(test_support_fault==4)++test_pre.sequence;
        if(test_support_fault==5)test_pre.observed_ms=GetTickCount64()-251;
        if(test_support_fault==6)++test_pre.gameplay_epoch;
        if(test_support_fault==7)++test_pre.native_cg_identity;
        if(test_support_fault==8)++test_pre.native_game_time;
    }
    return true;
}
bool copy_alternate_fixture(std::uintptr_t,const bovr::t5::PreCommandSnapshot&,bovr::t5::AlternateSlots* out) noexcept {
    ++test_alternate_reads;*out=test_alternate;
    if(test_alternate_fault==1)return false;
    if(test_alternate_reads==2) {
        if(test_alternate_fault==2)out->types={2,0,0,0};
        if(test_alternate_fault==3)++out->weapon;
        if(test_alternate_fault==4)++out->requested;
    }
    if(test_alternate_reads==1) {
        if(test_alternate_fault==5)++test_pre.sequence;
        if(test_alternate_fault==6)test_pre.observed_ms=GetTickCount64()-251;
        if(test_alternate_fault==7)++test_pre.native_cg_identity;
        if(test_alternate_fault==8)++test_pre.context.native_frame_time;
    }
    return true;
}
void __cdecl capture_native_command(int,int,const char* command) {
    test_native_commands.emplace_back(command);
    test_selection_events.emplace_back(command);
    if(test_sink_effect)test_sink_effect();
}
}
namespace bovr::t5 {
bool manual_reload_uses_offhand_context_test_copy() noexcept {return test_reload_busy;}
bool manual_grenade_consumes_trigger_context_test_copy() noexcept {return test_grenade_busy;}
bool exact_profile_aim_enabled_context_test_copy() noexcept {return test_exact_aim;}
bool cancel_manual_reload_for_selection_context_test_copy(const CommandContext& context) noexcept {
    ++test_cancel_calls;test_selection_events.emplace_back("cancel");
    if(test_cancel_effect)test_cancel_effect();
    return test_cancel_allowed&&context.valid&&context.native_thread==GetCurrentThreadId()&&
        context.command_number==test_pre.context.command_number&&
        context.native_frame_time==test_pre.context.native_frame_time;
}
void retire_native_command_aim_for_selection_context_test_copy(const CommandContext&) noexcept {
    ++test_retire_calls;test_selection_events.emplace_back("retire");
}
bool read_vr_input_snapshot_context_test_copy(wawvr::xr::ActionSnapshot* output,
                                           std::uint64_t* generation) noexcept {
    *output=test_snapshot;*generation=test_generation;return test_snapshot_live;
}
bool read_pre_command_snapshot_context_test_copy(PreCommandSnapshot* output) noexcept {
    ++test_pre_reads;if(test_pre_effect)test_pre_effect();
    *output=test_pre_live?test_pre:PreCommandSnapshot{};return test_pre_live;
}
}

namespace {
struct CycleFixture {
    bovr::t5::Adapter state{0,false,true};
    bovr::t5::CommandContext context{};
    CycleFixture() {
        test_native_commands.clear();test_pre={};test_pre_reads=0;
        test_pre_live=test_snapshot_live=true;test_pre_effect=test_sink_effect=nullptr;test_generation=7;
        test_reload_busy=test_grenade_busy=test_exact_aim=false;test_cancel_allowed=true;
        test_cancel_calls=test_retire_calls=0;test_cancel_effect=nullptr;test_selection_events.clear();
        context.valid=true;context.local_client=0;context.native_thread=GetCurrentThreadId();
        context.command_number=100;context.native_frame_time=1000;context.connection_state=10;
        // This fixture has no native yaw memory. It tests cycle delivery only.
        context.viewangles[1]=std::numeric_limits<float>::quiet_NaN();
        test_snapshot={};test_snapshot.focused=true;
        for(auto& h:test_snapshot.hands) {
            h.grip.active=h.grip.position_valid=h.grip.orientation_valid=true;
            h.grip.position_tracked=h.grip.orientation_tracked=true;
            h.trigger.active=h.stick.active=true;
        }
        test_pre.native_cg_identity=0x12340000;test_pre.gameplay_epoch=1;
        test_pre.native_health=100;test_pre.native_gameplay_fields_valid=test_pre.gameplay_active=true;
        state.execute=capture_native_command;
        state.copy_alternate=copy_alternate_fixture;
        state.copy_support=copy_support_fixture;
        test_alternate={{{0,0,2,0}},32,32};test_alternate_reads=test_alternate_fault=0;
        test_support={test_pre.native_cg_identity,0x123f0000,32,{24,0x13000000,0x13001000,0x13002000,2,4,1}};
        test_support_available=false;test_support_reads=test_support_fault=0;
    }
    void prepare(float y) {
        ++context.command_number;context.native_frame_time+=16;
        ++test_pre.sequence;test_pre.native_game_time+=16;test_pre.context=context;
        test_pre.observed_ms=GetTickCount64();
        ++test_snapshot.sequence;test_snapshot.hands[1].stick.current={0,y};
    }
    void invoke() {bovr::t5::before_native(&state,context);}
    void step(float y) {prepare(y);invoke();}
};
CycleFixture* recursive_fixture{};
void revoke_during_cancel() {bovr::t5::revoke_cycle(recursive_fixture->state);}
void grenade_during_edge() {test_sink_effect=nullptr;test_grenade_busy=true;}
void reload_during_edge() {test_sink_effect=nullptr;test_reload_busy=true;}
void support_assignment_during_edge() {test_sink_effect=nullptr;++test_support.descriptor.weapon;}
unsigned boundary_checks{},boundary_failures{};
// Compare the same object's owned storage immediately around each rejected
// callback. Atomic revocation is deliberately excluded: it must change. The
// owner is suspended during the joined foreign call, so this fixture introduces
// no concurrent non-atomic observation or production/private-layout seam.
std::vector<unsigned char> owner_metadata(const bovr::t5::Adapter& a) {
    std::vector<unsigned char> bytes;
    const auto append=[&]<class T>(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* first=reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(),first,first+sizeof(value));
    };
    append(a.base);append(a.router);append(a.turn);append(a.execute);append(a.copy_alternate);append(a.copy_support);append(a.controls);
    append(a.generation);append(a.samples);append(a.edges);append(a.owner);
    append(a.gameplay);append(a.override_active);append(a.pending_release);
    append(a.seen_command);append(a.last_command);append(a.cycle_enabled);
    append(a.cycle_busy);append(a.cycle_seen_revocation);append(a.cycle_previous);
    append(a.cycle_generation);append(a.cycle_requests);
    return bytes;
}
void check_boundary(bool ok,const char* name) {
    ++boundary_checks;
    if(!ok) {++boundary_failures;printf("FAIL rejected callback boundary %s\n",name);}
}
void observe_rejected_callbacks(CycleFixture& fixture) {
    auto c=fixture.context;c.native_thread=GetCurrentThreadId();
    auto& a=fixture.state;
    const auto original=owner_metadata(a);
    const auto owner=a.cycle_owner.load();
    const auto revision=a.cycle_revocations.load();
    const auto commands=test_native_commands.size();const auto reads=test_pre_reads;
    bovr::t5::before_native(&a,c);
    check_boundary(owner_metadata(a)==original,"before preserves all owned metadata");
    check_boundary(a.cycle_owner.load()==owner && a.cycle_revocations.load()==revision+1,
                   "before only revokes atomically");
    check_boundary(test_native_commands.size()==commands && test_pre_reads==reads,
                   "before performs no delivery or copied-pre read");
    std::array<unsigned char,0x34> command{};command.fill(0x96);const auto saved=command;
    bovr::t5::after_native(&a,c,command.data());
    check_boundary(owner_metadata(a)==original,"after preserves all owned metadata");
    check_boundary(a.cycle_owner.load()==owner && a.cycle_revocations.load()==revision+2,
                   "after only revokes atomically");
    check_boundary(command==saved,"after preserves every one of52 command bytes");
    check_boundary(test_native_commands.size()==commands && test_pre_reads==reads,
                   "after performs no delivery or copied-pre read");
}
void nested_effect() {
    test_sink_effect=nullptr;
    observe_rejected_callbacks(*recursive_fixture);
}
void pre_nested_effect() {
    test_pre_effect=nullptr;
    observe_rejected_callbacks(*recursive_fixture);
}
void throw_after_effect() {test_sink_effect=nullptr;throw std::runtime_error("recorded native-like effect");}
void foreign_effect() {
    test_sink_effect=nullptr;
    std::thread foreign([] {
        observe_rejected_callbacks(*recursive_fixture);
    });foreign.join();
}
}

int main() {
    unsigned failures{};
    const auto owner=GetCurrentThreadId();
    for(unsigned failure_case=0;failure_case<3;++failure_case) {
        bovr::t5::Adapter state(0,false);
        state.owner=owner;state.gameplay=true;state.override_active=false;
        state.controls.forward=1;state.controls.right=-1;
        bovr::t5::CommandContext before{};
        before.valid=failure_case!=0;
        before.local_client=failure_case==1?1:0;
        before.native_thread=failure_case==2?owner+1:owner;
        before.connection_state=9;
        bovr::t5::before_native(&state,before);
        bovr::t5::CommandContext after{};
        after.valid=true;after.local_client=0;after.native_thread=owner;after.connection_state=9;
        std::array<unsigned char,0x34> command{},expected{};command.fill(0x5a);expected=command;
        bovr::t5::after_native(&state,after,command.data());
        const bool passed=!state.gameplay && state.controls.forward==0 && state.controls.right==0 &&
                          state.pending_release && command==expected;
        printf("%s invalid_pre_case=%u preserves52_post_bytes\n",passed?"PASS":"FAIL",failure_case);
        if(!passed)++failures;
    }
    {
        // Map changes can reset the command ring while XR remains focused and
        // continuously publishes held controls. Exercise the production callback
        // using copied XR values and a command sink; no engine memory is mapped.
        bovr::t5::Adapter state(0,false);
        state.execute=capture_native_command;
        bovr::t5::CommandContext context{};
        context.valid=true;context.local_client=0;context.native_thread=owner;
        context.connection_state=10;context.native_frame_time=1234;
        context.command_number=100;
        test_snapshot_live=true;test_snapshot.sequence=1;test_snapshot.focused=true;
        for(auto& hand:test_snapshot.hands) {
            hand.grip.active=hand.grip.orientation_valid=hand.grip.position_valid=true;
            hand.grip.orientation_tracked=hand.grip.position_tracked=true;
            hand.trigger.active=true;hand.stick.active=true;
        }
        bovr::t5::before_native(&state,context); // Valid neutral arms controls.
        test_snapshot.sequence=2;test_snapshot.hands[1].trigger.current=.8f;
        test_snapshot.hands[0].stick.current.y=1.f;context.command_number=101;
        bovr::t5::before_native(&state,context);
        std::array<unsigned char,0x34> moving{};moving.fill(0x5a);
        bovr::t5::after_native(&state,context,moving.data());
        const bool initially_active=test_native_commands.size()==1 &&
            test_native_commands[0].find("+attack 28672 ")==0 && moving[0x1b]==127;
        test_snapshot.sequence=3;context.command_number=0;
        bovr::t5::before_native(&state,context);
        std::array<unsigned char,0x34> command{},expected{};command.fill(0x5a);expected=command;
        bovr::t5::after_native(&state,context,command.data());
        const bool reset_safe=initially_active && test_native_commands.size()==2 &&
            test_native_commands[1].find("-attack 28672 ")==0 &&
            state.controls.forward==0 && state.controls.right==0 && command==expected;
        // More fresh, still-held data must remain inhibited in the new map.
        test_snapshot.sequence=4;context.command_number=1;
        bovr::t5::before_native(&state,context);
        const bool held_suppressed=test_native_commands.size()==2 && state.controls.forward==0;
        test_snapshot.sequence=5;context.command_number=2;
        test_snapshot.hands[1].trigger.current=0;test_snapshot.hands[0].stick.current.y=0;
        bovr::t5::before_native(&state,context);
        test_snapshot.sequence=6;context.command_number=3;
        test_snapshot.hands[1].trigger.current=.8f;test_snapshot.hands[0].stick.current.y=1;
        bovr::t5::before_native(&state,context);
        const bool rearmed=test_native_commands.size()==3 &&
            test_native_commands[2].find("+attack 28672 ")==0 && state.controls.forward==1;
        const bool passed=reset_safe && held_suppressed && rearmed;
        printf("%s native_ring_reset_releases_and_requires_neutral\n",passed?"PASS":"FAIL");
        if(!passed)++failures;
    }
    unsigned cycle_checks{};
    auto verify=[&](bool ok,const char* label){++cycle_checks;if(!ok){++failures;printf("FAIL cycle %s\n",label);}};
    {
        CycleFixture f;f.step(0);f.step(1);
        verify(test_native_commands==std::vector<std::string>{"weapnext"},"exact one-shot payload");
        verify(!f.state.controls.weapon_next,"one-shot output consumed before native delivery");
        std::array<unsigned char,0x34> command{};command.fill(0xa5);const auto original=command;
        bovr::t5::after_native(&f.state,f.context,command.data());
        verify(command==original,"all52 native command bytes preserved including weapon/offhand/alt");
        f.invoke();f.step(1);verify(test_native_commands.size()==1,"duplicate native command and held XR never retry");
        f.step(0);f.step(1);verify(test_native_commands.size()==2,"new neutral restores next request");
        f.step(0);verify(test_native_commands.size()==2,"neutral has no fake release");
    }
    for(unsigned fault=0;fault<21;++fault) {
        CycleFixture f;f.step(0);f.prepare(1);
        switch(fault) {
        case 0:test_pre_live=false;break;
        case 1:test_pre.context.valid=false;break;
        case 2:test_pre.context.local_client=1;break;
        case 3:++test_pre.context.command_number;break;
        case 4:++test_pre.context.native_frame_time;break;
        case 5:++test_pre.context.native_thread;break;
        case 6:test_pre.context.key_catcher=8;break;
        case 7:--test_pre.context.connection_state;break;
        case 8:test_pre.context.native_override=true;break;
        case 9:test_pre.native_gameplay_fields_valid=false;break;
        case 10:test_pre.gameplay_active=false;test_pre.native_pm_type=5;break;
        case 11:test_pre.native_cg_identity=0;break;
        case 12:test_pre.gameplay_epoch=0;break;
        case 13:test_pre.sequence=0;break;
        case 14:test_pre.observed_ms=GetTickCount64()+10000;break;
        case 15:test_pre.observed_ms=GetTickCount64()-1000;break;
        case 16:test_pre.sequence=f.state.cycle_previous.sequence;break;
        case 17:--f.context.command_number;test_pre.context=f.context;break;
        case 18:test_pre.native_game_time=0;break;
        case 19:test_snapshot_live=false;break;
        case 20:test_snapshot.focused=false;break;
        }
        f.invoke();verify(test_native_commands.empty(),"bad current/source gate refuses before dispatch");
        std::array<unsigned char,0x34> refused_command{};refused_command.fill(0x5a);const auto refused_original=refused_command;
        bovr::t5::after_native(&f.state,f.context,refused_command.data());
        verify(refused_command==refused_original,"refused cycle preserves all52 post bytes");
        test_pre_live=test_snapshot_live=true;test_snapshot.focused=true;
        test_pre.native_cg_identity=0x12340000;test_pre.gameplay_epoch=1;
        test_pre.native_gameplay_fields_valid=test_pre.gameplay_active=true;test_pre.native_pm_type=0;
        test_pre.sequence=100;test_pre.native_game_time=1000;
        f.step(1);verify(test_native_commands.empty(),"restoring failed source cannot revive held press");
        f.step(0);f.step(1);verify(test_native_commands.size()==1,"fresh neutral after gate recovery");
    }
    for(unsigned transition=0;transition<4;++transition) {
        CycleFixture f;f.step(0);f.prepare(1);
        if(transition==0)++test_pre.gameplay_epoch;
        if(transition==1)++test_pre.native_cg_identity;
        if(transition==2)++test_generation;
        if(transition==3){f.context.command_number=1;test_pre.context=f.context;}
        f.invoke();verify(test_native_commands.empty(),"epoch/cg/generation/native regression disarms");
        f.step(0);f.step(1);verify(test_native_commands.size()==1,"neutral in new context arms");
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        CycleFixture f;f.prepare(0);
        // These values are invalid on the first observation, independent of
        // previous-generation or monotonicity checks. Invalid neutral must not
        // silently arm a later held upward gesture.
        if(invalid==0)test_generation=0;
        if(invalid==1)test_snapshot.sequence=0;
        if(invalid==2)test_pre.observed_ms=0;
        if(invalid==3)test_pre.native_game_time=0;
        if(invalid==4)test_pre.native_game_time=-1;
        f.invoke();
        verify(f.state.cycle_revocations.load()!=0 && f.state.cycle_previous.sequence==0 &&
               test_native_commands.empty(),"zero/nonpositive first occurrence latches refusal");
        test_generation=7;test_pre.native_game_time=1000;
        f.step(1);verify(test_native_commands.empty(),"invalid first neutral cannot arm restored held up");
        f.step(0);f.step(1);verify(test_native_commands.size()==1,"valid first neutral followed by fresh press recovers");
    }
    for(unsigned fault=0;fault<4;++fault) {
        CycleFixture f;recursive_fixture=&f;f.step(0);
        // Nonzero axes make a missing nested/foreign post guard observable in
        // command bytes; the metadata snapshot also covers the router and turn.
        test_snapshot.hands[0].stick.current.y=.8f;
        if(fault==0)test_sink_effect=throw_after_effect;
        if(fault==1)test_sink_effect=nested_effect;
        if(fault==2)test_sink_effect=foreign_effect;
        if(fault==3)test_pre_effect=pre_nested_effect;
        bool threw{};
        try {f.step(1);}catch(const std::runtime_error&){threw=true;}
        verify(threw==(fault==0),"throw remains visible to existing caller containment");
        verify(test_native_commands.size()==(fault==3?0u:1u),"effect/nesting boundary emits at most once");
        f.step(1);verify(test_native_commands.size()==(fault==3?0u:1u),"no retry after effect/reentry/foreign refusal");
        f.step(0);f.step(1);verify(test_native_commands.size()==(fault==3?1u:2u),"owner recovers only after fresh neutral");
        verify(!f.state.cycle_busy,"owner scope retired after normal or exceptional exit");
    }
    {
        CycleFixture f;f.step(0);test_snapshot.hands[0].stick.current.y=.8f;f.step(0);
        verify(f.state.controls.forward!=0,"foreign idle boundary has live owned movement");
        std::thread foreign([&]{observe_rejected_callbacks(f);});foreign.join();
        f.step(1);verify(test_native_commands.empty(),"foreign idle revocation requires neutral");
        f.step(0);f.step(1);verify(test_native_commands.size()==1,"foreign idle owner recovery");
    }
    for(unsigned fault=0;fault<3;++fault) {
        CycleFixture f;recursive_fixture=&f;f.step(0);
        test_snapshot.hands[1].trigger.current=.8f;
        test_sink_effect=fault==0?throw_after_effect:fault==1?nested_effect:foreign_effect;
        bool threw{};
        try {f.step(1);}catch(const std::runtime_error&){threw=true;}
        verify(threw==(fault==0),"paired-edge exception remains visible");
        verify(f.state.cycle_requests==1 && test_native_commands.size()==1 &&
               test_native_commands[0].find("+attack 28672 ")==0,
               "cycle consumed before earlier paired edge throws or revokes");
        f.step(1);verify(test_native_commands.size()==1,"lost reserved cycle has no retry on held input");
        test_snapshot.hands[1].trigger.current=0;f.step(0);f.step(1);
        verify(f.state.cycle_requests==2 && test_native_commands.size()==3 &&
               test_native_commands[1].find("-attack 28672 ")==0 && test_native_commands[2]=="weapnext",
               "paired edge releases normally and a new neutral cycle can dispatch");
        verify(!f.state.cycle_busy,"paired-edge exceptional scope retires");
    }
    {
        CycleFixture f;f.step(0);f.prepare(1);++test_snapshot.sequence;
        // A second, fresh XR publication on the same native command is refused.
        --f.context.command_number;test_pre.context=f.context;f.invoke();
        verify(test_native_commands.empty(),"fresh XR cannot multiply a native command");
    }
    {
        CycleFixture f;f.step(0);f.state.cycle_revocations.store((std::numeric_limits<unsigned long>::max)());
        f.step(1);f.step(0);f.step(1);verify(test_native_commands.empty(),"saturated revocation cannot ABA-rearm");
    }
    {
        CycleFixture f;f.context.valid=false;f.prepare(0);
        f.state.gameplay=true;f.state.controls.forward=.75f;f.state.pending_release=true;
        const auto initial=owner_metadata(f.state);f.invoke();
        verify(owner_metadata(f.state)==initial,"nonowned initial before preserves all owned metadata");
        std::array<unsigned char,0x34> command{};command.fill(0xb7);const auto original=command;
        bovr::t5::after_native(&f.state,f.context,command.data());
        verify(owner_metadata(f.state)==initial && command==original,
               "nonowned initial after preserves metadata and52bytes");
        verify(f.state.cycle_owner.load()==0 && test_native_commands.empty(),"invalid first callback cannot claim cycle owner");
        f.context.valid=true;f.step(0);f.step(1);
        verify(f.state.cycle_owner.load()==GetCurrentThreadId() && test_native_commands.size()==1,"first valid owner claims then requires neutral");
    }
    verify(boundary_checks==42 && boundary_failures==0,"all nested and foreign immediate boundaries pass");
    for(unsigned fault=0;fault<3;++fault) {
        CycleFixture f;recursive_fixture=&f;f.state.cod4_controls=true;
        f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;
        test_snapshot.hands[1].squeeze.active=true;
        test_reload_busy=true;test_exact_aim=true;
        f.step(0);
        test_snapshot.hands[0].secondary.current=true;
        test_snapshot.hands[1].squeeze.current=1;
        if(fault==1)test_cancel_allowed=false;
        if(fault==2)test_cancel_effect=revoke_during_cancel;
        f.step(0);
        verify(test_cancel_calls==1,"fresh Y consults current reload cancellation exactly once");
        verify(test_selection_events==(fault==0?std::vector<std::string>{"cancel","retire","weapnext"}:
               std::vector<std::string>{"cancel"}),
               "reload cancellation precedes aim retirement and native selection; refusal or revocation stops delivery");
        verify(test_reload_busy,"unconfirmed native selection does not clear outstanding reload safety in input layer");
        verify(!f.state.controls.weapon_next&&!f.state.controls.weapon_alternate&&!f.state.cycle_busy,
               "reload selection request and scope are consumed without retaining a retry");
        const auto event_count=test_selection_events.size();
        f.invoke();f.step(0);
        verify(test_cancel_calls==1&&test_selection_events.size()==event_count,
               "duplicate or held Y never retries rejected or ineffective native selection");
        test_cancel_allowed=true;test_cancel_effect=nullptr;
        test_snapshot.hands[0].secondary.current=false;f.step(0);
        test_snapshot.hands[0].secondary.current=true;f.step(0);
        verify(test_cancel_calls==2&&test_selection_events.size()==event_count+3&&
               test_selection_events.back()=="weapnext","fresh Y can try another safe selection after refusal");
    }
    for(bool during_edge:{false,true}) {
        CycleFixture f;f.state.cod4_controls=true;f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;test_reload_busy=true;test_exact_aim=true;
        f.step(0);test_snapshot.hands[0].secondary.current=true;
        if(during_edge){test_snapshot.hands[1].trigger.current=1;test_sink_effect=grenade_during_edge;}
        else test_grenade_busy=true;
        f.step(0);
        verify(test_cancel_calls==0&&test_retire_calls==0&&
               std::find(test_native_commands.begin(),test_native_commands.end(),"weapnext")==test_native_commands.end(),
               "grenade ownership at routing or after paired native edge prevents cancellation and selection");
        test_grenade_busy=false;test_snapshot.hands[1].trigger.current=0;f.step(0);
        verify(test_cancel_calls==0,"ending grenade ownership cannot replay held Y escape");
    }
    for(unsigned fault=0;fault<5;++fault) {
        CycleFixture f;recursive_fixture=&f;f.state.cod4_controls=true;
        f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;
        test_snapshot.hands[1].squeeze.active=true;test_snapshot.hands[1].squeeze.current=0;
        f.step(0);test_snapshot.hands[1].squeeze.current=1;
        if(fault==1)test_sink_effect=throw_after_effect;
        if(fault==2)test_sink_effect=nested_effect;
        if(fault==3)test_sink_effect=foreign_effect;
        if(fault==4)test_pre_effect=pre_nested_effect;
        bool threw{};try {f.step(0);}catch(const std::runtime_error&){threw=true;}
        const auto expected=fault==4?std::vector<std::string>{}:
            std::vector<std::string>{"+actionslot 3","-actionslot 3"};
        verify(test_native_commands==expected,"alternate command pair stays balanced at every native effect boundary");
        verify(threw==(fault==1),"alternate Down exception remains visible after bounded Up release");
        verify(!f.state.controls.weapon_alternate&&!f.state.controls.weapon_next&&!f.state.cycle_busy,
               "alternate one-shot and owner scope consumed even on failure");
        f.invoke();f.step(0);verify(test_native_commands==expected,"duplicate command and held grip cannot retry alternate");
        test_snapshot.hands[1].squeeze.current=0;f.step(0);
        test_snapshot.hands[1].squeeze.current=1;f.step(0);
        verify(test_native_commands.size()==expected.size()+2&&
               test_native_commands[test_native_commands.size()-2]=="+actionslot 3"&&
               test_native_commands.back()=="-actionslot 3","fresh neutral restores exactly one alternate pair");
        test_snapshot.hands[0].secondary.current=true;f.step(0);
        verify(test_native_commands.back()=="weapnext","Y cycles normally even with right grip still held");
    }
    for(unsigned slot=0;slot<4;++slot) {
        CycleFixture f;f.state.cod4_controls=true;f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;
        test_snapshot.hands[1].squeeze.active=true;test_snapshot.hands[1].squeeze.current=0;
        test_alternate.types={};test_alternate.types[slot]=2;
        f.step(0);test_snapshot.hands[1].squeeze.current=1;f.step(0);
        const auto number=std::to_string(slot+1);
        verify(test_native_commands==std::vector<std::string>{"+actionslot "+number,"-actionslot "+number},
               "each native mission alternate slot uses its actual one-based argument");
        verify(test_alternate_reads==2,"alternate native slot copied twice before dispatch");
        test_snapshot.hands[1].squeeze.current=0;f.step(0);
        // The selector dispatches by native slot type, independent of whether
        // the native current weapon is the rifle or its attached alternate.
        test_alternate.weapon=177;test_alternate.requested=177;
        test_snapshot.hands[1].squeeze.current=1;f.step(0);
        verify(test_native_commands==std::vector<std::string>{"+actionslot "+number,"-actionslot "+number,
               "+actionslot "+number,"-actionslot "+number},
               "a fresh grip can toggle back with a different current native weapon");
    }
    for(unsigned fault=1;fault<=11;++fault) {
        CycleFixture f;f.state.cod4_controls=true;f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;
        test_snapshot.hands[1].squeeze.active=true;test_snapshot.hands[1].squeeze.current=0;
        f.step(0);test_snapshot.hands[1].squeeze.current=1;
        test_alternate_fault=fault<=8?fault:0;
        if(fault==9)test_alternate.types={1,3,0,0};
        if(fault==10)test_alternate.types={2,0,2,0};
        if(fault==11)test_alternate.types={0,-1,2,0};
        f.step(0);
        verify(test_native_commands.empty()&&f.state.cycle_requests==1,
               "unreadable changed stale ambiguous or absent alternate consumes grip without unrelated dispatch");
        test_alternate={{{0,0,2,0}},32,32};test_alternate_reads=test_alternate_fault=0;
        test_pre.native_cg_identity=0x12340000;
        f.step(0);verify(test_native_commands.empty(),"recovered assignment cannot replay held grip");
        test_snapshot.hands[1].squeeze.current=0;f.step(0);
        test_snapshot.hands[1].squeeze.current=1;f.step(0);
        verify(test_native_commands==std::vector<std::string>{"+actionslot 3","-actionslot 3"},
               "new neutral gesture recovers after native assignment rejection");
    }
    const auto support_checks_start=cycle_checks;
    for(unsigned fault=0;fault<5;++fault) {
        CycleFixture f;recursive_fixture=&f;f.state.cod4_controls=true;
        f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;test_support_available=true;test_exact_aim=true;
        f.step(0);test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};
        test_support_reads=0;
        if(fault==1)test_sink_effect=throw_after_effect;
        if(fault==2)test_sink_effect=nested_effect;
        if(fault==3)test_sink_effect=foreign_effect;
        if(fault==4)test_pre_effect=pre_nested_effect;
        bool threw{};try {f.step(0);}catch(const std::runtime_error&){threw=true;}
        const auto expected=fault==4?std::vector<std::string>{}:
            std::vector<std::string>{"+actionslot 4","-actionslot 4"};
        verify(test_native_commands==expected,"support slot four pulse stays balanced through normal, throwing and reentrant delivery");
        verify(threw==(fault==1),"support Down exception remains visible after balanced Up");
        verify(test_cancel_calls==0&&test_retire_calls==(fault==4?0u:1u),
               "support selection retires occurrence aim without cancelling a physical reload");
        verify(!f.state.controls.weapon_support&&!f.state.controls.weapon_alternate&&!f.state.controls.weapon_next&&!f.state.cycle_busy,
               "support request and owned scope are consumed before every native effect boundary");
        if(fault==0) {
            verify(test_support_reads==4,"support native identity is copied twice before routing and twice before dispatch");
            verify(test_selection_events==std::vector<std::string>{"retire","+actionslot 4","-actionslot 4"},
                   "support aim retirement precedes its exact native action-slot command");
        }
        f.invoke();f.step(0);
        verify(test_native_commands==expected,"duplicate command and held support chord never retry selection");
        test_snapshot.hands[0].secondary.current=false;test_snapshot.hands[0].stick.current={};f.step(0);
        test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};f.step(0);
        verify(test_native_commands.size()==expected.size()+2&&test_native_commands.back()=="-actionslot 4",
               "fresh combined neutral recovers one support pulse after delivery boundaries");
    }
    for(unsigned fault=1;fault<=8;++fault) {
        CycleFixture f;f.state.cod4_controls=true;f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;test_support_available=true;test_exact_aim=true;
        f.step(0);test_support_reads=0;test_support_fault=fault;
        test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};f.step(0);
        verify(test_native_commands.empty()&&test_retire_calls==0&&test_cancel_calls==0,
               "unreadable, changed or stale support identity cannot retire aim or dispatch selection");
        test_support_fault=0;test_pre.native_cg_identity=test_support.cg;test_pre.gameplay_epoch=1;
        f.step(0);verify(test_native_commands.empty(),"recovered support facts cannot replay a held rejected chord");
        test_snapshot.hands[0].secondary.current=false;test_snapshot.hands[0].stick.current={};f.step(0);
        test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};f.step(0);
        verify(test_native_commands==std::vector<std::string>{"+actionslot 4","-actionslot 4"},
               "new support gesture recovers after source or publication failure");
    }
    for(unsigned fault=0;fault<6;++fault) {
        CycleFixture f;recursive_fixture=&f;f.state.cod4_controls=true;
        f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;test_support_available=true;test_exact_aim=true;
        f.step(0);test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};
        test_snapshot.hands[1].trigger.current=1.f;
        test_sink_effect=fault==0?support_assignment_during_edge:fault==1?grenade_during_edge:
            fault==2?reload_during_edge:fault==3?nested_effect:fault==4?foreign_effect:throw_after_effect;
        bool threw{};try {f.step(0);}catch(const std::runtime_error&){threw=true;}
        verify(threw==(fault==5),"earlier paired-edge exception remains visible for support selection");
        verify(test_native_commands.size()==1&&test_native_commands[0].find("+attack 28672 ")==0&&
               test_retire_calls==0&&test_cancel_calls==0,
               "changed assignment, ownership or occurrence after paired edge refuses reserved support dispatch");
        verify(!f.state.controls.weapon_support&&!f.state.cycle_busy,
               "support reservation is consumed before a paired edge changes ownership or throws");
        test_reload_busy=test_grenade_busy=false;test_support.descriptor.weapon=24;
        f.step(0);verify(test_native_commands.size()==1,"lost support reservation cannot replay after paired-edge recovery");
        test_snapshot.hands[1].trigger.current=0.f;
        test_snapshot.hands[0].secondary.current=false;test_snapshot.hands[0].stick.current={};f.step(0);
        test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};f.step(0);
        verify(test_native_commands.size()==4&&test_native_commands[1].find("-attack 28672 ")==0&&
               test_native_commands[2]=="+actionslot 4"&&test_native_commands[3]=="-actionslot 4",
               "fresh neutral releases paired attack and restores one safe support selection");
    }
    {
        CycleFixture f;f.state.cod4_controls=true;f.state.router=bovr::input::ActionRouter(false,true);
        test_snapshot.hands[0].secondary.active=true;test_support_available=true;
        f.step(0);test_snapshot.hands[0].secondary.current=true;f.step(0);
        verify(test_native_commands.empty(),"available support keeps plain Y pending before any native weapon change");
        test_snapshot.hands[0].secondary.current=false;f.step(0);
        verify(test_native_commands==std::vector<std::string>{"weapnext"},"plain Y release still delivers exactly one native weapon cycle");
        f.step(0);test_snapshot.hands[0].secondary.current=true;test_snapshot.hands[0].stick.current={-1.f,0.f};
        test_grenade_busy=true;f.step(0);
        verify(test_native_commands==std::vector<std::string>{"weapnext"},"grenade ownership blocks support at initial routing");
        test_grenade_busy=false;f.step(0);
        verify(test_native_commands==std::vector<std::string>{"weapnext"},"ending grenade ownership cannot resume held support chord");
    }
    printf("support_weapon_adapter_checks=%u\n",cycle_checks-support_checks_start);
    for(unsigned combination=0;combination<256;++combination) {
        bovr::t5::AlternateSlots slots{};unsigned value=combination,count{},expected{};
        for(unsigned slot=0;slot<4;++slot) {
            slots.types[slot]=int(value%4);value/=4;
            if(slots.types[slot]==2){++count;expected=slot+1;}
        }
        verify(bovr::t5::unique_alternate_slot(slots)==(count==1?expected:0),
               "all action-slot assignments select only a unique alternate type");
    }
    {
        using namespace bovr::t5;
        constexpr std::size_t image_size=0x2bf6000,cg_size=0xaa000;
        auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE,PAGE_NOACCESS));
        auto* cg=static_cast<unsigned char*>(VirtualAlloc(nullptr,cg_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        const bool allocated=image&&cg&&VirtualAlloc(image+0x2bf5000,0x1000,MEM_COMMIT,PAGE_READWRITE);
        verify(allocated,"private native-slot boundary memory allocated");
        if(allocated) {
            auto* global=reinterpret_cast<std::uintptr_t*>(image+0x2bf5354);
            *global=reinterpret_cast<std::uintptr_t>(cg);
            PreCommandSnapshot pre{};pre.native_cg_identity=*global;pre.native_game_time=1234;
            *reinterpret_cast<int*>(cg+0x8a348)=pre.native_game_time;
            cg[0x8a364+0x144]=32;*reinterpret_cast<int*>(cg+0xa9be8)=32;
            const std::array<int,4> types{0,0,2,0};memcpy(cg+0x8a364+0x500,types.data(),sizeof(types));
            AlternateSlots out{};const auto base=reinterpret_cast<std::uintptr_t>(image);
            verify(copy_native_alternate(base,pre,&out)&&out.types==types&&out.weapon==32&&out.requested==32,
                   "protected retail-layout slot reader copies exact current native assignment");
            for(unsigned fault=0;fault<5;++fault) {
                const auto saved=out;auto bad=pre;
                if(fault==0)++bad.native_cg_identity;
                if(fault==1)++bad.native_game_time;
                if(fault==2)*reinterpret_cast<int*>(cg+0xa9be8)=33;
                if(fault==3)cg[0x8a364+0x144]=0;
                if(fault==4){*global=reinterpret_cast<std::uintptr_t>(image);bad.native_cg_identity=*global;} // reserved, unreadable cg
                verify(!copy_native_alternate(base,bad,&out)&&out.types==saved.types&&out.weapon==saved.weapon,
                       "native identity time selection and protected-read faults leave copied output untouched");
                *global=reinterpret_cast<std::uintptr_t>(cg);cg[0x8a364+0x144]=32;
                *reinterpret_cast<int*>(cg+0xa9be8)=32;
            }
        }
        if(cg)VirtualFree(cg,0,MEM_RELEASE);if(image)VirtualFree(image,0,MEM_RELEASE);
    }
    {
        // Default-off actual callback never reads the new copied gate or emits a
        // cycle, even with a valid neutral-to-up sequence and provider enabled.
        CycleFixture f;bovr::t5::Adapter disabled(0,false);disabled.execute=capture_native_command;
        f.prepare(0);bovr::t5::before_native(&disabled,f.context);
        f.prepare(1);bovr::t5::before_native(&disabled,f.context);
        verify(test_pre_reads==0 && test_native_commands.empty(),"default-off has no new source/native calls");
    }
    {
        using namespace bovr::t5;
        const auto offset=mounted_delivery::adjust_angles_guard.address-0x400000;
        std::vector<unsigned char> image(offset+mounted_delivery::adjust_angles_guard.size,0);
        verify(mounted_delivery::adjust_angles_loaded_guard(image.data()),
               "authored CL_AdjustAngles guard fixture admits native yaw delivery");
        for(std::size_t n=0;n<mounted_delivery::adjust_angles_guard.size;++n) {
            auto& byte=image[offset+n];byte^=1;
            verify(!mounted_delivery::adjust_angles_loaded_guard(image.data()),
                   "every CL_AdjustAngles consumer byte is required before native yaw writes");
            byte^=1;
        }
        verify(!mounted_delivery::adjust_angles_loaded_guard(nullptr),
               "null CL_AdjustAngles image refuses native yaw delivery");
        auto* inaccessible=static_cast<unsigned char*>(VirtualAlloc(nullptr,image.size(),MEM_RESERVE,PAGE_NOACCESS));
        verify(inaccessible!=nullptr,"private unreadable CL_AdjustAngles reservation");
        if(inaccessible){verify(!mounted_delivery::adjust_angles_loaded_guard(inaccessible),
            "protected CL_AdjustAngles read fault refuses native yaw delivery");VirtualFree(inaccessible,0,MEM_RELEASE);}
    }
    {
        using namespace bovr::t5;
        std::vector<unsigned char> image(execute_rva+cycle_guard_spans[0].size,0);
        verify(cycle_guards_match(image.data()),"owned image admits authored mapped guards");
        for(const auto& guard:cycle_guard_spans) {
            for(std::size_t n=0;n<guard.size;++n) {
                auto& byte=image[guard.address+n];byte^=1;
                verify(!cycle_guards_match(image.data()),"each guard byte mismatch refuses");byte^=1;
            }
        }
        verify(!cycle_guards_match(nullptr),"null image guard refuses");
        auto* inaccessible=static_cast<unsigned char*>(VirtualAlloc(nullptr,image.size(),MEM_RESERVE,PAGE_NOACCESS));
        verify(inaccessible!=nullptr,"private unreadable guard reservation");
        if(inaccessible) {verify(!cycle_guards_match(inaccessible),"protected guard read fault refuses");VirtualFree(inaccessible,0,MEM_RELEASE);}
    }
    {
        using namespace bovr::t5;
        const auto& last=alternate_guard_spans.back();std::vector<unsigned char> image(last.address+last.size,0);
        verify(alternate_guards_match(image.data()),"alternate authored registration and handler fixtures accepted");
        for(const auto& guard:alternate_guard_spans) {
            for(std::size_t n=0;n<guard.size;++n) {
                auto& byte=image[guard.address+n];byte^=1;
                verify(!alternate_guards_match(image.data()),"every alternate handler and native executor byte is required");byte^=1;
            }
        }
        for(const auto& guard:cycle_guard_spans)for(std::size_t n=0;n<guard.size;++n){
            auto& byte=image[guard.address+n];byte^=1;
            verify(!alternate_guards_match(image.data()),"every shared cycle guard byte is required by alternate delivery");byte^=1;}
        verify(!alternate_guards_match(nullptr),"alternate null image refuses");
        auto* inaccessible=static_cast<unsigned char*>(VirtualAlloc(nullptr,image.size(),MEM_RESERVE,PAGE_NOACCESS));
        verify(inaccessible!=nullptr,"alternate inaccessible reservation");
        if(inaccessible){verify(!alternate_guards_match(inaccessible),"alternate inaccessible image refuses");VirtualFree(inaccessible,0,MEM_RELEASE);}
    }
    printf("weapon_cycle_adapter_checks=%u copied_sources_only=1\n",cycle_checks);
    printf("weapon_cycle_rejected_boundary_checks=%u failures=%u\n",boundary_checks,boundary_failures);
    printf("real_native_command_dispatches=0 game_processes=0 failures=%u\n",failures);
    return failures?1:0;
}
