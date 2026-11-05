// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_melee_scene.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
using namespace bovr;
namespace melee=t5::native_melee_scene;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::printf("FAIL native melee: %s\n",why);std::exit(1);}}
template<class T> auto bytes(const T& value){std::array<unsigned char,sizeof(T)> copy{};std::memcpy(copy.data(),&value,sizeof value);return copy;}
t5::NativeSceneSample scene(const t5::NativeWeaponContext& p){
    return {p.native_thread,p.cg_identity,p.refdef_identity,p.native_time,p.native_frame_time,p.refdef_time,p.stock_view,true};
}
t5::PreCommandSnapshot pre(const t5::NativeWeaponContext& p,std::uint64_t now){
    t5::PreCommandSnapshot s{};s.sequence=10;s.observed_ms=now;s.gameplay_epoch=3;
    s.context.valid=true;s.context.native_thread=p.native_thread;s.context.command_number=p.command_number;
    s.context.connection_state=10;s.context.native_frame_time=5000;
    s.native_cg_identity=p.cg_identity;s.native_game_time=p.native_time;s.native_health=100;
    s.native_gameplay_fields_valid=s.gameplay_active=true;return s;
}
struct Fixture {
    vr::FrameTicket ticket{};
    vr::NativeBuildIdentity failed{0x3b40100,100,0},next{0x3b40100,101,8};
    t5::WeaponRenderWitness missed{},fresh{};
    t5::PreCommandSnapshot failed_pre{};
    melee::Deferred deferred{};
    melee::Current current{};
    std::uint64_t now{1016};
    Fixture(){
        ticket.key={7,11};ticket.published_ms=1000;ticket.frame.frame_id=11;
        ticket.frame.should_render=ticket.frame.views_valid=true;
        auto& p=missed.producer;
        p.valid=p.body_valid=p.normal_context=p.server_normal=p.primary_viewmodel=p.weapon_definition_valid=true;
        p.native_thread=123;p.call_serial=40;p.draw_gun=1;p.command_number=10;
        p.cg_identity=0x200000;p.refdef_identity=p.cg_identity+0x8c100;
        p.viewmodel_identity=0x300000;p.dobj_identity=0x400000;p.variant_identity=0x500000;p.definition_identity=0x600000;
        p.weapon_index=4;p.weapon_class=4;p.weapon_state=17;p.native_time=p.refdef_time=4000;p.native_frame_time=16;
        p.model_lifetime_verified=true;p.model_generation=2;
        p.stock_view.width=2560;p.stock_view.height=1440;p.stock_view.tan_half_fov_x=.8f;p.stock_view.tan_half_fov_y=.5f;
        p.stock_view.axis.forward={1,0,0};p.stock_view.axis.left={0,1,0};p.stock_view.axis.up={0,0,1};
        missed.observed=missed.expected=true;failed_pre=pre(p,1000);
        check(melee::retain_first(deferred,ticket,failed,missed,t5::WeaponRenderAdmission::no_transaction,failed_pre,1000),
              "first rejected tracked invocation records its original key/time and native provenance");
        fresh.producer=p;fresh.observed=true;
        auto& n=fresh.producer;n.call_serial=41;n.command_number=11;n.native_time=n.refdef_time=4016;
        n.weapon_state=18;n.primary_viewmodel=n.weapon_definition_valid=false;n.variant_identity=n.definition_identity=0;
        // Native melee can legitimately use a different DObj; it must match its
        // own protected reread, not the failed firearm's old model/descriptor.
        n.viewmodel_identity=0x300100;n.dobj_identity=0x400100;n.model_generation=3;
        n.weapon_type=n.weapon_class=0;
        sync();
    }
    void sync(){current.native=fresh.producer;current.scene=scene(fresh.producer);
        current.before=pre(fresh.producer,now);current.before.sequence=11;current.after=current.before;current.copied=true;}
    bool admit()const{return melee::admit(deferred,ticket,next,fresh,current,16,now);}
};

