// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "t5/native_camera_mode.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace bovr::t5::helicopter {
// The PBR boat and Hind share the native local-driver command/aim path.
inline constexpr bool supported_driver_type(int type) noexcept { return type==4||type==6; }
// Read-only identity, not permission to alter native vehicle/script flags.
// Startup code must separately verify the pinned retail instruction ranges.
struct Host {
    void* context{};
    bool (*copy)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
    bool (*pre)(void*,PreCommandSnapshot*) noexcept{};
    DWORD (*thread)(void*) noexcept{};
};
struct PlayerFacts {
    std::uintptr_t ps{};
    int pm_type{},health{},vehicle_pos{},vehicle_type{};
    std::uint32_t pm_flags{},weapon_flags{},entity_flags{},entity_flags2{},viewlock{};
    std::int16_t vehicle_entity_index{};
    std::uint8_t local{},weapon{};
    friend bool operator==(const PlayerFacts&,const PlayerFacts&)=default;
};
struct MapIdentity {
    std::uintptr_t dvar{},name{},current{};
    std::uint32_t flags{},type{};
    std::array<char,8> name_text{};
    std::array<char,15> value_text{};
    friend bool operator==(const MapIdentity&,const MapIdentity&)=default;
};
struct Facts {
    std::uintptr_t cg{},server{},player_entity{},vehicle_entity{},scr_vehicle{},info{};
    int entity_index{},info_index{},vehicle_number{},occupant{},player_number{},info_type{},game_time{};
    std::uint32_t vehicle_flags{};
    PlayerFacts predicted{},authoritative{};
    // Redemption intentionally disables handheld weapons while the local
    // Huey driver still operates the vehicle and its script-polled weapons.
    bool scripted_huey{};
    MapIdentity map{};
    // cg offsets 8A360, C, A9D44, 1C, 14, A4780, A4680.
    std::array<int,7> camera{};
    int command_number{},command_clock{},catcher{},connection{},override_value{};
    friend bool operator==(const Facts&,const Facts&)=default;
};
namespace detail {
constexpr std::uintptr_t image_base=0x400000,entities=0x1a796f8,entity_stride=0x34c;
constexpr std::uintptr_t infos=0x1cc1480,info_stride=0x1dc8;
inline bool bounded(std::uintptr_t p,std::size_t size) noexcept {
    return p>=0x10000&&p<=UINT32_MAX&&size&&size<=UINT32_MAX&&size-1<=UINT32_MAX-p;
}
inline bool player_eligible(const PlayerFacts& p,bool scripted_huey=false) noexcept {
    return bounded(p.ps,0x450)&&p.pm_type==1&&p.health>0&&!p.local&&!p.weapon&&
        !(p.pm_flags&0xc00)&&!(p.weapon_flags&(scripted_huey&&p.vehicle_type==6?0x100u:0x180u))&&
        (p.entity_flags&0x4000)&&!(p.entity_flags&0x300)&&
        !(p.entity_flags2&0x50040000)&&!p.viewlock&&!p.vehicle_pos&&supported_driver_type(p.vehicle_type)&&
        p.vehicle_entity_index>=0&&p.vehicle_entity_index<=1021;
}
inline bool same_command(const CommandContext& a,const CommandContext& b) noexcept {
    return a.valid==b.valid&&a.local_client==b.local_client&&a.command_number==b.command_number&&
        a.native_frame_time==b.native_frame_time&&a.native_thread==b.native_thread&&
        a.key_catcher==b.key_catcher&&a.connection_state==b.connection_state&&a.native_override==b.native_override&&
        !std::memcmp(a.viewangles,b.viewangles,sizeof(a.viewangles))&&
        !std::memcmp(a.kickangles,b.kickangles,sizeof(a.kickangles));
}
inline bool same_pre(const PreCommandSnapshot& a,const PreCommandSnapshot& b) noexcept {
    return same_command(a.context,b.context)&&a.sequence==b.sequence&&a.observed_ms==b.observed_ms&&
        a.gameplay_epoch==b.gameplay_epoch&&a.native_cg_identity==b.native_cg_identity&&
        a.native_game_time==b.native_game_time&&a.native_health==b.native_health&&
        a.native_pm_type==b.native_pm_type&&a.native_pm_flags==b.native_pm_flags&&
        a.native_weapon_flags==b.native_weapon_flags&&a.native_entity_flags==b.native_entity_flags&&
        a.native_entity_flags2==b.native_entity_flags2&&a.native_gameplay_fields_valid==b.native_gameplay_fields_valid&&
        a.gameplay_active==b.gameplay_active&&a.linked_weapon_active==b.linked_weapon_active;
}
inline bool request_current(const Host& h,const PreCommandSnapshot& p,const CommandContext& c,std::uint64_t now) noexcept {
    if(!h.copy||!h.pre||!h.thread||!c.valid||c.local_client||!c.native_thread||h.thread(h.context)!=c.native_thread||
       c.command_number<0||c.native_frame_time<0||c.key_catcher||c.native_override||c.connection_state<9||
       !p.sequence||p.sequence==UINT64_MAX||!p.gameplay_epoch||p.gameplay_epoch==UINT64_MAX||
       !p.observed_ms||now<p.observed_ms||now-p.observed_ms>100||!p.native_gameplay_fields_valid||
       p.native_game_time<=0||!same_command(p.context,c))return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(c.viewangles[i])||!std::isfinite(c.kickangles[i]))return false;
    return true;
}
inline bool bytes(const Host& h,std::uintptr_t at,void* out,std::size_t size) noexcept {
    return out&&bounded(at,size)&&h.copy(h.context,at,out,size);
}
template<class T> inline bool value(const Host& h,std::uintptr_t base,std::uintptr_t offset,T& out) noexcept {
    return bounded(base,sizeof(T))&&offset<=UINT32_MAX-base&&bytes(h,base+offset,&out,sizeof(T));
}
inline bool pointer(const Host& h,std::uintptr_t base,std::uintptr_t offset,std::uintptr_t& out) noexcept {
    std::uint32_t p{};
    if(!value(h,base,offset,p))return false;
    out=p;return true;
}
inline bool underwaterbase(const MapIdentity& m) noexcept {
    return bounded(m.dvar,0x1c)&&bounded(m.name,m.name_text.size())&&bounded(m.current,m.value_text.size())&&
        m.flags==0x44&&m.type==7&&!std::memcmp(m.name_text.data(),"mapname",m.name_text.size())&&
        !std::memcmp(m.value_text.data(),"underwaterbase",m.value_text.size());
}
inline bool read_underwaterbase(const Host& h,MapIdentity& m) noexcept {
    return pointer(h,0x2899cec,0,m.dvar)&&bounded(m.dvar,0x1c)&&
        pointer(h,m.dvar,0,m.name)&&value(h,m.dvar,0xc,m.flags)&&value(h,m.dvar,0x10,m.type)&&
        pointer(h,m.dvar,0x18,m.current)&&bytes(h,m.name,m.name_text.data(),m.name_text.size())&&
        bytes(h,m.current,m.value_text.data(),m.value_text.size())&&underwaterbase(m);
}
inline bool player(const Host& h,std::uintptr_t ps,PlayerFacts& p) noexcept {
    if(!bounded(ps,0x450))return false;
    p.ps=ps;
    return value(h,ps,4,p.pm_type)&&value(h,ps,0xc,p.pm_flags)&&value(h,ps,0x10,p.weapon_flags)&&
        value(h,ps,0xe0,p.entity_flags)&&value(h,ps,0xe4,p.entity_flags2)&&value(h,ps,0x130,p.local)&&
        value(h,ps,0x144,p.weapon)&&value(h,ps,0x1c4,p.health)&&value(h,ps,0x440,p.viewlock)&&
        value(h,ps,0x444,p.vehicle_entity_index)&&value(h,ps,0x448,p.vehicle_pos)&&value(h,ps,0x44c,p.vehicle_type);
}
inline bool disjoint(std::uintptr_t a,std::size_t as,std::uintptr_t b,std::size_t bs) noexcept {
    return bounded(a,as)&&bounded(b,bs)&&
        (static_cast<std::uint64_t>(a)+as<=b||static_cast<std::uint64_t>(b)+bs<=a);
}
inline bool graph_distinct(const Facts& f) noexcept {
    const std::array<std::uintptr_t,6> p{f.predicted.ps,f.server,f.player_entity,f.vehicle_entity,f.scr_vehicle,f.info};
    constexpr std::array<std::size_t,6> sizes{0x450,0x450,0x14c,0x14c,0x624,0x42};
    for(unsigned i=0;i<p.size();++i)for(unsigned j=0;j<i;++j)
        if(!disjoint(p[i],sizes[i],p[j],sizes[j]))return false;
    return true;
}
inline bool read_graph(const Host& h,Facts& f) noexcept {
    if(!pointer(h,0x2ff5354,0,f.cg)||!bounded(f.cg,0xa9d48)||
       !pointer(h,0x1c03140,0,f.server)||!bounded(f.server,0x450))return false;
    f.player_entity=entities;
    std::uintptr_t local_server{};
    if(!pointer(h,f.player_entity,0x13c,local_server)||local_server!=f.server||
       !value(h,f.player_entity,0,f.player_number)||f.player_number!=0||
       !player(h,f.cg+0x8a364,f.predicted)||!player(h,f.server,f.authoritative)||
       !player_eligible(f.predicted,true)||!player_eligible(f.authoritative,true))return false;
    f.entity_index=f.predicted.vehicle_entity_index;
    if(f.authoritative.vehicle_entity_index!=f.entity_index)return false;
    f.vehicle_entity=entities+static_cast<std::uintptr_t>(f.entity_index)*entity_stride;
    if(!value(h,f.vehicle_entity,0,f.vehicle_number)||f.vehicle_number!=f.entity_index||
       !pointer(h,f.vehicle_entity,0x148,f.scr_vehicle)||!bounded(f.scr_vehicle,0x624))return false;
    std::int16_t index{},type{};
    if(!value(h,f.scr_vehicle,0x1ec,index)||index<0||index>=32)return false;
    f.info_index=index;f.info=infos+static_cast<std::uintptr_t>(index)*info_stride;
    if(!value(h,f.info,0x40,type)||!value(h,f.scr_vehicle,0x1f0,f.vehicle_flags)||
       !value(h,f.scr_vehicle,0x620,f.occupant))return false;
    f.info_type=type;
    f.scripted_huey=false;f.map={};
    MapIdentity map{};
    const bool huey=f.info_type==6&&f.predicted.vehicle_type==6&&f.authoritative.vehicle_type==6&&
        read_underwaterbase(h,map);
    // Ordinary Hind/boat admission does not depend on a map dvar. The extra
    // disabled-weapon/path authority is granted only by an exact map copy.
    if((((f.predicted.weapon_flags|f.authoritative.weapon_flags)&0x80)||(f.vehicle_flags&0x80))&&!huey)return false;
    if(huey){f.scripted_huey=true;f.map=map;}
    if(!player_eligible(f.predicted,f.scripted_huey)||!player_eligible(f.authoritative,f.scripted_huey))return false;
    // A zero occupant is the native local entity0, already joined to server.
    if(f.occupant!=f.player_number||!graph_distinct(f))return false;
    constexpr std::array<std::uintptr_t,7> camera_offsets{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
    for(unsigned i=0;i<camera_offsets.size();++i)if(!value(h,f.cg,camera_offsets[i],f.camera[i]))return false;
    return value(h,f.cg,0x8a348,f.game_time)&&value(h,0x2993850,0,f.command_number)&&
        value(h,0x2481764,0,f.command_clock)&&value(h,0x2910160,0,f.catcher)&&
        value(h,0x2910164,0,f.connection)&&value(h,0x2911e2c,0,f.override_value);
}
inline bool matches(const Facts& f,const PreCommandSnapshot& p) noexcept {
    const auto& a=f.predicted;const auto& c=p.context;
    return f.cg==p.native_cg_identity&&f.game_time==p.native_game_time&&a.health==p.native_health&&
        a.pm_type==p.native_pm_type&&a.pm_flags==p.native_pm_flags&&a.weapon_flags==p.native_weapon_flags&&
        a.entity_flags==p.native_entity_flags&&a.entity_flags2==p.native_entity_flags2&&
        f.command_number==c.command_number&&f.command_clock==c.native_frame_time&&f.catcher==c.key_catcher&&
        f.connection==c.connection_state&&f.override_value==static_cast<int>(c.native_override);
}
inline bool protected_copy(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {
    if(!out||!bounded(at,size))return false;
    __try {std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool native_pre(void*,PreCommandSnapshot* p) noexcept {return read_pre_command_snapshot(p);}
inline DWORD native_thread(void*) noexcept {return GetCurrentThreadId();}
}
inline bool eligible(const Facts& f) noexcept {
    if((f.scripted_huey&&(f.info_type!=6||!detail::underwaterbase(f.map)))||
       !detail::bounded(f.cg,0xa9d48)||!detail::player_eligible(f.predicted,f.scripted_huey)||
       !detail::player_eligible(f.authoritative,f.scripted_huey)||
       f.predicted.ps!=f.cg+0x8a364||f.authoritative.ps!=f.server||f.player_entity!=detail::entities||f.player_number||
       f.entity_index<0||f.entity_index>1021||f.predicted.vehicle_entity_index!=f.entity_index||
       f.authoritative.vehicle_entity_index!=f.entity_index||f.vehicle_number!=f.entity_index||
       f.vehicle_entity!=detail::entities+static_cast<std::uintptr_t>(f.entity_index)*detail::entity_stride||
       f.info_index<0||f.info_index>=32||f.info!=detail::infos+static_cast<std::uintptr_t>(f.info_index)*detail::info_stride||
       !supported_driver_type(f.info_type)||f.predicted.vehicle_type!=f.info_type||
       f.authoritative.vehicle_type!=f.info_type||f.occupant||!(f.vehicle_flags&1)||
       (f.vehicle_flags&(f.scripted_huey?0x102u:0x182u))||!detail::graph_distinct(f)||
       f.game_time<=0||f.command_number<0||f.command_clock<0||f.catcher||f.connection<9||f.override_value)return false;
    if(!native_first_person_camera(f.camera[0])||f.camera[4]==3||f.camera[5]||f.camera[6]!=2)return false;
    for(unsigned i=1;i<4;++i)if(f.camera[i])return false;
    return true;
}
inline bool flight_control_allowed(const Facts& f) noexcept {
    // Keep native scripted flight until ReturnPlayerControl clears the path bit.
    // Aiming and script buttons may be admitted earlier; no native flags change.
    return eligible(f)&&!(f.vehicle_flags&0x80);
}
inline bool same_vehicle(const Facts& a,const Facts& b) noexcept {
    return eligible(a)&&eligible(b)&&a.cg==b.cg&&a.server==b.server&&a.player_entity==b.player_entity&&
        a.vehicle_entity==b.vehicle_entity&&a.scr_vehicle==b.scr_vehicle&&a.info==b.info&&
        a.entity_index==b.entity_index&&a.info_index==b.info_index&&a.info_type==b.info_type&&a.occupant==b.occupant&&
        a.scripted_huey==b.scripted_huey&&a.map==b.map&&
        (a.vehicle_flags&0x183)==(b.vehicle_flags&0x183);
}
inline bool copy_current(const Host& host,std::uintptr_t base,const PreCommandSnapshot& pre,
                         const CommandContext& context,std::uint64_t now,Facts* out) noexcept {
    if(!out)return false;*out={};
    if(base!=detail::image_base||!detail::request_current(host,pre,context,now))return false;
    PreCommandSnapshot first{},last{};Facts a{},b{};
    if(!host.pre(host.context,&first)||!detail::same_pre(first,pre)||
       !detail::read_graph(host,a)||!eligible(a)||!detail::matches(a,pre)||
       !detail::read_graph(host,b)||a!=b||!eligible(b)||!detail::matches(b,pre)||
       !host.pre(host.context,&last)||!detail::same_pre(first,last)||
       host.thread(host.context)!=context.native_thread)return false;
    *out=b;return true;
}
inline bool copy_current(std::uintptr_t base,const PreCommandSnapshot& pre,const CommandContext& context,
                         std::uint64_t now,Facts* out) noexcept {
    const auto error=GetLastError();
    const bool result=copy_current({nullptr,detail::protected_copy,detail::native_pre,detail::native_thread},
                                   base,pre,context,now,out);
    SetLastError(error);return result;
}
} // namespace bovr::t5::helicopter
