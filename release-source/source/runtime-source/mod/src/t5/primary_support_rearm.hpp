// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/primary_weapon_tracking.hpp"
#include "t5/weapon_command_profile.hpp"

namespace bovr::t5::primary_support_rearm {
// Called only after the exact command-profile reader succeeds for this current
// occurrence. This copied-value predicate supplies no native ownership proof.
inline bool single_primary(const command_profile::Copy& profile) noexcept {
    if(!input::profile_admission::valid(profile.profile)||profile.native.dual_wield||
       profile.native.weapon_type!=0||profile.native.weapon_class<0||profile.native.weapon_class>=9||
       profile.native.fire_type<0||profile.native.fire_type>6)return false;
    using primary_weapon_tracking::Mode;
    const auto mode=profile.identity.tracking_mode;
    return (mode==static_cast<unsigned>(Mode::ordinary)&&!profile.identity.server_link)||
        (mode==static_cast<unsigned>(Mode::linked)&&primary_weapon_tracking::bounded_link(profile.identity.server_link));
}
inline input::profile_admission::NeutralSample sample(
    input::profile_admission::NeutralSample value,const command_profile::Copy& profile,
    bool dominant_idle=false) noexcept {
    // Holding the foregrip cannot fire a single primary gun. Exempt that one
    // analog value from AIM admission only; trigger, menu and tracking checks
    // remain intact, as does dominant-grip neutral by default. Routing is unchanged.
    // Unknown, inactive and invalid support input must not become neutral.
    if(single_primary(profile)&&value.squeeze_active[0]&&std::isfinite(value.squeeze[0])&&
       value.squeeze[0]>=0&&value.squeeze[0]<=1)value.squeeze[0]=0;
    // A held alternate-mode grip cannot fire an idle ordinary single gun.
    // This optional exemption affects AIM neutral observation only. The caller
    // must freshly exclude reload/grenade/button actions; native alternate-mode
    // routing keeps its independent edge/rearm requirements. Never extend this
    // to linked scenes, dual pistols, projectile launchers or a melee animation.
    if(dominant_idle&&single_primary(profile)&&profile.native.weapon_class<=4&&
       profile.identity.tracking_mode==static_cast<unsigned>(primary_weapon_tracking::Mode::ordinary)&&
       !profile.identity.server_link&&!profile.native.weapon_state&&!profile.native.other_state&&
       value.squeeze_active[1]&&std::isfinite(value.squeeze[1])&&value.squeeze[1]>=0&&value.squeeze[1]<=1){
        auto candidate=value;candidate.squeeze[1]=0;
        // Both trigger channels, focus, tracking and menu must be genuinely
        // neutral. A held trigger never becomes neutral through this policy.
        if(input::profile_admission::neutral(candidate))value=candidate;
    }
    return value;
}
} // namespace bovr::t5::primary_support_rearm
