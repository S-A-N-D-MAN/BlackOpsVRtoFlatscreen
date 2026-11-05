// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace bovr::t5 {
// Retail SP cg+0x8A360: 0 is first person, 1 draws the third-person model,
// and 2 enables the first-person player shadow. The updater at 0x792E50
// sets 2 only from 0; native view selection tests exactly 1 at 0x79434D.
// This classifies this one field; callers retain their other context gates.
constexpr bool native_first_person_camera(int mode) noexcept {
    return mode == 0 || mode == 2;
}
}
