// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace bovr::t5::manual_reload_flag_policy {
struct FlagDecision {
    bool write_disable{};
    bool own_after{};
};

// A preexisting unknown DisableReload bit does not grant ownership and is not
// itself an explicit-manual-reload admission failure. The caller still checks
// the native weapon, gameplay, transaction, and synchronized storage guards.
// current_owned means a proven owner of this exact current role identity.
// If write_disable is true, publish own_after only after the guarded native
// write succeeds and its result is verified. A decision alone owns nothing.
constexpr FlagDecision decide(bool bit_set,bool current_owned) noexcept {
    return {!bit_set,current_owned||!bit_set};
}
}
