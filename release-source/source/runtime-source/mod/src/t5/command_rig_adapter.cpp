// SPDX-License-Identifier: GPL-3.0-only
#include "t5/command_rig_adapter.hpp"
#include "t5/consumed_command_probe.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
namespace bovr::t5 {
namespace {
namespace xr=wawvr::xr;
using input::CommandRigStatus;
struct RigPending {
    PreCommandSnapshot native{};
    input::CommandRigSample rig{};
    std::uint64_t serial{},captured_ms{};
    bool present{};
};
struct RigTrace {
    std::uint64_t count{},serial{},scope{},epoch{},generation{},frame{},actions{},published{},observed{};
    DWORD producer_thread{},server_thread{};
    int command{};
    CommandRigStatus status{CommandRigStatus::invalid};
    input::NativeCommandBytes bytes{};
    bool emit{};
};
struct RigAdapter {
    std::mutex mutex;
    input::CommandRigLedger ledger;
    RigPending pending;
    input::CommandRigMatch current;
    std::uint64_t producer_serial{},publications{},consumptions{},cancellations{};
    std::uint64_t ambiguous_epoch{},ambiguous_generation{};
    DWORD producer_thread{};
    bool overlapping{};
    CommandRigStatus last_publish{CommandRigStatus::invalid},last_consume{CommandRigStatus::invalid};
    unsigned publish_changes{},consume_changes{};
};
std::atomic<RigAdapter*> rig_adapter{};
bool rig_option(const wchar_t* name) noexcept {
    wchar_t value[8]{};GetEnvironmentVariableW(name,value,8);return !wcscmp(value,L"1");
}
bool rig_fresh(std::uint64_t then,std::uint64_t now) noexcept {
    return then&&now>=then&&now-then<=input::CommandRigLedger::maximum_age_ms;
}
bool rig_native_active(const PreCommandSnapshot& p,std::uint64_t now) noexcept {
    return p.sequence&&p.gameplay_epoch&&p.context.valid&&p.context.local_client==0&&p.context.native_thread&&
        p.context.connection_state>=9&&!p.context.key_catcher&&!p.context.native_override&&
        p.native_gameplay_fields_valid&&primary_weapon_active(p)&&p.native_cg_identity&&rig_fresh(p.observed_ms,now);
}
bool same_command_context(const CommandContext& a,const CommandContext& b) noexcept {
    // Native FinishMove may reset override; native/input code may change yaw.
    // Those mutable values cannot supply command identity after construction.
    return a.valid&&b.valid&&a.local_client==b.local_client&&a.native_thread==b.native_thread&&
        a.command_number==b.command_number&&a.native_frame_time==b.native_frame_time&&
        a.key_catcher==b.key_catcher&&a.connection_state==b.connection_state;
}
bool copy_final_command(const unsigned char* source,input::NativeCommandBytes* out) noexcept {
    if(!source||!out)return false;
    __try {memcpy(out->data(),source,out->size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool trace_due(std::uint64_t count,CommandRigStatus status,CommandRigStatus& previous,unsigned& changes) noexcept {
    const bool changed=status!=previous;previous=status;
    return count<=8||count%120==0||(changed&&changes++<16);
}
const char* rig_status_name(CommandRigStatus status) noexcept {
    switch(status) {
    case CommandRigStatus::accepted:return "accepted";case CommandRigStatus::invalid:return "invalid";
    case CommandRigStatus::stale:return "stale";case CommandRigStatus::regressed:return "regressed";
    case CommandRigStatus::unavailable:return "unavailable";case CommandRigStatus::ambiguous:return "ambiguous";
    case CommandRigStatus::replayed:return "replayed";case CommandRigStatus::capacity_exceeded:return "capacity_exceeded";
    case CommandRigStatus::forgotten:return "forgotten";
    }return "unknown";
}
void emit_rig_trace(const char* event,const RigTrace& t) noexcept {
    if(!t.emit)return;
    char bytes[105]{};constexpr char digits[]{"0123456789abcdef"};
    for(std::size_t i=0;i<t.bytes.size();++i){bytes[i*2]=digits[t.bytes[i]>>4];bytes[i*2+1]=digits[t.bytes[i]&15];}
    trace(event,"count=%llu status=%s publication=%llu scope=%llu epoch=%llu generation=%llu xr_frame=%llu actions=%llu published_ms=%llu observed_ms=%llu command=%d producer_thread=%lu server_thread=%lu bytes52=%s command_writes=0 shot_writes=0",
        t.count,rig_status_name(t.status),t.serial,t.scope,t.epoch,t.generation,t.frame,t.actions,t.published,t.observed,
        t.command,t.producer_thread,t.server_thread,bytes);
}
void rig_clear(RigAdapter& a) {
    a.pending={};a.current={};a.ledger.clear();
}
RigTrace publication_trace(RigAdapter& a,const input::ProducedRigCommand& p,CommandRigStatus status) {
    RigTrace t{};t.count=++a.publications;t.serial=p.publication_serial;t.epoch=p.gameplay_epoch;t.generation=p.rig.xr_generation;
    t.frame=p.rig.frame.frame_id;t.actions=p.rig.frame.actions.sequence;t.published=p.rig.published_ms;t.observed=p.observed_ms;
    t.command=p.command_number;t.producer_thread=p.native_thread;t.bytes=p.bytes;t.status=status;
    t.emit=trace_due(t.count,status,a.last_publish,a.publish_changes);return t;
}
void consumed_rig_before(void* opaque,const NativeConsumedCommand& native) {
    auto& a=*static_cast<RigAdapter*>(opaque);const auto now=GetTickCount64();
    PreCommandSnapshot p{};xr::ActionSnapshot actions{};std::uint64_t generation{};
    const bool active=read_pre_command_snapshot(&p)&&rig_native_active(p,now)&&
        read_vr_input_snapshot(&actions,&generation)&&actions.focused&&generation&&
        native.readable&&native.local_identity&&native.active&&!native.mismatches&&!native.nesting_invalidated&&
        native.scope_serial&&native.native_thread==GetCurrentThreadId()&&native.image_base==0x400000;
    input::ConsumedCommandIdentity identity{};identity.bytes=native.command;identity.scope_serial=native.scope_serial;
    identity.gameplay_epoch=p.gameplay_epoch;identity.xr_generation=generation;identity.observed_ms=native.observed_ms;
    identity.native_thread=native.native_thread;identity.local_active=active;
    RigTrace evidence{};
    {
        std::lock_guard lock(a.mutex);a.current={};
        if(!active)a.ledger.clear();
        const auto status=a.ledger.consume(identity,now,&a.current);
        evidence.count=++a.consumptions;evidence.status=status;evidence.scope=native.scope_serial;
        evidence.epoch=p.gameplay_epoch;evidence.generation=generation;evidence.observed=native.observed_ms;
        evidence.server_thread=native.native_thread;evidence.bytes=native.command;
        if(a.current.valid) {
            const auto& produced=a.current.produced;evidence.serial=produced.publication_serial;
            evidence.frame=produced.rig.frame.frame_id;evidence.actions=produced.rig.frame.actions.sequence;
            evidence.published=produced.rig.published_ms;evidence.command=produced.command_number;evidence.producer_thread=produced.native_thread;
        }
        evidence.emit=trace_due(evidence.count,status,a.last_consume,a.consume_changes);
    }
    // The observer intentionally hides its scope getter during this callback.
    // No code treats this copied match as permission to modify a native shot.
    emit_rig_trace("t5_command_rig_consumed",evidence);
}
void consumed_rig_after(void* opaque,const NativeConsumedCommand& native) {
    auto& a=*static_cast<RigAdapter*>(opaque);std::lock_guard lock(a.mutex);
    a.ledger.end_scope(native.scope_serial);
    if(a.current.scope_serial==native.scope_serial)a.current={};
    if(native.nesting_invalidated||!native.native_returned)rig_clear(a);
}
}
bool configure_command_rig_adapter(HMODULE executable,bool input_ready,bool consumed_ready) noexcept {
    if(!rig_option(L"BOVR_COMMAND_RIG_PROBE")||!rig_option(L"BOVR_INPUT_PROBE")||!rig_option(L"BOVR_XR_STEREO_PROBE")||
       !rig_option(L"BOVR_COMMAND_PROBE")||!rig_option(L"BOVR_CONSUMED_COMMAND_PROBE")||!input_ready||!consumed_ready)return false;
    if(rig_adapter.load())return true;
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    try {
        std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        auto candidate=std::make_unique<RigAdapter>();
        if(!set_consumed_command_callbacks({candidate.get(),consumed_rig_before,consumed_rig_after}))return false;
        rig_adapter.store(candidate.release());
        trace("t5_command_rig_configured","default=off native_fingerprint_bytes=52 maximum_age_ms=100 newer_post_pose_reads=0 command_callbacks_replaced=0 command_writes=0 shot_writes=0");
        return true;
    } catch(...) {return false;}
}
bool command_rig_enabled() noexcept {return rig_adapter.load()!=nullptr;}
void begin_command_rig(const CommandContext& context,const xr::ActionSnapshot& actions,std::uint64_t generation,
                       const input::CommandRigSample& rig,bool actions_available) noexcept {
    auto* a=rig_adapter.load();if(!a)return;
    try {
        const auto now=GetTickCount64();PreCommandSnapshot p{};
        const bool good=read_pre_command_snapshot(&p)&&rig_native_active(p,now)&&same_command_context(context,p.context)&&
            context.native_thread==GetCurrentThreadId()&&actions_available&&actions.focused&&actions.sequence&&generation&&
            rig.xr_generation==generation&&rig.anchor_generation==generation&&rig.frame.actions.sequence==actions.sequence&&
            rig.frame.actions.focused==actions.focused&&rig.frame.frame_id&&rig.frame.should_render&&rig.frame.views_valid&&
            rig_fresh(rig.published_ms,now);
        bool overlap=false;
        {
            std::lock_guard lock(a->mutex);const auto serial=++a->producer_serial;
            if(a->pending.present) {
                // No per-dispatch post token exists in the original command
                // hook. An overlap/unpaired begin therefore poisons this whole
                // gameplay/XR pair; never guess which return owns a newer rig.
                a->overlapping=true;a->ambiguous_epoch=p.gameplay_epoch;a->ambiguous_generation=generation;
                rig_clear(*a);overlap=true;
            } else {
                if(a->overlapping&&good&&(p.gameplay_epoch!=a->ambiguous_epoch||generation!=a->ambiguous_generation))a->overlapping=false;
                if(!good||a->overlapping||(a->producer_thread&&a->producer_thread!=context.native_thread))rig_clear(*a);
                else {a->producer_thread=context.native_thread;a->pending={p,rig,serial,now,true};}
            }
        }
        if(overlap)trace("t5_command_rig_rejected","reason=overlapping_or_unpaired_producer epoch=%llu generation=%llu until_native_or_xr_epoch_change=1 command_writes=0 shot_writes=0",p.gameplay_epoch,generation);
    } catch(...) {cancel_command_rig("begin_exception");}
}
void finish_command_rig(const CommandContext& context,const unsigned char* command) noexcept {
    auto* a=rig_adapter.load();if(!a)return;
    try {
        const auto now=GetTickCount64();PreCommandSnapshot current{};input::NativeCommandBytes bytes{};
        const bool copied=copy_final_command(command,&bytes),has_native=read_pre_command_snapshot(&current);
        RigTrace evidence{};
        {
            std::lock_guard lock(a->mutex);const auto pending=a->pending;a->pending={};
            if(!pending.present)return;
            input::ProducedRigCommand value{};value.bytes=bytes;value.rig=pending.rig;value.publication_serial=pending.serial;
            // The XR owner may publish between native pre-context capture and
            // this input callback. Capture time follows the actual cache read;
            // native context time and original XR publication remain separate.
            value.gameplay_epoch=pending.native.gameplay_epoch;value.observed_ms=pending.captured_ms;
            value.native_thread=pending.native.context.native_thread;value.command_number=pending.native.context.command_number;
            value.gameplay_active=copied&&has_native&&!a->overlapping&&context.native_thread==GetCurrentThreadId()&&
                same_command_context(context,pending.native.context)&&current.sequence==pending.native.sequence&&
                current.gameplay_epoch==pending.native.gameplay_epoch&&rig_native_active(current,now);
            const auto status=a->ledger.publish(value,now);
            if(status!=CommandRigStatus::accepted)a->current={};
            evidence=publication_trace(*a,value,status);
        }
        emit_rig_trace("t5_command_rig_published",evidence);
    } catch(...) {cancel_command_rig("finish_exception");}
}
void cancel_command_rig(const char* reason) noexcept {
    auto* a=rig_adapter.load();if(!a)return;
    try {
        std::uint64_t count{};{std::lock_guard lock(a->mutex);rig_clear(*a);count=++a->cancellations;}
        if(count<=8||count%120==0)trace("t5_command_rig_cancelled","count=%llu reason=%s command_writes=0 shot_writes=0",count,reason?reason:"unspecified");
    } catch(...) {}
}
bool read_current_command_rig(input::CommandRigMatch* output) noexcept {
    if(!output)return false;*output={};auto* a=rig_adapter.load();if(!a)return false;
    try {
        NativeConsumedCommand native{};PreCommandSnapshot p{};xr::ActionSnapshot actions{};std::uint64_t generation{};
        const auto now=GetTickCount64();
        if(!read_consumed_command_scope(&native)||!native.active||native.mismatches||native.nesting_invalidated||
           native.native_thread!=GetCurrentThreadId()||!read_pre_command_snapshot(&p)||!rig_native_active(p,now)||
           !read_vr_input_snapshot(&actions,&generation)||!actions.focused)return false;
        std::lock_guard lock(a->mutex);
        if(a->current.scope_serial!=native.scope_serial||a->current.server_thread!=native.native_thread||
           a->current.produced.bytes!=native.command||!a->ledger.current(a->current,p.gameplay_epoch,generation,now))return false;
        *output=a->current;return true;
    } catch(...) {return false;}
}
bool trace_native_shot_command_rig(const NativeLocalShot& shot) noexcept {
    if(!command_rig_enabled()||!shot.local_shot_confirmed||shot.native_thread!=GetCurrentThreadId())return false;
    const DWORD saved_error=GetLastError();
    input::CommandRigMatch match{};
    const bool matched=read_current_command_rig(&match);
    static std::atomic<std::uint64_t> observations{};
    const auto count=observations.fetch_add(1)+1;
    if(count<=8||count%120==0) {
        const auto& p=match.produced;const auto& rig=p.rig;
        char bytes[105]{};constexpr char digits[]{"0123456789abcdef"};
        for(std::size_t i=0;i<p.bytes.size();++i){bytes[i*2]=digits[p.bytes[i]>>4];bytes[i*2+1]=digits[p.bytes[i]&15];}
        const auto& left=rig.frame.actions.hands[0].grip.pose.position;
        const auto& right=rig.frame.actions.hands[1].grip.pose.position;
        trace("t5_native_shot_command_rig","shot=%llu matched=%u publication=%llu scope=%llu epoch=%llu generation=%llu xr_frame=%llu actions=%llu published_ms=%llu input_ms=%llu shot_ms=%llu producer_thread=%lu server_thread=%lu weapon=%d command_weapon=%u bytes52=%s left_grip=%.5f,%.5f,%.5f right_grip=%.5f,%.5f,%.5f shot_writes=0 placement_writes=0",
            shot.serial,matched,p.publication_serial,match.scope_serial,p.gameplay_epoch,rig.xr_generation,rig.frame.frame_id,
            rig.frame.actions.sequence,rig.published_ms,p.observed_ms,shot.observed_ms,p.native_thread,shot.native_thread,
            shot.weapon_index,static_cast<unsigned>(p.bytes[0x18]),bytes,left.x,left.y,left.z,right.x,right.y,right.z);
    }
    SetLastError(saved_error);return matched;
}
}
