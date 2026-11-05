// SPDX-License-Identifier: GPL-3.0-only
#include "t5/dual_pistol_shot.hpp"
#include "t5/dual_pistol_effects.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <atomic>
namespace bovr::t5 {
namespace {
template<class T>T value(std::uintptr_t p) noexcept {T out{};memcpy(&out,reinterpret_cast<const void*>(p),sizeof(out));return out;}
struct Owner {
    std::uintptr_t cg{},client{},server{},variant{},definition{},partner_variant{},partner_definition{},source{};
    unsigned weapon{},partner{},event{},parameter{},failure{};bool projectile{},ordinary{};int fire_type{};
    int cursor{},old_sequence{},sequence{},server_time{},client_time{};
    primary_weapon_tracking::Player predicted{},authoritative{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server server_state{};
    std::array<std::uint32_t,6> returns{};
    input::NativeCommandBytes command{};
    std::array<unsigned char,68> parameters{};
    friend bool operator==(const Owner&,const Owner&)=default;
};
bool owner(const std::uint32_t* original_stack,unsigned event,bool projectile,Owner* out) noexcept {
    if(!original_stack||!out||dual_pistol_shot::hand(event)<0)return false;
    out->failure=1;
    const auto entry=reinterpret_cast<std::uintptr_t>(original_stack);
    // The projectile call pushes seven arguments rather than the bullet's
    // five. Normalize by eight bytes only for their shared enclosing frame.
    const auto b=entry+(projectile?8u:0u);ULONG_PTR low{},high{};
    GetCurrentThreadStackLimits(&low,&high);
    if((entry&3)||entry<low||b<entry||high<0x160||b>high-0x160)return false;
    __try {
        const auto u=[](std::uintptr_t p){return value<std::uint32_t>(p);};
        const auto i=[](std::uintptr_t p){return value<int>(p);};
        const auto byte=[](std::uintptr_t p){return value<unsigned char>(p);};
        Owner o{};o.projectile=projectile;
        constexpr std::uintptr_t entity=0x1a796f8;
        o.returns={u(entry),u(b+0x78),u(b+0xec),u(b+0x120),u(b+0x138),u(b+0x154)};
        out->returns=o.returns;out->failure=2;
        if(o.returns[0]!=(projectile?0x4cfe93u:0x4cfdd7u)||(o.returns[1]!=0x552043&&o.returns[1]!=0x552058)||
           o.returns[2]!=0x69d782||o.returns[3]!=0x53b4cf||o.returns[4]!=0x513e5d||o.returns[5]!=0x5afe8d||
           u(b+0x13c)||u(b+0x140)||u(b+0x124)!=0x1a53770)return false;
        if(projectile){
            if(u(entry+4)!=entity||u(entry+0x10)!=b+0x34||u(entry+0xc)!=u(b+0x7c)||
               u(entry+0x14)!=0xa5e370||u(entry+0x18)||u(entry+0x1c)!=entry+0x30)return false;
        }else if(u(b+4)!=entity||u(b+16)!=entity||u(b+12)!=b+0x34||u(b+8)!=u(b+0x7c)||
                 u(b+20)!=u(b+0x80))return false;
        o.cg=u(0x2ff5354);o.client=u(entity+0x13c);o.server=u(0x286d01c);o.source=u(b+0x15c);
        out->failure=3;
        if(o.cg<0x10000||o.client<0x10000||o.client!=u(0x1c03140)||o.server<0x10000||
           o.server!=u(b+0x158)||i(o.server)!=4||o.source<low||o.source>high-52||u(b+0x128)!=o.client+0x1a30||
           u(0x1a53770)!=o.client||i(entity)||byte(o.client+0x130)||
           (projectile&&(byte(o.client+0x4e8)&2)))return false;
        const auto ps=o.cg+0x8a364;if(byte(ps+0x130))return false;
        out->failure=4;
        o.weapon=byte(o.client+0x144);
        if(!o.weapon||o.weapon!=byte(ps+0x144)||o.weapon!=byte(entity+0xd4)||
           (projectile&&u(entry+8)!=o.weapon))return false;
        o.variant=u(0xbe19a8+4*o.weapon);if(o.variant<0x10000)return false;
        const int type=projectile?2:0;
        o.definition=u(o.variant+8);if(o.definition<0x10000||i(o.definition+0x1c)!=type)return false;
        const auto dual=byte(o.definition+0x56a);const auto kind=i(o.definition+0x20);o.fire_type=i(o.definition+0x30);
        if(dual>1)return false;
        o.ordinary=dual==0;
        if(o.ordinary){
            if(projectile||dual_pistol_effects::ordinary_mode()==dual_pistol_effects::OrdinaryMode::disabled||
               !dual_pistol_effects::ordinary_weapon(type,kind,o.fire_type,dual)||
               !dual_pistol_effects::ordinary_event(event,o.weapon,o.weapon))return false;
        }else if(kind!=4)return false;
        out->weapon=o.weapon;out->failure=5;
        out->ordinary=o.ordinary;
        if(!o.ordinary){
        o.partner=u(o.definition+0x594);if(!o.partner||o.partner>=256||o.partner==o.weapon)return false;
        o.partner_variant=u(0xbe19a8+4*o.partner);if(o.partner_variant<0x10000)return false;
        o.partner_definition=u(o.partner_variant+8);
        if(o.partner_definition<0x10000||i(o.partner_definition+0x1c)!=type||i(o.partner_definition+0x20)!=4||
           byte(o.partner_definition+0x56a)!=1||u(o.partner_definition+0x594)!=o.weapon)return false;
        }
        out->partner=o.partner;out->failure=6;
        const auto player=[&](std::uintptr_t p){return primary_weapon_tracking::Player{i(p+4),i(p+0x1c4),
            u(p+0xc),u(p+0x10),u(p+0xe0),u(p+0xe4)};};
        o.predicted=player(ps);o.authoritative=player(o.client);
        o.view={i(o.cg+0x8a360),i(0x2910160),i(0x2910164),i(0x2911e2c)!=0,i(o.cg+0xc)!=0,i(o.cg+0xa9d44)!=0};
        o.server_state={o.authoritative,true,i(entity+0x184),i(o.client+0x1a2c),i(o.client+0x1a18),u(o.client+0x1c0c),u(entity+0x298)};
        if(primary_weapon_tracking::paired_mode(primary_weapon_tracking::client_mode(o.predicted,o.view),
           primary_weapon_tracking::server_mode(o.server_state))!=primary_weapon_tracking::Mode::ordinary)return false;
        for(auto p:{ps,o.client})if(!native_weapon_tracking_state(i(p+0x158))||!native_weapon_tracking_state(i(p+0x15c)))return false;
        out->failure=7;
        o.cursor=i(b+0xf0);o.old_sequence=i(b+0xf4);o.sequence=value<short>(o.client+0xe8);o.event=event;o.parameter=u(b+0x84);
        if(!dual_pistol_shot::event_current(o.old_sequence,o.cursor,o.sequence,event,
           u(o.client+0xec+4*(o.cursor&3)),o.parameter,u(o.client+0xfc+4*(o.cursor&3))))return false;
        out->failure=8;
        memcpy(o.command.data(),reinterpret_cast<void*>(o.source),o.command.size());
        if(o.command[0x18]!=o.weapon||memcmp(o.command.data(),reinterpret_cast<void*>(o.server+0x10f38),o.command.size())||
           memcmp(o.command.data(),reinterpret_cast<void*>(o.client+0x1a30),o.command.size())||
           memcmp(o.command.data(),reinterpret_cast<void*>(0x1a53770+4),o.command.size())||
           memcmp(o.command.data(),reinterpret_cast<void*>(b+0x144),4))return false;
        o.server_time=i(0x1c04890);o.client_time=i(o.cg+0x8a348);
        out->failure=9;
        memcpy(o.parameters.data(),reinterpret_cast<void*>(b+0x34),o.parameters.size());
        if(u(b+0x70)!=o.variant||u(b+0x74)!=o.definition)return false;
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool matches(const Owner& o,const CommandAimPublication& p) noexcept {
    return p.dual_wield==!o.ordinary&&p.cg_identity==o.cg&&p.weapon==o.weapon&&p.dual_weapon==o.partner&&
        p.variant_identity==o.variant&&p.definition_identity==o.definition&&p.dual_variant_identity==o.partner_variant&&
        p.dual_definition_identity==o.partner_definition&&p.command.bytes==o.command;
}
void report(unsigned status,unsigned event,const Owner& o,const CommandAimPublication& p,const NativeBulletGeometry* g=nullptr) noexcept {
    if(o.ordinary){
        static std::array<std::atomic<unsigned>,6> ordinary_counts{};const auto n=++ordinary_counts[status];if(n>64&&n%120)return;
        NativeBulletGeometry original{};memcpy(&original,o.parameters.data(),sizeof(original));
        int command_time{};std::uint32_t packed{};memcpy(&command_time,o.command.data(),4);memcpy(&packed,p.command.bytes.data()+0x20,4);
        const auto mode=dual_pistol_effects::ordinary_mode();
        trace("t5_hitscan_shot","status=%u count=%u event=%u weapon=%u command_time=%d command=%d publication=%llu sequence=%llu cursor=%d native_sequence=%d mode=%u packed=0x%08x forward=%.7f,%.7f,%.7f original=%.7f,%.7f,%.7f native_muzzle_preserved=1 original_parameter_writes=0 scratch_basis_bytes=%u native_calls_added=0 owner_stage=%u exact_bytes=52",
            status,n,event,o.weapon,command_time,p.command.command_number,p.command.publication_serial,p.command.rig.frame.actions.sequence,
            o.cursor,o.sequence,unsigned(mode),packed,g?g->forward[0]:0.f,g?g->forward[1]:0.f,g?g->forward[2]:0.f,
            original.forward[0],original.forward[1],original.forward[2],g&&mode==dual_pistol_effects::OrdinaryMode::correct?36u:0u,o.failure);
        return;
    }
    static std::array<std::atomic<unsigned>,6> counts{};const auto n=++counts[status];if(n>16&&n%120)return;
    trace(o.projectile?"t5_dual_pistol_projectile":"t5_dual_pistol_shot","status=%u count=%u event=%u hand=%d weapon=%u partner=%u command=%d publication=%llu sequence=%llu cursor=%d native_sequence=%d selected=%u forward=%.6f,%.6f,%.6f native_muzzle_preserved=1 native_calls_added=0 owner_stage=%u returns=%x,%x,%x,%x,%x,%x",
        status,n,event,dual_pistol_shot::hand(event),o.weapon,o.partner,p.command.command_number,p.command.publication_serial,
        p.command.rig.frame.actions.sequence,o.cursor,o.sequence,g?1u:0u,g?g->forward[0]:0.f,g?g->forward[1]:0.f,g?g->forward[2]:0.f,
        o.failure,o.returns[0],o.returns[1],o.returns[2],o.returns[3],o.returns[4],o.returns[5]);
}
}
bool dual_pistol_shot_guards(std::uintptr_t base) noexcept {
    if(base!=0x400000)return false;
    // Original prefix excludes the five bytes owned by the bullet wrapper.
    return !native_guard::compare(reinterpret_cast<void*>(0x4cfc90),0x142,"1ead48ad8dd938661534c962f3b4fb240fbe64af4e088ba6861e73d42b57b71e")&&
        !native_guard::compare(reinterpret_cast<void*>(0x551f50),0x10b,"6326638d498e9e9c470d18066cea69a4cd1788b58114a0fca1f1ee26cb7aa936")&&
        !native_guard::compare(reinterpret_cast<void*>(0x55243c),0xf5,"bded432e9899ee6f308133a1e018304f3feb52fd2458f788acd6d53b16603fc3")&&
        !native_guard::compare(reinterpret_cast<void*>(0x513dc0),0xa4,"b69d165f077670f566973d701dd287b0320006f931d2186acd2877a2ea91b1dd")&&
        !native_guard::compare(reinterpret_cast<void*>(0x53b430),0xe6,"53cd103ad4cdaafc9326bb36d11664055a9f3e4cb22e94c8c93aacd90d7b6ab0")&&
        !native_guard::compare(reinterpret_cast<void*>(0x69d450),0x332,"594084a49b2a305f4e5691174466176a5c8645941d5ad3152b6118a338c0fd62");
}
static bool select_dual_pistol_shot_kind(std::uintptr_t base,const std::uint32_t* stack,unsigned event,bool projectile,unsigned char* scratch) noexcept {
    if(base!=0x400000||!scratch||!exact_profile_aim_enabled()||dual_pistol_shot::hand(event)<0)return false;
    Owner first{},second{};first.projectile=projectile;CommandAimPublication p{},again{};
    if(!owner(stack,event,projectile,&first)){if(first.weapon||event==31||event==32)report(1,event,first,p);return false;}
    if(!read_native_command_aim_for_shot(first.command,first.cg,&p,first.ordinary)||!matches(first,p)){report(2,event,first,p);return false;}
    NativeBulletGeometry original{},selected{};memcpy(&original,first.parameters.data(),sizeof(original));
    // Derive the original command's historical hand direction at its actual
    // publication time. The retained reader separately bounds server delay.
    std::uint32_t packed{};memcpy(&packed,p.command.bytes.data()+0x20,4);
    const bool proposed=first.ordinary?dual_pistol_effects::command_geometry(packed,original,&selected):
        dual_pistol_shot::geometry(p.command.rig,p.command.rig.frame.actions,p.aim.body,event,p.command.observed_ms,original,&selected);
    if(!proposed){
        report(3,event,first,p);return false;}
    unsigned recheck{};
    const bool generation=consumed_command_input_generation_current(p.command.rig);
    const bool reread=generation&&owner(stack,event,projectile,&second);
    if(!generation)recheck|=1;if(!reread)recheck|=2;
    if(reread){
        // The client render clock advances independently while this owned
        // server event runs. It is diagnostic data, not shot identity. Keep
        // the server-event clock stable along with every native owner field.
        if(first.server_time!=second.server_time)recheck|=4;
        if(first.cg!=second.cg||first.client!=second.client||first.server!=second.server||first.variant!=second.variant||
           first.definition!=second.definition||first.partner_variant!=second.partner_variant||
           first.partner_definition!=second.partner_definition||first.source!=second.source||
           first.weapon!=second.weapon||first.partner!=second.partner||first.returns!=second.returns||
           first.ordinary!=second.ordinary||first.fire_type!=second.fire_type)recheck|=8;
        if(first.event!=second.event||first.parameter!=second.parameter||first.cursor!=second.cursor||
           first.old_sequence!=second.old_sequence||first.sequence!=second.sequence)recheck|=16;
        if(first.command!=second.command)recheck|=32;
        if(first.parameters!=second.parameters)recheck|=64;
        if(first.predicted!=second.predicted||first.authoritative!=second.authoritative)recheck|=128;
        if(first.view!=second.view||first.server_state!=second.server_state)recheck|=256;
    }
    if(!recheck){
        if(!read_native_command_aim_for_shot(first.command,first.cg,&again,first.ordinary))recheck|=512;
        else if(again.command.publication_serial!=p.command.publication_serial||again.invalidation_revision!=p.invalidation_revision)recheck|=1024;
    }
    if(recheck){
        static std::atomic<unsigned> failures{};const auto n=++failures;
        if(n<=24||n%120==0)trace("t5_dual_pistol_shot_recheck",
            "mask=%u event=%u projectile=%u owner_stage=%u old_client_time=%d new_client_time=%d old_server_time=%d new_server_time=%d publication=%llu",
            recheck,event,projectile,second.failure,first.client_time,second.client_time,first.server_time,second.server_time,p.command.publication_serial);
        report(4,event,first,p);return false;
    }
    const bool changed=dual_pistol_effects::apply_command_scratch(dual_pistol_effects::ordinary_mode(),!first.ordinary,
        selected,first.parameters.data(),scratch);
    report(0,event,first,p,&selected);return changed;
}
bool select_dual_pistol_shot(std::uintptr_t base,const std::uint32_t* stack,unsigned event,unsigned char* scratch) noexcept {
    return select_dual_pistol_shot_kind(base,stack,event,false,scratch);
}
bool select_dual_pistol_projectile_shot(std::uintptr_t base,const std::uint32_t* stack,unsigned event,unsigned char* scratch) noexcept {
    return select_dual_pistol_shot_kind(base,stack,event,true,scratch);
}
}
