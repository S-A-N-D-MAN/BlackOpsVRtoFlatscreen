// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/native_camera_mode.hpp"
#include <cstdint>
#include <limits>

namespace bovr::t5::primary_weapon_tracking {
// Weapon eligibility only. This must not grant movement, cycling, reload or
// permission to bypass a native camera, firing or script override.
enum class Mode : std::uint8_t { rejected, ordinary, linked };
struct Player {
    int pm_type{-1},health{};
    std::uint32_t pm_flags{},weapon_flags{},entity_flags{},entity_flags2{};
    friend bool operator==(const Player&,const Player&)=default;
};
struct View {
    int camera_mode{-1},key_catcher{},connection_state{};
    bool native_override{},cubemap{},draw_suppressed{};
    friend bool operator==(const View&,const View&)=default;
};
struct Server {
    Player player{};
    // Established by the caller's protected local-entity/client ownership copy.
    bool local_identity{};
    int entity_health{},connected{},session{};
    std::uint32_t flags{},link{}; // pinned local entity+0x298, identity only
    friend bool operator==(const Server&,const Server&)=default;
};

constexpr Mode player_mode(const Player& p) noexcept {
    if(p.health<=0||(p.entity_flags&0x4300)||(p.entity_flags2&0x40040000))return Mode::rejected;
    // Black Ops can leave offhand-disable 0x100 set after a scripted breach
    // while the equipped primary weapon and first-person camera are active.
    // Weapon-update-disable 0x80 still rejects the primary weapon.
    if(p.pm_type==0)
        return !(p.pm_flags&0x800)&&!(p.weapon_flags&0x80)?Mode::ordinary:Mode::rejected;
    // Linked primary weapons still require native weapon updates and unfrozen
    // controls. Offhand-disable0x100 alone does not disable the equipped gun.
    if(p.pm_type==1)
        return !(p.pm_flags&0xc00)&&!(p.weapon_flags&0x80)?Mode::linked:Mode::rejected;
    return Mode::rejected;
}
constexpr Mode client_mode(const Player& p,const View& v) noexcept {
    if(!native_first_person_camera(v.camera_mode)||v.key_catcher||v.connection_state<9||
       v.native_override||v.cubemap||v.draw_suppressed)return Mode::rejected;
    return player_mode(p);
}
constexpr bool bounded_link(std::uint32_t link) noexcept {
    // The caller validates the owning native entity; this only bounds the copied
    // x86 pointer value. It does not dereference or claim to validate a parent.
    return link>=0x10000&&!(link&3)&&link<=(std::numeric_limits<std::uint32_t>::max)()-3;
}
constexpr Mode server_mode(const Server& s) noexcept {
    if(!s.local_identity||s.entity_health<=0||s.connected!=2||s.session||(s.flags&0x17))return Mode::rejected;
    const auto mode=player_mode(s.player);
    if(mode==Mode::ordinary)return !s.link?mode:Mode::rejected;
    if(mode==Mode::linked)return bounded_link(s.link)?mode:Mode::rejected;
    return Mode::rejected;
}
constexpr Mode paired_mode(Mode client,Mode server) noexcept {
    return client==server&&(client==Mode::ordinary||client==Mode::linked)?client:Mode::rejected;
}
constexpr bool same_link_mode(Mode before,std::uint32_t before_link,Mode after,std::uint32_t after_link) noexcept {
    if(before!=after||before_link!=after_link)return false;
    return before==Mode::ordinary?!before_link:before==Mode::linked&&bounded_link(before_link);
}
} // namespace bovr::t5::primary_weapon_tracking
