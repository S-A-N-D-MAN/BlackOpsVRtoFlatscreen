// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/support_script_aim.hpp"
#include "input/native_use_gaze.hpp"
#include <array>
#include <cstring>

namespace bovr::t5::native_use_gaze {
namespace xr=wawvr::xr;
using Copy=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Host {void* context{};Copy copy{};};
struct Call {
    std::uintptr_t returned{},entity{},forward{},right{},up{},stack{},low{},high{};
    DWORD thread{};
};
struct Owner {
    std::uint32_t cg{},server{},topdown{},link{};
    primary_weapon_tracking::Player client{},player{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server session{};
    int client_time{},server_time{},scene_time{},refdef_time{};
    std::uint8_t client_local{},server_local{};
    std::array<float,3> client_camera{},server_camera{};
};
inline bool bounded(std::uintptr_t p,std::size_t size) noexcept {
    return p>=0x10000&&p<=UINT32_MAX&&size&&size<=UINT32_MAX-p;
}
inline bool bytes(const Host& h,std::uintptr_t p,void* out,std::size_t size) noexcept {
    return h.copy&&out&&bounded(p,size)&&h.copy(h.context,p,out,size);
}
template<class T>bool get(const Host& h,std::uintptr_t p,T& out) noexcept {return bytes(h,p,&out,sizeof(out));}
inline bool player(const Host& h,std::uintptr_t p,primary_weapon_tracking::Player& o) noexcept {
    return bounded(p,0x46c)&&get(h,p+4,o.pm_type)&&get(h,p+0x1c4,o.health)&&
        get(h,p+0xc,o.pm_flags)&&get(h,p+0x10,o.weapon_flags)&&
        get(h,p+0xe0,o.entity_flags)&&get(h,p+0xe4,o.entity_flags2);
}
inline bool copy_owner(const Host& h,const Call& c,const PreCommandSnapshot& pre,std::uint64_t now,Owner* out) noexcept {
    if(!out||c.returned!=0x817e32||c.entity!=0x1a796f8||c.right||c.up||!c.thread||
       c.stack%4||c.stack<c.low||c.high<c.stack||c.high-c.stack<0xa4||
       !bounded(c.stack,0xa4)||c.forward!=c.stack+0x98||
       !pre.sequence||!pre.gameplay_epoch||!pre.native_gameplay_fields_valid||!pre.gameplay_active||
       !support_weapon::script_pre_current(pre.observed_ms,now)||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9)return false;
    std::array<std::uint32_t,5> args{};DWORD main{},server_thread{};
    if(!get(h,c.stack,args)||args[0]!=c.returned||args[1]!=c.entity||args[2]!=c.forward||args[3]||args[4]||
       !get(h,0x251ca98,main)||!get(h,0x251cac0,server_thread)||
       (c.thread!=main&&c.thread!=server_thread))return false;
    Owner o{};std::uint32_t server_root{};int number{},cubemap{},suppressed{},override_value{};
    if(!get(h,0x2ff5354,o.cg)||!bounded(o.cg,0xa9d48)||o.cg!=pre.native_cg_identity||
       !get(h,c.entity+0x13c,o.server)||!bounded(o.server,0x1c10)||
       !get(h,0x1c03140,server_root)||server_root!=o.server||!get(h,c.entity,number)||number)return false;
    const auto ps=o.cg+0x8a364;
    if(!player(h,ps,o.client)||!player(h,o.server,o.player)||
       !get(h,ps+0x130,o.client_local)||!get(h,o.server+0x130,o.server_local)||o.client_local||o.server_local||
       !get(h,o.cg+0x8a360,o.view.camera_mode)||!get(h,0x2910160,o.view.key_catcher)||
       !get(h,0x2910164,o.view.connection_state)||!get(h,0x2911e2c,override_value)||
       !get(h,o.cg+0xc,cubemap)||!get(h,o.cg+0xa9d44,suppressed))return false;
    o.view.native_override=override_value!=0;o.view.cubemap=cubemap!=0;o.view.draw_suppressed=suppressed!=0;
    o.session.player=o.player;o.session.local_identity=true;
    if(!get(h,c.entity+0x184,o.session.entity_health)||!get(h,o.server+0x1a2c,o.session.connected)||
       !get(h,o.server+0x1a18,o.session.session)||!get(h,o.server+0x1c0c,o.session.flags)||
       !get(h,c.entity+0x298,o.session.link))return false;
    namespace p=primary_weapon_tracking;
    if(p::paired_mode(p::client_mode(o.client,o.view),p::server_mode(o.session))!=p::Mode::ordinary)return false;
    // Native BG_GetPlayerViewDirection composes this extra camera rotation.
    // A scripted camera retains its own direction instead of this exception.
    if(!get(h,ps+0x464,o.client_camera)||!get(h,o.server+0x464,o.server_camera)||
       o.client_camera!=std::array<float,3>{}||o.server_camera!=std::array<float,3>{})return false;
    int type{},value{};std::uint32_t name{};constexpr char expected[]="player_topDownCamMode";char actual[sizeof expected]{};
    if(!get(h,0xbcd150,o.topdown)||!bounded(o.topdown,0x1c)||!get(h,o.topdown+0x10,type)||type!=5||
       !get(h,o.topdown+0x18,value)||value||!get(h,o.topdown,name)||
       !bytes(h,name,actual,sizeof actual)||std::memcmp(actual,expected,sizeof actual))return false;
    if(!get(h,ps,o.client_time)||!get(h,o.server,o.server_time)||
       o.client_time<=0||o.server_time<=0||std::abs(static_cast<long long>(o.client_time)-o.server_time)>250||
       !get(h,o.cg+0x8a348,o.scene_time)||!get(h,o.cg+0x8c158,o.refdef_time)||
       !support_weapon::script_scene_current(o.scene_time,o.refdef_time,pre.native_game_time))return false;
    *out=o;return true;
}
inline bool same_owner(const Owner& a,const Owner& b) noexcept {
    return a.cg==b.cg&&a.server==b.server&&a.topdown==b.topdown&&a.client==b.client&&a.player==b.player&&
        a.view==b.view&&a.session==b.session&&a.client_time==b.client_time&&a.server_time==b.server_time&&
        a.scene_time==b.scene_time&&a.refdef_time==b.refdef_time&&a.client_local==b.client_local&&
        a.server_local==b.server_local&&a.client_camera==b.client_camera&&a.server_camera==b.server_camera;
}
inline bool same_pre(const PreCommandSnapshot& a,const PreCommandSnapshot& b) noexcept {
    return a.sequence==b.sequence&&a.gameplay_epoch==b.gameplay_epoch&&a.native_cg_identity==b.native_cg_identity&&
        a.native_game_time==b.native_game_time&&a.observed_ms==b.observed_ms;
}
}
