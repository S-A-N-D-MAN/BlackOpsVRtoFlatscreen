// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/helicopter_context.hpp"

namespace bovr::t5::tunnel_crawl {
namespace rd=helicopter::detail;
using Host=helicopter::Host;
struct Facts {
    std::uintptr_t cg{},map{},name{},current{};
    helicopter::PlayerFacts player{};
    int game_time{},command_number{},clock{},catcher{},connection{},override_value{};
    std::uint32_t map_flags{},map_type{};
    std::array<char,8> map_name{},map_value{};
    std::array<int,7> camera{};
    std::array<float,4> clamp{};
    friend bool operator==(const Facts&,const Facts&)=default;
};
inline bool read_graph(const Host& h,Facts& f) noexcept {
    if(!rd::pointer(h,0x2ff5354,0,f.cg)||!rd::bounded(f.cg,0xa9d48)||
       !rd::player(h,f.cg+0x8a364,f.player))return false;
    const auto& p=f.player;
    if(p.pm_type!=1||p.health<=0||p.local||p.weapon||!(p.weapon_flags&0x80)||
       (p.pm_flags&0xc00)||(p.entity_flags&0x4300)||(p.entity_flags2&0x50040000)||p.viewlock||
       !rd::value(h,p.ps,0x1a0,f.clamp))return false;
    for(float value:f.clamp)if(!std::isfinite(value))return false;
    if(f.clamp[2]<=0||f.clamp[2]>.02f||f.clamp[3]<=0||f.clamp[3]>.02f)return false;
    constexpr std::array<std::uintptr_t,7> offsets{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
    for(unsigned i=0;i<offsets.size();++i)if(!rd::value(h,f.cg,offsets[i],f.camera[i]))return false;
    if(!native_first_person_camera(f.camera[0])||f.camera[1]||f.camera[2]||f.camera[3]||
       f.camera[4]==3||f.camera[5]||f.camera[6]!=1)return false;
    if(!rd::pointer(h,0x2899cec,0,f.map)||!rd::bounded(f.map,0x1c)||
       !rd::pointer(h,f.map,0,f.name)||!rd::pointer(h,f.map,0x18,f.current)||
       !rd::value(h,f.map,0xc,f.map_flags)||f.map_flags!=0x44||
       !rd::value(h,f.map,0x10,f.map_type)||f.map_type!=7||
       !rd::bytes(h,f.name,f.map_name.data(),8)||std::memcmp(f.map_name.data(),"mapname",8)||
       !rd::bytes(h,f.current,f.map_value.data(),8)||std::memcmp(f.map_value.data(),"creek_1",8))return false;
    return rd::value(h,f.cg,0x8a348,f.game_time)&&rd::value(h,0x2993850,0,f.command_number)&&
        rd::value(h,0x2481764,0,f.clock)&&rd::value(h,0x2910160,0,f.catcher)&&
        rd::value(h,0x2910164,0,f.connection)&&rd::value(h,0x2911e2c,0,f.override_value);
}
inline bool matches(const Facts& f,const PreCommandSnapshot& p) noexcept {
    const auto& a=f.player;const auto& c=p.context;
    return f.cg==p.native_cg_identity&&f.game_time==p.native_game_time&&a.health==p.native_health&&
        a.pm_type==p.native_pm_type&&a.pm_flags==p.native_pm_flags&&a.weapon_flags==p.native_weapon_flags&&
        a.entity_flags==p.native_entity_flags&&a.entity_flags2==p.native_entity_flags2&&
        f.command_number==c.command_number&&f.clock==c.native_frame_time&&f.catcher==c.key_catcher&&
        f.connection==c.connection_state&&f.override_value==static_cast<int>(c.native_override);
}
inline bool current(const Host& h,std::uintptr_t base,const PreCommandSnapshot& pre,
                    const CommandContext& c,std::uint64_t now) noexcept {
    if(base!=rd::image_base||!rd::request_current(h,pre,c,now))return false;
    PreCommandSnapshot first{},last{};Facts a{},b{};
    return h.pre(h.context,&first)&&rd::same_pre(first,pre)&&read_graph(h,a)&&matches(a,pre)&&
        read_graph(h,b)&&a==b&&matches(b,pre)&&h.pre(h.context,&last)&&rd::same_pre(first,last)&&
        h.thread(h.context)==c.native_thread;
}
inline bool current(std::uintptr_t base,const PreCommandSnapshot& p,const CommandContext& c,
                    std::uint64_t now) noexcept {
    const auto error=GetLastError();
    const bool ok=current({nullptr,rd::protected_copy,rd::native_pre,rd::native_thread},base,p,c,now);
    SetLastError(error);return ok;
}
}
