// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_stack_fire_count.hpp"
#include "t5/weapon_placement_adapter.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/command_probe.hpp"
#include "loader/trace.hpp"
#include <atomic>

namespace bovr::t5 {
namespace {
namespace ip=input;
using Status=ip::WeaponPlacementStatus;
struct ObservationIdentity {
    std::uintptr_t cg{},viewmodel{},dobj{},variant{},definition{};
    std::uint32_t weapon{};
    std::uint64_t generation{};
    bool lifetime_verified{};
    primary_weapon_tracking::Mode tracking_mode{};
    std::uint32_t server_link{};
    friend bool operator==(const ObservationIdentity&,const ObservationIdentity&)=default;
};
ObservationIdentity observed(const NativeWeaponContext& c) noexcept {
    return {c.cg_identity,c.viewmodel_identity,c.dobj_identity,c.variant_identity,c.definition_identity,c.weapon_index,
        c.model_generation,c.model_lifetime_verified,primary_weapon_mode(c),c.server_link_identity};
}
bool eligible(const NativeWeaponContext& c) noexcept {
    return c.valid&&c.body_valid&&c.local_client==0&&c.draw_gun&&primary_weapon_context(c)&&
        c.primary_viewmodel&&c.weapon_definition_valid&&c.weapon_type==0&&!c.dual_wield&&native_valid_stack_fire_count(static_cast<std::uint32_t>(c.stack_count))&&
        ((c.weapon_state>=0&&c.weapon_state<18)||c.manual_grenade_primary_visual)&&
        c.cg_identity&&c.dobj_identity&&c.variant_identity&&c.definition_identity;
}
struct ProposalHost {
    void* context{};
    bool (*reserve)(void*,const NativeWeaponContext&,ip::ReservedWeaponFrame*,ip::WeaponBodySample*){};
    void (*cancel)(void*,const ip::WeaponFrameIdentity&){};
    std::uint64_t (*now)(void*){};
    void (*profile)(void*,const NativeWeaponContext&,const ip::ReservedWeaponFrame&,const ip::WeaponCalibration&){};
    void (*sample)(void*,const NativeWeaponContext&,Status,const ip::WeaponPlacementProposal&,std::uint64_t){};
    bool (*validate)(void*,const NativeWeaponContext&,const ip::WeaponFrameIdentity&){};
    bool (*command_allowed)(void*,const NativeWeaponContext&,std::uint64_t){};
};
// Model continuity here is explicitly observational. It cannot prove allocator
// address reuse, so this adapter has no mutation or externally published pose API.
struct PassivePlacementAdapter {
    ProposalHost host{};
    wawvr::xr::Hand hand{wawvr::xr::Hand::Right};
    DWORD owner{};
    bool placement_enabled{};
    ObservationIdentity identity{};
    bool identity_valid{};
    std::uint64_t epoch{},revision{},last_wall{},last_generation{},pre_call{};
    int last_native_time{};
    ip::WeaponCalibration calibration{};
    ip::WeaponFrameIdentity reserved_key{};
    ip::WeaponPlacementProposal last_proposal{};
    Status last_status{Status::context_rejected};
    NativeWeaponContext pre_context{};
    void cancel() noexcept {
        if(reserved_key.reservation_serial&&host.cancel) host.cancel(host.context,reserved_key);
        reserved_key={};
    }
    void invalidate() noexcept {cancel();calibration={};identity_valid=false;last_generation=0;last_proposal={};}
    void report(const NativeWeaponContext& c,Status status) noexcept {
        last_status=status;
        if(host.sample) host.sample(host.context,c,status,last_proposal,epoch);
    }
    void reject_proposal(Status status) noexcept {
        // A lost hand or unavailable calibration changes only weapon output.
        // Preserve the canonical head/eye scene and let native place its weapon.
        if(status==Status::reservation_mismatch||status==Status::stale_prediction||status==Status::invalid_prediction||status==Status::invalid_body) cancel();
    }
    void before(const NativeWeaponContext& c) noexcept {
        if(!owner) owner=c.native_thread;
        // Registration runs before gameplay. The native frontend owns all
        // mutable adapter state; a foreign thread must not replace it.
        if(!owner||c.native_thread!=owner) return;
        last_proposal={};
        pre_call=c.call_serial;pre_context=c;
        const auto now=host.now?host.now(host.context):0;
        const bool gap=last_wall&&(now<last_wall||now-last_wall>250);
        const bool clock_back=identity_valid&&c.native_time<last_native_time;
        last_wall=now;last_native_time=c.native_time;
        if(!eligible(c)||!now) {invalidate();report(c,Status::context_rejected);return;}
        const auto current=observed(c);
        if(gap||clock_back||!identity_valid||current!=identity) {
            invalidate();identity=current;identity_valid=true;++epoch;
        }
        ip::ReservedWeaponFrame frame{};ip::WeaponBodySample body{};
        if(!host.reserve||!host.reserve(host.context,c,&frame,&body)) {report(c,Status::reservation_mismatch);return;}
        reserved_key=frame.identity;
        if(last_generation&&last_generation!=frame.identity.generation) {calibration={};++epoch;}
        last_generation=frame.identity.generation;
        const auto model_generation=c.model_lifetime_verified?c.model_generation:epoch;
        ip::WeaponPlacementContext context{frame.identity,now,model_generation,calibration.revision,c.weapon_index,true};
        if(!calibration.confirmed) {
            // Native state0 is the only initial alignment reference accepted
            // by this diagnostic. Other primary states may only reuse it.
            if(c.weapon_state!=0) {report(c,Status::calibration_mismatch);return;}
            context.calibration_revision=++revision;
            const auto status=ip::derive_experimental_native_alignment(frame,body,context,c.placement,hand,&calibration);
            if(status!=Status::accepted) {reject_proposal(status);report(c,status);return;}
            if(host.profile) host.profile(host.context,c,frame,calibration);
        }
        context.calibration_revision=calibration.revision;
        const auto status=ip::propose_weapon_placement(frame,body,calibration,context,c.placement,&last_proposal);
        if(status!=Status::accepted) reject_proposal(status);
        report(c,status);
    }
    bool select(const NativeWeaponContext& c,ip::ScaledWeaponPlacement* scratch) noexcept {
        if(scratch) *scratch={};
        if(!scratch||!placement_enabled||c.native_thread!=owner||c.call_serial!=pre_call||c.stack_count!=pre_context.stack_count||!eligible(c)||
           !c.model_lifetime_verified||!c.model_generation||observed(c)!=identity||!last_proposal.valid||
           last_proposal.identity!=reserved_key||calibration.model_generation!=c.model_generation||
           !host.validate||!host.command_allowed) return false;
        const auto now=host.now?host.now(host.context):0;
        if(!now||!host.command_allowed(host.context,c,now)||!host.validate(host.context,c,reserved_key)) return false;
        *scratch=last_proposal.placement;return true;
    }
    void after(const NativeWeaponContext& c) noexcept {
        if(c.native_thread!=owner||!reserved_key.reservation_serial) return;
        // A post snapshot can invalidate this exact ticket, never mutate the
        // native result. Native already consumed its unchanged original args.
        if(c.call_serial!=pre_call||c.stack_count!=pre_context.stack_count||!eligible(c)||observed(c)!=identity||c.native_time!=pre_context.native_time||
           c.refdef_identity!=pre_context.refdef_identity) {
            invalidate();report(c,Status::context_rejected);
        }
    }
};
bool reserve_host(void*,const NativeWeaponContext& c,ip::ReservedWeaponFrame* frame,ip::WeaponBodySample* body) {
    return reserve_weapon_scene_frame(c,frame,body);
}
void cancel_host(void*,const ip::WeaponFrameIdentity& key) {cancel_weapon_scene_frame(key);}
std::uint64_t now_host(void*) {return GetTickCount64();}
bool validate_host(void*,const NativeWeaponContext& c,const ip::WeaponFrameIdentity& key) {return validate_weapon_scene_reservation(c,key);}
bool command_host(void*,const NativeWeaponContext& c,std::uint64_t now) {
    PreCommandSnapshot s{};
    if(!read_pre_command_snapshot(&s)) return false;
    const auto& p=s.context;
    return p.valid&&p.local_client==0&&p.native_thread==c.native_thread&&!p.native_override&&!p.key_catcher&&p.connection_state>=9&&
        p.native_frame_time==c.command_clock&&c.command_number>=p.command_number&&
        static_cast<std::uint64_t>(static_cast<std::int64_t>(c.command_number)-p.command_number)<=1&&
        now>=s.observed_ms&&now-s.observed_ms<=100;
}
void log_profile(void*,const NativeWeaponContext& c,const ip::ReservedWeaponFrame& frame,const ip::WeaponCalibration& calibration) {
    const auto& p=calibration.origin_from_grip_aim_local_meters;const auto& q=calibration.model_to_aim_iw;
    trace("t5_weapon_experimental_profile","source=experimental_native_alignment weapon=%u observed_identity_epoch=%llu revision=%llu hand=%u generation=%llu xr_frame=%llu reservation=%llu build=%llu sample=%llu cg=0x%lx dobj=0x%lx variant=0x%lx definition=0x%lx aim_local_offset_m=%.6f,%.6f,%.6f model_to_aim_iw=%.7f,%.7f,%.7f,%.7f lifetime_evidence=separate_native_lifecycle_log grip_tag_verified=0 muzzle_verified=0 physical_acceptance=0 native_writes=0",
        c.weapon_index,calibration.model_generation,calibration.revision,static_cast<unsigned>(calibration.hand),frame.identity.generation,frame.identity.frame_id,
        frame.identity.reservation_serial,frame.identity.native_build_serial,frame.prediction.actions.sequence,
        static_cast<unsigned long>(c.cg_identity),static_cast<unsigned long>(c.dobj_identity),static_cast<unsigned long>(c.variant_identity),static_cast<unsigned long>(c.definition_identity),
        p.x,p.y,p.z,q.x,q.y,q.z,q.w);
}
void log_sample(void*,const NativeWeaponContext& c,Status status,const ip::WeaponPlacementProposal& p,std::uint64_t epoch) {
    static std::uint64_t counts[12]{},total{};
    const auto index=static_cast<unsigned>(status);if(index>=12) return;
    const auto count=++counts[index];++total;
    const std::uint64_t interval=status==Status::accepted?120:600;
    if(count>3&&count%interval) return;
    trace("t5_weapon_proposal_totals","total=%llu status=%u status_count=%llu accepted=%llu reservation_mismatch=%llu invalid_tracking=%llu calibration_mismatch=%llu",total,index,count,counts[0],counts[3],counts[6],counts[7]);
    trace("t5_weapon_proposal","call=%llu status=%u valid=%u weapon=%u state=%d observed_identity_epoch=%llu generation=%llu xr_frame=%llu reservation=%llu build=%llu predicted=%lld sample=%llu revision=%llu native_time=%d refdef_time=%d body_origin=%.5f,%.5f,%.5f native_origin=%.5f,%.5f,%.5f proposed_origin=%.5f,%.5f,%.5f proposed_quat=%.6f,%.6f,%.6f,%.6f lifetime_evidence=separate_native_lifecycle_log native_writes=0 shot_writes=0",
        c.call_serial,static_cast<unsigned>(status),p.valid,c.weapon_index,c.weapon_state,epoch,p.identity.generation,p.identity.frame_id,
        p.identity.reservation_serial,p.identity.native_build_serial,p.predicted_display_time,p.action_sequence,p.calibration_revision,c.native_time,c.refdef_time,
        c.stock_view.origin.x,c.stock_view.origin.y,c.stock_view.origin.z,c.placement.origin.x,c.placement.origin.y,c.placement.origin.z,
        p.placement.origin.x,p.placement.origin.y,p.placement.origin.z,p.placement.quaternion.x,p.placement.quaternion.y,p.placement.quaternion.z,p.placement.quaternion.w);
}
void before_callback(void* context,const NativeWeaponContext& c) {static_cast<PassivePlacementAdapter*>(context)->before(c);}
void after_callback(void* context,const NativeWeaponContext& c) {static_cast<PassivePlacementAdapter*>(context)->after(c);}
bool select_callback(void* context,const NativeWeaponContext& c,ip::ScaledWeaponPlacement* scratch) {return static_cast<PassivePlacementAdapter*>(context)->select(c,scratch);}
std::atomic<PassivePlacementAdapter*> configured{};
}
bool configure_weapon_placement_adapter(HMODULE executable,bool weapon_hook) noexcept {
    wchar_t opt[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_RESERVATION_PROBE",opt,8);
    if(wcscmp(opt,L"1")||!weapon_hook||executable!=GetModuleHandleW(nullptr)) return false;
    if(configured.load()) return true;
    try {
        auto* state=new PassivePlacementAdapter;
        state->host={nullptr,reserve_host,cancel_host,now_host,log_profile,log_sample,validate_host,command_host};
        wchar_t placement[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",placement,8);state->placement_enabled=!wcscmp(placement,L"1");
        wchar_t hand[16]{};GetEnvironmentVariableW(L"BOVR_WEAPON_HAND",hand,16);
        if(!wcscmp(hand,L"left")) state->hand=wawvr::xr::Hand::Left;
        else if(hand[0]&&wcscmp(hand,L"right")) {delete state;return false;}
        if(!set_weapon_callbacks({state,before_callback,after_callback,state->placement_enabled?select_callback:nullptr})) {delete state;return false;}
        configured.store(state);
        trace("t5_weapon_proposal_configured","scratch_placement_enabled=%u experimental_native_alignment=1 hand=%u original_argument_writes=0 shot_writes=0 activation_requires_verified_lifetime=1",state->placement_enabled,static_cast<unsigned>(state->hand));
        return true;
    } catch(...) {return false;}
}
}
