// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace bovr::t5 {
// In particular, a setter can mutate state and then lose ownership or fail.
// The caller records its restoration obligation before entering this seam.
template<class Current,class Call> bool water_owned_call(Current&& current,Call&& call) noexcept {
    if(!current())return false;
    const bool succeeded=call();
    const bool still_current=current();
    return succeeded&&still_current;
}
}
