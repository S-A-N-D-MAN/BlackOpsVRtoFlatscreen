// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include "t5/weapon_render_admission.hpp"
#include <cmath>
#include <limits>

namespace bovr::t5::native_melee_scene {
inline constexpr std::uint64_t maximum_age_ms=100;

// Provenance of the FIRST failed native invocation for this exact prediction.
// Repeated failures may not refresh its timestamp or replace its identity.
struct Deferred {
    bool active{};
    vr::FrameKey key{};
    std::uint64_t published_ms{},gameplay_epoch{};
    vr::NativeBuildIdentity failed_native{};
    NativeWeaponContext failed_producer{};
};

// The adapter supplies a protected, read-only native context recopy bracketed
// by two actual pre-command snapshots, plus the current RenderScene sample.
// A pre-command snapshot alone cannot prove the native viewmodel or state.
struct Current {
    NativeWeaponContext native{};
    NativeSceneSample scene{};
    PreCommandSnapshot before{},after{};
    bool copied{};
};

inline bool fresh(std::uint64_t timestamp,std::uint64_t now) noexcept {
    return timestamp&&now>=timestamp&&now-timestamp<=maximum_age_ms;
}
inline bool finite_view(const SceneView& v) noexcept {
    const auto finite=[](const auto& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    return v.width>0&&v.height>0&&std::isfinite(v.tan_half_fov_x)&&v.tan_half_fov_x>0&&
        std::isfinite(v.tan_half_fov_y)&&v.tan_half_fov_y>0&&finite(v.origin)&&
        finite(v.axis.forward)&&finite(v.axis.left)&&finite(v.axis.up)&&std::isfinite(v.near_clip)&&v.near_clip>=0;
}
inline bool ordinary_pre(const PreCommandSnapshot& p,const NativeWeaponContext& c,std::uint64_t now) noexcept {
    using namespace primary_weapon_tracking;
    return p.sequence&&p.gameplay_epoch&&fresh(p.observed_ms,now)&&p.context.valid&&
        p.context.local_client==0&&p.context.native_thread==c.native_thread&&
        p.context.command_number==c.command_number&&p.context.connection_state>=9&&
        !p.context.key_catcher&&!p.context.native_override&&p.native_gameplay_fields_valid&&
        p.gameplay_active&&!p.linked_weapon_active&&p.native_cg_identity==c.cg_identity&&
        p.native_game_time<=c.native_time&&p.native_game_time>=0&&
        player_mode({p.native_pm_type,p.native_health,p.native_pm_flags,p.native_weapon_flags,
                     p.native_entity_flags,p.native_entity_flags2})==Mode::ordinary;
}
inline bool same_pre(const PreCommandSnapshot& a,const PreCommandSnapshot& b) noexcept {
    return a.sequence==b.sequence&&a.observed_ms==b.observed_ms&&a.gameplay_epoch==b.gameplay_epoch&&
        a.native_cg_identity==b.native_cg_identity&&a.native_game_time==b.native_game_time&&
        a.native_health==b.native_health&&a.native_pm_type==b.native_pm_type&&
        a.native_pm_flags==b.native_pm_flags&&a.native_weapon_flags==b.native_weapon_flags&&
        a.native_entity_flags==b.native_entity_flags&&a.native_entity_flags2==b.native_entity_flags2&&
        a.native_gameplay_fields_valid==b.native_gameplay_fields_valid&&a.gameplay_active==b.gameplay_active&&
        a.linked_weapon_active==b.linked_weapon_active&&a.context.valid==b.context.valid&&
        a.context.local_client==b.context.local_client&&a.context.native_thread==b.context.native_thread&&
        a.context.command_number==b.context.command_number&&a.context.native_frame_time==b.context.native_frame_time&&
        a.context.connection_state==b.context.connection_state&&a.context.key_catcher==b.context.key_catcher&&
        a.context.native_override==b.context.native_override;
}
inline bool ordinary_owner(const NativeWeaponContext& c) noexcept {
    return c.valid&&c.body_valid&&primary_weapon_mode(c)==primary_weapon_tracking::Mode::ordinary&&
        c.local_client==0&&c.draw_gun==1&&c.native_thread&&c.call_serial&&
        c.cg_identity>=0x10000&&c.cg_identity<=(std::numeric_limits<std::uint32_t>::max)()-0x8c100&&
        c.refdef_identity==c.cg_identity+0x8c100&&c.viewmodel_identity&&c.dobj_identity&&
        c.weapon_index>0&&c.weapon_index<256&&c.command_number>=0&&c.native_time>=0&&
        c.native_time==c.refdef_time&&finite_view(c.stock_view);
}
inline bool native_melee_owner(const NativeWeaponContext& c) noexcept {
    // Native18..20 may use the previous viewmodel. The reader intentionally
    // omits firearm descriptors here: absent type/class/dual fields are NOT
    // proof of a rifle. This admits a native scene, never tracked gun placement.
    return ordinary_owner(c)&&c.weapon_state>=18&&c.weapon_state<=20&&native_weapon_tracking_state(c.other_weapon_state)&&
        !c.primary_viewmodel&&!c.manual_grenade_primary_visual&&!c.support_marker_native_visual&&
        !c.rebirth_native_prop_visual&&!c.weapon_definition_valid&&!c.variant_identity&&!c.definition_identity;
}
inline bool same_current(const NativeWeaponContext& a,const NativeWeaponContext& b) noexcept {
    return a.native_thread==b.native_thread&&a.call_serial==b.call_serial&&
        a.cg_identity==b.cg_identity&&a.refdef_identity==b.refdef_identity&&
        a.viewmodel_identity==b.viewmodel_identity&&a.dobj_identity==b.dobj_identity&&
        a.weapon_index==b.weapon_index&&a.weapon_state==b.weapon_state&&a.other_weapon_state==b.other_weapon_state&&
        a.offhand==b.offhand&&a.stack_count==b.stack_count&&a.command_number==b.command_number&&
        a.native_time==b.native_time&&a.native_frame_time==b.native_frame_time&&a.refdef_time==b.refdef_time&&
        a.model_lifetime_verified==b.model_lifetime_verified&&a.model_generation==b.model_generation&&
        same_scene_view(a.stock_view,b.stock_view);
}
inline bool retain_first(Deferred& out,const vr::FrameTicket& ticket,vr::NativeBuildIdentity failed,
    const WeaponRenderWitness& witness,WeaponRenderAdmission admission,const PreCommandSnapshot& pre,
    std::uint64_t now) noexcept {
    if(out.active&&out.key==ticket.key)return false;
    const auto& p=witness.producer;
    if(admission!=WeaponRenderAdmission::no_transaction||!witness.observed||!witness.expected||
       witness.nested||witness.substituted||witness.hidden_primary_weapon||witness.native_call_suppressed||
       !ordinary_owner(p)||p.dual_wield||!expects_tracked_weapon_render(p,true)||
       !ordinary_pre(pre,p,now)||!failed.buffer_identity||!failed.native_frame||
       !usable_weapon_scene_publication(ticket,now,maximum_age_ms))return false;
    out={true,ticket.key,ticket.published_ms,pre.gameplay_epoch,failed,p};return true;
}
inline bool admit(const Deferred& old,const vr::FrameTicket& ticket,vr::NativeBuildIdentity native,
    const WeaponRenderWitness& witness,const Current& current,int frame_time,std::uint64_t now) noexcept {
    if(!old.active||old.key!=ticket.key||old.published_ms!=ticket.published_ms||!old.gameplay_epoch||
       !old.failed_native.buffer_identity||!old.failed_native.native_frame||
       !ordinary_owner(old.failed_producer)||old.failed_producer.dual_wield||
       !expects_tracked_weapon_render(old.failed_producer,true)||
       !usable_weapon_scene_publication(ticket,now,maximum_age_ms)||!current.copied||
       !witness.observed||witness.expected||witness.nested||witness.substituted||
       witness.hidden_primary_weapon||witness.native_call_suppressed)return false;
    const auto& p=witness.producer;const auto& previous=old.failed_producer;
    if(!native_melee_owner(p)||!native_melee_owner(current.native)||!same_current(p,current.native)||
       !same_pre(current.before,current.after)||!ordinary_pre(current.before,p,now)||
       current.before.gameplay_epoch!=old.gameplay_epoch||!matches_weapon_scene(p,current.scene)||
       !matches_weapon_scene(current.native,current.scene)||current.scene.native_frame_time!=frame_time)return false;
    // An adapter-generated build serial labels the new reservation only. Real
    // native call/frame/time progress proves this is not the failed invocation.
    return native.buffer_identity&&native.build_serial&&native.native_frame>old.failed_native.native_frame&&
        p.call_serial>previous.call_serial&&p.native_time>previous.native_time&&
        p.command_number>=previous.command_number&&p.native_thread==previous.native_thread&&
        p.cg_identity==previous.cg_identity&&p.refdef_identity==previous.refdef_identity&&
        p.weapon_index==previous.weapon_index;
}
} // namespace bovr::t5::native_melee_scene
