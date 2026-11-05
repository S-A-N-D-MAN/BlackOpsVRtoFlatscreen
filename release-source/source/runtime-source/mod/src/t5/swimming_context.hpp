// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/helicopter_context.hpp"

namespace bovr::t5::swimming {
using Host=helicopter::Host;
namespace rd=helicopter::detail;
enum class Locomotion { swimming, walking };
struct Dvar {
    std::uintptr_t address{},name{};
    std::uint32_t type{},bits{};
    std::array<char,32> name_text{};
    friend bool operator==(const Dvar&,const Dvar&)=default;
};
struct Facts {
    std::uintptr_t cg{};
    helicopter::PlayerFacts predicted{};
    int game_time{},refdef_time{},waterlevel{};
    int command_number{},command_clock{},catcher{},connection{},override_value{};
    std::array<float,3> delta_angles{};
    std::array<float,9> refdef_axis{}; // Native forward, left, up rows.
    std::array<int,7> camera{};
    std::array<Dvar,4> dvars{};
    float pitch_up_limit{},pitch_down_limit{};
    friend bool operator==(const Facts&,const Facts&)=default;
};
inline bool player_eligible(const helicopter::PlayerFacts& p) noexcept {
    // Swimming is PM_NORMAL plus native waterlevel; it has no separate PM flag.
    // DisableWeapons (0x80) and DisableOffhandWeapons (0x100) govern weapons,
    // not PM_SwimMove. The sampan approach intentionally sets 0x80 while the
    // player must swim and supply script-polled melee input. This reader grants
    // locomotion facts only, never firing, reload, weapon tracking or melee.
    // No weapon identity exception admits linked, mounted, frozen, mantling or
    // charging players.
    return rd::bounded(p.ps,0x560)&&p.pm_type==0&&p.health>0&&!p.local&&
        !(p.pm_flags&(0xc00u|4u|8u|0x20000u))&&
        !(p.entity_flags&0x4300u)&&!(p.entity_flags2&0x50040000u)&&!p.viewlock;
}
inline bool axis_valid(const std::array<float,9>& a) noexcept {
    for(float value:a)if(!std::isfinite(value))return false;
    const auto dot=[&](unsigned i,unsigned j){return a[i]*a[j]+a[i+1]*a[j+1]+a[i+2]*a[j+2];};
    const float determinant=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    return std::fabs(dot(0,0)-1.f)<.02f&&std::fabs(dot(3,3)-1.f)<.02f&&std::fabs(dot(6,6)-1.f)<.02f&&
        std::fabs(dot(0,3))<.02f&&std::fabs(dot(0,6))<.02f&&std::fabs(dot(3,6))<.02f&&determinant>.98f;
}
inline bool read_dvar(const Host& h,std::uintptr_t slot,const char* expected,std::uint32_t type,Dvar& d) noexcept {
    const auto size=std::strlen(expected)+1;
    return size<=d.name_text.size()&&rd::pointer(h,slot,0,d.address)&&rd::bounded(d.address,0x1c)&&
        rd::pointer(h,d.address,0,d.name)&&rd::value(h,d.address,0x10,d.type)&&d.type==type&&
        rd::value(h,d.address,0x18,d.bits)&&rd::bytes(h,d.name,d.name_text.data(),size)&&
        !std::memcmp(d.name_text.data(),expected,size);
}
inline bool read_graph(const Host& h,Facts& f,Locomotion mode=Locomotion::swimming) noexcept {
    // The 0xA9D48 extent covers the deepest camera getter; PS+0x55C requires
    // at least CG+0x8A8C4, beyond the older ladder-only reader's extent.
    if(!rd::pointer(h,0x2ff5354,0,f.cg)||!rd::bounded(f.cg,0xa9d48)||
       !rd::player(h,f.cg+0x8a364,f.predicted)||!player_eligible(f.predicted)||
       !rd::value(h,f.predicted.ps,0x55c,f.waterlevel)||
       (mode==Locomotion::swimming?(f.waterlevel<3||f.waterlevel>5):(f.waterlevel<0||f.waterlevel>2))||
       !rd::value(h,f.predicted.ps,0x84,f.delta_angles)||
       !rd::value(h,f.cg,0x8a348,f.game_time)||!rd::value(h,f.cg,0x8c158,f.refdef_time)||
       f.refdef_time!=f.game_time||!rd::value(h,f.cg,0x8c134,f.refdef_axis)||!axis_valid(f.refdef_axis))return false;
    for(float value:f.delta_angles)if(!std::isfinite(value)||std::fabs(value)>360000.f)return false;
    constexpr std::array<std::uintptr_t,7> camera_offsets{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
    for(unsigned i=0;i<camera_offsets.size();++i)if(!rd::value(h,f.cg,camera_offsets[i],f.camera[i]))return false;
    if(!native_first_person_camera(f.camera[0])||f.camera[1]||f.camera[2]||f.camera[3]||
       f.camera[4]==3||f.camera[5]||
       (mode==Locomotion::swimming?f.camera[6]:(f.camera[6]!=0&&f.camera[6]!=1)))return false;
    constexpr std::array<std::uintptr_t,4> slots{0xbcd150,0xbcd1b8,0xbcaf04,0xbcaebc};
    constexpr const char* names[]{"player_topDownCamMode","player_viewLockEnt","player_view_pitch_up","player_view_pitch_down"};
    for(unsigned i=0;i<4;++i)if(!read_dvar(h,slots[i],names[i],i<2?5u:1u,f.dvars[i]))return false;
    if(f.dvars[0].bits||f.dvars[1].bits)return false;
    std::memcpy(&f.pitch_up_limit,&f.dvars[2].bits,sizeof(float));
    std::memcpy(&f.pitch_down_limit,&f.dvars[3].bits,sizeof(float));
    if(!std::isfinite(f.pitch_up_limit)||!std::isfinite(f.pitch_down_limit)||
       f.pitch_up_limit<.2f||f.pitch_up_limit>90.f||f.pitch_down_limit<.2f||f.pitch_down_limit>90.f)return false;
    return rd::value(h,0x2993850,0,f.command_number)&&rd::value(h,0x2481764,0,f.command_clock)&&
        rd::value(h,0x2910160,0,f.catcher)&&rd::value(h,0x2910164,0,f.connection)&&
        rd::value(h,0x2911e2c,0,f.override_value);
}
inline bool matches(const Facts& f,const PreCommandSnapshot& p) noexcept {
    const auto& a=f.predicted;const auto& c=p.context;
    return f.cg==p.native_cg_identity&&f.game_time==p.native_game_time&&a.health==p.native_health&&
        a.pm_type==p.native_pm_type&&a.pm_flags==p.native_pm_flags&&a.weapon_flags==p.native_weapon_flags&&
        a.entity_flags==p.native_entity_flags&&a.entity_flags2==p.native_entity_flags2&&
        f.command_number==c.command_number&&f.command_clock==c.native_frame_time&&f.catcher==c.key_catcher&&
        f.connection==c.connection_state&&f.override_value==static_cast<int>(c.native_override);
}
inline bool copy_locomotion_current(const Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                         const CommandContext& c,std::uint64_t now,Facts* out,Locomotion mode) noexcept {
    if(!out)return false;*out={};
    // gameplay_active is primary-weapon eligibility, which DisableWeapons
    // deliberately clears. Validate actual movement/camera state below instead.
    if(base!=rd::image_base||!rd::request_current(h,pre,c,now))return false;
    PreCommandSnapshot first{},last{};Facts a{},b{};
    if(!h.pre(h.context,&first)||!rd::same_pre(first,pre)||!read_graph(h,a,mode)||!matches(a,pre)||
       !read_graph(h,b,mode)||a!=b||!matches(b,pre)||!h.pre(h.context,&last)||!rd::same_pre(first,last)||
       h.thread(h.context)!=c.native_thread)return false;
    *out=b;return true;
}
inline bool copy_current(const Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                         const CommandContext& c,std::uint64_t now,Facts* out) noexcept {
    return copy_locomotion_current(h,base,pre,c,now,out,Locomotion::swimming);
}
inline bool copy_walking_current(const Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                         const CommandContext& c,std::uint64_t now,Facts* out) noexcept {
    // Hiding/disabling the weapon in Creek's computer room does not disable
    // PM_NORMAL walking or camera turning. Native linked/frozen/camera modes
    // remain excluded; this reader never grants primary-weapon permission.
    return copy_locomotion_current(h,base,pre,c,now,out,Locomotion::walking);
}
inline bool copy_current(std::uintptr_t base,const PreCommandSnapshot& p,const CommandContext& c,
                         std::uint64_t now,Facts* out) noexcept {
    const auto error=GetLastError();
    const bool ok=copy_current({nullptr,rd::protected_copy,rd::native_pre,rd::native_thread},base,p,c,now,out);
    SetLastError(error);return ok;
}
inline bool copy_walking_current(std::uintptr_t base,const PreCommandSnapshot& p,const CommandContext& c,
                         std::uint64_t now,Facts* out) noexcept {
    const auto error=GetLastError();
    const bool ok=copy_walking_current({nullptr,rd::protected_copy,rd::native_pre,rd::native_thread},base,p,c,now,out);
    SetLastError(error);return ok;
}
}
