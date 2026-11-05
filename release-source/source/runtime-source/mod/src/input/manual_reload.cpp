// SPDX-License-Identifier: GPL-3.0-only
#include "input/manual_reload.hpp"
#include "input/two_hand_aim.hpp"
#include "xr_math.h"
#include <algorithm>
#include <cmath>
namespace bovr::input::manual {
namespace g=wawvr::gameplay;
namespace {
bool finite(xr::Vec3f v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool unit(xr::Quaternionf q) {const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::fabs(n-1)<.003f;}
bool tracked(const xr::TrackedPose& p) {return p.active&&p.position_valid&&p.position_tracked&&p.orientation_valid&&p.orientation_tracked&&finite(p.pose.position)&&unit(p.pose.orientation);}
xr::Vec3f add(xr::Vec3f a,xr::Vec3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
xr::Vec3f sub(xr::Vec3f a,xr::Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot(xr::Vec3f a,xr::Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool near(xr::Vec3f a,xr::Vec3f b,float radius){const auto d=sub(a,b);return dot(d,d)<=radius*radius;}
bool near_segment(xr::Vec3f point,xr::Vec3f start,xr::Vec3f end,float radius){
    const auto axis=sub(end,start);const float length2=dot(axis,axis);
    if(length2<.000001f)return near(point,start,radius);
    const float t=std::clamp(dot(sub(point,start),axis)/length2,0.f,1.f);
    return near(point,add(start,{axis.x*t,axis.y*t,axis.z*t}),radius);
}
bool held(const xr::FloatActionState& f,float threshold){return f.active&&std::isfinite(f.current)&&f.current>=threshold;}
bool bounded_offset(xr::Vec3f v){return finite(v)&&dot(v,v)<=1.f;}
bool repeated(ReloadKind k){return k==ReloadKind::internal_rounds||k==ReloadKind::break_action;}
bool valid(const ReloadMechanism& m){return m.valid&&static_cast<unsigned>(m.kind)<=static_cast<unsigned>(ReloadKind::belt)&&
    m.rounds_per_insert<=max_reload_capacity&&(!repeated(m.kind)||m.rounds_per_insert>0)&&
    bounded_offset(m.receiver_offset)&&bounded_offset(m.handle_offset);}
bool same(const ReloadMechanism& a,const ReloadMechanism& b){return a.valid==b.valid&&a.kind==b.kind&&
    a.rounds_per_insert==b.rounds_per_insert&&a.charge_on_empty==b.charge_on_empty&&a.cycle_after_shot==b.cycle_after_shot&&a.grip_pump==b.grip_pump&&
    a.receiver_offset.x==b.receiver_offset.x&&a.receiver_offset.y==b.receiver_offset.y&&a.receiver_offset.z==b.receiver_offset.z&&
    a.handle_offset.x==b.handle_offset.x&&a.handle_offset.y==b.handle_offset.y&&a.handle_offset.z==b.handle_offset.z;}
Stage stage(g::ReloadStage s){switch(s){
    case g::ReloadStage::FeedDeviceAvailable:return Stage::available;
    case g::ReloadStage::HoldingFeedDevice:return Stage::held;
    case g::ReloadStage::NearInsertionPoint:return Stage::insertion;
    case g::ReloadStage::Committing:return Stage::committing;
    default:return Stage::ready;}}
}
bool supported_weapon(unsigned w) noexcept {return w==4||w==32||w==14;}
void Controller::reset() noexcept {
    reload_={};output_={};sequence_=sample_ms_=epoch_=generation_=0;weapon_=0;
    baseline_=reload_held_=grip_held_=trigger_held_=charge_trigger_armed_=empty_=inserted_=charge_grabbed_=charge_opened_=false;charge_start_=0;
    mechanism_={};previous_clip_=0;commit_rounds_=0;shot_sequence_=0;cycle_pending_=left_handed_=cod4_controls_=commit_ack_held_=false;
}
Output Controller::cancel() noexcept {
    cycle_pending_=cycle_pending_||(empty_&&mechanism_.charge_on_empty);
    reload_={};empty_=inserted_=charge_grabbed_=charge_opened_=false;
    charge_start_=0;commit_rounds_=0;commit_ack_held_=false;
    output_.stage=cycle_pending_?Stage::charge:Stage::ready;
    output_.block_attack=output_.cycle_pending=cycle_pending_;
    output_.request_commit=output_.held_device=false;output_.commit_rounds=0;output_.charge_fraction=0;
    output_.pump_gripped=false;
    return output_;
}
Output Controller::update(const xr::FrameState& f,const Context& c) noexcept {
    const unsigned hand=c.left_handed?0:1,off=hand^1;
    const auto& main=f.actions.hands[hand];const auto& support=f.actions.hands[off];
    const auto& reload_button=c.cod4_controls?main.primary:main.secondary;
    auto mechanism=c.mechanism;
    if(!mechanism.valid&&supported_weapon(c.weapon))mechanism=ReloadMechanism{true};
    const bool supported=valid(mechanism)&&c.weapon>0&&c.weapon<256;
    const bool same_context=epoch_==c.epoch&&generation_==c.generation&&weapon_==c.weapon&&
        same(mechanism_,mechanism)&&left_handed_==c.left_handed&&cod4_controls_==c.cod4_controls;
    const bool fresh=f.actions.sequence&&c.now&&(!sequence_||f.actions.sequence>=sequence_)&&
        (!sample_ms_||(c.now>=sample_ms_&&(f.actions.sequence!=sequence_||c.now-sample_ms_<=100)));
    if(!same_context||!c.valid||!supported||!fresh||!f.actions.focused||
       !f.views_valid||!finite(f.head_center.position)||!unit(f.head_center.orientation)||
       !tracked(main.grip)||!tracked(main.aim)||!tracked(support.grip)||
       !reload_button.active||!support.squeeze.active||!std::isfinite(support.squeeze.current)||
       !support.trigger.active||!std::isfinite(support.trigger.current)||
       c.clip<0||c.reserve<0||c.capacity<1||c.capacity>static_cast<int>(max_reload_capacity)||c.clip>c.capacity) {
        const bool retain_cycle=same_context&&c.valid&&supported&&
            (cycle_pending_||(mechanism.cycle_after_shot&&c.confirmed_cycle_pending));
        reset();epoch_=c.epoch;generation_=c.generation;weapon_=c.weapon;mechanism_=mechanism;
        left_handed_=c.left_handed;cod4_controls_=c.cod4_controls;cycle_pending_=retain_cycle;
        if(!same_context&&c.valid&&supported) return update(f,c);
        if(retain_cycle){Output blocked{};blocked.supported=blocked.block_attack=blocked.cycle_pending=true;
            blocked.stage=Stage::charge;blocked.weapon=c.weapon;blocked.hand=hand;blocked.epoch=c.epoch;
            blocked.generation=c.generation;blocked.updated_ms=c.now;blocked.kind=mechanism.kind;
            blocked.transaction=transaction_;return blocked;}
        return {};
    }
    const bool new_sample=f.actions.sequence!=sequence_;
    if(new_sample)sample_ms_=c.now;
    sequence_=f.actions.sequence;
    const bool reload=reload_button.current,grip=held(support.squeeze,.65f),trigger=held(support.trigger,.55f);
    if(new_sample&&!trigger&&support.trigger.current>=0.f)charge_trigger_armed_=true;
    Output out{};out.supported=true;out.weapon=c.weapon;out.hand=hand;
    out.epoch=c.epoch;out.generation=c.generation;out.updated_ms=c.now;
    out.kind=mechanism.kind;out.rounds_per_insert=mechanism.rounds_per_insert;
    auto flat=xr::Rotate(f.head_center.orientation,{0,0,-1});flat.y=0;
    const float length=std::sqrt(dot(flat,flat));
    if(length>.01f){flat.x/=length;flat.z/=length;}else flat={0,0,-1};
    const xr::Vec3f side{-flat.z,0,flat.x};const float lateral=c.left_handed?.24f:-.24f;
    out.supply=add(f.head_center.position,{side.x*lateral+flat.x*.08f,-.55f,side.z*lateral+flat.z*.08f});
    const auto orientation=main.aim.pose.orientation;
    // Deliberately generous alpha guides relative to j_gun at the grip.
    out.receiver=add(main.grip.pose.position,xr::Rotate(orientation,mechanism.receiver_offset));
    out.handle=add(main.grip.pose.position,xr::Rotate(orientation,mechanism.handle_offset));
    const bool pump_near=mechanism.grip_pump&&grip&&near(support.grip.pose.position,out.handle,.13f);
    if(mechanism.cycle_after_shot&&c.confirmed_cycle_pending)cycle_pending_=true;
    if(c.cancel_requested){
        // Consume current buttons so cancellation cannot replay A or a pickup.
        reload_held_=reload;grip_held_=grip;trigger_held_=trigger;
        previous_clip_=c.clip;shot_sequence_=c.shot_sequence;out.transaction=transaction_;output_=out;
        return cancel();
    }
    if(!baseline_) {reload_held_=reload;grip_held_=grip;trigger_held_=trigger;
        previous_clip_=c.clip;shot_sequence_=c.shot_sequence;commit_ack_held_=c.commit_completed;
        out.cycle_pending=cycle_pending_;out.block_attack=cycle_pending_;if(cycle_pending_)out.stage=Stage::charge;
        out.pump_gripped=pump_near;
        // A held support grip or trigger must not starve fresh A presses after
        // recovery. Record both without inventing a pickup or charge. Reload
        // needs A-up then a fresh press; charging has its own neutral gate.
        if(!reload)baseline_=true;output_=out;return out;}
    const bool reload_edge=reload&&!reload_held_,grip_edge=grip&&!grip_held_,grip_release=!grip&&grip_held_;
    const bool trigger_release=new_sample&&!trigger&&trigger_held_;
    reload_held_=reload;grip_held_=grip;if(new_sample)trigger_held_=trigger;
    const bool shot=mechanism.cycle_after_shot&&c.clip<previous_clip_&&
        (!c.shot_sequence||(shot_sequence_&&c.shot_sequence>shot_sequence_));
    previous_clip_=c.clip;shot_sequence_=c.shot_sequence;
    if(shot&&reload_.stage==g::ReloadStage::Ready){cycle_pending_=true;charge_grabbed_=charge_opened_=false;}
    const bool repeat=repeated(mechanism.kind);
    const bool begin=reload_edge&&reload_.stage==g::ReloadStage::Ready&&c.clip<c.capacity&&c.reserve>0;
    if(begin){empty_=c.clip==0;inserted_=charge_grabbed_=charge_opened_=false;commit_rounds_=0;++transaction_;}
    // Return to the same deliberate fore-end support grip used for aiming to
    // finish a partially loaded tube/chamber. Ammo pickup still owns the belt
    // and receiver, and an already-held grip cannot discard a shell or finish.
    // The existing support policy is right-dominant; left-handed reloads keep A.
    const bool support_finish=repeat&&reload_.stage==g::ReloadStage::FeedDeviceAvailable&&c.clip>0&&
        c.cod4_controls&&!c.left_handed&&new_sample&&grip_edge&&
        !near(support.grip.pose.position,out.supply,.22f)&&
        !near(support.grip.pose.position,out.receiver,.13f)&&
        (mechanism.grip_pump||!near(support.grip.pose.position,out.handle,.13f))&&
        input::two_hand_aim_basis(f.actions,f.head_center,xr::Hand::Right,true).paired;
    // Neither finishing gesture can discard a held round or acknowledge ammo.
    const bool finish=repeat&&(reload_edge||support_finish)&&
        reload_.stage==g::ReloadStage::FeedDeviceAvailable&&c.clip>0;
    const bool exhausted=repeat&&reload_.stage==g::ReloadStage::FeedDeviceAvailable&&
        (c.clip==c.capacity||c.reserve==0);
    if(finish||exhausted){reload_={};inserted_=false;commit_rounds_=0;
        cycle_pending_=cycle_pending_||(empty_&&mechanism.charge_on_empty);empty_=false;}
    const auto profile=g::make_reload_profile(g::ReloadProfileKind::DetachableMagazine);
    g::ReloadInput in{};in.enabled=in.focused=in.weapon_supported=true;in.can_reload=c.clip<c.capacity&&c.reserve>0;
    in.reload_pressed_edge=reload_edge&&!finish&&!exhausted;in.native_commit_completed=c.commit_completed&&!commit_ack_held_;
    commit_ack_held_=c.commit_completed;
    in.off_hand_pose_valid=true;in.grip_pressed_edge=grip_edge;in.grip_held=grip;in.grip_released_edge=grip_release;
    in.hand_in_feed_device_zone=near(support.grip.pose.position,out.supply,.22f);
    in.hand_in_insertion_zone=near(support.grip.pose.position,out.receiver,.13f);
    auto result=g::update_manual_reload(profile,in,&reload_);
    if(result.event==g::ReloadEvent::CommitRequested){inserted_=true;
        const unsigned available=static_cast<unsigned>(std::min(c.capacity-c.clip,c.reserve));
        commit_rounds_=mechanism.rounds_per_insert?std::min(available,mechanism.rounds_per_insert):available;}
    if(result.event==g::ReloadEvent::CommitCompleted){inserted_=charge_grabbed_=charge_opened_=false;commit_rounds_=0;
        if(repeat){
            if(c.clip<c.capacity&&c.reserve>0){reload_.stage=g::ReloadStage::FeedDeviceAvailable;
                reload_.active_profile=g::ReloadProfileKind::DetachableMagazine;++transaction_;}
            else {cycle_pending_=cycle_pending_||(empty_&&mechanism.charge_on_empty);empty_=false;}
        }else {empty_=false;cycle_pending_=false;}
    }
    out.transaction=transaction_;out.block_attack=result.manual_reload_active;
    out.held_device=result.render_detached_feed_device;
    out.stage=stage(reload_.stage);out.block_attack=reload_.stage!=g::ReloadStage::Ready;
    const bool insertion_charge=inserted_&&!repeat&&((empty_&&mechanism.charge_on_empty)||cycle_pending_);
    const bool cycle_charge=cycle_pending_&&reload_.stage==g::ReloadStage::Ready;
    if(insertion_charge||cycle_charge) {
        out.stage=charge_grabbed_?Stage::charging:Stage::charge;
        out.block_attack=true;
        const auto forward=xr::Rotate(orientation,{0,0,-1});
        const float coordinate=dot(sub(support.grip.pose.position,out.handle),forward);
        out.charge_trigger_armed=charge_trigger_armed_;out.charge_sample_fresh=new_sample;
        const auto handle_relative=sub(support.grip.pose.position,out.handle);
        out.charge_handle_distance=std::sqrt(dot(handle_relative,handle_relative));
        out.charge_coordinate=coordinate;
        // Ordinary magazine weapons share a generic receiver/handle anchor.
        // Accept a deliberate trigger grab over that bounded receiver region
        // as well as the handle. A support squeeze keeps the capsule-only area
        // available for aiming; bolt/pump strokes retain their exact handle.
        const bool handle_near=near(support.grip.pose.position,out.handle,.13f);
        const bool receiver_assist=mechanism.kind==ReloadKind::magazine&&!mechanism.cycle_after_shot&&
            !mechanism.grip_pump&&!grip&&near_segment(support.grip.pose.position,out.receiver,out.handle,.13f);
        out.charge_capture_near=handle_near||receiver_assist;
        // The support trigger may already be held while reaching the handle.
        // Capture at its current position only on a fresh tracked sample; the
        // complete rearward stroke and trigger release still follow that grab.
        if(!charge_grabbed_&&new_sample&&(mechanism.grip_pump?grip:(trigger&&charge_trigger_armed_))&&out.charge_capture_near){
            charge_grabbed_=true;charge_start_=coordinate;charge_opened_=false;
        }
        if(charge_grabbed_&&!new_sample)out.charge_fraction=output_.charge_fraction;
        if(charge_grabbed_&&new_sample){
            const float travel=charge_start_-coordinate;
            out.charge_fraction=std::clamp(travel/charging_travel_meters,0.f,1.f);
            if(out.charge_fraction>=.9f)charge_opened_=true;
            if(mechanism.grip_pump){
                const auto relative=sub(support.grip.pose.position,out.handle);
                const auto lateral_squared=std::max(0.f,dot(relative,relative)-coordinate*coordinate);
                // Keep the hand on the pump axis. A trip to the hip, a released
                // grip or a short pull cannot finish an action. Both directions
                // require fresh tracking samples; stationary holds never cycle.
                if(!grip||lateral_squared>.13f*.13f){charge_grabbed_=charge_opened_=false;out.charge_fraction=0;}
                else if(charge_opened_&&std::fabs(travel)<=.02f){
                    empty_=false;cycle_pending_=false;charge_grabbed_=charge_opened_=false;out.charge_fraction=0;
                }
            }else if(trigger_release){if(charge_opened_){empty_=false;cycle_pending_=false;}
                charge_grabbed_=charge_opened_=false;}}
        // Publish actual ownership on the acquisition/release sample, rather
        // than leaving aiming/visual ownership one action sample behind.
        out.stage=charge_grabbed_?Stage::charging:Stage::charge;
        out.charge_acquired=charge_grabbed_;out.charge_opened=charge_opened_;
    }
    out.request_commit=result.request_native_commit&&commit_rounds_&&(repeat||
        (!cycle_pending_&&(!empty_||!mechanism.charge_on_empty)));
    out.commit_rounds=out.request_commit?commit_rounds_:0;
    if(out.request_commit)out.stage=Stage::committing;
    if(cycle_charge&&!cycle_pending_){out.stage=Stage::ready;out.block_attack=false;}
    out.cycle_pending=cycle_pending_;
    out.pump_gripped=mechanism.grip_pump&&grip&&!out.held_device&&
        (out.stage==Stage::ready||out.stage==Stage::charge||out.stage==Stage::charging)&&
        (charge_grabbed_||pump_near);
    output_=out;return out;
}
}
