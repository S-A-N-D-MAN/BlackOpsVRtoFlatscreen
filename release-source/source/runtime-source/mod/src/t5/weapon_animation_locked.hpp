// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_animation_math.hpp"
#include <cstdint>

namespace bovr::t5::animation_detail {
enum class LockedStatus {accepted,invalid,identity,same_clock,cache_unavailable,cache_changed,bones_missing,copy_failed,native_clear_not_empty};
struct LockedRequest {std::uintptr_t object{};unsigned gun_bone{},flash_bone{},bone_count{};int clock{};bool require_new_clock{},require_completed_native_clear{};};
struct CacheCopy {std::uintptr_t matrices{};int clock{},stamp{};PoseCopy pose{};bool gun_computed{},flash_computed{};std::array<std::uint32_t,15> part_bits{};};
struct LockedCopy {NativeMat32 gun{},flash{};PoseCopy pose{};int clock{},old_stamp{},final_stamp{};bool calculated{};};
// Provider read/copy routines contain only protected memory copies. The lower
// native calls may throw/raise SEH and MUST remain outside exception containment.
// All pointer identities are borrowed only for this synchronous function.
struct LockedHost {
    void* context{};
    void (*lock)(void*,std::uintptr_t){};
    void (*unlock)(void*,std::uintptr_t){};
    bool (*state)(void*,const LockedRequest&,CacheCopy*){};
    int (*cache_for_bone)(void*,std::uintptr_t,unsigned,std::uintptr_t*){};
    void (*hierarchy)(void*,std::uintptr_t,unsigned,std::uint32_t*){};
    void (*pose)(void*,std::uintptr_t,const std::uint32_t*){};
    void (*skeleton)(void*,std::uintptr_t,const std::uint32_t*){};
    bool (*copy)(void*,const LockedRequest&,std::uintptr_t,NativeMat32*,NativeMat32*){};
};
inline bool same_pose(const PoseCopy& a,const PoseCopy& b) noexcept {
    return a.origin.x==b.origin.x&&a.origin.y==b.origin.y&&a.origin.z==b.origin.z&&
        a.angles.x==b.angles.x&&a.angles.y==b.angles.y&&a.angles.z==b.angles.z;
}
inline LockedStatus locked_work(const LockedHost& h,const LockedRequest& r,LockedCopy* output) {
    CacheCopy first{},created{},computed{},last{};
    if(!h.state(h.context,r,&first)||first.clock!=r.clock)return LockedStatus::identity;
    if(r.require_completed_native_clear&&first.part_bits!=std::array<std::uint32_t,15>{})return LockedStatus::native_clear_not_empty;
    if(r.require_new_clock&&first.stamp==first.clock)return LockedStatus::same_clock;
    std::uintptr_t matrices{};
    const int cached=h.cache_for_bone(h.context,r.object,r.flash_bone,&matrices);
    if(!matrices)return LockedStatus::cache_unavailable; // Native allocation failure also returns1.
    if((r.require_new_clock||r.require_completed_native_clear)&&cached!=0)return LockedStatus::cache_changed;
    if(!h.state(h.context,r,&created)||created.clock!=r.clock||created.stamp!=r.clock||created.matrices!=matrices||!same_pose(first.pose,created.pose))return LockedStatus::cache_changed;
    if(!cached){
        std::uint32_t bits[5]{};
        h.hierarchy(h.context,r.object,r.flash_bone,bits);
        h.pose(h.context,r.object,bits);
        h.skeleton(h.context,r.object,bits);
    }
    if(!h.state(h.context,r,&computed)||computed.clock!=r.clock||computed.stamp!=r.clock||computed.matrices!=matrices||!same_pose(first.pose,computed.pose))return LockedStatus::cache_changed;
    if(!computed.gun_computed||!computed.flash_computed)return LockedStatus::bones_missing;
    LockedCopy value{};
    if(!h.copy(h.context,r,matrices,&value.gun,&value.flash))return LockedStatus::copy_failed;
    if(!h.state(h.context,r,&last)||last.clock!=r.clock||last.stamp!=r.clock||last.matrices!=matrices||!same_pose(computed.pose,last.pose)||!last.gun_computed||!last.flash_computed)return LockedStatus::cache_changed;
    value.pose=last.pose;value.clock=r.clock;value.old_stamp=first.stamp;value.final_stamp=last.stamp;value.calculated=!cached;
    *output=value;return LockedStatus::accepted;
}
inline LockedStatus capture_locked(const LockedHost& h,const LockedRequest& r,LockedCopy* output) {
    if(!output)return LockedStatus::invalid;*output={};
    if((r.require_new_clock&&r.require_completed_native_clear)||!r.object||r.clock<=0||!r.bone_count||r.bone_count>160||r.gun_bone>=r.bone_count||r.flash_bone>=r.bone_count||
       !h.lock||!h.unlock||!h.state||!h.cache_for_bone||!h.hierarchy||!h.pose||!h.skeleton||!h.copy)return LockedStatus::invalid;
    h.lock(h.context,r.object);
    // Deliberately no RAII/finally unlock: original47E180 has no native unwind
    // unlock. A lower native fault propagates with the same acquired lock state.
    // Every normal return/rejected protected copy below unlocks exactly once.
    const auto status=locked_work(h,r,output);
    h.unlock(h.context,r.object);
    return status;
}
}
