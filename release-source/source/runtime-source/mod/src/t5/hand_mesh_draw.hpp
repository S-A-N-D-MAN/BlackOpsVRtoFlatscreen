// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "loader/d3d9_observer.hpp"
namespace bovr::t5 {
namespace borrowed_profile {struct Profile;}
// Called only after the existing exact-model admission and locked pose edit.
bool publish_hand_mesh(std::uintptr_t model,unsigned row,std::uint64_t epoch,std::uint64_t generation) noexcept;
// Caller supplies an owned, validated arms-first profile while holding the same
// native DObj/model scope. Only copied layout membership survives publication.
bool publish_left_hand_mesh(std::uintptr_t model,const borrowed_profile::Profile& copied_profile,
    unsigned arms,std::uint64_t epoch,std::uint64_t generation) noexcept;
bool before_hand_mesh_draw(IDirect3DDevice9*,const IndexedDrawCall&) noexcept;
void after_hand_mesh_draw() noexcept;
void abandon_hand_mesh_draw() noexcept;
bool retire_hand_mesh_draw() noexcept;
}
