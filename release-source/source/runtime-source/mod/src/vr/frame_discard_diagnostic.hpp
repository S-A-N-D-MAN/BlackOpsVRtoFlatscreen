// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace bovr::vr {
// Owner-thread log budget only; never input to frame admission or retirement.
struct FrameDiscardDiagnosticBudget final {
    unsigned startup{},gameplay{};
    bool admit(bool enabled,bool handheld_seen) noexcept {
        if(!enabled)return false;
        auto& used=handheld_seen?gameplay:startup;
        const unsigned maximum=handheld_seen?256u:8u;
        if(used>=maximum)return false;
        ++used;return true;
    }
    bool available(bool enabled,bool handheld_seen) const noexcept {
        return enabled&&(handheld_seen?gameplay<256u:startup<8u);
    }
};
}
