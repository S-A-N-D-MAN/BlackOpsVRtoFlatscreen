// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_animation_locked.hpp"
#include "t5/borrowed_profile_shape.hpp"
#include <cstring>
namespace bovr::t5::animation_detail {
inline bool same_raw(const LockedCopy& a,const LockedCopy& b) noexcept {
    return std::memcmp(&a.gun,&b.gun,32)==0&&std::memcmp(&a.flash,&b.flash,32)==0&&same_pose(a.pose,b.pose)&&a.clock==b.clock;
}
// Read-only current-cache recheck, distinct from capture_locked: never calls
// allocation/cache lookup, hierarchy, native controllers or skeleton evaluator.
// state() additionally verifies the complete bounded current asset payload.
inline LockedStatus existing_matrix_work(const LockedHost& h,const LockedRequest& r,const LockedCopy& expected,LockedCopy* out) {
    CacheCopy first{},last{};LockedCopy value{},repeat{};
    if(!h.state(h.context,r,&first)||first.clock!=r.clock||first.stamp!=r.clock||!first.matrices||
       !first.gun_computed||!first.flash_computed)return LockedStatus::identity;
    if(!h.copy(h.context,r,first.matrices,&value.gun,&value.flash))return LockedStatus::copy_failed;
    value.pose=first.pose;value.clock=r.clock;value.old_stamp=value.final_stamp=first.stamp;
    if(!same_raw(value,expected))return LockedStatus::cache_changed;
    if(!h.state(h.context,r,&last)||last.clock!=first.clock||last.stamp!=first.stamp||last.matrices!=first.matrices||
       !last.gun_computed||!last.flash_computed||!same_pose(first.pose,last.pose))return LockedStatus::cache_changed;
    if(!h.copy(h.context,r,last.matrices,&repeat.gun,&repeat.flash))return LockedStatus::copy_failed;
    repeat.pose=last.pose;repeat.clock=r.clock;
    if(!same_raw(repeat,value))return LockedStatus::cache_changed;
    *out=value;return LockedStatus::accepted;
}
inline LockedStatus copy_existing_locked(const LockedHost& h,const LockedRequest& r,const LockedCopy& expected,LockedCopy* out) {
    if(!out)return LockedStatus::invalid;*out={};
    if(!r.object||r.clock<=0||!borrowed_profile::matrix_shape(r.gun_bone,r.flash_bone,r.bone_count)||r.require_new_clock||r.require_completed_native_clear||
       !h.lock||!h.unlock||!h.state||!h.copy)return LockedStatus::invalid;
    h.lock(h.context,r.object);
    // Original lock failure semantics: native faults propagate with acquired
    // native state unchanged. Protected-copy rejection returns normally/unlocks.
    const auto result=existing_matrix_work(h,r,expected,out);
    h.unlock(h.context,r.object);return result;
}
}
