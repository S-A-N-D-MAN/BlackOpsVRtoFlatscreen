// SPDX-License-Identifier: GPL-3.0-only
// Exact retail SP cannon command/consumer proof: alpha21/CANNON-NATIVE-MAPPING.json.
#pragma once
#include "loader/native_span_guard.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>
namespace bovr::t5::helicopter_cannon_delivery {
using Guard = bovr::native_guard::Span;
inline constexpr Guard guards[]{
    {0x4bba4d,43,"fdd18774c9fd4e98d4cd1407692791e92b5251fcf96706cbc4dca1afd8deeb3f"}, // vehicleattack_registration
    {0x880470,13,"a024dfa8c5dbc1b17bd41fc838c29a73db7a616aad7682c637137758ebe497f7"}, // vehicleattack_down
    {0x880480,13,"165c9f80754ca6d02c146176d4bebf19d9b1b80d47387320fe8a0d97008981b4"}, // vehicleattack_up
    {0xa44284,15,"c86921b6eea4ae1042288c14407a008fbdc545b2b87e2d3cd64e0f2c9e2d0859"}, // vehicleattack_name
    {0x9f9bc8,15,"0605954542d60d727e3f3c148862c6ad31ade4ca8a5b8568193d945af975eff4"}, // vehicleattack_release_name
    {0x881240,29,"bf944ccb19df3e2c0304d41b649f9ffa991b283cb48be1855c38352035b32886"}, // command_button_base
    {0x881446,29,"60dcded8a8ae24abdfe00079193fc7794c6848c50c8688141e94d4292b9e8b8f"}, // vehicleattack_button34_build
    {0x8111c7,161,"95aa81723baa189a4fa1d27d05cbf3831177ac6569d7a8a24bfebef5a3594ac1"}, // driver_local_fire_gate
};
inline bool loaded_guards(const unsigned char* image) noexcept {
    if(reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
    __try {
        for(const auto& g:guards)
            if(!bovr::native_guard::matches(image+g.address-0x400000,g.size,g.sha256))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
