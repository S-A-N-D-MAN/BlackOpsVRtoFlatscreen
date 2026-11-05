// SPDX-License-Identifier: GPL-3.0-only
// Retail callee contracts are represented by relative address, size and SHA-256.
#pragma once
#include "loader/native_span_guard.hpp"

namespace bovr::t5::manual_grenade_native_guards {
inline constexpr std::uintptr_t execute_rva=0x219d00;
inline constexpr bovr::native_guard::Span execute_entry_guard{execute_rva,28,"6c96774c46507a14060204cf64d9fc2c9249fd2ed8e3665e8d59a62a14e6a71a"};
inline constexpr std::uintptr_t update_view_weapon_anim_rva=0xb6870;
inline constexpr bovr::native_guard::Span update_view_weapon_anim_entry_guard{update_view_weapon_anim_rva,32,"9a9bbb0229fb527691231432bd35cffd4aafa9dd26db0bce98c63cde10800571"};
inline constexpr std::uintptr_t add_view_weapon_rva=0x277610;
inline constexpr bovr::native_guard::Span add_view_weapon_entry_guard{add_view_weapon_rva,32,"72e5277690b3cf143f3a6c27bb1260f20eb5b0fa90f9b2a77e551f4be977fa55"};

inline bool entry_matches(const unsigned char* image,
                          const bovr::native_guard::Span& guard) noexcept {
    return image && bovr::native_guard::matches(image+guard.address,guard.size,guard.sha256);
}
inline bool execute_loaded(const unsigned char* image) noexcept {
    return entry_matches(image,execute_entry_guard);
}
inline bool renderer_loaded(const unsigned char* image) noexcept {
    return entry_matches(image,update_view_weapon_anim_entry_guard)&&
        entry_matches(image,add_view_weapon_entry_guard);
}
} // namespace bovr::t5::manual_grenade_native_guards
