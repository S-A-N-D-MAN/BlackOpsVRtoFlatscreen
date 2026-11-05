// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_animation_locked.hpp"
#include <cstdint>
namespace bovr::t5::animation_detail {
// Refusal-only observer policy. True preserves the original checks; it grants
// no copied-profile, native-object, placement or frame authority.
constexpr bool borrowed_normal_preparation_required(bool gate_enabled,
        bool writer_enabled,bool primary_grenade_visual,bool active_claim) noexcept {
    return !gate_enabled||!writer_enabled||primary_grenade_visual||active_claim;
}
struct ClearComparisonKey {
    std::uint64_t command_epoch{},command_sequence{};
    std::uint32_t thread{},weapon{},profile_row{};
    std::uint32_t profile_revision{1};
    int native_time{},frame_time{},command_number{},command_clock{},skeleton_clock{},weapon_state{},other_weapon_state{};
    unsigned gun_bone{},flash_bone{},bone_count{},pool_index{};
    // Exact copied address bits are opaque observations for equality ONLY.
    // They are never cast/dereferenced after the original callback, and never
    // constitute allocation history or an active writer's model generation.
    std::array<std::uint32_t,6> opaque_identity{};
    friend bool operator==(const ClearComparisonKey&,const ClearComparisonKey&)=default;
};
struct ClearValue {ClearComparisonKey key{};LockedCopy copy{};std::uint64_t serial{},published_ms{};bool valid{};std::uint64_t invocation_token{};};
// No native object or stack pointer is retained. Every completed clear (even
// rejected/unknown/foreign) retires the previous normal observation. Camera is
// consumable by exactly the immediately following supported normal clear.
class NativeClearPair {
public:
    void begin(std::uint64_t serial,bool camera) noexcept {
        normal_={};if(!serial||serial<=last_||camera||serial!=camera_.serial+1)camera_={};last_=serial;
    }
    void camera(ClearValue value) noexcept {camera_={};if(value.valid&&!value.invocation_token&&value.serial==last_)camera_=value;}
    bool normal(ClearValue value,ClearValue* before) noexcept {
        if(before)*before={};const auto camera=camera_;camera_={};normal_={};
        if(!before||!value.valid||!value.invocation_token||value.serial!=last_)return false;
        normal_=value;
        if(!camera.valid||camera.serial+1!=value.serial||camera.key!=value.key||
           !camera.published_ms||value.published_ms<camera.published_ms||value.published_ms-camera.published_ms>100)return false;
        *before=camera;return true;
    }
    bool take_normal(const ClearComparisonKey& key,std::uint64_t serial,std::uint64_t token,std::uint64_t now,ClearValue* value) noexcept {
        if(value)*value={};const auto saved=normal_;normal_={};
        if(!value||!saved.valid||!token||token!=saved.invocation_token||!serial||serial!=last_||serial!=saved.serial||saved.key!=key||
           !saved.published_ms||now<saved.published_ms||now-saved.published_ms>100)return false;
        *value=saved;return true;
    }
    void invalidate() noexcept {camera_={};normal_={};}
    bool has_normal(std::uint64_t serial)const noexcept{return normal_.valid&&serial&&serial==last_&&serial==normal_.serial;}
private: ClearValue camera_{},normal_{};std::uint64_t last_{};
};
}
