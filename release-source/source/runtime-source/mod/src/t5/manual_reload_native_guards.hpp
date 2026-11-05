// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "loader/native_span_guard.hpp"

namespace bovr::t5::manual_native {
inline constexpr bovr::native_guard::Span begin_guard{0x766220,441,"02a4f6d811f0d5723121eeb4a32427742770c99d98848e3561a85420587a70cf"};
inline constexpr bovr::native_guard::Span clip_guard{0x518030,381,"5972c060ce747b18920e945258af7e8e9a84af721eac7b08cac841caf1f15328"};
inline constexpr bovr::native_guard::Span bit_test_guard{0x40b3f0,35,"054fc7f231d0267e6cae0d819af5cbccbe22e717cf96479f012b7a471c212627"};
inline constexpr bovr::native_guard::Span fire_call_guard{0x766c84,5,"a6d7eb6f97b246e62b8122cef28cabffaa9bc797a761c11e37b7c63375365228"};
inline constexpr bovr::native_guard::Span reload_tail_guard{0x769376,5,"411d104b92e098cf596f9ed506885a7f8b575caccda76a20e94211a6728f062c"};
inline constexpr bovr::native_guard::Span empty_reload_disable_guard{0x766c12,16,"1b214f27c6009428a3782d2f82bc449b84264c5c50f0f2fda685e52c1ee9e25d"};
inline constexpr bovr::native_guard::Span button_reload_disable_guard{0x76919a,20,"649abd8a905196388ffa61712923f09d6502d1f9160bad21c38d40f28d80bdbd"};
inline constexpr bovr::native_guard::Span enable_reload_script_guard{0x7d8c70,110,"07877f899efbeda2f755478657afc81a85beb7c8c111b1053387695e2b01abe0"};
inline constexpr bovr::native_guard::Span disable_reload_script_guard{0x7d8ce0,110,"c80b771880374caabf0acf67658a6df85e2127f270a7153e62a180ac6af7a3b0"};
inline constexpr bovr::native_guard::Span server_wait_guard{0x640c50,130,"51d77e9e320c20bfbb798cf100904f24bbe2afda8741df5d285e330239932848"};
inline constexpr bovr::native_guard::Span server_command_sync_guard{0x829cd2,29,"56df978b58576d97ba7a5116be9e8f44268767906a68874ed90a4a1ae30a532e"};
inline constexpr bovr::native_guard::Span main_thread_init_guard{0x4aadd0,133,"662226974fed1791a0b13900c6c0b7961bbb3036f95fa62ba4b8316699942c6b"};
inline bool loaded_guards() noexcept {
    return
        bovr::native_guard::matches_absolute(begin_guard) &&
        bovr::native_guard::matches_absolute(clip_guard) &&
        bovr::native_guard::matches_absolute(bit_test_guard) &&
        bovr::native_guard::matches_absolute(fire_call_guard) &&
        bovr::native_guard::matches_absolute(reload_tail_guard) &&
        bovr::native_guard::matches_absolute(empty_reload_disable_guard) &&
        bovr::native_guard::matches_absolute(button_reload_disable_guard) &&
        bovr::native_guard::matches_absolute(enable_reload_script_guard) &&
        bovr::native_guard::matches_absolute(disable_reload_script_guard) &&
        bovr::native_guard::matches_absolute(server_wait_guard) &&
        bovr::native_guard::matches_absolute(server_command_sync_guard) &&
        bovr::native_guard::matches_absolute(main_thread_init_guard);
}
}
