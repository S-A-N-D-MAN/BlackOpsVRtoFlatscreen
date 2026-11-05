// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/final_ui_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include "t5/hud_census.hpp"
#include <array>
#include <atomic>
#include <cstring>
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t final_call_rva=0x2eba14,loop_rva=0x2eb6a0,event_rva=0x189430;


#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr native_guard::Span event_guard{event_rva,6,"e4f521579f039f772ce89c42922dc54152fcb8030b95440c11b2d08eb8866e51"};
constexpr auto final_call_hash="e8dde0bd101f7fdcb64b9246c39376da8112baa9035b28cff8a3c5f525650f18";
constexpr auto loop_hash="af5ef8ef4b3e4e999866f18d9657973728318ca8d6f87836d13c29e5e6cec249";
#else
constexpr native_guard::Span event_guard{event_rva,6,"b29b43a4673ecadd61abbf15be09fc1b9003985bd3ab08f9426fb396fc09d556"};
constexpr auto final_call_hash="42d82dee26aeb3b5edaa8e2fc228bb3c82e24fe158e2b829828193e148d4a2bb";
constexpr auto loop_hash="2c1bd76d3883fc5263762ae8be8b4d890ebc8062f5eb97092c56c31be3c8d0ff";
#endif
std::uintptr_t final_base{},event_base{};
void* native_loop{};void* native_events{};
std::atomic<BeforeFinalUi> before_callback{};
std::atomic<AfterFinalUi> after_callback{};
std::atomic<void(*)()> event_callback{};
std::atomic<unsigned long> ui_calls{},event_calls{};
struct UiCall { unsigned depth{}; NativeFinalUi before{}; void* window{}; unsigned long serial{}; };
thread_local UiCall ui_call;
thread_local bool event_service_active{};
NativeFinalUi snapshot(void* commands,void* window) noexcept {
    NativeFinalUi result{};result.commands=reinterpret_cast<std::uintptr_t>(commands);
    __try {
        auto* backend=*reinterpret_cast<unsigned char**>(final_base+0x3a5b5f8);
        result.backend=reinterpret_cast<std::uintptr_t>(backend);
        if (result.backend!=final_base+0x3740100 && result.backend!=final_base+0x38c6400) return result;
        std::uintptr_t native_commands{};memcpy(&native_commands,backend+0x16cd24,4);
        if (!commands || window || native_commands!=result.commands) return result;
        memcpy(&result.native_frame,backend+0x15d7a0,4);
        memcpy(&result.connection_state,reinterpret_cast<void*>(final_base+0x2510164),4);
        memcpy(&result.key_catchers,reinterpret_cast<void*>(final_base+0x2510160),4);
        memcpy(&result.menu_count,reinterpret_cast<void*>(final_base+0x216bf4c),4);
        result.valid=read_native_menu_page(final_base,&result.page);
    } __except(EXCEPTION_EXECUTE_HANDLER) {result.valid=false;}
    return result;
}
void __cdecl before_ui(void* commands,void* window) noexcept {
    if (ui_call.depth++!=0) return;
    ui_call.before=snapshot(commands,window);ui_call.window=window;
    ui_call.serial=++ui_calls;
    observe_hud_commands(final_base,ui_call.before,ui_call.serial);
    try {if (const auto callback=before_callback.load()) callback(ui_call.before);}
    catch (...) {trace("t5_final_ui_callback_exception","stage=before");}
}
void __cdecl after_ui() noexcept {
    if (!ui_call.depth || --ui_call.depth!=0) return;
    const auto after=snapshot(reinterpret_cast<void*>(ui_call.before.commands),ui_call.window);
    if (ui_call.serial<=12 || ui_call.serial%120==0)
        trace("t5_final_ui","call=%lu valid_before=%u valid_after=%u backend=0x%lx native_frame=%lu commands=0x%lx state=%d catchers=0x%x menus=%d same_frame=%u original_calls=1",
              ui_call.serial,ui_call.before.valid,after.valid,static_cast<unsigned long>(after.backend),after.native_frame,
              static_cast<unsigned long>(after.commands),after.connection_state,after.key_catchers,after.menu_count,
              after.backend==ui_call.before.backend && after.native_frame==ui_call.before.native_frame);
    try {if (const auto callback=after_callback.load()) callback(ui_call.before,after);}
    catch (...) {trace("t5_final_ui_callback_exception","stage=after");}
}
// Exact final-only CALL ABI: ECX=command stream, EAX=null UI3D window,
// no stack arguments. Restore entry state before original, and original return
// state after passive/custom completion callbacks (including flags/x87/SSE).
__declspec(naked) void final_ui_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+28]
        push dword ptr [ebp+24]
        call before_ui
        lea esp,[esp+8]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        call dword ptr [native_loop]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call after_ui
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
void __cdecl after_events() noexcept {
    if (event_service_active) return;
    event_service_active=true;
    const auto serial=++event_calls;
    if (serial<=6 || serial%120==0) trace("t5_event_loop","call=%lu original_drained=1",serial);
    try {if (const auto callback=event_callback.load()) callback();}
    catch (...) {trace("t5_event_callback_exception","original_drained=1");}
    event_service_active=false;
}
__declspec(naked) void event_detour() {
    __asm {
        call dword ptr [native_events]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call after_events
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
void relative(unsigned char* location,unsigned char opcode,const void* target) noexcept {
    location[0]=opcode;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(location)-5);
    memcpy(location+1,&displacement,4);
}
bool install_final_at(std::uintptr_t base) noexcept {
    if (native_loop) return base==final_base;
    auto* call=reinterpret_cast<unsigned char*>(base+final_call_rva);
    if (bovr::native_guard::compare(call,5,final_call_hash) || bovr::native_guard::compare(reinterpret_cast<void*>(base+loop_rva),5,loop_hash)) return false;
    DWORD previous{},unused{};
    if (!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&previous)) return false;
    if (bovr::native_guard::compare(call,5,final_call_hash)) {VirtualProtect(call,5,previous,&unused);return false;}
    final_base=base;native_loop=reinterpret_cast<void*>(base+loop_rva);
    relative(call,0xe8,reinterpret_cast<void*>(&final_ui_detour));
    const bool restored=VirtualProtect(call,5,previous,&unused)!=FALSE;
    FlushInstructionCache(GetCurrentProcess(),call,5);
    trace("t5_final_ui_installed","call=%p target=%p span=5 protection_restored=%u",call,native_loop,restored);return true;
}
bool install_event_at(std::uintptr_t base) noexcept {
    if (native_events) return base==event_base;
    auto* entry=reinterpret_cast<unsigned char*>(base+event_rva);
    if (!native_guard::matches(base,event_guard)) return false;
    std::array<unsigned char,6> original{};
    memcpy(original.data(),entry,original.size());
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!trampoline) return false;
    memcpy(trampoline,original.data(),original.size());relative(trampoline+6,0xe9,entry+6);
    DWORD previous{},unused{};
    if (!VirtualProtect(trampoline,11,PAGE_EXECUTE_READ,&previous)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,11);
    if (!VirtualProtect(entry,6,PAGE_EXECUTE_READWRITE,&previous)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    if (!native_guard::matches(base,event_guard)) {VirtualProtect(entry,6,previous,&unused);VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    event_base=base;native_events=trampoline;
    relative(entry,0xe9,reinterpret_cast<void*>(&event_detour));entry[5]=0x90;
    const bool restored=VirtualProtect(entry,6,previous,&unused)!=FALSE;
    FlushInstructionCache(GetCurrentProcess(),entry,6);
    trace("t5_event_loop_installed","entry=%p span=6 protection_restored=%u",entry,restored);return true;
}
bool accepted(HMODULE image,const wchar_t* option) {
    wchar_t enabled[8]{};GetEnvironmentVariableW(option,enabled,8);
    if (wcscmp(enabled,L"1") || image!=GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(image)!=0x400000) return false;
    std::string reason;
    return validate_path(executable_path()).accepted && validate_loaded_headers(image,reason);
}
}
bool install_final_ui_probe(HMODULE image) noexcept {
    try {return accepted(image,L"BOVR_FINAL_UI_PROBE") && install_final_at(reinterpret_cast<std::uintptr_t>(image));}
    catch (...) {return false;}
}
bool install_event_loop_probe(HMODULE image) noexcept {
    try {return accepted(image,L"BOVR_EVENT_LOOP_PROBE") && install_event_at(reinterpret_cast<std::uintptr_t>(image));}
    catch (...) {return false;}
}
bool set_final_ui_callbacks(BeforeFinalUi before,AfterFinalUi after) noexcept {
    if ((before || after) && !native_loop) return false;
    before_callback=before;after_callback=after;return true;
}
bool set_event_loop_callback(void(*callback)()) noexcept {
    if (callback && !native_events) return false;
    event_callback=callback;return true;
}
}
