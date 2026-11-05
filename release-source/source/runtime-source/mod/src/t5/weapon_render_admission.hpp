// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_scene_transaction.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include <span>

namespace bovr::t5 {
inline bool native_support_marker_visual(const NativeWeaponContext& c) noexcept {
    return c.support_marker_native_visual&&c.valid&&c.weapon_definition_valid&&
        c.weapon_type==2&&c.weapon_class==4&&!c.dual_wield&&
        c.weapon_index>0&&c.weapon_index<256&&c.variant_identity&&c.definition_identity;
}
inline bool native_rebirth_prop_visual(const NativeWeaponContext& c) noexcept {
    return c.rebirth_native_prop_visual&&c.valid&&c.body_valid&&c.weapon_definition_valid&&
        primary_weapon_mode(c)==primary_weapon_tracking::Mode::ordinary&&
        c.weapon_type==0&&c.weapon_class==0&&!c.dual_wield&&
        c.weapon_index>0&&c.weapon_index<256&&c.variant_identity&&c.definition_identity;
}
// Preserve native props, mounted, offhand and weapon-switch transitions.
// Verified linked primary weapons share tracking, without ordinary controls.
// A profile/reservation/tracking failure inside an otherwise supported primary
// handheld scope must not turn that same scene into an ordinary stock-gun pair.
inline bool expects_tracked_weapon_render(const NativeWeaponContext& c,bool writer_enabled) noexcept {
    return writer_enabled&&!c.manual_grenade_primary_visual&&!native_support_marker_visual(c)&&!native_rebirth_prop_visual(c)&&
        c.valid&&c.body_valid&&primary_weapon_context(c)&&
        c.primary_viewmodel&&c.weapon_definition_valid&&c.local_client==0&&c.draw_gun&&
        c.native_thread&&c.call_serial&&c.weapon_index>0&&c.weapon_index<256&&
        (c.weapon_type==0||c.weapon_type==2||c.weapon_type==4)&&
        c.weapon_class>=0&&(c.weapon_class!=9||c.weapon_type==4)&&
        (!c.dual_wield||native_dual_weapon_tracking_type(c.weapon_type,c.weapon_class))&&native_valid_stack_fire_count(static_cast<std::uint32_t>(c.stack_count))&&
        tracked_primary_weapon_state(c)&&native_weapon_tracking_state(c.other_weapon_state)&&
        c.cg_identity&&c.refdef_identity==c.cg_identity+0x8c100&&
        c.viewmodel_identity&&c.dobj_identity&&c.variant_identity&&c.definition_identity&&
        c.native_time==c.refdef_time;
}
inline bool suppress_untracked_primary_knife_for_world(const NativeWeaponContext& c,bool writer_enabled) noexcept {
    return c.primary_knife_variant&&expects_tracked_weapon_render(c,writer_enabled);
}

enum class WeaponRenderAdmission : unsigned {
    ordinary, tracked, hidden_primary_weapon, native_scene, nested, no_transaction, different_invocation,
    original_placement, incomplete, unverified
};
inline bool weapon_render_admitted(WeaponRenderAdmission status) noexcept {
    return status==WeaponRenderAdmission::ordinary||status==WeaponRenderAdmission::tracked||
        status==WeaponRenderAdmission::hidden_primary_weapon;
}
inline const char* weapon_render_admission_name(WeaponRenderAdmission status) noexcept {
    switch(status) {
    case WeaponRenderAdmission::ordinary:return "ordinary";
    case WeaponRenderAdmission::tracked:return "tracked";
    case WeaponRenderAdmission::hidden_primary_weapon:return "hidden_primary_weapon";
    case WeaponRenderAdmission::native_scene:return "native_scene";
    case WeaponRenderAdmission::nested:return "nested";
    case WeaponRenderAdmission::no_transaction:return "no_transaction";
    case WeaponRenderAdmission::different_invocation:return "different_invocation";
    case WeaponRenderAdmission::original_placement:return "original_placement";
    case WeaponRenderAdmission::incomplete:return "incomplete";
    case WeaponRenderAdmission::unverified:return "unverified";
    }
    return "unknown";
}
inline WeaponRenderAdmission assess_weapon_render(const WeaponRenderWitness& witness,const NativeSceneSample& scene,
    const WeaponSceneTransaction& transaction,int frame_time) noexcept {
    if(!witness.observed||!witness.expected)return WeaponRenderAdmission::ordinary;
    const auto& producer=witness.producer;
    // A consumed/missed earlier scene is never authority for a later frame or
    // another view. Within the current body/time identity, all detailed scene
    // checks remain mandatory; no mismatch can silently become a stock pair.
    if(scene.valid&&(producer.native_thread!=scene.native_thread||producer.cg_identity!=scene.cg_identity||
        producer.refdef_identity!=scene.refdef_identity||producer.native_time!=scene.native_time))
        return WeaponRenderAdmission::ordinary;
    if(!matches_weapon_scene(producer,scene)||scene.native_frame_time!=frame_time)
        return WeaponRenderAdmission::native_scene;
    if(witness.nested)return WeaponRenderAdmission::nested;
    // A synchronously suppressed Vorkuta stock-weapon call may render world-only
    // stereo only when it already owns the exact native build reservation.
    // Suppression by itself is never frame authority.
    if(witness.hidden_primary_weapon){
        if(!producer.primary_knife_variant||witness.substituted||!witness.native_call_suppressed)
            return WeaponRenderAdmission::original_placement;
        if(!transaction.active||!transaction.hidden_primary_weapon||transaction.borrowed.active)
            return WeaponRenderAdmission::no_transaction;
        const auto& hidden=transaction.producer;
        if(hidden.call_serial!=producer.call_serial||hidden.native_thread!=producer.native_thread||
            hidden.weapon_index!=producer.weapon_index||hidden.viewmodel_identity!=producer.viewmodel_identity||
            hidden.dobj_identity!=producer.dobj_identity||hidden.variant_identity!=producer.variant_identity||
            hidden.definition_identity!=producer.definition_identity||hidden.model_generation!=producer.model_generation||
            hidden.model_lifetime_verified!=producer.model_lifetime_verified||!matches_weapon_scene(hidden,scene))
            return WeaponRenderAdmission::different_invocation;
        return WeaponRenderAdmission::hidden_primary_weapon;
    }
    if(!transaction.active||transaction.hidden_primary_weapon||!transaction.borrowed.active)return WeaponRenderAdmission::no_transaction;
    const auto& current=transaction.producer;
    if(current.call_serial!=producer.call_serial||current.native_thread!=producer.native_thread||
        current.weapon_index!=producer.weapon_index||current.viewmodel_identity!=producer.viewmodel_identity||
        current.dobj_identity!=producer.dobj_identity||current.variant_identity!=producer.variant_identity||
        current.definition_identity!=producer.definition_identity||current.model_generation!=producer.model_generation||
        current.model_lifetime_verified!=producer.model_lifetime_verified||!matches_weapon_scene(current,scene))
        return WeaponRenderAdmission::different_invocation;
    if(!witness.substituted||!transaction.borrowed.selected)return WeaponRenderAdmission::original_placement;
    if(!transaction.borrowed.completed)return WeaponRenderAdmission::incomplete;
    if(!transaction.borrowed.world_verified)return WeaponRenderAdmission::unverified;
    // Current admission token, reservation age, native buffer/frame ownership,
    // and coordinator consumption are still checked by the existing consumer.
    return WeaponRenderAdmission::tracked;
}
// A native frontend may already be one frame ahead of the XR owner's Present.
// Rejecting its new stock-gun scene must not cancel an earlier tracked pair
// merely because that earlier pair still owns the coordinator prediction.
struct WeaponRenderProductionIdentity {
    bool valid{};
    vr::FrameKey key{};
    vr::BackendFrameIdentity stamp{};
    std::uint32_t native_frame{};
};
enum class WeaponRenderRejectionOwnership : unsigned { reject_unbound, reject_current, preserve_other };
inline WeaponRenderRejectionOwnership classify_weapon_render_rejection(vr::FrameKey pending,
    std::uintptr_t current_buffer,std::uint32_t current_native_frame,
    std::span<const WeaponRenderProductionIdentity> production) noexcept {
    bool bound_elsewhere=false;
    for(const auto& p:production) {
        if(!p.valid||p.key!=pending||!p.stamp.buffer_identity||!p.stamp.producer_serial)continue;
        if(p.stamp.buffer_identity==current_buffer&&p.native_frame==current_native_frame)
            return WeaponRenderRejectionOwnership::reject_current;
        // Completion is deliberately not required: the earlier backend may
        // still be finishing its independently admitted native production.
        bound_elsewhere=true;
    }
    return bound_elsewhere?WeaponRenderRejectionOwnership::preserve_other:WeaponRenderRejectionOwnership::reject_unbound;
}
enum class WeaponRenderRejectionAction : unsigned { preserve_other,preserve_unowned,invalidate,stale_key };
inline WeaponRenderRejectionAction resolve_weapon_render_rejection(vr::FrameCoordinator& coordinator,
        WeaponRenderAdmission admission,WeaponRenderRejectionOwnership ownership,vr::FrameKey pending,
        std::uint64_t now,std::uint64_t maximum_age) {
    if(ownership==WeaponRenderRejectionOwnership::preserve_other)return WeaponRenderRejectionAction::preserve_other;
    const bool allow=admission==WeaponRenderAdmission::no_transaction&&ownership==WeaponRenderRejectionOwnership::reject_unbound;
    switch(coordinator.reject_render_frame_or_preserve_unowned(pending,allow,now,maximum_age)){
    case vr::RenderRejectionResult::preserved_unowned:return WeaponRenderRejectionAction::preserve_unowned;
    case vr::RenderRejectionResult::invalidated:return WeaponRenderRejectionAction::invalidate;
    case vr::RenderRejectionResult::key_not_current:return WeaponRenderRejectionAction::stale_key;
    }
    return WeaponRenderRejectionAction::stale_key;
}
}
