// SPDX-License-Identifier: GPL-3.0-only
#include "t5/dual_pistol_projectile.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#if !defined(BOVR_TEST_DUAL_PROJECTILE_ABI)
#include "t5/dual_pistol_shot.hpp"
#include "t5/identity.hpp"
#include "loader/native_span_guard.hpp"
#endif
#if !defined(_M_IX86)
#error Pinned dual pistol projectile bridge requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
void* original_projectile{};
#if defined(BOVR_TEST_DUAL_PROJECTILE_ABI)
bool (__cdecl* test_select)(const std::uint32_t*,unsigned,unsigned char*){};
#else
bool prepared{},installed{};
std::array<unsigned char,113> caller{};
std::array<unsigned char,502> callee{};
std::array<unsigned char,5> original_call{};
#endif
void __cdecl before_projectile(std::uint32_t* saved) noexcept {
    const auto error=GetLastError();
    // Saved layout: eight registers, flags, 68 scratch bytes, private WP
    // pointer, then the untouched native return and seven native arguments.
    const auto* stack=saved+27;auto* scratch=reinterpret_cast<unsigned char*>(saved+9);
    saved[26]=stack[4];
#if defined(BOVR_TEST_DUAL_PROJECTILE_ABI)
    const bool selected=test_select&&test_select(stack,saved[2],scratch);
#else
    const bool selected=select_dual_pistol_projectile_shot(0x400000,stack,saved[2],scratch);
#endif
    if(selected)saved[26]=reinterpret_cast<std::uintptr_t>(scratch);
    SetLastError(error);
}
__declspec(naked) void projectile_bridge() {
    __asm {
        lea esp,[esp-72]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call before_projectile
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        push dword ptr [esp+100]
        push dword ptr [esp+100]
        push dword ptr [esp+100]
        push dword ptr [esp+80]
        push dword ptr [esp+100]
        push dword ptr [esp+100]
        push dword ptr [esp+100]
        call dword ptr [original_projectile]
        lea esp,[esp+28]
        lea esp,[esp+72]
        ret
    }
}
}
#if defined(BOVR_TEST_DUAL_PROJECTILE_ABI)
void* dual_pistol_projectile_test_entry(void* native,bool (__cdecl* select)(const std::uint32_t*,unsigned,unsigned char*)) noexcept {
    original_projectile=native;test_select=select;return reinterpret_cast<void*>(&projectile_bridge);
}
#else
bool prepare_dual_pistol_projectile(HMODULE executable) noexcept {
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    if(installed)return true;
    prepared=false;
    try {
        std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        if(native_guard::compare(reinterpret_cast<void*>(0x4cfe25),caller.size(),
            "c8f902088a5c44e10a7ad4532351ce838b9413ed3eba86466a27e61167db8e9e")||
           native_guard::compare(reinterpret_cast<void*>(0x681ea0),callee.size(),
            "f490f0e4295e8d16b02af6e38d1c471b6cd8984285866b7671c5a356979d0e21"))return false;
        memcpy(caller.data(),reinterpret_cast<const void*>(0x4cfe25),caller.size());
        memcpy(callee.data(),reinterpret_cast<const void*>(0x681ea0),callee.size());
        memcpy(original_call.data(),caller.data()+0x69,5);
        prepared=native_guard::matches(caller.data(),caller.size(),
            "c8f902088a5c44e10a7ad4532351ce838b9413ed3eba86466a27e61167db8e9e")&&
            native_guard::matches(callee.data(),callee.size(),
            "f490f0e4295e8d16b02af6e38d1c471b6cd8984285866b7671c5a356979d0e21");
        return prepared;
    }catch(...){return false;}
}
bool commit_dual_pistol_projectile() noexcept {
    if(installed)return true;if(!prepared)return false;
    // The coordinator owns peer suspension and serialization. No hash, log,
    // allocation, file operation or mod lock belongs in this commit function.
    __try {
        auto* site=reinterpret_cast<unsigned char*>(0x4cfe8e);
        if(memcmp(reinterpret_cast<const void*>(0x4cfe25),caller.data(),caller.size())||
           memcmp(reinterpret_cast<const void*>(0x681ea0),callee.data(),callee.size()))return false;
        DWORD protection{},old{};if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        if(memcmp(site,original_call.data(),5)){VirtualProtect(site,5,protection,&old);return false;}
        original_projectile=reinterpret_cast<void*>(0x681ea0);
        const auto offset=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&projectile_bridge)-reinterpret_cast<std::uintptr_t>(site+5));
        memcpy(site+1,&offset,4);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
        const bool restored=VirtualProtect(site,5,protection,&old)!=FALSE;
        if(!flushed||!restored){DWORD writable{},ignored{};
            if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&writable)){
                memcpy(site,original_call.data(),5);FlushInstructionCache(GetCurrentProcess(),site,5);
                VirtualProtect(site,5,protection,&ignored);}
            return false;}
        installed=true;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#endif
}
