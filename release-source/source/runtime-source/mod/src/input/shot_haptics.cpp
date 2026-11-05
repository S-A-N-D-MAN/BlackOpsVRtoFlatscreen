// SPDX-License-Identifier: GPL-3.0-only
#include "shot_haptics.hpp"

namespace bovr::input {
namespace {
constexpr auto maximum_age=wawvr::mod::kFiringHapticMaxAgeMilliseconds;
bool fresh(std::uint64_t published,std::uint64_t now) {
    return published && now>=published && now-published<=maximum_age;
}
}
HapticPlan ShotHaptics::clear_locked() {
    HapticPlan plan{};plan.stop_mask=pulse_mask_;
    for(auto& mailbox:mailboxes_) wawvr::mod::reset_firing_haptic_mailbox(&mailbox);
    allowed_mask_=pulse_mask_=0;context_={};last_service_=0;
    return plan;
}
HapticPlan ShotHaptics::reset() {std::lock_guard lock(mutex_);return clear_locked();}
HapticPlan ShotHaptics::service(const HapticContext& value,std::uint64_t now) {
    std::lock_guard lock(mutex_);
    HapticPlan plan{};
    const bool valid=value.xr_generation && value.gameplay_epoch && value.sequence &&
        value.focused && value.gameplay && fresh(value.published_ms,now);
    const bool changed=context_.xr_generation &&
        (value.xr_generation!=context_.xr_generation || value.gameplay_epoch!=context_.gameplay_epoch);
    const bool regressed=last_service_ && (now<last_service_ ||
        (!changed && (value.sequence<context_.sequence || value.published_ms<context_.published_ms ||
          (value.sequence==context_.sequence && value.published_ms!=context_.published_ms))));
    if(!valid || regressed) return clear_locked();
    if(changed || (last_service_ && now-last_service_>maximum_age)) plan=clear_locked();
    // Duplicate samples can service/drain, but cannot extend the source age.
    context_=value;last_service_=now;
    unsigned allowed{};
    for(unsigned hand=0;hand<2;++hand) {
        const unsigned bit=1u<<hand;
        if(value.tracked[hand]) allowed|=bit;
        else {plan.stop_mask|=pulse_mask_&bit;pulse_mask_&=~bit;}
        wawvr::mod::set_firing_haptic_acceptance(&mailboxes_[hand],value.tracked[hand],now);
        plan.shots[hand]=wawvr::mod::consume_firing_haptic_events(&mailboxes_[hand],now);
        if(plan.shots[hand]) pulse_mask_|=bit;
    }
    allowed_mask_=allowed;
    return plan;
}
bool ShotHaptics::queue(const ConfirmedLocalShot& event,std::uint64_t now) {
    std::lock_guard lock(mutex_);
    if(!event.local_shot_confirmed || event.hand>=2 || !event.serial ||
       event.xr_generation!=context_.xr_generation || event.gameplay_epoch!=context_.gameplay_epoch ||
       !(allowed_mask_&(1u<<event.hand)) || !fresh(context_.published_ms,now) ||
       !fresh(last_service_,now) || !fresh(event.observed_ms,now) ||
       event.observed_ms<context_.published_ms || event.serial<=last_shot_serial_) return false;
    // Serial belongs to the verified native shot dispatch, not a trigger edge.
    // Keep the high-water mark across hand changes to reject duplicate routing.
    last_shot_serial_=event.serial;
    return wawvr::mod::queue_firing_haptic_event(&mailboxes_[event.hand],now);
}
}
