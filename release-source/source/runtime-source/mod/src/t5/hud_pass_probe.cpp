// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/hud_pass_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstring>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t publish_call_rva=0x2d7bb8, publish_native_rva=0x2d5a40;
constexpr std::uintptr_t pass_call_rva=0x2eb829, pass_native_rva=0x2d2cd0;




// R_EndFrame resumes here, reads this frontend's array, then clears used/critical/last.


constexpr std::uintptr_t enclosing_prefix_rva=0x2d7b90,ordinary_end_call_rva=0x3a18aa;


std::uintptr_t hud_base{};
void* native_publish{};
void* native_pass{};
std::atomic<bool> installed{};
std::atomic<std::uint64_t> next_serial{};
SRWLOCK install_lock=SRWLOCK_INIT, callbacks_lock=SRWLOCK_INIT;
struct Callbacks {HudPassCallback published{},before{},after{};};
Callbacks callbacks;
struct Invocation {unsigned depth{};HudPassPoint point{};Callbacks selected{};};
thread_local Invocation publication, pass;
struct ExclusiveLock {
    SRWLOCK& lock;
    explicit ExclusiveLock(SRWLOCK& value) noexcept:lock(value){AcquireSRWLockExclusive(&lock);}
    ~ExclusiveLock(){ReleaseSRWLockExclusive(&lock);}
};
Callbacks read_callbacks() noexcept {
    AcquireSRWLockShared(&callbacks_lock);
    const auto result=callbacks;
    ReleaseSRWLockShared(&callbacks_lock);
    return result;
}
void callback_cpp(HudPassCallback callback,const HudPassPoint& point,const char* stage) noexcept {
    try {if(callback) callback(point);}
    catch(...) {trace("t5_hud_pass_callback_exception","stage=%s call=%llu kind=cpp",stage,point.call_serial);}
}
void callback_boundary(HudPassCallback callback,const HudPassPoint& point,const char* stage) noexcept {
    __try {callback_cpp(callback,point,stage);}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        trace("t5_hud_pass_callback_exception","stage=%s call=%llu kind=seh",stage,point.call_serial);
    }
}
void enter(Invocation& value,bool is_publication,std::uintptr_t enclosing_return=0) noexcept {
    if(value.depth++!=0) return;
    value.point={hud_base,GetCurrentThreadId(),next_serial.fetch_add(1,std::memory_order_relaxed)+1};
    value.point.enclosing_return=enclosing_return;
    value.selected=read_callbacks();
    if(!is_publication) callback_boundary(value.selected.before,value.point,"before_pass");
}
void leave(Invocation& value,bool is_publication) noexcept {
    if(!value.depth) return;
    if(value.depth>1) {--value.depth;return;}
    // Keep the outer depth occupied during callbacks as well as native execution.
    // A callback-triggered native invocation must not recursively call observers.
    const auto point=value.point;
    callback_boundary(is_publication?value.selected.published:value.selected.after,point,
                      is_publication?"published":"after_pass");
    value.depth=0;
}
void __cdecl enter_publication(std::uintptr_t original_entry_stack) noexcept {
    const DWORD saved=GetLastError();std::uintptr_t enclosing{};
    __try {if(original_entry_stack&&original_entry_stack<=UINTPTR_MAX-12)
        memcpy(&enclosing,reinterpret_cast<void*>(original_entry_stack+8),4);}
    __except(EXCEPTION_EXECUTE_HANDLER){enclosing=0;}
    enter(publication,true,enclosing);SetLastError(saved);
}
void __cdecl leave_publication() noexcept {
    const DWORD saved=GetLastError();leave(publication,true);SetLastError(saved);
}
void __cdecl enter_pass() noexcept {
    const DWORD saved=GetLastError();enter(pass,false);SetLastError(saved);
}
void __cdecl leave_pass() noexcept {
    const DWORD saved=GetLastError();leave(pass,false);SetLastError(saved);
}
// Both pinned CALL sites have no stack arguments. The wrapper contributes only
// its own return address; native routines retain their ordinary cdecl call/ret.
// Preserve all GP registers/flags and the complete x87/SSE image independently
// at entry and return. CLD applies only to our C++ code, never to native state.
__declspec(naked) void publication_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        lea eax,[ebp+36]
        push eax
        call enter_publication
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        call dword ptr [native_publish]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call leave_publication
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
__declspec(naked) void pass_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call enter_pass
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        call dword ptr [native_pass]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call leave_pass
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
bool guards_match(std::uintptr_t base) noexcept {
    if(!base||base>UINTPTR_MAX-ordinary_end_call_rva-5) return false;
    __try {
#if defined(BOVR_TEST_NATIVE_GUARDS)
        constexpr auto enclosing_hash="2c34ce1df23b838c5abf2a7f6437cca3d3067ed509ff25f11df6b11b582b51eb";
        constexpr auto ordinary_hash="a14f708eb5524d826e60e4e33798b1c6adbb9193de97ae97687ea15acb610a0e";
        constexpr auto publish_call_hash="e8dde0bd101f7fdcb64b9246c39376da8112baa9035b28cff8a3c5f525650f18";
        constexpr auto publish_native_hash="8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4";
        constexpr auto publish_return_hash="b0f66adc83641586656866813fd9dd0b8ebb63796075661ba45d1aa8089e1d44";
        constexpr auto pass_call_hash="2ac7a26d0d0f1bc4b2ed795694a5909fc62443e08f7e9c04646708012c1e392b";
        constexpr auto pass_native_hash=publish_return_hash;
        constexpr auto pass_return_hash="68b50534f0ab3d4a740d9915093dd8228a69f6c01867faaea436093efd3da225";
#else
        constexpr auto enclosing_hash="6d9eb66c6afe95ee4d7a51a4a180b66a755ecc436f09620ba37dec13c78fbf9a";
        constexpr auto ordinary_hash="28c531f31c97ee5a9b17202dd226dbb525bec68db4e655e58db98b6ffb056e93";
        constexpr auto publish_call_hash="e8fa2f40232236874f86e27e9acd5261af16af690f094acd6a03ed9270d5252c";
        constexpr auto publish_native_hash="e30f00976ffe21c7092e45f57cd0d438eb5a9d2ad8c4b7ab188571b95fad5d8a";
        constexpr auto publish_return_hash="0bd382ab4743a5c5461531a0bf479f3ec834a3f3663909fa0b175c43583f8104";
        constexpr auto pass_call_hash="60eb98d21be8e1de0d718cecdd037015bae73c430d3f381ed96262dec7b7a3d2";
        constexpr auto pass_native_hash="b29b43a4673ecadd61abbf15be09fc1b9003985bd3ab08f9426fb396fc09d556";
        constexpr auto pass_return_hash="4802a5f804ee2374dbb6beb0745c8993f0b46ef3c4b63306ec66399a692d3d35";
#endif
        return !bovr::native_guard::compare(reinterpret_cast<void*>(base+enclosing_prefix_rva),40,enclosing_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+ordinary_end_call_rva),5,ordinary_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+publish_call_rva),5,publish_call_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+publish_native_rva),5,publish_native_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+publish_call_rva+5),6,publish_return_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+pass_call_rva),5,pass_call_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+pass_native_rva),6,pass_native_hash)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+pass_call_rva+5),2,pass_return_hash);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void write_call(unsigned char* site,void* destination) noexcept {
    const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination)-reinterpret_cast<std::uintptr_t>(site)-5);
    memcpy(site+1,&relative,4); // The already-verified CALL opcode stays unchanged.
}
bool install_at(std::uintptr_t base) noexcept {
    ExclusiveLock serialized(install_lock);
    if(installed.load(std::memory_order_acquire)) return base==hud_base;
    if(!guards_match(base)) return false;
    auto* first=reinterpret_cast<unsigned char*>(base+publish_call_rva);
    auto* second=reinterpret_cast<unsigned char*>(base+pass_call_rva);
    DWORD old_first{},old_second{},unused{};
    if(!VirtualProtect(first,5,PAGE_EXECUTE_READWRITE,&old_first)) return false;
    if(!VirtualProtect(second,5,PAGE_EXECUTE_READWRITE,&old_second)) {
        VirtualProtect(first,5,old_first,&unused);return false;
    }
    if(!guards_match(base)) {
        VirtualProtect(second,5,old_second,&unused);VirtualProtect(first,5,old_first,&unused);return false;
    }
    hud_base=base;native_publish=reinterpret_cast<void*>(base+publish_native_rva);
    native_pass=reinterpret_cast<void*>(base+pass_native_rva);
    write_call(first,reinterpret_cast<void*>(&publication_detour));
    write_call(second,reinterpret_cast<void*>(&pass_detour));
    FlushInstructionCache(GetCurrentProcess(),first,5);FlushInstructionCache(GetCurrentProcess(),second,5);
    const bool restored_second=VirtualProtect(second,5,old_second,&unused)!=FALSE;
    const bool restored_first=VirtualProtect(first,5,old_first,&unused)!=FALSE;
    installed.store(true,std::memory_order_release);
    trace("t5_hud_pass_installed","publication_call=%p per_view_call=%p protection_restored=%u,%u callback_data_writes=0",
          first,second,restored_first,restored_second);
    return true;
}
}
bool install_hud_pass_probe(HMODULE image) noexcept {
    try {
        wchar_t option[8]{},menu[8]{};GetEnvironmentVariableW(L"BOVR_HUD_COMPOSITION_PROBE",option,8);
        GetEnvironmentVariableW(L"BOVR_MENU_FRAME_EPOCH_PROBE",menu,8);
        if((wcscmp(option,L"1")&&wcscmp(menu,L"1"))||image!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(image)!=0x400000) return false;
        std::string reason;
        return validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&
            install_at(reinterpret_cast<std::uintptr_t>(image));
    } catch(...) {return false;}
}
bool set_hud_pass_callbacks(HudPassCallback published,HudPassCallback before,HudPassCallback after) noexcept {
    if((published||before||after)&&!installed.load(std::memory_order_acquire)) return false;
    ExclusiveLock serialized(callbacks_lock);callbacks={published,before,after};return true;
}
}
