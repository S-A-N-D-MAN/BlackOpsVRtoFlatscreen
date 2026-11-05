// SPDX-License-Identifier: GPL-3.0-only
// Pinned SP registration/handler/input-consumer bytes from native-look-alias-review-runtime/native-mapping.json.
#pragma once
#include "loader/native_span_guard.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <windows.h>
namespace bovr::t5::mounted_delivery {
using Guard = bovr::native_guard::Span;
#if defined(BOVR_TEST_NATIVE_GUARDS)
inline constexpr Guard adjust_angles_guard{
    0x880d40,191,"50b5a0a1d8505b42b7686d846de866287584e786452f792c132c3e52ffd39310"}; // authored zero span
#else
inline constexpr Guard adjust_angles_guard{
    0x880d40,191,"190735958101da2cab9a872982c482f75e60bbaff5a94eea5e86cab915250bd6"}; // CL_AdjustAngles
#endif
inline constexpr Guard guards[]{
    {0x4bb5aa,20,"ee82d861c297bff7a78cee29b604409b3e707b48d263946e47d8962a0639dc3f"}, // +lookup registration
    {0x4bb5be,20,"41f6ab9a6b2dcfdcc1e13ce606d75ec6b00fafd22fc176589adf81db30600f16"}, // -lookup registration
    {0x4bb5d2,20,"f1fe0a0c8a152cbc51fcc947e72fc354707b3a0aea0fbf9f8c0e61c8aec755e1"}, // +lookdown registration
    {0x4bb5e6,20,"e81fab37ed005ce06a8a09f4d1076252fa5b87719805b489661ff7118bd76318"}, // -lookdown registration
    {0x87fd60,198,"c88e91e0a188911b482377d1923d7b97eea0388bc144801616e6ba85ce6fd0e0"}, // IN_KeyDown
    {0x87fe30,151,"7a5e535e7c3f64b644270a451659e1b9af1c8da8d8997082fa80bd1adb65be02"}, // IN_KeyUp
    {0x87fed0,98,"b83b18d0cb1d45523228f82438cca0177318f2935a0b6a1b80e29244dccde24b"}, // CL_KeyState
    adjust_angles_guard,
    {0x590f9f,133,"4af96988da1b213fb7ec7a45d05bf8ea6e5425e59c49ce04cbeca14397493295"}, // pitch_speed_registrations
    {0x880060,13,"bac10db3ec1c70c611eca8df1ad6bd07d77f39892c91a6cace28b3ce8459a9d1"}, // +lookup_handler
    {0x880070,13,"65b99d6266ca890ed599978cbebe5946a2fe09d5f550cb7b7399dbdceebcc9d1"}, // -lookup_handler
    {0x880080,13,"11d769750d501ac09df58c2326e4d7a2e577b9509c574879b575ecb0d4e7282a"}, // +lookdown_handler
    {0x880090,13,"5c6a8f68b19fb6815118716479b73b5eff6e7aa83b05202a641126b867839d20"}, // -lookdown_handler
    {0xa22d60,8,"39eb7a51d7b45b41ce89161c346863ff3d5ab907d16c089490dbd94e756872a4"}, // +lookup literal
    {0x9df91c,8,"c46522db9a9b7a9f8ae323d284b7057db95ff2f67597e7cf99958a9806bfff8d"}, // -lookup literal
    {0xa12400,10,"b629ec66c948ac5a053e16578782024ed902a6af4232fad9cd1d211958950b14"}, // +lookdown literal
    {0x9c3904,10,"418f94d305e3270bd097de5430eb86576d26bf78576bcef49470e5667f730d88"}, // -lookdown literal
};
inline bool adjust_angles_loaded_guard(const unsigned char* image) noexcept {
    if(!image)return false;
    __try {return bovr::native_guard::matches(image+adjust_angles_guard.address-0x400000,
        adjust_angles_guard.size,adjust_angles_guard.sha256);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool loaded_guards(const unsigned char* image) noexcept {
    if(!image)return false;
    __try {for(const auto& g:guards)if(!bovr::native_guard::matches(image+g.address-0x400000,g.size,g.sha256))return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return true;
}
}
