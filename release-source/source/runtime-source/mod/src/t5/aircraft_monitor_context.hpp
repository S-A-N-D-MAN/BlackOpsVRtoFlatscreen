// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace bovr::t5::aircraft_monitor {
struct Player {
    int pm{},health{};
    std::uint32_t pm_flags{},weapon_flags{},entity_flags{},entity_flags2{};
    std::uint8_t primary{};
    std::array<float,3> origin{};
    std::array<float,2> clamp_range{};
    friend bool operator==(const Player&,const Player&)=default;
};
struct Facts {
    std::uintptr_t map{},name{},current{},cg{},server{};
    std::uint32_t flags{},type{};
    std::array<char,8> map_name{};
    std::array<char,9> map_value{};
    Player predicted{},authoritative{};
    friend bool operator==(const Facts&,const Facts&)=default;
};
enum class CopyReason : unsigned {
    accepted,base,map_pointer,map_storage,map_identity,player_storage,
    predicted_role,authoritative_role,pre_join,exception
};
enum class ReadReason : unsigned { accepted,pre_invalid,first_copy,second_copy,changed };
struct Diagnostic {
    ReadReason reason{ReadReason::accepted};
    CopyReason first_reason{CopyReason::accepted},second_reason{CopyReason::accepted};
    Facts first{},second{};
};
inline unsigned player_difference(const Player& a,const Player& b) noexcept {
    return (a.pm!=b.pm?1u:0u)|(a.health!=b.health?2u:0u)|
        (a.pm_flags!=b.pm_flags?4u:0u)|(a.weapon_flags!=b.weapon_flags?8u:0u)|
        (a.entity_flags!=b.entity_flags?16u:0u)|(a.entity_flags2!=b.entity_flags2?32u:0u)|
        (a.primary!=b.primary?64u:0u)|(a.origin!=b.origin?128u:0u)|(a.clamp_range!=b.clamp_range?256u:0u);
}
inline unsigned pre_difference(const Player& p,const PreCommandSnapshot& pre) noexcept {
    return (p.pm!=pre.native_pm_type?1u:0u)|(p.health!=pre.native_health?2u:0u)|
        (p.pm_flags!=pre.native_pm_flags?4u:0u)|(p.weapon_flags!=pre.native_weapon_flags?8u:0u)|
        (p.entity_flags!=pre.native_entity_flags?16u:0u)|(p.entity_flags2!=pre.native_entity_flags2?32u:0u);
}
inline bool authoritative_role(const Player& p) noexcept {
    // After returning from infantry, the observed authoritative origin
    // alternates between cockpit and ground while the operator role remains.
    return p.pm==1&&p.health>0&&p.primary==0&&!(p.pm_flags&0x800)&&(p.weapon_flags&0x80)&&
        !(p.entity_flags&0x4300)&&!(p.entity_flags2&0x40040000)&&
        std::isfinite(p.origin[0])&&std::isfinite(p.origin[1])&&
        std::isfinite(p.origin[2])&&
        p.clamp_range[0]==0.f&&p.clamp_range[1]==0.f;
}
inline bool role(const Player& p) noexcept {
    // Prediction remains in the fully clamped airborne cockpit. This still
    // excludes boarding and infantry; the separately rendered feed is unused.
    return authoritative_role(p)&&p.origin[2]>10000.f;
}
inline bool same_authoritative_role(const Player& a,const Player& b) noexcept {
    return authoritative_role(a)&&authoritative_role(b)&&a.pm==b.pm&&a.health==b.health&&
        a.pm_flags==b.pm_flags&&a.weapon_flags==b.weapon_flags&&
        a.entity_flags==b.entity_flags&&a.entity_flags2==b.entity_flags2&&
        a.primary==b.primary&&a.clamp_range==b.clamp_range;
}
inline bool same_read(const Facts& a,const Facts& b) noexcept {
    // Only the server's validated finite origin may vary between reads. Map,
    // storage, predicted cockpit and all other server role fields stay exact.
    return a.map==b.map&&a.name==b.name&&a.current==b.current&&a.cg==b.cg&&a.server==b.server&&
        a.flags==b.flags&&a.type==b.type&&a.map_name==b.map_name&&a.map_value==b.map_value&&
        a.predicted==b.predicted&&same_authoritative_role(a.authoritative,b.authoritative);
}
inline void read_player(std::uintptr_t p,Player& v) {
    std::memcpy(&v.pm,reinterpret_cast<const void*>(p+4),4);
    std::memcpy(&v.health,reinterpret_cast<const void*>(p+0x1c4),4);
    std::memcpy(&v.pm_flags,reinterpret_cast<const void*>(p+0xc),4);
    std::memcpy(&v.weapon_flags,reinterpret_cast<const void*>(p+0x10),4);
    std::memcpy(&v.entity_flags,reinterpret_cast<const void*>(p+0xe0),4);
    std::memcpy(&v.entity_flags2,reinterpret_cast<const void*>(p+0xe4),4);
    std::memcpy(&v.primary,reinterpret_cast<const void*>(p+0x144),1);
    std::memcpy(v.origin.data(),reinterpret_cast<const void*>(p+0x24),12);
    std::memcpy(v.clamp_range.data(),reinterpret_cast<const void*>(p+0x1a8),8);
}
inline bool copy_once(std::uintptr_t base,const PreCommandSnapshot& pre,Facts& f,CopyReason* reason=nullptr) noexcept {
    const auto mark=[&](CopyReason value){if(reason)*reason=value;};
    mark(CopyReason::base);
    if(!base||base>UINT32_MAX-0x2bf5358)return false;
    __try {
        mark(CopyReason::map_pointer);
        std::memcpy(&f.map,reinterpret_cast<const void*>(base+0x2499cec),4);
        if(f.map<0x10000||f.map>UINT32_MAX-0x20)return false;
        mark(CopyReason::map_storage);
        std::memcpy(&f.name,reinterpret_cast<const void*>(f.map),4);
        std::memcpy(&f.flags,reinterpret_cast<const void*>(f.map+0xc),4);
        std::memcpy(&f.type,reinterpret_cast<const void*>(f.map+0x10),4);
        std::memcpy(&f.current,reinterpret_cast<const void*>(f.map+0x18),4);
        if(f.name<0x10000||f.name>UINT32_MAX-8||f.current<0x10000||f.current>UINT32_MAX-9)return false;
        std::memcpy(f.map_name.data(),reinterpret_cast<const void*>(f.name),8);
        std::memcpy(f.map_value.data(),reinterpret_cast<const void*>(f.current),9);
        mark(CopyReason::map_identity);
        if(f.flags!=0x44||f.type!=7||std::memcmp(f.map_name.data(),"mapname",8)||
           std::memcmp(f.map_value.data(),"wmd_sr71",9))return false;
        mark(CopyReason::player_storage);
        std::memcpy(&f.cg,reinterpret_cast<const void*>(base+0x2bf5354),4);
        std::memcpy(&f.server,reinterpret_cast<const void*>(base+0x1803140),4);
        if(f.cg!=pre.native_cg_identity||f.cg<0x10000||f.cg>UINT32_MAX-0x8a534||
           f.server<0x10000||f.server>UINT32_MAX-0x1c8)return false;
        read_player(f.cg+0x8a364,f.predicted);read_player(f.server,f.authoritative);
        mark(CopyReason::predicted_role);
        if(!role(f.predicted))return false;
        mark(CopyReason::authoritative_role);
        if(!authoritative_role(f.authoritative))return false;
        mark(CopyReason::pre_join);
        const bool matches=f.predicted.pm==pre.native_pm_type&&f.predicted.health==pre.native_health&&f.predicted.pm_flags==pre.native_pm_flags&&
            f.predicted.weapon_flags==pre.native_weapon_flags&&
            f.predicted.entity_flags==pre.native_entity_flags&&f.predicted.entity_flags2==pre.native_entity_flags2;
        if(matches)mark(CopyReason::accepted);
        return matches;
    } __except(EXCEPTION_EXECUTE_HANDLER){mark(CopyReason::exception);return false;}
}
// Caller joins the current native command/epoch and input freshness first.
inline bool current(std::uintptr_t base,const PreCommandSnapshot& pre,Diagnostic* diagnostic=nullptr) noexcept {
    Diagnostic d{};
    if(!pre.native_gameplay_fields_valid||!pre.native_cg_identity)d.reason=ReadReason::pre_invalid;
    else if(!copy_once(base,pre,d.first,&d.first_reason))d.reason=ReadReason::first_copy;
    else if(!copy_once(base,pre,d.second,&d.second_reason))d.reason=ReadReason::second_copy;
    else if(!same_read(d.first,d.second))d.reason=ReadReason::changed;
    if(diagnostic)*diagnostic=d;
    return d.reason==ReadReason::accepted;
}
}
