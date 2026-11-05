// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/grenade_throw_hook.hpp"
#include <cstddef>

namespace bovr::t5::grenade_projectile {
inline constexpr unsigned max_entities=1022;
struct Key {
    std::uint64_t gameplay_epoch{},xr_generation{},transaction{};
    unsigned weapon{};
    friend bool operator==(const Key&,const Key&)=default;
};
struct Stamp {std::uint32_t generation{},birth{};unsigned char inuse{};};
struct Baseline {
    Key key{};
    std::array<Stamp,max_entities> slots{};
    std::uintptr_t cg{},server{},variant{},definition{};
    int time{};unsigned count{};DWORD owner{};
    bool ready{},retired{};
};
struct Access {
    void* context{};
    bool (*synchronize)(void*) noexcept{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
    // Called only while the synchronization lease is current. Must compare
    // every expected byte before writing the one bounded trajectory span.
    bool (*compare_write)(void*,std::uintptr_t,const void*,const void*,std::size_t) noexcept{};
};
enum class Status {invalid,waiting,callback_rejected,ambiguous,stale,write_rejected,applied,retired};
struct Result {Status status{Status::invalid};unsigned entity{};std::uint32_t generation{};int birth{},time{};};
// No code patches. configure pins the retail executable and read/write recipes.
bool configure(HMODULE) noexcept;
const Access& native_access() noexcept;
// Capture before the native grenade Down edge. Key must be nonzero and current.
bool capture(Baseline*,const Key&,const Access& = native_access()) noexcept;
// Poll from the existing main-thread input callback. Acquires SV_WaitServer
// even if no reload is active. Do not hold the host callback's mutex here.
// Never relocates currentOrigin: rebases trajectory at the native current
// position/time once, leaving prior native collision results intact.
Result poll(Baseline*,const Key&,GrenadeThrowCallback,const Access& = native_access()) noexcept;
}