void positive_and_states(){
    Fixture f;const auto original=bytes(f);check(f.admit(),"fresh exact native melee18 qualifies without becoming tracked firearm");
    check(bytes(f)==original,"policy reads cannot mutate prediction, witness, deferred provenance or current copies");
    check(!t5::expects_tracked_weapon_render(f.fresh.producer,true),"native melee qualification never changes tracked-firearm admission");
    check(f.deferred.failed_native.build_serial==0&&f.admit(),"missing historical adapter build serial is not fabricated as proof");
    f.next.buffer_identity=0x3cc6400;check(f.admit(),"new actual native frame may use the other legitimate frontend buffer");
    for(int state=-1;state<=53;++state){Fixture a;a.fresh.producer.weapon_state=state;a.sync();
        check(a.admit()==(state>=18&&state<=20),"only18..20 permit this native melee transition route");}
    for(int state=-1;state<=53;++state){Fixture a;a.fresh.producer.other_weapon_state=state;a.sync();
        check(a.admit()==t5::native_weapon_tracking_state(state),"active offhand/unknown companion states remain excluded");}
    f=Fixture{};f.now=1100;check(f.admit(),"original publication's inclusive100ms limit is retained");
    f.now=1101;check(!f.admit(),"no native melee route extends original prediction past100ms");
    f.now=999;check(!f.admit(),"reversed publication clock rejects");
}
void provenance_is_first_and_nonrenewable(){
    Fixture f;const auto old=bytes(f.deferred);auto later=f.missed;later.producer.call_serial+=20;
    check(!melee::retain_first(f.deferred,f.ticket,{0x3cc6400,105,99},later,
        t5::WeaponRenderAdmission::no_transaction,f.failed_pre,1030)&&bytes(f.deferred)==old,
        "repeat rejection of same key cannot replace call/frame or renew publication time");
    for(unsigned fault=0;fault<23;++fault){Fixture a;melee::Deferred output{};const auto unchanged=bytes(output);
        auto ticket=a.ticket;auto native=a.failed;auto witness=a.missed;auto before=a.failed_pre;
        auto status=t5::WeaponRenderAdmission::no_transaction;std::uint64_t now=1000;
        switch(fault){
        case 0:status=t5::WeaponRenderAdmission::original_placement;break;
        case 1:witness.observed=false;break;case 2:witness.expected=false;break;
        case 3:witness.nested=true;break;case 4:witness.substituted=true;break;
        case 5:witness.hidden_primary_weapon=true;break;case 6:witness.native_call_suppressed=true;break;
        case 7:witness.producer.valid=false;break;case 8:witness.producer.dual_wield=true;break;
        case 9:witness.producer.normal_context=false;witness.producer.linked_weapon_context=true;break;
        case 10:witness.producer.weapon_state=18;break;case 11:witness.producer.weapon_definition_valid=false;break;
        case 12:native.buffer_identity=0;break;case 13:native.native_frame=0;break;
        case 14:before.gameplay_epoch=0;break;case 15:before.native_cg_identity+=4;break;
        case 16:before.context.command_number--;break;case 17:before.context.native_thread++;break;
        case 18:before.gameplay_active=false;break;case 19:ticket.frame.views_valid=false;break;
        case 20:ticket.key.generation=0;break;case 21:now=1101;break;case 22:now=999;break;
        }
        check(!melee::retain_first(output,ticket,native,witness,status,before,now)&&bytes(output)==unchanged,
              "unproven first failure cannot mint deferred ownership or modify output");
    }
    f=Fixture{};auto new_ticket=f.ticket;new_ticket.key.frame_id++;new_ticket.frame.frame_id++;
    new_ticket.published_ms=1016;auto newer_pre=f.failed_pre;newer_pre.observed_ms=1016;
    check(melee::retain_first(f.deferred,new_ticket,f.failed,f.missed,t5::WeaponRenderAdmission::no_transaction,newer_pre,1016)&&
        f.deferred.key==new_ticket.key&&f.deferred.published_ms==1016,
        "separately rejected replacement key can retain its own first provenance");
    check(!f.admit(),"old ticket cannot use replacement-key provenance");
}
void rejects_current_and_identity_faults(){
    for(unsigned fault=0;fault<63;++fault){Fixture f;
        switch(fault){
        case 0:f.deferred.active=false;break;case 1:f.deferred.key.frame_id++;break;
        case 2:f.deferred.published_ms--;break;case 3:f.deferred.gameplay_epoch++;break;
        case 4:f.deferred.failed_native.buffer_identity=0;break;case 5:f.deferred.failed_native.native_frame=0;break;
        case 6:f.deferred.failed_producer.dual_wield=true;break;case 7:f.deferred.failed_producer.valid=false;break;
        case 8:f.next.buffer_identity=0;break;case 9:f.next.build_serial=0;break;
        case 10:f.next.native_frame=f.failed.native_frame;break;
        case 11:f.next.native_frame=f.failed.native_frame-1;break;
        case 12:f.fresh.producer.call_serial=f.missed.producer.call_serial;f.sync();break;
        case 13:f.fresh.producer.call_serial=f.missed.producer.call_serial-1;f.sync();break;
        case 14:f.fresh.producer.native_time=f.fresh.producer.refdef_time=f.missed.producer.native_time;f.sync();break;
        case 15:f.fresh.producer.command_number=f.missed.producer.command_number-1;f.sync();break;
        case 16:f.fresh.producer.cg_identity+=4;f.fresh.producer.refdef_identity+=4;f.sync();break;
        case 17:f.fresh.producer.native_thread++;f.sync();break;
        case 18:f.fresh.producer.weapon_index++;f.sync();break;
        case 19:f.fresh.observed=false;break;case 20:f.fresh.expected=true;break;
        case 21:f.fresh.nested=true;break;case 22:f.fresh.substituted=true;break;
        case 23:f.fresh.hidden_primary_weapon=true;break;case 24:f.fresh.native_call_suppressed=true;break;
        case 25:f.current.copied=false;break;case 26:f.current.native.weapon_state++;break;
        case 27:f.current.native.other_weapon_state++;break;case 28:f.current.native.weapon_index++;break;
        case 29:f.current.native.viewmodel_identity+=4;break;case 30:f.current.native.dobj_identity+=4;break;
        case 31:f.current.native.model_generation++;break;case 32:f.current.native.model_lifetime_verified=false;break;
        case 33:f.current.native.command_number++;break;case 34:f.current.native.native_time++;break;
        case 35:f.current.native.normal_context=false;break;case 36:f.current.native.server_normal=false;break;
        case 37:f.current.native.linked_weapon_context=true;break;case 38:f.current.native.body_valid=false;break;
        case 39:f.current.scene.valid=false;break;case 40:f.current.scene.cg_identity+=4;break;
        case 41:f.current.scene.refdef_identity+=4;break;case 42:f.current.scene.native_thread++;break;
        case 43:f.current.scene.native_time++;break;case 44:f.current.scene.native_frame_time++;break;
        case 45:f.current.scene.refdef_time++;break;case 46:f.current.scene.stock_view.origin.x+=.01f;break;
        case 47:f.current.before.gameplay_epoch++;f.current.after=f.current.before;break;
        case 48:f.current.before.sequence=0;f.current.after=f.current.before;break;
        case 49:f.current.before.observed_ms=900;f.current.after=f.current.before;break;
        case 50:f.current.before.observed_ms=1017;f.current.after=f.current.before;break;
        case 51:f.current.before.context.key_catcher=1;f.current.after=f.current.before;break;
        case 52:f.current.before.context.native_override=true;f.current.after=f.current.before;break;
        case 53:f.current.before.context.connection_state=8;f.current.after=f.current.before;break;
        case 54:f.current.before.native_health=0;f.current.after=f.current.before;break;
        case 55:f.current.before.native_game_time=f.fresh.producer.native_time+1;f.current.after=f.current.before;break;
        case 56:f.current.before.native_weapon_flags=0x80;f.current.after=f.current.before;break;
        case 57:f.current.after.sequence++;break;case 58:f.current.after.gameplay_epoch++;break;
        case 59:f.current.after.native_cg_identity+=4;break;case 60:f.current.after.context.command_number++;break;
        case 61:f.current.after.observed_ms++;break;case 62:f.ticket.frame.should_render=false;break;
        }
        const auto unchanged=bytes(f);
        check(!f.admit(),"changed lifecycle, witness, native recopy, exact scene or real-newness rejects");
        check(bytes(f)==unchanged,"every refused admission preserves all inputs");
    }
}
void rejects_false_native_melee_and_descriptor_inference(){
    for(unsigned fault=0;fault<20;++fault){Fixture f;auto& p=f.fresh.producer;
        switch(fault){
        case 0:p.valid=false;break;case 1:p.body_valid=false;break;case 2:p.local_client=1;break;
        case 3:p.draw_gun=0;break;case 4:p.draw_gun=2;break;case 5:p.normal_context=false;break;
        case 6:p.server_normal=false;break;case 7:p.primary_viewmodel=true;break;
        case 8:p.weapon_definition_valid=true;break;case 9:p.variant_identity=0x100000;break;
        case 10:p.definition_identity=0x100000;break;case 11:p.manual_grenade_primary_visual=true;break;
        case 12:p.support_marker_native_visual=true;break;case 13:p.rebirth_native_prop_visual=true;break;
        case 14:p.viewmodel_identity=0;break;case 15:p.dobj_identity=0;break;
        case 16:p.stock_view.origin.x=(std::numeric_limits<float>::quiet_NaN)();break;
        case 17:p.stock_view.tan_half_fov_x=0;break;case 18:p.stock_view.near_clip=-1;break;
        case 19:p.stock_view.axis.up.z=(std::numeric_limits<float>::infinity)();break;
        }
        f.sync();check(!f.admit(),"expected=false alone or zero firearm metadata cannot grant native melee authority");
    }
    Fixture f;f.fresh.producer.weapon_type=99;f.fresh.producer.weapon_class=99;f.sync();
    check(f.admit(),"absent-descriptor type/class values are not used to infer firearm type or authorize tracking");
    check(!t5::expects_tracked_weapon_render(f.fresh.producer,true),"native scene policy does not create a tracked gun profile");
}

