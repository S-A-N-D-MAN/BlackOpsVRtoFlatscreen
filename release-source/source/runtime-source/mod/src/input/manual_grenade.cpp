// SPDX-License-Identifier: GPL-3.0-only
#include "input/manual_grenade.hpp"
#include "xr_math.h"
#include <cmath>
namespace bovr::input::grenade {
namespace {
bool finite(xr::Vec3f v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool unit(xr::Quaternionf q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::fabs(n-1)<.003f;}
bool tracked(const xr::TrackedPose& p){return p.active&&p.position_valid&&p.position_tracked&&p.orientation_valid&&p.orientation_tracked&&finite(p.pose.position)&&unit(p.pose.orientation);}
g::ManualGrenadePoint point(xr::Vec3f v){return {v.x,v.y,v.z};}
xr::Vec3f vector(g::ManualGrenadePoint v){return {v.x,v.y,v.z};}
}
void Controller::reset() noexcept {gesture_={};history_={};identity_=generation_=sequence_=published_=0;previous_={};button_decided_=button_reserved_=false;}
void Controller::retire() noexcept {(void)g::rearm_manual_grenade_gesture(&gesture_);history_={};}
Output Controller::update(const CommandRigSample& r,const Context& c) noexcept {
    const auto& f=r.frame;const auto& h=f.actions.hands[0];
    const bool changed=c.identity!=identity_||c.generation!=generation_||c.button_mode!=button_mode_;
    const bool valid=c.valid&&c.identity&&c.generation&&c.now&&r.xr_generation==c.generation&&
        r.anchor_generation==c.generation&&r.published_ms&&c.now>=r.published_ms&&c.now-r.published_ms<=100&&
        f.frame_id&&f.actions.sequence&&f.actions.focused&&f.views_valid&&tracked(h.grip)&&
        finite(f.head_center.position)&&unit(f.head_center.orientation)&&finite(r.tracking_anchor.position)&&unit(r.tracking_anchor.orientation)&&
        h.trigger.active&&std::isfinite(h.trigger.current)&&(!sequence_||f.actions.sequence>=sequence_);
    const bool owned=gesture_.stage!=g::ManualGrenadeStage::ready;
    if(changed||!valid){reset();identity_=c.identity;generation_=c.generation;button_mode_=c.button_mode;
        if(!valid){Output out{};out.event=owned?g::ManualGrenadeEvent::reset:g::ManualGrenadeEvent::none;return out;}}
    const auto local=xr::OpenXrPoseToIwRelative(h.grip.pose,r.tracking_anchor,xr::kIwUnitsPerMeter);
    auto forward=xr::Rotate(f.head_center.orientation,{0,0,-1});forward.y=0;
    const float length=std::sqrt(forward.x*forward.x+forward.z*forward.z);
    if(length>.01f){forward.x/=length;forward.z/=length;}else forward={0,0,-1};
    const xr::Vec3f d{h.grip.pose.position.x-f.head_center.position.x,h.grip.pose.position.y-f.head_center.position.y,h.grip.pose.position.z-f.head_center.position.z};
    g::ManualGrenadePoint belt{(d.x*forward.x+d.z*forward.z)*xr::kIwUnitsPerMeter,
        (d.x*forward.z-d.z*forward.x)*xr::kIwUnitsPerMeter,d.y*xr::kIwUnitsPerMeter};
    if(c.button_mode)belt={0,h.stick_click.active&&h.stick_click.current?-12.f:12.f,-24.f};
    const auto zone=g::manual_grenade_slot_at(belt);
    const bool available=zone==g::ManualGrenadeSlot::frag?c.frag_available:
        (zone==g::ManualGrenadeSlot::tactical&&c.tactical_available);
    const bool fresh=f.actions.sequence!=sequence_;
    if(!c.button_mode&&fresh&&r.published_ms!=published_){
        if(published_&&r.published_ms>published_&&r.published_ms-published_<=100){
            const float dt=float(r.published_ms-published_)*.001f;
            (void)g::record_manual_grenade_velocity(r.published_ms*1000000,
                {(local.position.x-previous_.x)/dt,(local.position.y-previous_.y)/dt,(local.position.z-previous_.z)/dt},&history_);
        }
        previous_=local.position;published_=r.published_ms;sequence_=f.actions.sequence;
    }
    Output out{};
    if(c.button_mode){
        // Decide at the start of the analog excursion, before the jump router's
        // lower press threshold. A blocked press cannot become a grenade when
        // its grip/reload blocker clears. Duplicates never press/release/rearm.
        if(fresh){
            if(h.trigger.current<=g::kManualGrenadeTriggerRelease)button_decided_=button_reserved_=false;
            else if(!button_decided_){button_decided_=true;button_reserved_=!c.blocked;}
            (void)g::update_manual_grenade_gesture(true,true,h.trigger.current,belt,
                c.blocked||!button_reserved_||!available||!h.stick_click.active,&gesture_,&out.event);
            sequence_=f.actions.sequence;published_=r.published_ms;
        }
    }else (void)g::update_manual_grenade_gesture(true,true,h.trigger.current,belt,c.blocked||!available,&gesture_,&out.event);
    if(out.event==g::ManualGrenadeEvent::grab_frag||out.event==g::ManualGrenadeEvent::grab_tactical)++transaction_;
    out.stage=gesture_.stage;out.slot=gesture_.held_slot;out.transaction=transaction_;out.updated_ms=c.now;
    out.generation=c.generation;out.tracking_pose=h.grip.pose;out.local_origin=local.position;out.local_forward=local.axis.forward;
    if(c.button_mode)out.local_forward=xr::OpenXrPoseToIwRelative(f.head_center,r.tracking_anchor,xr::kIwUnitsPerMeter).axis.forward;
    out.consume_trigger=out.stage!=g::ManualGrenadeStage::ready||(c.button_mode?button_reserved_:
        (zone!=g::ManualGrenadeSlot::none&&h.trigger.current>=g::kManualGrenadeTriggerRelease));
    out.valid=true;g::ManualGrenadePoint velocity{};std::uint64_t age{};
    if(g::select_manual_grenade_release_velocity(c.now*1000000,history_,&velocity,&age))out.local_velocity=vector(velocity);
    return out;
}
bool button_launch_velocity(xr::Vec3f forward,const g::ManualGrenadeLaunchCalibration& tuning,xr::Vec3f* out) noexcept {
    if(!out)return false;*out={};g::ManualGrenadeLaunchResult checked{};
    if(!g::build_manual_grenade_launch_velocity({},point(forward),tuning,&checked))return false;
    const float n=std::sqrt(forward.x*forward.x+forward.y*forward.y+forward.z*forward.z);
    const float speed=tuning.native_projectile_speed+tuning.native_projectile_speed_forward;
    if(!std::isfinite(n)||n<.001f||!std::isfinite(speed)||speed<=0)return false;
    const xr::Vec3f velocity{forward.x/n*speed,forward.y/n*speed,
                            forward.z/n*speed+tuning.native_projectile_speed_up};
    if(!finite(velocity))return false;*out=velocity;return true;
}
}
