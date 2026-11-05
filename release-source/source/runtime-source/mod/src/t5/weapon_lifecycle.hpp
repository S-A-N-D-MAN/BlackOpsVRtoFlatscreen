// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
namespace bovr::t5 {
struct WeaponLifetime {
    std::uint64_t generation{};
    std::uintptr_t viewmodel_identity{},dobj_identity{};
    DWORD native_thread{};
    bool verified{};
};
// Observe actual local handle1536/client0 allocation, all frees of handle1536,
// unsupported-client creation invalidation, and native owner/pool
// reset. Never mint a lifetime from an already present pointer on first read.
bool install_weapon_lifecycle(HMODULE executable) noexcept;
bool read_weapon_lifetime(std::uintptr_t viewmodel,std::uintptr_t dobj,WeaponLifetime*) noexcept;
namespace detail {
// The actual hook owns synchronization. This value state is separately tested.
struct WeaponLifetimeState {
    std::uint64_t serial{};
    WeaponLifetime live{};
    unsigned creation_depth{};
    bool nested{};
    DWORD creating_thread{};
    std::uintptr_t creating_owner{};
    void invalidate() noexcept {++serial;live={};creation_depth=0;nested=false;creating_thread=0;creating_owner=0;}
    void begin(std::uintptr_t owner,DWORD thread) noexcept {
        ++serial;live={};
        if(creation_depth++) {nested=true;return;}
        creating_owner=owner;creating_thread=thread;nested=false;
    }
    void finish(std::uintptr_t owner,std::uintptr_t dobj,DWORD thread,bool native_slot_matches) noexcept {
        if(!creation_depth) return;
        const bool accept=creation_depth==1&&!nested&&owner&&owner==creating_owner&&dobj&&thread&&thread==creating_thread&&native_slot_matches;
        --creation_depth;
        if(accept) live={serial,owner,dobj,thread,true};
        if(!creation_depth) {nested=false;creating_owner=0;creating_thread=0;}
    }
    bool read(std::uintptr_t owner,std::uintptr_t dobj,DWORD thread,WeaponLifetime* output) const noexcept {
        if(output) *output={};
        if(!output||creation_depth||!live.verified||!live.generation||live.viewmodel_identity!=owner||live.dobj_identity!=dobj||live.native_thread!=thread) return false;
        *output=live;return true;
    }
};
}
}
