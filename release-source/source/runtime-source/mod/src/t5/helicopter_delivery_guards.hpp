// SPDX-License-Identifier: GPL-3.0-only
// Generated from reviewed alpha20 helicopter registration, consumer and identity spans.
#pragma once
#include "loader/native_span_guard.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>
namespace bovr::t5::helicopter_delivery {
using Guard = bovr::native_guard::Span;
inline constexpr Guard guards[]{
    {0x42df40,612,"7183ff7d42606aa6e275dd8beac1dc4db6c74ebed8e4d7368e049500ce722eb6"}, // chopper_driver_and_control_modes
    {0x4e8bc0,74,"755db192a0ff81afc7b531ac5e329ffc71d6324651eaa07143c57fa5bc1595cb"}, // seat_occupant_entity
    {0x409ba3,51,"d1506f32fa257128fce612458c377c7255b883ff66fb0bc7e06d824c6f40dbcb"}, // yaw_altitude_default2_registration
    {0x409c47,60,"0709ac30098e6413be7e24b15d8bcf274c24d6d32347336db156997f92d02b8c"}, // invert_up_down_defaultfalse_registration
    {0x409dc9,55,"6420ce9828a3249acf366a76a3ccb896241426b6aadbd08cac4044bc8b5110b8"}, // always_face_camera_defaulttrue_registration
    {0x4bba4d,181,"50e68f2361dd037e3cfd1efa2f2c88b724cae7debd536d733bc77573430ddcbc"}, // vehicle_key_command_registration
    {0x880470,125,"bd7a53b38be792580dda68853c703e930b6ae8bd9198115d2e9180e414897ed2"}, // vehicle_key_handlers
    {0x881240,666,"1e14a12c9e16e5ab99ac050b1860798e696cc9c1c1450f2157a080b337a21b17"}, // native_command_buttons
    {0x4bb92f,83,"83d7ee622250215b1e10ebf2790e20092ee3089fea5cee4a21bcd4ed3f559040"}, // throw_speedthrow_registration
    {0x880940,29,"ca079640724773e4520aff968ce1703e79c34a086a56383dc479756b6a76bb34"}, // throw_handler
    {0x880a20,93,"30bc53d828bc6055e97bb304ba7bdee8fac72b689d02f953c8f537e7962ebf40"}, // speedthrow_handler
    {0x466b20,143,"2b7c031a1700aa408c12611e75730d04e50bc79492ad008fb51e9b0a7a39510b"}, // throw_script_method
    {0x487ad0,166,"44c66dca79ed448ed6b28509ded55ae1b7beb16ef0954627813592b3568647ae"}, // reload_script_method
    {0x4fed40,143,"05fd17b808f5251e09ca9aa4c8ec474b24f4c231418f3b1e5e7f609b4c812c7c"}, // attack_script_method
    {0x88227d,64,"64b40b1d34dd8cc73e4ae531e9ad3acc5eb19de69a67c3b2a82429421241d24f"}, // gamepad_angles_sink
    {0x881cd2,235,"115a8567d7da4db699da217b112f32dd2f9f0e2ffeaefeff1bfc34961695010c"}, // mouse_angles_and_axis_bytes
    {0x5fba30,116,"60b2e3fbe2844d98d0dea419ac4342bf73765e31d5145ebd7c6e4b20e5f029ca"}, // VEH_LinkPlayer_entry_role_roots
    {0x5fbd81,260,"6540e9173103ad872747b5860fb2bbdc54ba54d44f3fa5484d0c9dc92c7336b2"}, // VEH_LinkPlayer_seat_and_ps_identity_publication
    {0x4e8bc0,74,"755db192a0ff81afc7b531ac5e329ffc71d6324651eaa07143c57fa5bc1595cb"}, // VEH_GetSeatOccupantEntity_complete
    {0x5389d0,16,"16324a09c97e3dbd710060b3f6cfe3ad4b45b60615f0fe487d3925f302e26198"}, // BG_GetVehicleInfo_complete
    {0x678a20,34,"50feb34c44fa92356b70cdecd7872aaca1c8fb23adf419a3e488f7d470324a79"}, // VEH_Update_info_root
    {0x678afc,90,"28073ecd53c4bd3714158e8f7659d182923945c417a5ee67975c440450b6bf59"}, // VEH_Update_chopper_type_and_control_dispatch
    {0x42df40,199,"e03485915cf9959fdcc4597667f316ae1ca4610d5bd18bc7e53dbe1b188813a6"}, // VEH_UpdateClientChopper_driver_and_control_gates
    {0x5f3c87,47,"ee46925471d46058916dbf228e7ace4bf0590a1b93f8b0f45ff7c1ed6a6187fe"}, // EntHandle_set_ent_number_plus_one
};
inline bool loaded_guards(const unsigned char* image) noexcept {
    if(reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
    __try {
        for(const auto& g:guards)if(!bovr::native_guard::matches(image+g.address-0x400000,g.size,g.sha256))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
