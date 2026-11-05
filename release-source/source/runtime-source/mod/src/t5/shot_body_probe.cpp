// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_stack_fire_count.hpp"
#include "t5/shot_body_probe.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/command_rig_adapter.hpp"
#include "t5/consumed_command_probe.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#if !defined(_M_IX86)
#error Pinned SP shot-body reader requires x86.
#endif
namespace bovr::t5 {
namespace {
struct BodyNativeFields {
    std::uintptr_t server{},client{},clients{},link{},variant{},definition{};
    int server_state{},entity_number{},entity_health{},pm_type{},health{},connected{},session{};
    int weapon_type{},weapon_state{},other_weapon_state{},stack_count{},catcher{},connection{},override_active{};
    int server_time{},level_time{},last_server_time{};
    unsigned char client_number{},entity_weapon{},client_weapon{},dual{};
    std::uint32_t flags{},pm_flags{},weapon_flags{},eflags{},eflags2{};
    std::array<float,3> origin{};
    float view_height{},yaw{},lean{};
    input::NativeCommandBytes stored_command{},source_command{};
    friend bool operator==(const BodyNativeFields&,const BodyNativeFields&)=default;
};
template<class T>T body_load(std::uintptr_t address) noexcept {T result{};memcpy(&result,reinterpret_cast<const void*>(address),sizeof(result));return result;}
bool copy_body_native(std::uintptr_t base,const NativeConsumedCommand& scope,BodyNativeFields* output) noexcept {
    BodyNativeFields value{};
    __try {
        const auto entity=base+0x16796f8;
        value.server=body_load<std::uintptr_t>(base+0x246d01c);
        value.client=body_load<std::uintptr_t>(entity+0x13c);
        value.clients=body_load<std::uintptr_t>(base+0x1803140);
        if(!value.server||value.server!=scope.server_client_identity||!value.client||value.client!=value.clients||value.client!=scope.game_client_identity)return false;
        const auto client=value.client;
        value.server_state=body_load<int>(value.server);value.entity_number=body_load<int>(entity);
        value.entity_health=body_load<int>(entity+0x184);value.link=body_load<std::uintptr_t>(entity+0x298);
        value.entity_weapon=body_load<unsigned char>(entity+0xd4);value.client_number=body_load<unsigned char>(client+0x130);
        value.client_weapon=body_load<unsigned char>(client+0x144);
        value.pm_type=body_load<int>(client+4);value.health=body_load<int>(client+0x1c4);
        value.connected=body_load<int>(client+0x1a2c);value.session=body_load<int>(client+0x1a18);
        value.flags=body_load<std::uint32_t>(client+0x1c0c);value.pm_flags=body_load<std::uint32_t>(client+0xc);
        value.weapon_flags=body_load<std::uint32_t>(client+0x10);value.eflags=body_load<std::uint32_t>(client+0xe0);value.eflags2=body_load<std::uint32_t>(client+0xe4);
        value.weapon_state=body_load<int>(client+0x158);value.other_weapon_state=body_load<int>(client+0x15c);value.stack_count=body_load<int>(client+0x1b0);
        value.catcher=body_load<int>(base+0x2510160);value.connection=body_load<int>(base+0x2510164);value.override_active=body_load<int>(base+0x2511e2c);
        memcpy(value.origin.data(),reinterpret_cast<void*>(client+0x24),12);
        value.lean=body_load<float>(client+0x7c);value.yaw=body_load<float>(client+0x184);value.view_height=body_load<float>(client+0x190);
        value.server_time=body_load<int>(base+0x246d014);value.level_time=body_load<int>(base+0x1804890);value.last_server_time=body_load<int>(client+0x1d00);
        if(!value.entity_weapon||value.entity_weapon!=value.client_weapon)return false;
        value.variant=body_load<std::uintptr_t>(base+0x7e19a8+value.entity_weapon*4);
        if(!value.variant)return false;value.definition=body_load<std::uintptr_t>(value.variant+8);if(!value.definition)return false;
        value.weapon_type=body_load<int>(value.definition+0x1c);value.dual=body_load<unsigned char>(value.definition+0x56a);
        memcpy(value.stored_command.data(),reinterpret_cast<void*>(value.server+0x10f38),52);
        if(!scope.source_command_identity)return false;
        memcpy(value.source_command.data(),reinterpret_cast<void*>(scope.source_command_identity),52);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    *output=value;return true;
}
bool body_fresh(std::uint64_t time,std::uint64_t now) noexcept {return time&&now>=time&&now-time<=input::CommandRigLedger::maximum_age_ms;}
bool scope_valid(std::uintptr_t base,const NativeConsumedCommand& scope,const NativeBulletSelectionContext& shot,DWORD thread) noexcept {
    std::int32_t copied_time{};memcpy(&copied_time,scope.command.data(),4);
    return scope.readable&&scope.local_identity&&scope.active&&!scope.native_returned&&!scope.nesting_invalidated&&!scope.mismatches&&
        scope.scope_serial&&scope.image_base==base&&scope.native_thread==thread&&scope.local_client_index==0&&scope.server_state==4&&
        scope.entity_identity==base+0x16796f8&&scope.entity_identity==shot.native.attacker_identity&&
        scope.game_client_identity&&scope.game_client_identity==shot.native.client_identity&&scope.command_time>=0&&scope.command_time==copied_time;
}
bool same_scope(const NativeConsumedCommand& a,const NativeConsumedCommand& b) noexcept {
    return a.scope_serial==b.scope_serial&&a.observed_ms==b.observed_ms&&a.native_thread==b.native_thread&&a.image_base==b.image_base&&
        a.server_client_identity==b.server_client_identity&&a.entity_identity==b.entity_identity&&a.game_client_identity==b.game_client_identity&&
        a.source_command_identity==b.source_command_identity&&a.command==b.command&&a.command_time==b.command_time;
}
bool rig_valid(const input::CommandRigMatch& rig,const NativeConsumedCommand& scope,const NativeBulletSelectionContext& shot,std::uint64_t now) noexcept {
    const auto& p=rig.produced;const auto& r=p.rig;
    return rig.valid&&rig.ledger_revision&&rig.scope_serial==scope.scope_serial&&rig.server_thread==scope.native_thread&&
        p.publication_serial&&p.gameplay_epoch&&p.gameplay_active&&p.native_thread&&p.command_number>=0&&p.bytes==scope.command&&
        p.bytes[0x18]==shot.native.weapon_index&&r.xr_generation&&r.anchor_generation==r.xr_generation&&r.frame.frame_id&&
        r.frame.actions.sequence&&r.frame.actions.focused&&r.frame.should_render&&r.frame.views_valid&&
        body_fresh(p.observed_ms,now)&&body_fresh(r.published_ms,now)&&p.observed_ms>=r.published_ms;
}
bool same_rig(const input::CommandRigMatch& a,const input::CommandRigMatch& b) noexcept {
    const auto& p=a.produced;const auto& q=b.produced;
    // Ledger records are immutable within this exact revision/publication.
    // Keep original copied rig values; a getter never refreshes their timestamps.
    return a.valid&&b.valid&&a.scope_serial==b.scope_serial&&a.server_thread==b.server_thread&&a.ledger_revision==b.ledger_revision&&
        p.publication_serial==q.publication_serial&&p.gameplay_epoch==q.gameplay_epoch&&p.native_thread==q.native_thread&&
        p.command_number==q.command_number&&p.observed_ms==q.observed_ms&&p.bytes==q.bytes&&
        p.rig.xr_generation==q.rig.xr_generation&&p.rig.anchor_generation==q.rig.anchor_generation&&p.rig.published_ms==q.rig.published_ms&&
        p.rig.frame.frame_id==q.rig.frame.frame_id&&p.rig.frame.actions.sequence==q.rig.frame.actions.sequence;
}
ShotBodyStatus body_fields_status(const BodyNativeFields& b,const NativeBulletSelectionContext& shot,const NativeConsumedCommand& scope) noexcept {
    if(b.server_state!=4||b.entity_number||b.client_number||b.entity_weapon!=shot.native.weapon_index||
       b.variant!=shot.native.variant_identity||b.definition!=shot.native.definition_identity||
       b.pm_type!=shot.native.server_pm_type||b.entity_health!=shot.native.server_health||b.weapon_state!=shot.native.server_weapon_state||
       b.stored_command!=scope.command||b.source_command!=scope.command)return ShotBodyStatus::identity_changed;
    if(b.entity_health<=0||b.health<=0||b.pm_type||b.connected!=2||b.session||b.link||b.weapon_type||b.dual||
       b.weapon_state<0||b.weapon_state>17||b.other_weapon_state<0||b.other_weapon_state>17||!native_valid_stack_fire_count(static_cast<std::uint32_t>(b.stack_count))||b.catcher||b.connection<9||b.override_active||
       (b.flags&0x17)||(b.pm_flags&0x800)||(b.weapon_flags&0x180)||(b.eflags&0x4300)||(b.eflags2&0x40040000))return ShotBodyStatus::unsupported_context;
    if(!std::isfinite(b.origin[0])||!std::isfinite(b.origin[1])||!std::isfinite(b.origin[2])||
       !std::isfinite(b.view_height)||!std::isfinite(b.yaw)||!std::isfinite(b.lean)||!std::isfinite(b.origin[2]+b.view_height))return ShotBodyStatus::nonfinite;
    if(b.lean!=0)return ShotBodyStatus::lean_unsupported;
    return ShotBodyStatus::accepted;
}
ShotBodyStatus read_body_impl(std::uintptr_t base,const NativeBulletSelectionContext& shot,NativeShotBody* output) noexcept {
    if(!output)return ShotBodyStatus::ineligible;*output={};
    const auto thread=GetCurrentThreadId();const auto started=GetTickCount64();
    const auto& native=shot.native;
    if(!shot.eligible||native.local_shot_confirmed||!native.readable||!native.local_identity||!native.normal_context||
       !native.weapon_identity||!native.finite||native.native_thread!=thread||native.weapon_index<=0||native.weapon_index>255)return ShotBodyStatus::ineligible;
    NativeConsumedCommand scope{},scope_after{};input::CommandRigMatch rig{},rig_after{};
    if(!read_consumed_command_scope(&scope)||!scope_valid(base,scope,shot,thread))return ShotBodyStatus::missing_scope;
    if(!read_current_command_rig(&rig)||!rig_valid(rig,scope,shot,started))return ShotBodyStatus::missing_rig;
    if(!body_fresh(scope.observed_ms,started)||!body_fresh(native.observed_ms,started))return ShotBodyStatus::stale;
    BodyNativeFields before{},after{};
    if(!copy_body_native(base,scope,&before))return ShotBodyStatus::unreadable;
    const auto status=body_fields_status(before,shot,scope);if(status!=ShotBodyStatus::accepted)return status;
    if(!read_consumed_command_scope(&scope_after)||!scope_valid(base,scope_after,shot,thread)||!same_scope(scope,scope_after)||
       !read_current_command_rig(&rig_after)||!same_rig(rig,rig_after))return ShotBodyStatus::changed;
    if(!copy_body_native(base,scope_after,&after))return ShotBodyStatus::unreadable;
    if(before!=after)return ShotBodyStatus::changed;
    const auto now=GetTickCount64();
    if(now<started||!body_fresh(started,now)||!body_fresh(scope.observed_ms,now)||!body_fresh(native.observed_ms,now)||!rig_valid(rig_after,scope_after,shot,now))return ShotBodyStatus::stale;
    NativeShotBody value{};value.rig=rig;value.observed_ms=now;value.scope_observed_ms=scope.observed_ms;value.native_thread=thread;
    value.attacker_identity=scope.entity_identity;value.client_identity=before.client;value.variant_identity=before.variant;value.definition_identity=before.definition;
    value.weapon_index=before.entity_weapon;value.weapon_state=before.weapon_state;value.other_weapon_state=before.other_weapon_state;
    value.native_command_time=scope.command_time;value.shot_game_time=native.game_time;value.current_server_time=before.server_time;
    value.current_level_time=before.level_time;value.current_last_server_time=before.last_server_time;
    memcpy(value.native_origin,before.origin.data(),12);memcpy(value.body_origin,before.origin.data(),12);value.body_origin[2]+=before.view_height;
    value.view_height=before.view_height;value.yaw_degrees=before.yaw;value.lean=before.lean;value.valid=true;*output=value;
    return ShotBodyStatus::accepted;
}
}
bool read_native_shot_body(std::uintptr_t base,const NativeBulletSelectionContext& shot,NativeShotBody* output) noexcept {
    const DWORD error=GetLastError();if(output)*output={};
    const bool result=base==0x400000&&command_rig_enabled()&&read_body_impl(base,shot,output)==ShotBodyStatus::accepted;
    SetLastError(error);return result;
}
void trace_native_shot_body(std::uintptr_t base,const NativeBulletSelectionContext& shot) noexcept {
    const DWORD error=GetLastError();
    if(base!=0x400000||!command_rig_enabled()){SetLastError(error);return;}
    NativeShotBody body{};const auto status=read_body_impl(base,shot,&body);
    static std::atomic<std::uint64_t> observations{},counts[static_cast<unsigned>(ShotBodyStatus::count)]{};
    const auto serial=observations.fetch_add(1)+1,count=counts[static_cast<unsigned>(status)].fetch_add(1)+1;
    if(count<=4||count%120==0) {
        const auto& rig=body.rig;const auto& p=rig.produced;
        trace("t5_native_shot_body","observation=%llu status=%u status_count=%llu valid=%u native_thread=%lu scope=%llu ledger_revision=%llu publication=%llu epoch=%llu generation=%llu xr_frame=%llu original_published_ms=%llu body_ms=%llu command_time=%d shot_game_time=%d server_time=%d level_time=%d last_server_time=%d weapon=%d states=%d,%d server_origin=%.6f,%.6f,%.6f view_height=%.6f yaw=%.6f lean=%.6f body_origin=%.6f,%.6f,%.6f native_helpers=0 render_camera_reads=0 shot_writes=0",
            serial,static_cast<unsigned>(status),count,body.valid,body.native_thread,rig.scope_serial,rig.ledger_revision,p.publication_serial,p.gameplay_epoch,p.rig.xr_generation,
            p.rig.frame.frame_id,p.rig.published_ms,body.observed_ms,body.native_command_time,body.shot_game_time,body.current_server_time,body.current_level_time,body.current_last_server_time,
            body.weapon_index,body.weapon_state,body.other_weapon_state,body.native_origin[0],body.native_origin[1],body.native_origin[2],body.view_height,body.yaw_degrees,body.lean,
            body.body_origin[0],body.body_origin[1],body.body_origin[2]);
    }
    SetLastError(error);
}
}
