// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/native_clear_pair.hpp"
#include "t5/borrowed_profile_shape.hpp"
#include "t5/weapon_probe.hpp"
#include "input/weapon_placement.hpp"
#include "input/weapon_profile_admission.hpp"
namespace bovr::t5 {
// Copied row/revision for an exact admitted model assembly.
// Native address observations remain copied equality values, never generations.
struct BorrowedWeaponClaim {
    animation_detail::ClearComparisonKey key{};
    std::uint64_t wrapper_token{},camera_serial{},camera_published_ms{};
    std::uint32_t profile_revision{1};
    input::profile_admission::Token command_admission{};
    friend bool operator==(const BorrowedWeaponClaim&,const BorrowedWeaponClaim&)=default;
};
inline bool valid_borrowed_claim(const BorrowedWeaponClaim& c,std::uint64_t now) noexcept {
    return c.wrapper_token&&c.camera_serial&&c.camera_published_ms&&now>=c.camera_published_ms&&now-c.camera_published_ms<=100&&
        c.profile_revision==c.key.profile_revision&&borrowed_profile::binding(c.key.profile_row,c.key.weapon,c.profile_revision)&&borrowed_profile::matches(c.key.profile_row,c.key.gun_bone,c.key.flash_bone,c.key.bone_count)&&
        c.key.command_epoch&&c.key.command_sequence&&c.key.thread;
}
inline bool borrowed_claim_matches_context(const BorrowedWeaponClaim& claim,const NativeWeaponContext& c) noexcept {
    const auto& k=claim.key;
    // A dual claim belongs to the exact command path. The scene adapter must
    // independently validate this token against the current admission ledger.
    // Revision zero is the live ledger's initial generation, not a missing token.
    return c.valid&&primary_weapon_context(c)&&c.body_valid&&c.primary_viewmodel&&
        (!c.dual_wield||(claim.command_admission.serial&&claim.command_admission.revision!=UINT64_MAX))&&c.weapon_index==k.weapon&&
        c.native_thread==k.thread&&c.native_time==k.native_time&&c.native_frame_time==k.frame_time&&
        c.command_number==k.command_number&&c.command_clock==k.command_clock&&c.weapon_state==k.weapon_state&&
        c.other_weapon_state==k.other_weapon_state&&c.cg_identity==k.opaque_identity[0]&&c.viewmodel_identity==k.opaque_identity[1]&&
        c.dobj_identity==k.opaque_identity[2]&&c.variant_identity==k.opaque_identity[3]&&c.definition_identity==k.opaque_identity[4];
}
inline bool borrowed_admission_matches_context(const input::profile_admission::ObservedIdentity& id,
                                               const NativeWeaponContext& c) noexcept {
    return primary_weapon_context(c)&&id.tracking_mode==static_cast<std::uint32_t>(primary_weapon_mode(c))&&
        id.server_link==c.server_link_identity;
}
class BorrowedCameraClaim {
public:
    void clear_entry(std::uint64_t serial) noexcept {if(serial!=camera_.serial)camera_={};}
    void publish(const animation_detail::ClearValue& v) noexcept {camera_={};if(v.valid&&!v.invocation_token)camera_=v;}
    bool take(const animation_detail::ClearComparisonKey& key,std::uint64_t clear_serial,std::uint64_t token,std::uint64_t now,
              BorrowedWeaponClaim* claim,animation_detail::ClearValue* value) noexcept {
        if(claim)*claim={};if(value)*value={};const auto saved=camera_;camera_={};
        if(!claim||!value||!saved.valid||saved.key!=key||saved.serial!=clear_serial)return false;
        BorrowedWeaponClaim c{key,token,saved.serial,saved.published_ms,key.profile_revision};if(!valid_borrowed_claim(c,now))return false;
        *claim=c;*value=saved;return true;
    }
    void invalidate() noexcept {camera_={};}
    bool available(std::uint64_t serial)const noexcept{return camera_.valid&&camera_.serial==serial;}
private:animation_detail::ClearValue camera_{};
};
struct BorrowedSceneRecord {
    bool active{},completed{};
    BorrowedWeaponClaim claim{};
    std::uint64_t normal_serial{};
    bool selected{},world_verified{};
};
inline bool borrowed_scene_completed(const BorrowedSceneRecord& r,std::uint64_t now,std::uint64_t clear_serial,
    std::uint64_t last_wrapper,std::uint64_t epoch,std::uint64_t command_sequence,bool wrapper_idle) noexcept {
    return r.active&&r.completed&&(!r.selected||r.world_verified)&&valid_borrowed_claim(r.claim,now)&&r.normal_serial==r.claim.camera_serial+1&&
        r.normal_serial==clear_serial&&r.claim.wrapper_token==last_wrapper&&wrapper_idle&&
        r.claim.key.command_epoch==epoch&&r.claim.key.command_sequence==command_sequence;
}
}
