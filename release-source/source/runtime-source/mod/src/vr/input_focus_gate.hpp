// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <cstdint>

namespace bovr::vr {
// OpenXR may retain valid controller poses and raw action states while VISIBLE.
// Preserve graphics tracking, but expose buttons/analog actions only from a
// fresh focused sample. Reset this gate whenever the XR session is recreated.
class InputFocusGate final {
public:
    void reset() noexcept { seen_=false; latest_sequence_=0; }
    bool apply(wawvr::xr::ActionSnapshot& snapshot) noexcept {
        const bool fresh=!seen_ || snapshot.sequence>latest_sequence_;
        if (!seen_ || snapshot.sequence>latest_sequence_) latest_sequence_=snapshot.sequence;
        seen_=true;
        if (snapshot.focused && fresh) return true;
        snapshot.menu={};
        for (auto& hand : snapshot.hands) {
            hand.trigger={}; hand.trigger_click={}; hand.squeeze={}; hand.stick={};
            hand.primary={}; hand.secondary={}; hand.stick_click={}; hand.thumbrest={};
        }
        return false;
    }
private:
    std::uint64_t latest_sequence_{};
    bool seen_{};
};
} // namespace bovr::vr
