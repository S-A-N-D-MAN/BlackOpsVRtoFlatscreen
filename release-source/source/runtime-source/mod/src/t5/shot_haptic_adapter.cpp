// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/native_stack_fire_count.hpp"
#include "t5/shot_haptic_adapter.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/command_probe.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "input/shot_haptics.hpp"
#include "input/predicted_shot_feedback.hpp"
#include "t5/identity.hpp"
#include "vr/frame_coordinator.hpp"
#include "loader/trace.hpp"
#include "openxr_runtime.h"
#include <atomic>
#include <cstring>
#include <mutex>

namespace bovr::t5 {
namespace {
namespace xr=wawvr::xr;
struct HapticAdapter {
    input::ShotHaptics policy;
    std::atomic<DWORD> owner{};
    std::atomic<std::uint64_t> confirmed{},queued{};
    std::uint64_t applied{},failed{};
    std::uint64_t diagnostic_queued{};
    unsigned diagnostic_steps{};
    unsigned hand{1};
    bool predicted_source{};
    std::mutex feedback_lock;
    input::PredictedShotFeedback feedback;
    unsigned pending_stops{};
    std::uint64_t predicted_serial{},predicted_shots{};
    std::uint32_t feedback_link{};
};
std::atomic<HapticAdapter*> adapter{};
bool enabled(const wchar_t* name) noexcept {
    wchar_t value[8]{};GetEnvironmentVariableW(name,value,8);return !wcscmp(value,L"1");
}
bool fresh_native(const PreCommandSnapshot& p,std::uint64_t now) noexcept {
    return p.gameplay_epoch&&primary_weapon_active(p)&&p.context.valid&&p.context.local_client==0&&
        p.context.native_thread&&p.context.connection_state>=9&&!p.context.key_catcher&&!p.context.native_override&&
        p.sequence&&p.observed_ms&&now>=p.observed_ms&&now-p.observed_ms<=100;
}
bool tracked(const xr::TrackedPose& p) noexcept {
    return p.active&&p.position_valid&&p.orientation_valid&&p.position_tracked&&p.orientation_tracked;
}
void clear_predicted_locked(HapticAdapter& a) {
    a.feedback.invalidate();a.pending_stops|=a.policy.reset().stop_mask;
}
bool predicted_code_guards() noexcept {
    // Retail BG_AddPredictableEventToPlayerstate: owned four-entry ring, event
    // and parameter fields, and explicit eight-bit sequence wrap. No hooks.

    __try {return !bovr::native_guard::compare(reinterpret_cast<const void*>(0x61f4bc), 53, "028fb64619cce09e3946195a6cfe0d4b6afe2ea79b8b563e58df51e65b1a24e3");}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>T feedback_load(std::uintptr_t address) noexcept {
    T value{};std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return value;
}
bool feedback_native_owner() noexcept {
    __try {return feedback_load<DWORD>(0x251ca98)==GetCurrentThreadId();}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct LinkedFeedbackScope {
    primary_weapon_tracking::Player player{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server server{};
    std::uintptr_t client{};
    friend bool operator==(const LinkedFeedbackScope&,const LinkedFeedbackScope&)=default;
};
primary_weapon_tracking::Player feedback_player(std::uintptr_t ps) noexcept {
    return {feedback_load<int>(ps+4),feedback_load<int>(ps+0x1c4),feedback_load<std::uint32_t>(ps+0xc),
        feedback_load<std::uint32_t>(ps+0x10),feedback_load<std::uint32_t>(ps+0xe0),feedback_load<std::uint32_t>(ps+0xe4)};
}
bool linked_feedback_scope(const PreCommandSnapshot& p,unsigned weapon,LinkedFeedbackScope* out) noexcept {
    const auto cg=p.native_cg_identity,entity=std::uintptr_t{0x1a796f8};LinkedFeedbackScope f{};
    f.player=feedback_player(cg+0x8a364);
    const primary_weapon_tracking::Player expected{p.native_pm_type,p.native_health,p.native_pm_flags,p.native_weapon_flags,p.native_entity_flags,p.native_entity_flags2};
    f.view={feedback_load<int>(cg+0x8a360),feedback_load<int>(0x2910160),feedback_load<int>(0x2910164),
        feedback_load<int>(0x2911e2c)!=0,feedback_load<int>(cg+0xc)!=0,feedback_load<int>(cg+0xa9d44)!=0};
    f.client=feedback_load<std::uintptr_t>(entity+0x13c);
    if(!f.client||f.client!=feedback_load<std::uintptr_t>(0x1c03140)||feedback_load<int>(entity)||
       feedback_load<unsigned char>(f.client+0x130)||feedback_load<unsigned char>(f.client+0x144)!=weapon||
       feedback_load<unsigned char>(entity+0xd4)!=weapon||f.player!=expected||feedback_load<int>(cg+0x8a348)!=p.native_game_time)return false;
    f.server={feedback_player(f.client),true,feedback_load<int>(entity+0x184),feedback_load<int>(f.client+0x1a2c),
        feedback_load<int>(f.client+0x1a18),feedback_load<std::uint32_t>(f.client+0x1c0c),feedback_load<std::uint32_t>(entity+0x298)};
    if(primary_weapon_tracking::paired_mode(primary_weapon_tracking::client_mode(f.player,f.view),
       primary_weapon_tracking::server_mode(f.server))!=primary_weapon_tracking::Mode::linked)return false;
    *out=f;return true;
}
bool copy_predicted_ring(const PreCommandSnapshot& p,input::PredictedFireSample* out,std::uint32_t* link=nullptr) noexcept {
    if(link)*link=0;
    if(!out||!p.native_gameplay_fields_valid||p.native_cg_identity<0x10000||
       p.native_cg_identity>UINT32_MAX-0xa9d48)return false;
    __try {
        if(feedback_load<DWORD>(0x251ca98)!=GetCurrentThreadId()||
           feedback_load<std::uintptr_t>(0x2ff5354)!=p.native_cg_identity)return false;
        const auto ps=p.native_cg_identity+0x8a364;
        const unsigned weapon=feedback_load<unsigned char>(ps+0x144);
        const bool linked=p.linked_weapon_active&&!p.gameplay_active;
        if(!weapon||feedback_load<unsigned char>(ps+0x130)||(!linked&&feedback_load<int>(ps+4))||
           feedback_load<int>(ps+0x1c4)<=0||feedback_load<unsigned char>(ps+0x61)||
           (feedback_load<std::uint32_t>(ps+0xc)&(linked?0xc00:0x800))||(feedback_load<std::uint32_t>(ps+0x10)&(linked?0x80:0x180))||
           (feedback_load<std::uint32_t>(ps+0xe0)&0x4300)||(feedback_load<std::uint32_t>(ps+0xe4)&0x50040000))return false;
        LinkedFeedbackScope first{},second{};
        if(linked&&!linked_feedback_scope(p,weapon,&first))return false;
        const auto variant=feedback_load<std::uintptr_t>(0xbe19a8+4*weapon);
        if(variant<0x10000||variant>UINT32_MAX-0x50)return false;
        const auto definition=feedback_load<std::uintptr_t>(variant+8);
        if(definition<0x10000||definition>UINT32_MAX-0x570)return false;
        const auto type=feedback_load<int>(definition+0x1c);
        if(type!=0&&type!=2&&type!=4)return false;
        const bool dual=feedback_load<unsigned char>(definition+0x56a)!=0;
        const int left_state=feedback_load<int>(ps+0x15c);
        if(dual&&(left_state<0||left_state>=18))return false;
        const int state=feedback_load<int>(ps+0x158);
        const auto stack_count=feedback_load<std::uint32_t>(ps+0x1b0);
        if(state<0||state>=18||!native_valid_stack_fire_count(stack_count))return false;
        const auto sequence=feedback_load<std::uint16_t>(ps+0xe8);if(sequence>255)return false;
        std::array<int,4> events{};std::memcpy(events.data(),reinterpret_cast<const void*>(ps+0xec),sizeof(events));
        if(stack_count!=feedback_load<std::uint32_t>(ps+0x1b0)||sequence!=feedback_load<std::uint16_t>(ps+0xe8)||weapon!=feedback_load<unsigned char>(ps+0x144)||
           feedback_load<std::uintptr_t>(0x2ff5354)!=p.native_cg_identity)return false;
        if(linked&&(!linked_feedback_scope(p,weapon,&second)||first!=second||
           feedback_load<std::uintptr_t>(0xbe19a8+4*weapon)!=variant||feedback_load<std::uintptr_t>(variant+8)!=definition||
           feedback_load<int>(definition+0x1c)!=type))return false;
        out->player=p.native_cg_identity;out->weapon=weapon;out->sequence=sequence;out->events=events;out->dual=dual;
        if(link&&linked)*link=first.server.link;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool queue_shot(HapticAdapter& a,const NativeLocalShot& shot,const PreCommandSnapshot& p,
                const xr::ActionSnapshot& actions,std::uint64_t generation,bool has_actions,std::uint64_t now) {
    if(a.hand>=xr::kHandCount) return false;
    const auto& grip=actions.hands[a.hand].grip;
    // Retail SP runs local server BulletFire on a different thread from
    // CL_CreateCmd. Both observers publish copied values; local shot identity
    // and the fresh gameplay epoch establish acceptance, not thread equality.
    return shot.local_shot_confirmed&&shot.native_thread&&fresh_native(p,now)&&
        has_actions&&actions.focused&&grip.active&&
        grip.position_valid&&grip.orientation_valid&&grip.position_tracked&&grip.orientation_tracked&&
        a.policy.queue({generation,p.gameplay_epoch,shot.serial,shot.observed_ms,a.hand,true},now);
}
void shot_callback(void* context,const NativeLocalShot& shot) {
    auto& a=*static_cast<HapticAdapter*>(context);
    if(!shot.local_shot_confirmed) return;
    const auto confirmed=a.confirmed.fetch_add(1)+1;
    const auto now=GetTickCount64();
    PreCommandSnapshot p{};xr::ActionSnapshot actions{};std::uint64_t generation{};
    const bool has_actions=read_vr_input_snapshot(&actions,&generation);
    const bool accepted=read_pre_command_snapshot(&p)&&queue_shot(a,shot,p,actions,generation,has_actions,now);
    if(accepted) a.queued.fetch_add(1);
    if(confirmed<=8||confirmed%120==0)
        trace("t5_local_shot_haptic_queue","shot=%llu confirmed=%llu queued_total=%llu accepted=%u generation=%llu gameplay_epoch=%llu hand=%u shot_thread=%lu command_thread=%lu native_shot_writes=0",
              shot.serial,confirmed,a.queued.load(),accepted,generation,p.gameplay_epoch,a.hand,shot.native_thread,p.context.native_thread);
}
bool owner(HapticAdapter& a) noexcept {
    DWORD expected{};a.owner.compare_exchange_strong(expected,GetCurrentThreadId());
    return a.owner.load()==GetCurrentThreadId();
}
template<class Runtime> void apply(HapticAdapter& a,Runtime& runtime,const input::HapticPlan& plan) {
    for(unsigned hand=0;hand<2;++hand) {
        const auto which=static_cast<xr::Hand>(hand);
        if(plan.stop_mask&(1u<<hand)) {
            const bool ok=runtime.StopHaptic(which);
            trace("t5_local_shot_haptic_stop","hand=%u success=%u",hand,ok);
        }
        if(plan.shots[hand]) {
            const bool ok=runtime.ApplyHaptic(which,plan.amplitude,plan.duration_seconds);
            if(ok) ++a.applied;else ++a.failed;
            trace("t5_local_shot_haptic_apply","hand=%u shots=%u success=%u applied=%llu failed=%llu amplitude=%.3f duration_seconds=%.3f source=%s",
                  hand,plan.shots[hand],ok,a.applied,a.failed,plan.amplitude,plan.duration_seconds,a.predicted_source?"predicted_event":"confirmed_bullet");
        }
    }
}
// The exact production gate and dispatch are testable with a value-only runtime
// double. Runtime calls happen after the mailbox lock has been released.
template<class Runtime> void service_on_owner(HapticAdapter& a,Runtime& runtime,const vr::FrameTicket* ticket,
                                             const PreCommandSnapshot* native,std::uint64_t now) {
    if(!owner(a)) return;
    input::HapticContext context{};
    if(ticket&&native&&fresh_native(*native,now)) {
        context.xr_generation=ticket->key.generation;context.gameplay_epoch=native->gameplay_epoch;
        context.sequence=ticket->frame.actions.sequence;context.published_ms=ticket->published_ms;
        context.focused=ticket->frame.actions.focused&&runtime.session_focused();
        context.gameplay=ticket->frame.should_render&&ticket->frame.views_valid;
        for(unsigned hand=0;hand<xr::kHandCount;++hand) {
            const auto& grip=ticket->frame.actions.hands[hand].grip;
            context.tracked[hand]=grip.active&&grip.position_valid&&grip.orientation_valid&&
                grip.position_tracked&&grip.orientation_tracked;
        }
    }
    unsigned predicted_stops{};
    if(a.predicted_source) {
        std::lock_guard lock(a.feedback_lock);
        if(!context.focused||!context.gameplay||!context.tracked[a.hand]||
           !a.feedback.current(context.xr_generation,context.gameplay_epoch,now)) {
            clear_predicted_locked(a);context.gameplay=false;
        }
        predicted_stops=a.pending_stops;a.pending_stops=0;
    }
    auto plan=a.policy.service(context,now);plan.stop_mask|=predicted_stops;
    const auto queued=a.queued.load();
    if(queued!=a.diagnostic_queued) {a.diagnostic_queued=queued;a.diagnostic_steps=8;}
    if(a.diagnostic_steps) {
        --a.diagnostic_steps;
        trace("t5_local_shot_haptic_service","queued=%llu now=%llu ticket=%u frame=%llu generation=%llu action_sequence=%llu published_ms=%llu native=%u native_sequence=%llu native_epoch=%llu native_active=%u native_ms=%llu context_epoch=%llu focused=%u gameplay=%u tracked=%u,%u shots=%u,%u stops=0x%x",
            queued,now,ticket!=nullptr,ticket?ticket->key.frame_id:0,context.xr_generation,context.sequence,context.published_ms,
            native!=nullptr,native?native->sequence:0,native?native->gameplay_epoch:0,native?native->gameplay_active:false,
            native?native->observed_ms:0,context.gameplay_epoch,context.focused,context.gameplay,context.tracked[0],context.tracked[1],
            plan.shots[0],plan.shots[1],plan.stop_mask);
    }
    apply(a,runtime,plan);
}
template<class Runtime> void reset_on_owner(HapticAdapter& a,Runtime& runtime) {
    if(!owner(a)) return;
    auto plan=a.policy.reset();
    if(a.predicted_source){std::lock_guard lock(a.feedback_lock);a.feedback.invalidate();plan.stop_mask|=a.pending_stops;a.pending_stops=0;}
    apply(a,runtime,plan);
}
}
bool configure_shot_haptic_adapter(bool bullet_hook,bool command_hook) noexcept {
    if(!enabled(L"BOVR_SHOT_HAPTICS")||!bullet_hook||!command_hook) return false;
    if(adapter.load()) return true;
    try {
        auto* a=new HapticAdapter;
        wchar_t hand[16]{};GetEnvironmentVariableW(L"BOVR_WEAPON_HAND",hand,16);
        if(!wcscmp(hand,L"left")) a->hand=0;
        if(!set_local_shot_callbacks({a,shot_callback})) {delete a;return false;}
        adapter.store(a);
        trace("t5_local_shot_haptics_enabled","hand=%u native_shot_writes=0",a->hand);
        return true;
    } catch(...) {return false;}
}
bool configure_predicted_shot_feedback(bool command_hook) noexcept {
    if(!enabled(L"BOVR_PREDICTED_SHOT_FEEDBACK")||!command_hook||enabled(L"BOVR_SHOT_HAPTICS")||
       enabled(L"BOVR_BULLET_PROBE"))return false;
    if(const auto* existing=adapter.load())return existing->predicted_source;
    try {
        if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000||
           !validate_path(executable_path()).accepted||!predicted_code_guards())return false;
        wchar_t hand[16]{};const auto n=GetEnvironmentVariableW(L"BOVR_WEAPON_HAND",hand,16);
        if(n>=16||(n&&wcscmp(hand,L"right")))return false;
        auto* a=new HapticAdapter;a->predicted_source=true;adapter.store(a);
        trace("t5_predicted_shot_feedback_configured","hand=1 ring=predicted_player events=29,30 kick_meters=0.018 duration_ms=120 new_instruction_hooks=0 bullet_hook=0 camera_kick=0");
        return true;
    }catch(...){return false;}
}
void observe_predicted_shot_feedback(const PreCommandSnapshot& p) noexcept {
    auto* a=adapter.load();if(!a||!a->predicted_source)return;
    // The pre-command seam runs on the native input owner. Foreign callbacks
    // cannot erase a legitimate owner's baseline or dispatch an XR operation.
    if(p.context.native_thread!=GetCurrentThreadId()||!feedback_native_owner())return;
    try {
        const auto now=GetTickCount64();input::PredictedFireSample s{};std::uint32_t link{};
        xr::ActionSnapshot actions{};input::CommandRigSample rig{};std::uint64_t generation{};
        const bool live=read_vr_command_input_snapshot(&actions,&generation,&rig);
        s.generation=generation;s.epoch=p.gameplay_epoch;s.occurrence=p.sequence;s.observed_ms=p.observed_ms;
        s.published_ms=rig.published_ms;s.native_time=p.native_game_time;
        s.valid=fresh_native(p,now)&&live&&generation==rig.xr_generation&&generation==rig.anchor_generation&&
            rig.frame.should_render&&rig.frame.views_valid&&actions.focused&&actions.sequence&&
            tracked(actions.hands[a->hand].grip)&&tracked(actions.hands[a->hand].aim)&&
            command_input_generation_current(rig)&&copy_predicted_ring(p,&s,&link);
        std::lock_guard lock(a->feedback_lock);
        if(a->feedback_link!=link){clear_predicted_locked(*a);a->feedback_link=link;}
        const auto result=a->feedback.observe(s,now);
        if(result.reset)a->pending_stops|=a->policy.reset().stop_mask;
        for(unsigned hand=0;hand<2;++hand)for(unsigned shot=0;shot<result.hand_shots[hand];++shot) {
            ++a->predicted_shots;
            const bool queued=a->policy.queue({s.generation,s.epoch,++a->predicted_serial,now,hand,true},now);
            if(queued)a->queued.fetch_add(1);
            trace("t5_predicted_shot_feedback","shot=%llu sequence=%u event_batch=%u weapon=%u generation=%llu epoch=%llu haptic_queued=%u kick_meters=0.018 predicted_event_only=1 native_shot_writes=0",
                a->predicted_shots,s.sequence,result.shots,s.weapon,s.generation,s.epoch,queued);
        }
    }catch(...){trace("t5_predicted_shot_feedback_exception","stage=observe");}
}
float predicted_shot_kickback(std::uint64_t generation,std::uint64_t epoch,unsigned weapon,std::uint64_t now) noexcept {
    auto* a=adapter.load();if(!a||!a->predicted_source)return 0;
    try {std::lock_guard lock(a->feedback_lock);return a->feedback.kickback(generation,epoch,weapon,now);}
    catch(...){return 0;}
}
void service_shot_haptics(xr::OpenXrRuntime& runtime,const vr::FrameTicket* ticket) noexcept {
    auto* a=adapter.load();if(!a||!owner(*a)) return;
    try {
        PreCommandSnapshot p{};const bool has_native=read_pre_command_snapshot(&p);
        service_on_owner(*a,runtime,ticket,has_native?&p:nullptr,GetTickCount64());
    } catch(...) {trace("t5_local_shot_haptic_exception","stage=service");}
}
void reset_shot_haptics(xr::OpenXrRuntime& runtime) noexcept {
    auto* a=adapter.load();if(!a||!owner(*a)) return;
    try {reset_on_owner(*a,runtime);}
    catch(...) {trace("t5_local_shot_haptic_exception","stage=reset");}
}
}
