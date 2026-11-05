// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/runtime_profile_types.hpp"
#include <array>
#include <atomic>
#include <mutex>
namespace bovr::t5::borrowed_profile {
struct Shape {unsigned arms,gun,flash,total,attachment; unsigned gun_count{10},flash_local{6},weapon{4},revision{1};};
inline constexpr unsigned max_profile_bones=160,max_profile_models=16;
// Keep legacy row numbers unassigned so a zero-initialized or stale claim can
// never name a live runtime entry. Every published row is derived from the
// player's current, twice-copied native profile.
inline constexpr unsigned runtime_row_base=8,max_runtime_profiles=2048,invalid_runtime_row=~0U,runtime_revision=5;
namespace catalog_detail {
// A release publication exposes both immutable pointers together. Slots are
// append-only for the process lifetime, so readers never acquire a write lock.
struct Published {Shape shape{};const Profile* profile{};};
inline std::array<Published,max_runtime_profiles> slots{};
inline std::atomic<unsigned> published{};
inline std::mutex registration_mutex;
inline const Published* runtime(unsigned row) noexcept {
    if(row<runtime_row_base)return nullptr;
    const auto index=row-runtime_row_base;
    return index<published.load(std::memory_order_acquire)?&slots[index]:nullptr;
}
inline const Shape* runtime_shape(unsigned row) noexcept {const auto* p=runtime(row);return p?&p->shape:nullptr;}
inline const Profile* runtime_profile(unsigned row) noexcept {const auto* p=runtime(row);return p?p->profile:nullptr;}
}
inline const Shape* shape(unsigned row) noexcept {
    return catalog_detail::runtime_shape(row);
}
inline const Profile* get(unsigned row) noexcept {return catalog_detail::runtime_profile(row);}
inline bool matches(unsigned row,unsigned gun,unsigned flash,unsigned total) noexcept {
    const auto* s=shape(row);return s&&s->gun==gun&&s->flash==flash&&s->total==total;
}
inline bool binding(unsigned row,unsigned weapon,unsigned revision) noexcept {
    const auto* s=shape(row);return s&&s->weapon==weapon&&s->revision==revision;
}
struct Selection {unsigned row,gun,flash,total;friend bool operator==(const Selection&,const Selection&)=default;};
inline bool same_selection(Selection current,Selection expected) noexcept {
    return current==expected&&matches(current.row,current.gun,current.flash,current.total);
}
// This supplies cache-copy bounds only. The native caller separately proves
// the current runtime row equals the expected camera/normal key before a lock.
inline bool matrix_shape(unsigned gun,unsigned flash,unsigned total) noexcept {
    const auto n=catalog_detail::published.load(std::memory_order_acquire);
    for(unsigned i=0;i<n;++i){const auto& s=catalog_detail::slots[i].shape;
        if(s.gun==gun&&s.flash==flash&&s.total==total)return true;}
    return false;
}
}
