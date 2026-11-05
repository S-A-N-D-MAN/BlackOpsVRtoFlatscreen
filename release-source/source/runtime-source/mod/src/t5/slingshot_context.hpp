// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "xr_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bovr::t5::slingshot {
struct Facts {
    PreCommandSnapshot pre{};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f axis{};
    std::uintptr_t cg{}, refdef{};
    int native_time{}, refdef_time{}, primary{}, offhand{};
    float delta_angles[3]{}, clamp_base[2]{}, clamp_range[2]{};
};

struct RelativeAim {
    float controller_pitch{}, controller_yaw{};
    float pitch_offset{}, yaw_offset{};
    float desired_pitch{}, desired_yaw{};
    float command_pitch{}, command_yaw{};
};

inline bool finite(const wawvr::xr::Vec3f& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float normalize180(float value) noexcept {
    return std::remainder(value,360.f);
}

inline bool native_aim_valid(const Facts& f) noexcept {
    for(const auto value:f.delta_angles)if(!std::isfinite(value))return false;
    for(const auto value:f.clamp_base)if(!std::isfinite(value))return false;
    return std::isfinite(f.clamp_range[0]) && std::isfinite(f.clamp_range[1]) &&
        f.clamp_range[0]>0.f && f.clamp_range[0]<=90.f &&
        f.clamp_range[1]>0.f && f.clamp_range[1]<=180.f;
}

inline bool emplacement_volume(const Facts& f) noexcept {
    return finite(f.origin) && f.origin.x>=-2700.f && f.origin.x<=-2550.f &&
        f.origin.y>=4525.f && f.origin.y<=4610.f &&
        f.origin.z>=1450.f && f.origin.z<=1510.f;
}

// Vorkuta's rooftop slingshot is a scripted linked-player state rather than a
// weapon or turret.  Combine its exact player flags and inventory with the
// small physical camera volume around the single emplacement so other linked
// campaign scenes keep their native controls.
inline bool eligible(const Facts& f) noexcept {
    const auto& p=f.pre;
    return p.native_gameplay_fields_valid && p.native_cg_identity &&
        p.native_pm_type==1 && (p.native_pm_flags&0x00200000u)!=0 &&
        (p.native_weapon_flags&0x02000080u)==0x02000080u &&
        p.native_entity_flags==0x000a0002u && p.native_entity_flags2==0 &&
        f.cg==p.native_cg_identity && f.refdef==f.cg+0x8c100 &&
        f.native_time>0 && f.native_time==p.native_game_time &&
        f.refdef_time==f.native_time && f.primary==0 && f.offhand==13 &&
        emplacement_volume(f) && native_aim_valid(f);
}

// The script changes its predicted weapon tuple on the first command after the
// pause menu closes.  Once the exact entry tuple has admitted this occurrence,
// retain it only while the same campaign epoch remains linked to the same
// rooftop emplacement with the slingshot offhand and native aim limits.
inline bool retained(const Facts& f,std::uintptr_t admitted_cg,std::uint64_t admitted_epoch) noexcept {
    const auto& p=f.pre;
    // A restored developer save clears EF_TELEPORT_BIT (0x20000) on the first
    // predicted command after admission.  Both values were captured at this
    // same rooftop emplacement; retain across that one-bit transition while
    // every stronger identity guard below remains current.
    const bool current_entity_flags=p.native_entity_flags==0x000a0002u ||
        p.native_entity_flags==0x00080002u;
    return admitted_cg && admitted_epoch && p.native_gameplay_fields_valid &&
        f.cg==admitted_cg && f.cg==p.native_cg_identity &&
        p.gameplay_epoch==admitted_epoch && f.refdef==f.cg+0x8c100 &&
        f.native_time>0 && f.native_time==p.native_game_time && f.refdef_time==f.native_time &&
        p.native_pm_type==1 && (p.native_weapon_flags&0x80u)!=0 &&
        current_entity_flags && p.native_entity_flags2==0 &&
        f.offhand==13 && emplacement_volume(f) && native_aim_valid(f);
}

// Controller orientation is calibrated when the live slingshot occurrence
// begins.  Subsequent physical angular motion is applied one-for-one around
// the script's clamp centre.  Subtracting the current delta_angles produces
// the client angle that PM_UpdateViewAngles will reconstruct as desired_pitch
// and desired_yaw before applying the native clamp.
inline bool relative_aim(const Facts& f,float anchor_pitch,float anchor_yaw,
                         float controller_pitch,float controller_yaw,RelativeAim* out) noexcept {
    if(!out){return false;}*out={};
    if(!native_aim_valid(f)||!std::isfinite(anchor_pitch)||!std::isfinite(anchor_yaw)||
       !std::isfinite(controller_pitch)||!std::isfinite(controller_yaw))return false;
    RelativeAim value{};value.controller_pitch=controller_pitch;value.controller_yaw=controller_yaw;
    value.pitch_offset=std::clamp(normalize180(controller_pitch-anchor_pitch),-f.clamp_range[0],f.clamp_range[0]);
    value.yaw_offset=std::clamp(normalize180(controller_yaw-anchor_yaw),-f.clamp_range[1],f.clamp_range[1]);
    value.desired_pitch=normalize180(f.clamp_base[0])+value.pitch_offset;
    value.desired_yaw=normalize180(f.clamp_base[1])+value.yaw_offset;
    value.command_pitch=normalize180(value.desired_pitch-f.delta_angles[0]);
    value.command_yaw=normalize180(value.desired_yaw-f.delta_angles[1]);
    if(!std::isfinite(value.command_pitch)||!std::isfinite(value.command_yaw))return false;
    *out=value;return true;
}

inline bool same_current(const Facts& a,const Facts& b) noexcept {
    const bool same_axis=
        a.axis.forward.x==b.axis.forward.x && a.axis.forward.y==b.axis.forward.y && a.axis.forward.z==b.axis.forward.z &&
        a.axis.left.x==b.axis.left.x && a.axis.left.y==b.axis.left.y && a.axis.left.z==b.axis.left.z &&
        a.axis.up.x==b.axis.up.x && a.axis.up.y==b.axis.up.y && a.axis.up.z==b.axis.up.z;
    return a.pre.sequence==b.pre.sequence &&
        a.pre.gameplay_epoch==b.pre.gameplay_epoch &&
        a.pre.context.command_number==b.pre.context.command_number &&
        a.pre.context.native_frame_time==b.pre.context.native_frame_time &&
        a.cg==b.cg && a.refdef==b.refdef &&
        a.native_time==b.native_time && a.refdef_time==b.refdef_time &&
        a.primary==b.primary && a.offhand==b.offhand &&
        a.delta_angles[0]==b.delta_angles[0] && a.delta_angles[1]==b.delta_angles[1] && a.delta_angles[2]==b.delta_angles[2] &&
        a.clamp_base[0]==b.clamp_base[0] && a.clamp_base[1]==b.clamp_base[1] &&
        a.clamp_range[0]==b.clamp_range[0] && a.clamp_range[1]==b.clamp_range[1] &&
        a.origin.x==b.origin.x && a.origin.y==b.origin.y && a.origin.z==b.origin.z && same_axis;
}
}
