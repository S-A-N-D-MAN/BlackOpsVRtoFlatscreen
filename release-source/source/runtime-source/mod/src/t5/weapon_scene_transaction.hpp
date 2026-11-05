// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_probe.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include "vr/frame_coordinator.hpp"
#include <cstring>
namespace bovr::t5 {
// Head/eye ownership depends on a renderable, fresh prediction, not action
// focus. The placement proposal separately rejects unfocused/untracked hands
// and falls back to the native weapon without cancelling this scene ticket.
inline bool usable_weapon_scene_publication(const vr::FrameTicket& ticket,std::uint64_t now,
                                            std::uint64_t maximum_age) noexcept {
    return ticket.key.generation&&ticket.key.frame_id&&ticket.published_ms&&
        ticket.frame.should_render&&ticket.frame.views_valid&&now>=ticket.published_ms&&
        now-ticket.published_ms<=maximum_age;
}
// Copied at the exact R_RenderScene entry before either eye mutates refdef.
// No native pointer here is dereferenced by the value matching function.
struct NativeSceneSample {
    DWORD native_thread{};
    std::uintptr_t cg_identity{},refdef_identity{};
    int native_time{},native_frame_time{},refdef_time{};
    SceneView stock_view{};
    bool valid{};
};
inline bool same_scene_view(const SceneView& a,const SceneView& b) noexcept {
    return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height&&
        a.tan_half_fov_x==b.tan_half_fov_x&&a.tan_half_fov_y==b.tan_half_fov_y&&a.near_clip==b.near_clip&&
        std::memcmp(&a.origin,&b.origin,sizeof(a.origin))==0&&std::memcmp(&a.axis,&b.axis,sizeof(a.axis))==0;
}
inline bool matches_weapon_scene(const NativeWeaponContext& producer,const NativeSceneSample& scene) noexcept {
    return producer.valid&&producer.body_valid&&primary_weapon_context(producer)&&scene.valid&&
        producer.native_thread==scene.native_thread&&producer.cg_identity&&producer.cg_identity==scene.cg_identity&&
        producer.refdef_identity&&producer.refdef_identity==scene.refdef_identity&&
        producer.native_time==scene.native_time&&producer.refdef_time==scene.refdef_time&&
        producer.native_frame_time==scene.native_frame_time&&scene.native_time==scene.refdef_time&&
        same_scene_view(producer.stock_view,scene.stock_view);
}
struct WeaponSceneTransaction {
    // Retain after cancellation/expiry/reset until the old native scene arrives.
    // Clearing this early would let that scene claim a newer XR prediction.
    bool active{};
    vr::SceneReservation reservation{};
    NativeWeaponContext producer{};
    wawvr::xr::Posef anchor{};
    BorrowedSceneRecord borrowed{};
    // True only when the weapon wrapper reserved this exact native build before
    // suppressing its stock knife draw. It never authorizes an unreserved scene.
    bool hidden_primary_weapon{};
    // Frozen together with the placement reservation. Render cannot reread a
    // different collision approval after the tracked skeleton was selected.
    wawvr::xr::Posef camera_head{};
    bool camera_frozen{};
};
enum class WeaponSceneClaim { ordinary, reserved, rejected };
inline WeaponSceneClaim claim_weapon_scene_prediction(WeaponSceneTransaction& transaction,vr::FrameCoordinator& coordinator,
    vr::NativeBuildIdentity native,const NativeSceneSample& scene,int frame_time,bool anchor_current,vr::FrameTicket* output,bool borrowed_current=false) {
    if (!transaction.active)
        return coordinator.claim_render_frame(output) ? WeaponSceneClaim::ordinary : WeaponSceneClaim::rejected;
    transaction.active=false;
    const bool matched=matches_weapon_scene(transaction.producer,scene)&&scene.native_frame_time==frame_time&&anchor_current&&
        (!transaction.borrowed.active||(transaction.borrowed.completed&&(!transaction.borrowed.selected||transaction.borrowed.world_verified)&&borrowed_current));
    if (!matched || !coordinator.consume_scene_reservation(transaction.reservation,native,output)) {
        coordinator.cancel_scene_reservation(transaction.reservation.ticket.key,transaction.reservation.serial);
        if (output) *output={};
        return WeaponSceneClaim::rejected;
    }
    return WeaponSceneClaim::reserved;
}
}
