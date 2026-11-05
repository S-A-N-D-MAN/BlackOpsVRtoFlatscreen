// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/support_script_aim.hpp"
#include <array>
#include <cstring>
#include <string_view>

namespace bovr::t5::strela {
using Copy=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Host {void* context{};Copy copy{};};
inline bool bounded(std::uintptr_t p,std::size_t n) noexcept {return p>=0x10000&&p<=UINT32_MAX&&n&&n<=UINT32_MAX-p;}
inline bool bytes(const Host& h,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return h.copy&&out&&bounded(p,n)&&h.copy(h.context,p,out,n);
}
template<class T>bool get(const Host& h,std::uintptr_t p,T& out) noexcept {return bytes(h,p,&out,sizeof out);}
struct Descriptor {
    unsigned weapon{};
    std::uint32_t variant{},definition{},name{};
    int type{},weapon_class{},fire_type{},capacity{},radius{},speed{};
    std::uint8_t requires_lock{};
    std::array<char,24> internal{};
    friend bool operator==(const Descriptor&,const Descriptor&)=default;
};
inline bool descriptor(const Host& h,unsigned weapon,Descriptor* out) noexcept {
    if(!out||!weapon||weapon>255)return false;
    Descriptor d{};d.weapon=weapon;
    if(!get(h,0xbe19a8+weapon*4,d.variant)||!bounded(d.variant,0x24)||
       !get(h,d.variant,d.name)||!get(h,d.variant+8,d.definition)||!bounded(d.definition,0x47d)||
       !get(h,d.variant+0x20,d.capacity)||d.capacity!=1||
       !get(h,d.definition+0x1c,d.type)||d.type!=2||
       !get(h,d.definition+0x20,d.weapon_class)||d.weapon_class!=6||
       !get(h,d.definition+0x30,d.fire_type)||d.fire_type!=0||
       !get(h,d.definition+0x474,d.radius)||d.radius<=0||d.radius>640||
       !get(h,d.definition+0x478,d.speed)||d.speed<=0||d.speed>60000||
       !get(h,d.definition+0x47c,d.requires_lock)||d.requires_lock!=1)return false;
    bool ended=false;
    for(std::size_t i=0;i<d.internal.size();++i){
        if(!get(h,d.name+i,d.internal[i]))return false;
        if(!d.internal[i]){ended=true;break;}
    }
    if(!ended)return false;
    const std::string_view name(d.internal.data());
    if(name!="strela_sp"&&name!="flashpoint_strela_sp")return false;
    *out=d;return true;
}
inline bool read_descriptor(Copy copy,void* context,unsigned weapon,Descriptor* out) noexcept {
    if(!out)return false;*out={};Descriptor a{},b{};const Host h{context,copy};
    if(!descriptor(h,weapon,&a)||!descriptor(h,weapon,&b)||a!=b)return false;
    *out=a;return true;
}
struct Owner {
    std::uint32_t cg{},server{},topdown{};
    Descriptor weapon{};
    primary_weapon_tracking::Player client{},player{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server session{};
    int client_time{},server_time{},scene_time{},refdef_time{};
    std::array<float,3> client_camera{},server_camera{};
    friend bool operator==(const Owner&,const Owner&)=default;
};
inline bool player(const Host& h,std::uintptr_t p,primary_weapon_tracking::Player& out) noexcept {
    return bounded(p,0x46c)&&get(h,p+4,out.pm_type)&&get(h,p+0x1c4,out.health)&&
        get(h,p+0xc,out.pm_flags)&&get(h,p+0x10,out.weapon_flags)&&
        get(h,p+0xe0,out.entity_flags)&&get(h,p+0xe4,out.entity_flags2);
}
inline bool copy_owner(const Host& h,std::uint32_t entref,DWORD thread,
    const PreCommandSnapshot& pre,std::uint64_t now,Owner* out) noexcept {
    if(!out||entref||!thread||!pre.sequence||!pre.gameplay_epoch||!pre.gameplay_active||
       !pre.native_gameplay_fields_valid||!support_weapon::script_pre_current(pre.observed_ms,now)||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9)return false;
    DWORD main{},server_thread{};std::uint32_t server_root{};int number{},override_value{},cubemap{},suppressed{};
    if(!get(h,0x251ca98,main)||!get(h,0x251cac0,server_thread)||(thread!=main&&thread!=server_thread))return false;
    Owner o{};
    if(!get(h,0x2ff5354,o.cg)||!bounded(o.cg,0xa9d48)||o.cg!=pre.native_cg_identity||
       !get(h,0x1a796f8+0x13c,o.server)||!bounded(o.server,0x1c10)||
       !get(h,0x1c03140,server_root)||server_root!=o.server||!get(h,0x1a796f8,number)||number)return false;
    const auto ps=o.cg+0x8a364;std::uint8_t client_local{},server_local{},client_weapon{},server_weapon{};
    if(!player(h,ps,o.client)||!player(h,o.server,o.player)||
       !get(h,ps+0x130,client_local)||!get(h,o.server+0x130,server_local)||client_local||server_local||
       !get(h,ps+0x144,client_weapon)||!get(h,o.server+0x144,server_weapon)||client_weapon!=server_weapon||
       !read_descriptor(h.copy,h.context,client_weapon,&o.weapon)||
       !get(h,o.cg+0x8a360,o.view.camera_mode)||!get(h,0x2910160,o.view.key_catcher)||
       !get(h,0x2910164,o.view.connection_state)||!get(h,0x2911e2c,override_value)||
       !get(h,o.cg+0xc,cubemap)||!get(h,o.cg+0xa9d44,suppressed))return false;
    o.view.native_override=override_value!=0;o.view.cubemap=cubemap!=0;o.view.draw_suppressed=suppressed!=0;
    o.session.player=o.player;o.session.local_identity=true;
    if(!get(h,0x1a796f8+0x184,o.session.entity_health)||!get(h,o.server+0x1a2c,o.session.connected)||
       !get(h,o.server+0x1a18,o.session.session)||!get(h,o.server+0x1c0c,o.session.flags)||
       !get(h,0x1a796f8+0x298,o.session.link))return false;
    namespace p=primary_weapon_tracking;
    if(p::paired_mode(p::client_mode(o.client,o.view),p::server_mode(o.session))!=p::Mode::ordinary)return false;
    if(!get(h,ps+0x464,o.client_camera)||!get(h,o.server+0x464,o.server_camera)||
       o.client_camera!=std::array<float,3>{}||o.server_camera!=std::array<float,3>{})return false;
    int type{},value{};std::uint32_t name{};constexpr char expected[]="player_topDownCamMode";char actual[sizeof expected]{};
    if(!get(h,0xbcd150,o.topdown)||!bounded(o.topdown,0x1c)||!get(h,o.topdown+0x10,type)||type!=5||
       !get(h,o.topdown+0x18,value)||value||!get(h,o.topdown,name)||
       !bytes(h,name,actual,sizeof actual)||std::memcmp(actual,expected,sizeof actual))return false;
    if(!get(h,ps,o.client_time)||!get(h,o.server,o.server_time)||o.client_time<=0||o.server_time<=0||
       std::abs(static_cast<long long>(o.client_time)-o.server_time)>250||
       !get(h,o.cg+0x8a348,o.scene_time)||!get(h,o.cg+0x8c158,o.refdef_time)||
       !support_weapon::script_scene_current(o.scene_time,o.refdef_time,pre.native_game_time))return false;
    *out=o;return true;
}
struct Call {std::uintptr_t returned{},entity{},forward{},right{},up{},stack{},low{},high{};DWORD thread{};};
// This is the direct G_WorldDirToScreenPos query, not any other shared native
// caller. All three outputs belong to disjoint, pinned locals in its stack.
inline bool call_valid(const Host& h,const Call& c) noexcept {
    if(c.returned!=0x812c3c||c.entity!=0x1a796f8||!c.thread||c.stack%4||
       c.stack<c.low||c.high<c.stack||c.high-c.stack<0x48||!bounded(c.stack,0x48)||
       c.forward!=c.stack+0x20||c.right!=c.stack+0x2c||c.up!=c.stack+0x38)return false;
    std::array<std::uint32_t,5> args{};std::uint32_t parent{};
    return get(h,c.stack,args)&&args==std::array<std::uint32_t,5>{
        static_cast<std::uint32_t>(c.returned),static_cast<std::uint32_t>(c.entity),
        static_cast<std::uint32_t>(c.forward),static_cast<std::uint32_t>(c.right),static_cast<std::uint32_t>(c.up)}&&
        get(h,c.stack+0x44,parent)&&parent==0x812ef4;
}
struct Axes {wawvr::xr::Vec3f forward{},right{},up{};};
inline bool axes(float pitch,float yaw,Axes* out) noexcept {
    if(!out||!std::isfinite(pitch)||!std::isfinite(yaw)||std::fabs(pitch)>90||std::fabs(yaw)>360)return false;
    constexpr float radians=.017453292519943295f;const float p=pitch*radians,y=yaw*radians;
    const float cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y);
    *out={{cp*cy,cp*sy,-sp},{sy,-cy,0},{sp*cy,sp*sy,cp}};return true;
}
}
