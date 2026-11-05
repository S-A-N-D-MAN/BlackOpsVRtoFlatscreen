// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/helicopter_context.hpp"

namespace bovr::t5::guided_missile {
using Host=helicopter::Host;
namespace rd=helicopter::detail;
struct Facts {
    std::uintptr_t cg{},server{},missile{},variant{},definition{};
    helicopter::PlayerFacts predicted{},authoritative{};
    int number{},launch_time{},game_time{},command_number{},command_clock{},catcher{},connection{},override_value{},guidance{};
    std::uint16_t type{},owner{};
    std::uint8_t weapon{},inuse{};
    bool emplaced_tow{};
    std::uintptr_t name{},overlay{};
    std::array<char,29> internal_name{};
    int weapon_type{},weapon_class{};
    friend bool operator==(const Facts&,const Facts&)=default;
};
inline bool player_eligible(const helicopter::PlayerFacts& p,bool emplaced_tow=false) noexcept {
    return rd::bounded(p.ps,0x450)&&(p.pm_type==0||(emplaced_tow&&p.pm_type==1))&&p.health>0&&!p.local&&p.weapon&&
        !(p.pm_flags&0xc00)&&!(p.entity_flags&0x4300)&&!(p.entity_flags2&0x40000000)&&
        (p.entity_flags2&0x40000)&&(p.weapon_flags&0x200)&&!p.viewlock&&
        p.vehicle_entity_index>0&&p.vehicle_entity_index<1022;
}
inline bool read_emplaced_tow(const Host& h,Facts& f) noexcept {
    constexpr char expected[]="m220_tow_emplaced_khesanh_sp";
    static_assert(sizeof expected==sizeof f.internal_name);
    return rd::bounded(f.variant,0x90)&&rd::pointer(h,f.variant,0,f.name)&&
        rd::bounded(f.name,sizeof expected)&&rd::bytes(h,f.name,f.internal_name.data(),f.internal_name.size())&&
        !std::memcmp(f.internal_name.data(),expected,sizeof expected)&&
        rd::pointer(h,f.variant,0x8c,f.overlay)&&rd::bounded(f.overlay,4)&&
        rd::value(h,f.definition,0x1c,f.weapon_type)&&f.weapon_type==2&&
        rd::value(h,f.definition,0x20,f.weapon_class)&&f.weapon_class==7;
}
inline bool read_graph(const Host& h,Facts& f) noexcept {
    if(!rd::pointer(h,0x2ff5354,0,f.cg)||!rd::bounded(f.cg,0xa9d48)||
       !rd::pointer(h,0x1c03140,0,f.server)||!rd::player(h,f.cg+0x8a364,f.predicted)||
       !rd::player(h,f.server,f.authoritative)||!player_eligible(f.predicted,true)||
       !player_eligible(f.authoritative,true)||f.predicted.weapon!=f.authoritative.weapon||
       f.predicted.vehicle_entity_index!=f.authoritative.vehicle_entity_index)return false;
    const auto index=f.predicted.vehicle_entity_index;
    f.missile=rd::entities+static_cast<std::uintptr_t>(index)*rd::entity_stride;
    std::uintptr_t local_client{};int player_number{};
    // SP entityState type is a short at BE, weapon a byte at D4; the owner
    // handle at 134 is one-based. Require the actual local player's TV missile.
    if(!rd::value(h,f.missile,0,f.number)||f.number!=index||
       !rd::value(h,f.missile,0xbe,f.type)||f.type!=4||
       !rd::value(h,f.missile,0xdd,f.inuse)||f.inuse!=1||
       !rd::value(h,f.missile,0x134,f.owner)||f.owner!=1||
       !rd::value(h,rd::entities,0,player_number)||player_number!=0||
       !rd::pointer(h,rd::entities,0x13c,local_client)||local_client!=f.server||
       !rd::value(h,f.missile,0xd4,f.weapon)||!f.weapon||
       !rd::value(h,f.missile,0x340,f.launch_time)||f.launch_time<=0||
       !rd::pointer(h,0xbe19a8,4u*f.weapon,f.variant)||!rd::bounded(f.variant,12)||
       !rd::pointer(h,f.variant,8,f.definition)||!rd::bounded(f.definition,0x66c)||
       !rd::value(h,f.definition,0x668,f.guidance)||f.guidance!=6)return false;
    // Launching a scoped TOW stops turret use and links its missile, while the
    // script may retain knife_sp and the truck's linked movement state. Only
    // this exact native missile descriptor may differ from the held weapon.
    f.emplaced_tow=f.weapon!=f.predicted.weapon||f.predicted.pm_type!=0||f.authoritative.pm_type!=0;
    if((f.emplaced_tow&&!read_emplaced_tow(h,f))||!player_eligible(f.predicted,f.emplaced_tow)||
       !player_eligible(f.authoritative,f.emplaced_tow))return false;
    return rd::value(h,f.cg,0x8a348,f.game_time)&&rd::value(h,0x2993850,0,f.command_number)&&
        rd::value(h,0x2481764,0,f.command_clock)&&rd::value(h,0x2910160,0,f.catcher)&&
        rd::value(h,0x2910164,0,f.connection)&&rd::value(h,0x2911e2c,0,f.override_value);
}
inline bool matches(const Facts& f,const PreCommandSnapshot& p) noexcept {
    const auto& a=f.predicted;const auto& c=p.context;
    return f.cg==p.native_cg_identity&&f.game_time==p.native_game_time&&a.health==p.native_health&&
        a.pm_type==p.native_pm_type&&a.pm_flags==p.native_pm_flags&&a.weapon_flags==p.native_weapon_flags&&
        a.entity_flags==p.native_entity_flags&&a.entity_flags2==p.native_entity_flags2&&
        f.command_number==c.command_number&&f.command_clock==c.native_frame_time&&f.catcher==c.key_catcher&&
        f.connection==c.connection_state&&f.override_value==static_cast<int>(c.native_override);
}
inline bool same_flight(const Facts& a,const Facts& b) noexcept {
    return a.cg==b.cg&&a.server==b.server&&a.missile==b.missile&&a.launch_time==b.launch_time&&
        a.weapon==b.weapon&&a.variant==b.variant&&a.definition==b.definition&&a.emplaced_tow==b.emplaced_tow;
}
inline bool copy_current(const Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                         const CommandContext& c,std::uint64_t now,Facts* out) noexcept {
    if(!out)return false;*out={};
    if(base!=rd::image_base||!rd::request_current(h,pre,c,now))return false;
    PreCommandSnapshot first{},last{};Facts a{},b{};
    if(!h.pre(h.context,&first)||!rd::same_pre(first,pre)||!read_graph(h,a)||!matches(a,pre)||
       !read_graph(h,b)||a!=b||!matches(b,pre)||!h.pre(h.context,&last)||!rd::same_pre(first,last)||
       h.thread(h.context)!=c.native_thread)return false;
    *out=b;return true;
}
inline bool copy_current(std::uintptr_t base,const PreCommandSnapshot& p,const CommandContext& c,
                         std::uint64_t now,Facts* out) noexcept {
    const auto error=GetLastError();
    const bool ok=copy_current({nullptr,rd::protected_copy,rd::native_pre,rd::native_thread},base,p,c,now,out);
    SetLastError(error);return ok;
}
}
