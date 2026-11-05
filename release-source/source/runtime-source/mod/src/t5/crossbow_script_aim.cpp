// SPDX-License-Identifier: GPL-3.0-only
#include "t5/crossbow_script_aim.hpp"
#include "t5/crossbow_policy.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/helicopter_aim_selection.hpp"
#include "t5/support_weapon_context.hpp"
#include "t5/support_script_aim.hpp"
#include "t5/strela_aim.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <cstring>
#include <atomic>

namespace bovr::t5 {
namespace {
using U32=std::uint32_t;
using Method=void(__cdecl*)(U32);
constexpr std::uintptr_t original_method=0x7d6c80,method_slot=0xa52ba4;
template<class T>T read(std::uintptr_t p){T value{};memcpy(&value,reinterpret_cast<const void*>(p),sizeof value);return value;}
struct Owner {
    std::uintptr_t cg{},server{},link{},variant{},definition{};
    unsigned weapon{};int server_time{};
    friend bool operator==(const Owner&,const Owner&)=default;
};
bool owner(U32 entref,Owner* out) noexcept {
    if(entref||!out)return false; // Exact server local-player entity, class 0.
    __try {
        Owner o{};o.cg=read<U32>(0x2ff5354);o.server=read<U32>(0x1a796f8+0x13c);
        if(o.cg<0x10000||o.server<0x10000||o.server!=read<U32>(0x1c03140)||read<int>(0x1a796f8))return false;
        const auto ps=o.cg+0x8a364;
        namespace t=primary_weapon_tracking;
        const auto player=[](std::uintptr_t p){return t::Player{read<int>(p+4),read<int>(p+0x1c4),
            read<U32>(p+0xc),read<U32>(p+0x10),read<U32>(p+0xe0),read<U32>(p+0xe4)};};
        const auto cp=player(ps),sp=player(o.server);
        const auto mode=t::client_mode(cp,{read<int>(o.cg+0x8a360),read<int>(0x2910160),read<int>(0x2910164),
            read<int>(0x2911e2c)!=0,read<int>(o.cg+0xc)!=0,read<int>(o.cg+0xa9d44)!=0});
        o.link=read<U32>(0x1a796f8+0x298);o.weapon=read<unsigned char>(ps+0x144);
        if(!o.weapon||o.weapon!=read<unsigned char>(o.server+0x144)||read<unsigned char>(ps+0x130)||read<unsigned char>(o.server+0x130))return false;
        o.variant=read<U32>(0xbe19a8+4*o.weapon);if(o.variant<0x10000)return false;
        o.definition=read<U32>(o.variant+8);if(o.definition<0x10000)return false;
        constexpr char name[]="crossbow_vzoom_alt_sp";
        const auto n=read<U32>(o.variant);if(n<0x10000||memcmp(reinterpret_cast<const void*>(n),name,sizeof name))return false;
        if(!crossbow::zipline_context(mode,{sp,true,read<int>(0x1a796f8+0x184),read<int>(o.server+0x1a2c),
            read<int>(o.server+0x1a18),read<U32>(o.server+0x1c0c),static_cast<U32>(o.link)},cp.weapon_flags,
            read<int>(o.definition+0x1c),read<int>(o.definition+0x20),read<int>(o.variant+0x20),name))return false;
        o.server_time=read<int>(o.server);const auto client_time=read<int>(ps);
        if(o.server_time<=0||client_time<=0||std::abs(static_cast<long long>(o.server_time)-client_time)>250)return false;
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct SupportOwner {
    std::uintptr_t cg{},server{};
    support_weapon::Descriptor weapon{};
    int server_time{},client_time{},scene_time{},refdef_time{};
    wawvr::xr::EnginePose stock{};
};
struct SupportDiagnostic {
    const char* stage="arguments";const char* phase="first";
    unsigned weapon{};int cg_time{},refdef_time{},pre_time{},client_time{},server_time{},catcher{};
    std::uint64_t pre_age{},rig_age{},sequence{},after_sequence{};
    unsigned attempts{};
};
bool support_diagnostic_asset(U32 entref,SupportDiagnostic* d) noexcept {
    if(entref||!d)return false;
    __try {
        const auto cg=read<U32>(0x2ff5354);
        if(!support_weapon::detail::bounded(cg,0xa9d48))return false;
        support_weapon::Descriptor marker{};
        const unsigned weapon=read<unsigned char>(cg+0x8a364+0x144);
        if(!support_weapon::read_descriptor(helicopter::detail::protected_copy,nullptr,weapon,&marker))return false;
        d->weapon=weapon;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool support_owner(U32 entref,const PreCommandSnapshot& pre,SupportOwner* out,SupportDiagnostic& diagnostic) noexcept {
    const auto now=GetTickCount64();
    diagnostic.stage="pre_context";diagnostic.pre_time=pre.native_game_time;diagnostic.catcher=pre.context.key_catcher;
    diagnostic.pre_age=now>=pre.observed_ms?now-pre.observed_ms:UINT64_MAX;
    if(entref||!out||!pre.sequence||!pre.gameplay_epoch||!support_weapon::script_pre_current(pre.observed_ms,now)||
       !pre.gameplay_active||!pre.native_gameplay_fields_valid||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||
       pre.context.native_override||pre.context.connection_state<9)return false;
    __try {
        diagnostic.stage="thread";
        const auto thread=GetCurrentThreadId();
        if(thread!=read<DWORD>(0x251ca98)&&thread!=read<DWORD>(0x251cac0))return false;
        SupportOwner o{};o.cg=read<U32>(0x2ff5354);o.server=read<U32>(0x1a796f8+0x13c);
        diagnostic.stage="local_identity";
        if(!support_weapon::detail::bounded(o.cg,0xa9d48)||!support_weapon::detail::bounded(o.server,0x1c10)||
           o.cg!=pre.native_cg_identity||o.server!=read<U32>(0x1c03140)||read<int>(0x1a796f8))return false;
        const auto ps=o.cg+0x8a364;namespace t=primary_weapon_tracking;
        const auto player=[](std::uintptr_t p){return t::Player{read<int>(p+4),read<int>(p+0x1c4),
            read<U32>(p+0xc),read<U32>(p+0x10),read<U32>(p+0xe0),read<U32>(p+0xe4)};};
        const auto client=t::client_mode(player(ps),{read<int>(o.cg+0x8a360),read<int>(0x2910160),
            read<int>(0x2910164),read<int>(0x2911e2c)!=0,read<int>(o.cg+0xc)!=0,read<int>(o.cg+0xa9d44)!=0});
        const auto server=t::server_mode({player(o.server),true,read<int>(0x1a796f8+0x184),
            read<int>(o.server+0x1a2c),read<int>(o.server+0x1a18),read<U32>(o.server+0x1c0c),read<U32>(0x1a796f8+0x298)});
        const unsigned weapon=read<unsigned char>(ps+0x144);
        diagnostic.stage="paired_ordinary_asset";
        if(t::paired_mode(client,server)!=t::Mode::ordinary||read<unsigned char>(ps+0x130)||
           read<unsigned char>(o.server+0x130)||weapon!=read<unsigned char>(o.server+0x144)||
           !support_weapon::read_descriptor(helicopter::detail::protected_copy,nullptr,weapon,&o.weapon))return false;
        const auto topdown=read<U32>(0xbcd150);
        constexpr char expected[]="player_topDownCamMode";char name[sizeof expected]{};
        diagnostic.stage="topdown";
        if(!support_weapon::detail::bounded(topdown,0x1c)||read<int>(topdown+0x10)!=5||read<int>(topdown+0x18)||
           !helicopter::detail::protected_copy(nullptr,read<U32>(topdown),name,sizeof name)||memcmp(name,expected,sizeof name))return false;
        o.server_time=read<int>(o.server);o.client_time=read<int>(ps);
        o.scene_time=read<int>(o.cg+0x8a348);o.refdef_time=read<int>(o.cg+0x8c158);
        diagnostic.server_time=o.server_time;diagnostic.client_time=o.client_time;
        diagnostic.cg_time=o.scene_time;diagnostic.refdef_time=o.refdef_time;
        diagnostic.stage="player_clocks";
        if(o.server_time<=0||o.client_time<=0||std::abs(static_cast<long long>(o.server_time)-o.client_time)>250)return false;
        diagnostic.stage="scene_clocks";
        if(!support_weapon::script_scene_current(o.scene_time,o.refdef_time,pre.native_game_time))return false;
        diagnostic.stage="stock_copy";
        memcpy(&o.stock.position,reinterpret_cast<const void*>(o.cg+0x8c100+0x20),sizeof(o.stock.position));
        memcpy(&o.stock.axis,reinterpret_cast<const void*>(o.cg+0x8c100+0x34),sizeof(o.stock.axis));
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool support_angles_impl(U32 entref,wawvr::xr::Vec3f* out,unsigned* weapon,std::uint64_t* sequence,SupportDiagnostic& d) noexcept {
    PreCommandSnapshot pre{},after_pre{};SupportOwner first{},last{};
    wawvr::xr::ActionSnapshot actions{};std::uint64_t generation{};input::CommandRigSample rig{};
    d.stage="read_pre";
    if(!out||!weapon||!sequence||!read_pre_command_snapshot(&pre))return false;
    d.sequence=pre.sequence;
    if(!support_owner(entref,pre,&first,d))return false;
    d.stage="read_rig";
    if(!read_vr_command_input_snapshot(&actions,&generation,&rig))return false;
    const auto now=GetTickCount64();d.rig_age=now>=rig.published_ms?now-rig.published_ms:UINT64_MAX;
    d.stage="rig_generation";if(generation!=rig.xr_generation)return false;
    d.stage="rig_geometry";if(!support_weapon::script_angles(rig,first.stock,now,out))return false;
    d.stage="generation_current";if(!command_input_generation_current(rig))return false;
    d.stage="read_after_pre";if(!read_pre_command_snapshot(&after_pre))return false;
    d.after_sequence=after_pre.sequence;
    d.stage="epoch_changed";if(after_pre.gameplay_epoch!=pre.gameplay_epoch)return false;
    d.stage="pre_changed";if(after_pre.sequence!=pre.sequence)return false;
    d.phase="last";if(!support_owner(entref,after_pre,&last,d))return false;
    d.stage="owner_identity_changed";
    if(first.cg!=last.cg||first.server!=last.server||first.weapon!=last.weapon)return false;
    d.stage="owner_changed";
    if(first.server_time!=last.server_time||first.client_time!=last.client_time||
       first.scene_time!=last.scene_time||first.refdef_time!=last.refdef_time)return false;
    d.stage="stock_changed";
    if(memcmp(&first.stock,&last.stock,sizeof(first.stock)))return false;
    *weapon=first.weapon.weapon;*sequence=rig.frame.actions.sequence;return true;
}
bool support_angles(U32 entref,wawvr::xr::Vec3f* out,unsigned* weapon,std::uint64_t* sequence) noexcept {
    SupportDiagnostic diagnostic{};
    if(!support_diagnostic_asset(entref,&diagnostic))return false;
    const auto exact_weapon=diagnostic.weapon;
    for(unsigned attempt=1;attempt<=3;++attempt) {
        diagnostic={};diagnostic.weapon=exact_weapon;diagnostic.attempts=attempt;
        if(out)*out={};if(weapon)*weapon=0;if(sequence)*sequence=0;
        // Each bounded retry obtains a new pre-command, native owner and rig.
        // No previous angle/pose is retained, and identity/input failures stop.
        if(support_angles_impl(entref,out,weapon,sequence,diagnostic))return true;
        if(!support_weapon::script_read_retry(diagnostic.stage,attempt))break;
    }
    static std::atomic<unsigned> rejected{};const auto count=++rejected;
    if(count<=16||count%120==0)trace("t5_support_script_aim_rejected",
        "count=%u stage=%s phase=%s attempts=%u weapon=%u pre_sequence=%llu after_sequence=%llu pre_age=%llu rig_age=%llu cg_time=%d pre_time=%d refdef_time=%d client_time=%d server_time=%d catcher=%d native_fallback=1",
        count,diagnostic.stage,diagnostic.phase,diagnostic.attempts,diagnostic.weapon,diagnostic.sequence,diagnostic.after_sequence,
        diagnostic.pre_age,diagnostic.rig_age,diagnostic.cg_time,diagnostic.pre_time,diagnostic.refdef_time,
        diagnostic.client_time,diagnostic.server_time,diagnostic.catcher);
    return false;
}
void __cdecl script_player_angles(U32 entref) {
    wawvr::xr::Vec3f strela_angles{};
    if(strela_script_angles(entref,&strela_angles)) {
        const float angles[]{strela_angles.x,strela_angles.y,strela_angles.z};
        reinterpret_cast<void(__cdecl*)(const float*,int)>(0x432940)(angles,0);
        return;
    }
    wawvr::xr::Vec3f support{};unsigned support_index{};std::uint64_t support_sequence{};
    if(support_angles(entref,&support,&support_index,&support_sequence)) {
        const float angles[]{support.x,support.y,support.z};
        reinterpret_cast<void(__cdecl*)(const float*,int)>(0x432940)(angles,0);
        static std::atomic<unsigned> support_calls{};const auto count=++support_calls;
        if(count<=12||count%120==0)trace("t5_support_script_aim","count=%u weapon=%u xr_sequence=%llu pitch=%.4f yaw=%.4f native_view_writes=0 fire_flag_writes=0",
            count,support_index,support_sequence,angles[0],angles[1]);
        return;
    }
    HelicopterSceneView boat_view{};PreCommandSnapshot boat_pre{};wawvr::xr::Vec3f boat_angles{};
    const helicopter::Host boat_host{nullptr,helicopter::detail::protected_copy,nullptr,nullptr};
    if(!entref&&read_helicopter_scene_view(&boat_view)&&read_pre_command_snapshot(&boat_pre)&&
       helicopter_aim::vehicle_script_angles(boat_host,entref,GetCurrentThreadId(),boat_view,boat_pre,GetTickCount64(),&boat_angles)&&
       command_input_generation_current(boat_view.rig)) {
        const float angles[]{boat_angles.x,boat_angles.y,boat_angles.z};
        reinterpret_cast<void(__cdecl*)(const float*,int)>(0x432940)(angles,0);
        static std::atomic<unsigned> boat_calls{};const auto count=++boat_calls;
        if(count<=8||count%120==0)trace(boat_view.owner.scripted_huey?"t5_huey_script_aim":"t5_boat_script_aim","count=%u entity=%d pitch=%.4f yaw=%.4f native_view_writes=0",count,boat_view.owner.entity_index,angles[0],angles[1]);
        return;
    }
    Owner before{},after{};CommandAimPublication p{};
    if(owner(entref,&before)&&read_latest_native_command_aim_publication(&p)&&
       p.cg_identity==before.cg&&p.command.bytes[0x18]==before.weapon&&
       std::isfinite(p.aim.pitch_degrees)&&std::isfinite(p.aim.yaw_degrees)&&
       std::fabs(p.aim.pitch_degrees)<=90.f&&std::fabs(p.aim.yaw_degrees)<=360.f&&
       owner(entref,&after)&&before==after) {
        const float angles[]{p.aim.pitch_degrees,p.aim.yaw_degrees,0.f};
        reinterpret_cast<void(__cdecl*)(const float*,int)>(0x432940)(angles,0);
        static std::atomic<unsigned> calls{};const auto count=++calls;
        if(count<=12||count%120==0)trace("t5_crossbow_script_aim","count=%u weapon=%u command=%d serial=%llu pitch=%.4f yaw=%.4f native_view_writes=0 fire_flag_writes=0",
            count,before.weapon,p.command.command_number,p.command.publication_serial,angles[0],angles[1]);
        return;
    }
    reinterpret_cast<Method>(original_method)(entref);
}
}
bool configure_crossbow_script_aim(HMODULE image) noexcept {
    if(reinterpret_cast<std::uintptr_t>(image)!=0x400000||!native_command_aim_enabled())return false;
    // Replace only the server GSC method-table pointer before scripts load.
    // All original code bytes and the client-script method remain unchanged.
    const bovr::native_guard::Span spans[]{
        {original_method,107,"5c1294740cb15f508bd2bc77942fa5188fd38e4c9aaa58337efdba8ab59c19b1"},
        {0x432940,48,"55a21e02f8b0e6ca07b2f00d7f36073519b631ad34ab7b209f66e3acf9173a35"},
        {method_slot-4,12,"8e88ad1d302bcb35c78686488d4948b41a32df7bfe6dbab540426b945931eca9"}};
    for(const auto& span:spans)if(!bovr::native_guard::matches_absolute(span))return false;
    auto* slot=reinterpret_cast<void* volatile*>(method_slot);DWORD previous{},ignored{};
    if(!VirtualProtect(reinterpret_cast<void*>(method_slot),4,PAGE_READWRITE,&previous))return false;
    const auto expected=reinterpret_cast<void*>(original_method);
    const bool installed=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&script_player_angles),expected)==expected;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(method_slot),4,previous,&ignored)!=FALSE;
    trace("t5_crossbow_script_aim_configured","installed=%u protection_restored=%u table=server_getplayerangles native_code_patches=0",installed,restored);
    if(installed&&restored)configure_strela_aim(image);
    return installed&&restored;
}
}
