// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <cstdint>

namespace bovr::input {
// Value-only cache. The owning adapter serializes access with its metadata lock.
class ActionSnapshotCache final {
public:
    void publish(const wawvr::xr::ActionSnapshot& actions,std::uint64_t generation,std::uint64_t now) noexcept {
        actions_=actions; generation_=generation; published_=now; valid_=generation!=0;
    }
    void clear() noexcept { actions_={}; generation_=published_=0; valid_=false; }
    bool read(std::uint64_t now,wawvr::xr::ActionSnapshot* actions,std::uint64_t* generation) const noexcept {
        if (actions) *actions={};
        if (generation) *generation=0;
        if (!actions || !generation || !valid_ || now<published_ || now-published_>250) return false;
        *actions=actions_; *generation=generation_; return true;
    }
private:
    wawvr::xr::ActionSnapshot actions_{};
    std::uint64_t generation_{},published_{};
    bool valid_{};
};
}
