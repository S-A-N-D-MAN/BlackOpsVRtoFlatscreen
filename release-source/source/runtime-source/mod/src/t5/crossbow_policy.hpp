// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/primary_weapon_tracking.hpp"
#include "input/weapon_profile_admission.hpp"
#include <string_view>
namespace bovr::t5::crossbow {
constexpr bool weapon(int type,int weapon_class,int capacity,std::string_view name) noexcept {
    return type==2&&weapon_class==0&&capacity==1&&
        (name=="crossbow"||name.starts_with("crossbow_"));
}
constexpr bool reload_weapon_flags(std::uint32_t flags,bool recognized) noexcept {
    return !(flags&0x80)&&(!(flags&0x100)||recognized);
}
constexpr bool zipline_context(primary_weapon_tracking::Mode client,
    const primary_weapon_tracking::Server& server,std::uint32_t client_weapon_flags,
    int type,int weapon_class,int capacity,std::string_view name) noexcept {
    using namespace primary_weapon_tracking;
    return name=="crossbow_vzoom_alt_sp"&&weapon(type,weapon_class,capacity,name)&&
        client==Mode::linked&&paired_mode(client,server_mode(server))==Mode::linked&&
        (client_weapon_flags&0x300)==0x300&&(server.player.weapon_flags&0x300)==0x300;
}
constexpr bool reload_context(primary_weapon_tracking::Mode client,
    const primary_weapon_tracking::Server& server,bool crossbow_weapon) noexcept {
    using namespace primary_weapon_tracking;
    return crossbow_weapon&&client==Mode::linked&&paired_mode(client,server_mode(server))==Mode::linked;
}
inline input::profile_admission::NeutralSample rearm_sample(
    input::profile_admission::NeutralSample sample,bool recognized) noexcept {
    // Supporting the bow cannot shoot. Keep trigger, menu, tracking and
    // dominant-grip neutral requirements across the scripted handoff.
    if(recognized&&sample.squeeze_active[0]&&std::isfinite(sample.squeeze[0])&&
       sample.squeeze[0]>=0&&sample.squeeze[0]<=1)sample.squeeze[0]=0;
    return sample;
}
}