std::uint64_t coordinator_now=1000;
std::uint64_t coordinator_clock() noexcept{return coordinator_now;}
struct Transport final:vr::FrameTransport {
    unsigned begins{},ends{},submits{};
    bool initialize(const vr::xr::RuntimeConfig&,const vr::xr::HostCallbacks&) override{return true;}
    vr::BeginStatus begin(vr::xr::FrameState* frame) override{
        *frame={};frame->frame_id=++begins;frame->should_render=frame->views_valid=true;
        frame->head_center.position={1,2,3};frame->actions.sequence=101;return vr::BeginStatus::started;
    }
    vr::TransportResult discard(const vr::xr::FrameState&) override{++ends;return {vr::TransportStatus::zero_layer,0,0,true};}
    vr::TransportResult submit(IDirect3DDevice9*,const vr::xr::FrameState&,const vr::xr::StereoSourceLayout&) override{
        ++ends;++submits;return {vr::TransportStatus::projection,1,3,true};
    }
    bool before_reset(bool) override{return true;}
    bool after_reset(bool,bool success) override{return success;}
    bool shutdown() override{return true;}
};
void real_coordinator_new_scene_lifecycle(){
    for(const int state:{18,19,20}){
        coordinator_now=1000;Transport transport;vr::FrameCoordinator coordinator(transport,coordinator_clock);
        check(coordinator.initialize({}),"real coordinator starts for native melee scene sequence");
        Fixture f;f.ticket=coordinator.begin_frame().ticket;f.deferred={};
        check(t5::resolve_weapon_render_rejection(coordinator,t5::WeaponRenderAdmission::no_transaction,
            t5::WeaponRenderRejectionOwnership::reject_unbound,f.ticket.key,1000,100)==t5::WeaponRenderRejectionAction::preserve_unowned,
            "failed tracked invocation A preserves pristine prediction with generic claims barred");
        check(melee::retain_first(f.deferred,f.ticket,f.failed,f.missed,t5::WeaponRenderAdmission::no_transaction,f.failed_pre,1000),
            "A's exact observed provenance is retained only after coordinator preservation");
        vr::FrameTicket claimed{};
        check(!coordinator.claim_render_frame(&claimed),"failed A cannot use ordinary render claim");
        auto old=f.fresh;old.producer.call_serial=f.missed.producer.call_serial;
        auto old_current=f.current;old_current.native=old.producer;old_current.scene=scene(old.producer);
        check(!melee::admit(f.deferred,f.ticket,f.next,old,old_current,16,1016),
            "same invocation cannot relabel itself as a later native melee scene");
        f.fresh.producer.weapon_state=state;f.sync();coordinator_now=1016;
        check(f.admit(),"independent B18..20 is proved by actual new call, native frame/time and exact current scene");
        vr::SceneReservation reservation{};
        check(coordinator.reserve_deferred_scene_frame(f.ticket.key,f.ticket.published_ms,f.next,coordinator_now,100,&reservation)&&
            reservation.ticket.key==f.ticket.key&&reservation.ticket.published_ms==1000&&
            reservation.ticket.frame.actions.sequence==101&&reservation.ticket.frame.head_center.position.x==1,
            "B atomically reserves exact original prediction without refreshing head/actions/time");
        check(!coordinator.claim_render_frame(&claimed),"real B reservation excludes all generic claims");
        check(coordinator.consume_scene_reservation(reservation,f.next,&claimed),"only B's exact native build consumes reservation");
        const vr::BackendFrameIdentity backend{f.next.buffer_identity,f.next.build_serial};
        check(coordinator.stage_render_frame(claimed.key,backend,{})&&coordinator.mark_eye_complete(claimed.key,backend,0),
            "B stages its own source and independently completes first eye");
        check(!coordinator.mark_eye_complete(claimed.key,{backend.buffer_identity,backend.producer_serial+1},1),
            "unrelated source cannot supply B's second eye");
        check(coordinator.mark_eye_complete(claimed.key,backend,1),"B independently completes exact second eye");
        check(coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted&&transport.begins==1&&transport.ends==1&&transport.submits==1,
            "one prediction submits its own two-eye B pair once, with no reused pixels or intervening zero-layer end");
        check(!coordinator.reserve_deferred_scene_frame(f.ticket.key,f.ticket.published_ms,f.next,1016,100,&reservation),
            "retired prediction cannot be reacquired from otherwise valid copied policy facts");
        check(coordinator.shutdown(),"native melee real coordinator sequence shuts down cleanly");
    }
}
}

void run_native_melee_scene_tests(){
    positive_and_states();provenance_is_first_and_nonrenewable();rejects_current_and_identity_faults();
    rejects_false_native_melee_and_descriptor_inference();real_coordinator_new_scene_lifecycle();
    std::printf("native melee scene policy: %u checks passed\n",checks);
}
