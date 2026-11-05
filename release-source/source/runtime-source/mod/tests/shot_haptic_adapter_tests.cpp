// SPDX-License-Identifier: GPL-3.0-only
// White-box instantiation of the same adapter gate/dispatch with a fake runtime.
// No game mapping, native callback installation or OpenXR calls are performed.
#define configure_shot_haptic_adapter configure_shot_haptic_adapter_test_copy
#define service_shot_haptics service_shot_haptics_test_copy
#define reset_shot_haptics reset_shot_haptics_test_copy
#define configure_predicted_shot_feedback configure_predicted_shot_feedback_test_copy
#define observe_predicted_shot_feedback observe_predicted_shot_feedback_test_copy
#define predicted_shot_kickback predicted_shot_kickback_test_copy
#include "t5/shot_haptic_adapter.cpp"
#undef configure_shot_haptic_adapter
#undef service_shot_haptics
#undef reset_shot_haptics
#undef configure_predicted_shot_feedback
#undef observe_predicted_shot_feedback
#undef predicted_shot_kickback
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <psapi.h>
// Keep this white-box fixture independent of the live renderer/runtime graph.
// The exercised protected reader and owner policy above are the product code;
// unrelated public installation/snapshot sinks refuse rather than attach.
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept {return false;}
bool read_vr_input_snapshot(wawvr::xr::ActionSnapshot*,std::uint64_t*) noexcept {return false;}
bool read_vr_command_input_snapshot(wawvr::xr::ActionSnapshot*,std::uint64_t*,input::CommandRigSample*) noexcept {return false;}
bool command_input_generation_current(const input::CommandRigSample&) noexcept {return false;}
bool set_local_shot_callbacks(const LocalShotCallbacks&) noexcept {return false;}
Validation validate_path(const std::wstring&) {return {};}
std::wstring executable_path() {return {};}
}
namespace wawvr::xr {
bool OpenXrRuntime::session_focused() const {return false;}
bool OpenXrRuntime::ApplyHaptic(Hand,float,float,float) {return false;}
bool OpenXrRuntime::StopHaptic(Hand) {return false;}
}
using namespace bovr;
namespace {
unsigned checks{};
void check(bool value,const char* why) {++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
struct Runtime {
    bool focused{true},success{true};unsigned focus_reads{},applies{},stops{},last_hand{9};
    input::ShotHaptics* probe_lock{};
    bool session_focused(){++focus_reads;return focused;}
    bool ApplyHaptic(wawvr::xr::Hand hand,float amplitude,float duration) {
        ++applies;last_hand=static_cast<unsigned>(hand);
        check(amplitude>0&&amplitude<=1&&duration>0&&duration<=0.1F,"bounded pulse parameters");
        // A runtime sink can re-enter a metadata-only method: service must not
        // keep the mailbox mutex locked across external runtime calls.
        if(probe_lock) check(!probe_lock->queue({},1000),"runtime dispatch holds no mailbox lock");
        return success;
    }
    bool StopHaptic(wawvr::xr::Hand hand){++stops;last_hand=static_cast<unsigned>(hand);return success;}
};
vr::FrameTicket ticket() {
    vr::FrameTicket t{};t.key={2,7};t.published_ms=1000;t.frame.frame_id=7;
    t.frame.should_render=t.frame.views_valid=t.frame.actions.focused=true;t.frame.actions.sequence=9;
    for(auto& hand:t.frame.actions.hands) {
        auto& g=hand.grip;g.active=g.position_valid=g.orientation_valid=g.position_tracked=g.orientation_tracked=true;
    }
    return t;
}
t5::PreCommandSnapshot native() {
    t5::PreCommandSnapshot p{};p.gameplay_epoch=3;p.gameplay_active=true;p.sequence=5;p.observed_ms=1000;
    p.context.valid=true;p.context.local_client=0;p.context.native_thread=GetCurrentThreadId();p.context.connection_state=10;
    return p;
}
t5::NativeLocalShot shot(std::uint64_t serial=1) {
    t5::NativeLocalShot s{};s.serial=serial;s.observed_ms=1001;s.native_thread=GetCurrentThreadId();s.local_shot_confirmed=true;return s;
}
void linked_predicted_reader() {
    // Owned x86 fixture pages at the pinned read-only global slots. No game
    // process or executable-code mapping is involved.
    constexpr std::uintptr_t pages[]{0x2510000,0x2ff0000,0x1a70000,0x1c00000,0x2910000,0xbe0000};
    for(auto address:pages) {
        const auto allocation=VirtualAlloc(reinterpret_cast<void*>(address),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(allocation!=reinterpret_cast<void*>(address)) {
            const auto error=GetLastError();MEMORY_BASIC_INFORMATION info{};
            VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info));
            std::printf("feedback allocation address=%p error=%lu existing_base=%p size=%zu state=%lu type=%lu\n",
                reinterpret_cast<void*>(address),error,info.AllocationBase,info.RegionSize,info.State,info.Type);
            wchar_t mapping_path[1024]{};
            const auto mapping_length=K32GetMappedFileNameW(GetCurrentProcess(),info.AllocationBase,mapping_path,1024);
            std::printf("feedback mapping name_length=%lu exe=%p region=%p name=%ls\n",mapping_length,
                GetModuleHandleW(nullptr),info.BaseAddress,mapping_path);
        }
        check(allocation==reinterpret_cast<void*>(address),"owned feedback fixture global page");
    }
    const auto put=[]<class T>(std::uintptr_t address,T value){memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));};
    std::vector<unsigned char> cg(0xd0000),client(0x2000),variant(0x60),definition(0x600);
    const auto c=reinterpret_cast<std::uintptr_t>(cg.data()),s=reinterpret_cast<std::uintptr_t>(client.data());
    const auto ps=c+0x8a364,entity=std::uintptr_t{0x1a796f8},var=reinterpret_cast<std::uintptr_t>(variant.data()),def=reinterpret_cast<std::uintptr_t>(definition.data());
    auto p=native();p.gameplay_active=false;p.linked_weapon_active=true;p.native_gameplay_fields_valid=true;p.native_cg_identity=c;
    p.native_pm_type=1;p.native_health=100;p.native_pm_flags=0x1800000;p.native_weapon_flags=8;p.native_entity_flags=0x80002;p.native_game_time=500;
    put(0x251ca98,GetCurrentThreadId());put(0x2ff5354,c);put(0x1c03140,s);put(entity+0x13c,s);put(entity+0x184,100);
    put(entity+0x298,std::uint32_t{0x306ed00});put(entity+0xd4,std::uint8_t{29});put(0x2910164,10);
    put(0xbe19a8+4*29,var);put(var+8,def);put(c+0x8a348,p.native_game_time);
    for(auto player:{ps,s}) {
        put(player+4,1);put(player+0x1c4,100);put(player+0xc,p.native_pm_flags);put(player+0x10,p.native_weapon_flags);
        put(player+0xe0,p.native_entity_flags);put(player+0x144,std::uint8_t{29});
    }
    put(s+0x1a2c,2);put(ps+0xe8,std::uint16_t{2});put(ps+0xec,29);put(ps+0xf0,30);
    input::PredictedFireSample copied{};std::uint32_t link{};
    check(t5::copy_predicted_ring(p,&copied,&link)&&copied.weapon==29&&copied.sequence==2&&copied.events[0]==29&&link==0x306ed00,
          "actual protected predicted ring admits current linked enabled Cuba pistol with paired server identity");
    for(std::uint32_t count:{0u,1u,2u,4u,7u,8u,0x80000000u,0xffffffffu}) {put(ps+0x1b0,count);
        check(t5::copy_predicted_ring(p,&copied,&link)==(count<=7),"actual predicted feedback reader admits valid native counts and rejects out-of-range values");}
    put(ps+0x1b0,std::uint32_t{0});
    p.native_weapon_flags=0x100;put(ps+0x10,p.native_weapon_flags);put(s+0x10,p.native_weapon_flags);
    check(t5::copy_predicted_ring(p,&copied,&link),"predicted primary recoil permits offhand-only disable in linked mode");
    for(unsigned fault=0;fault<10;++fault) {
        if(fault==0)put(ps+0xc,p.native_pm_flags|0x800);
        if(fault==1)put(s+0xc,p.native_pm_flags|0x400);
        if(fault==2)put(ps+0x10,std::uint32_t{0x80});
        if(fault==3)put(s+4,0);
        if(fault==4)put(entity+0x298,std::uint32_t{0});
        if(fault==5)put(s+0x144,std::uint8_t{30});
        if(fault==6)put(0x2911e2c,1);
        if(fault==7)put(c+0x8a360,1);
        if(fault==8)put(s+0x1c0c,std::uint32_t{4});
        if(fault==9)put(c+0x8a348,p.native_game_time+1);
        check(!t5::copy_predicted_ring(p,&copied,&link)&&link==0,"linked predicted reader rejects frozen suppressed changed unowned or overridden native context");
        put(ps+0xc,p.native_pm_flags);put(s+0xc,p.native_pm_flags);put(ps+0x10,p.native_weapon_flags);put(s+4,1);
        put(entity+0x298,std::uint32_t{0x306ed00});put(s+0x144,std::uint8_t{29});put(0x2911e2c,0);put(c+0x8a360,0);
        put(s+0x1c0c,std::uint32_t{0});put(c+0x8a348,p.native_game_time);
    }
    for(auto address:pages)check(VirtualFree(reinterpret_cast<void*>(address),0,MEM_RELEASE)!=FALSE,"owned feedback fixture page released");
}
}
int main() {
    using namespace t5;
    linked_predicted_reader();
    {
        HapticAdapter a;Runtime runtime;auto t=ticket();auto p=native();p.gameplay_active=false;p.linked_weapon_active=true;
        service_on_owner(a,runtime,&t,&p,1000);
        check(queue_shot(a,shot(),p,t.frame.actions,2,true,1001),"fresh linked weapon scope queues native-confirmed shot feedback without enabling ordinary gameplay");
        service_on_owner(a,runtime,&t,&p,1002);
        check(runtime.applies==1&&!p.gameplay_active,"linked primary feedback drains on XR owner while locomotion gate stays false");
    }
    {
        HapticAdapter a;Runtime runtime;auto t=ticket();auto p=native();auto s=shot();runtime.probe_lock=&a.policy;
        service_on_owner(a,runtime,&t,&p,1000);
        check(runtime.applies==0,"frame publication alone never emits a firing pulse");
        check(queue_shot(a,s,p,t.frame.actions,t.key.generation,true,1001),"confirmed current local shot queues");
        check(runtime.applies==0,"native shot producer makes no runtime calls");
        service_on_owner(a,runtime,&t,&p,1002);
        check(runtime.applies==1&&runtime.last_hand==1&&a.applied==1,"owner applies one right-hand pulse");
        check(!queue_shot(a,s,p,t.frame.actions,t.key.generation,true,1002),"same native shot serial cannot repeat");
        service_on_owner(a,runtime,&t,&p,1003);check(runtime.applies==1,"servicing same ticket does not replay pulse");
        const auto reads=runtime.focus_reads;
        std::thread foreign([&]{service_on_owner(a,runtime,&t,&p,1004);reset_on_owner(a,runtime);});foreign.join();
        check(runtime.focus_reads==reads&&runtime.stops==0,"foreign owner performs no runtime reads or reset dispatch");
        reset_on_owner(a,runtime);check(runtime.stops==1&&runtime.last_hand==1,"owner reset stops outstanding hand before retirement");
        reset_on_owner(a,runtime);check(runtime.stops==1,"reset retry does not repeat acknowledged pulse stop");
        check(!queue_shot(a,shot(2),p,t.frame.actions,t.key.generation,true,1004),"reset clears producer acceptance until fresh owner service");
    }
    for(unsigned variant=0;variant<15;++variant) {
        HapticAdapter a;Runtime runtime;auto t=ticket();auto p=native();auto s=shot();bool has_actions=true;std::uint64_t generation=2;
        service_on_owner(a,runtime,&t,&p,1000);
        switch(variant) {
        case 0:s.local_shot_confirmed=false;break;case 1:s.native_thread=0;break;case 2:p.gameplay_active=false;break;
        case 3:p.gameplay_epoch=4;break;case 4:p.context.native_override=1;break;case 5:p.context.key_catcher=0x10;break;
        case 6:p.context.valid=false;break;case 7:p.context.local_client=1;break;case 8:p.observed_ms=1;break;
        case 9:t.frame.actions.focused=false;break;case 10:t.frame.actions.hands[1].grip.position_tracked=false;break;
        case 11:t.frame.actions.hands[1].grip.orientation_valid=false;break;case 12:has_actions=false;break;
        case 13:generation=3;break;case 14:s.observed_ms=999;break;
        }
        check(!queue_shot(a,s,p,t.frame.actions,generation,has_actions,1001),"invalid shot/context/focus/tracking/epoch never queues");
        service_on_owner(a,runtime,&t,&p,1002);check(runtime.applies==0,"rejected event never reaches runtime");
    }
    {
        HapticAdapter a;Runtime runtime;auto t=ticket();auto p=native();auto s=shot();
        service_on_owner(a,runtime,&t,&p,1000);
        bool queued=false;
        std::thread server([&]{s.native_thread=GetCurrentThreadId();queued=queue_shot(a,s,p,t.frame.actions,2,true,1001);});
        server.join();
        check(s.native_thread!=p.context.native_thread&&queued,"confirmed local server shot can cross the input-thread mailbox");
        check(runtime.applies==0,"server-thread shot queues values without calling runtime");
        service_on_owner(a,runtime,&t,&p,1002);
        check(runtime.applies==1&&runtime.last_hand==1,"XR owner drains the server-thread event once");
        service_on_owner(a,runtime,&t,&p,1003);
        check(runtime.applies==1,"different server thread does not permit duplicate dispatch");
    }
    for(unsigned variant=0;variant<8;++variant) {
        HapticAdapter a;Runtime runtime;auto t=ticket();auto p=native();
        service_on_owner(a,runtime,&t,&p,1000);check(queue_shot(a,shot(),p,t.frame.actions,2,true,1001),"pulse fixture queues");
        service_on_owner(a,runtime,&t,&p,1002);check(runtime.applies==1,"pulse fixture drains");
        const vr::FrameTicket* supplied=&t;const PreCommandSnapshot* native_supplied=&p;std::uint64_t now=1003;
        switch(variant) {
        case 0:t.frame.actions.focused=false;break;case 1:runtime.focused=false;break;
        case 2:t.frame.actions.hands[1].grip.position_tracked=false;break;
        case 3:p.gameplay_epoch=4;break;case 4:t.key.generation=3;break;
        case 5:supplied=nullptr;break;case 6:native_supplied=nullptr;break;
        case 7:now=1200;p.observed_ms=1200;break;
        }
        service_on_owner(a,runtime,supplied,native_supplied,now);
        check(runtime.stops==1&&runtime.applies==1,"focus/tracking/map/generation/no-ticket/stale publication stops old pulse");
        check(t.published_ms==1000,"servicing never refreshes original XR publication time");
    }
    {
        HapticAdapter a;Runtime runtime;runtime.success=false;auto t=ticket();auto p=native();
        service_on_owner(a,runtime,&t,&p,1000);queue_shot(a,shot(),p,t.frame.actions,2,true,1001);
        service_on_owner(a,runtime,&t,&p,1002);service_on_owner(a,runtime,&t,&p,1003);
        check(runtime.applies==1&&a.failed==1&&a.applied==0,"failed runtime pulse is counted once and never requeued");
    }
    SetEnvironmentVariableW(L"BOVR_SHOT_HAPTICS",L"0");
    check(!configure_shot_haptic_adapter_test_copy(true,true),"adapter remains explicitly default disabled");
    SetEnvironmentVariableW(L"BOVR_SHOT_HAPTICS",L"1");
    check(!configure_shot_haptic_adapter_test_copy(false,true)&&!configure_shot_haptic_adapter_test_copy(true,false),
          "both native observer proofs are required before adapter configuration");
    SetEnvironmentVariableW(L"BOVR_SHOT_HAPTICS",L"0");
    {
        HapticAdapter a;a.predicted_source=true;Runtime runtime;auto t=ticket();auto p=native();
        input::PredictedFireSample source{};source.valid=true;source.generation=2;source.epoch=3;
        source.player=4;source.weapon=32;source.occurrence=1;source.observed_ms=source.published_ms=1000;
        source.native_time=1000;a.feedback.observe(source,1000);
        service_on_owner(a,runtime,&t,&p,1000);
        source.events[0]=29;source.sequence=1;source.occurrence=2;source.native_time=1001;
        source.observed_ms=source.published_ms=1001;
        check(a.feedback.observe(source,1001).shots==1,"predicted source observes current native fire");
        check(a.policy.queue({2,3,1,1001,1,true},1001),"predicted event uses existing bounded mailbox");
        service_on_owner(a,runtime,&t,&p,1002);
        check(runtime.applies==1&&a.feedback.kickback(2,3,32,1002)>0,"owner pulses and current gun kick remains available");
        runtime.focused=false;service_on_owner(a,runtime,&t,&p,1003);
        check(runtime.stops==1&&a.feedback.kickback(2,3,32,1003)==0,"XR focus loss clears recoil and stops pulse");
        runtime.focused=true;service_on_owner(a,runtime,&t,&p,1004);
        check(runtime.applies==1&&!a.feedback.current(2,3,1004),"XR recovery alone cannot restore stale predicted events");
        source.occurrence=3;source.observed_ms=source.published_ms=1005;
        check(a.feedback.observe(source,1005).shots==0,"native recovery seeds without replay");
        service_on_owner(a,runtime,&t,&p,1005);
        source.events[1]=30;source.sequence=2;source.occurrence=4;source.observed_ms=source.published_ms=1006;
        check(a.feedback.observe(source,1006).shots==1&&a.policy.queue({2,3,2,1006,1,true},1006),"new postfocus last-round event queues");
        service_on_owner(a,runtime,&t,&p,1007);
        check(runtime.applies==2,"owner resumes one pulse for new predicted event");
        clear_predicted_locked(a);
        check(runtime.stops==1,"producer retirement calls no runtime method");
        service_on_owner(a,runtime,&t,&p,1008);
        check(runtime.stops==2,"owner delivers producer requested stop once");
        service_on_owner(a,runtime,&t,&p,1009);
        check(runtime.stops==2,"producer stop cannot replay on next frame");
    }
    SetEnvironmentVariableW(L"BOVR_PREDICTED_SHOT_FEEDBACK",L"0");
    check(!configure_predicted_shot_feedback_test_copy(true),"predicted feedback stays default disabled");
    SetEnvironmentVariableW(L"BOVR_PREDICTED_SHOT_FEEDBACK",L"1");
    check(!configure_predicted_shot_feedback_test_copy(false),"predicted feedback needs existing command observer");
    SetEnvironmentVariableW(L"BOVR_SHOT_HAPTICS",L"1");
    check(!configure_predicted_shot_feedback_test_copy(true),"predicted and legacy sources cannot both configure");
    SetEnvironmentVariableW(L"BOVR_SHOT_HAPTICS",L"0");
    SetEnvironmentVariableW(L"BOVR_PREDICTED_SHOT_FEEDBACK",L"0");
    std::printf("shot_haptic_adapter_checks=%u failures=0 native_calls=0 xr_calls=0\n",checks);
}
