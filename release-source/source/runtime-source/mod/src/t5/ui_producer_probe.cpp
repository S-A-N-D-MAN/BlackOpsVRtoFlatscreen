// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/ui_producer_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstring>
#include <limits>

namespace bovr::t5 {
bool same_ui_production_range(const NativeUiProduction& a,const NativeUiProduction& b) noexcept {
    return a.valid && b.valid && a.call_serial && a.call_serial==b.call_serial &&
        a.thread_id==b.thread_id && a.local_client==b.local_client && a.refreshed_ui==b.refreshed_ui &&
        a.frontend==b.frontend && a.native_frame==b.native_frame && a.command_array==b.command_array &&
        a.bytes==b.bytes && a.capacity==b.capacity && a.configured_max_views==b.configured_max_views &&
        a.used_total>=0 && a.capacity>=4 && a.used_total<=a.capacity-4 &&
        b.used_total>=a.used_total+4 && b.used_total<=b.capacity &&
        a.bytes<=std::numeric_limits<std::uintptr_t>::max()-static_cast<unsigned>(a.used_total) &&
        b.commands==a.bytes+static_cast<unsigned>(a.used_total);
}
namespace {
constexpr std::uintptr_t call_rva=0x3a187e,producer_rva=0x3a1590;
#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr const char* call_sha256="76e5885791091de99b664414aa2fa081d791c954a38d8fdf9abef5d6d79f3be5"; // authored relative CALL
constexpr const char* producer_sha256="3055d3160ab94f3eac2b639a9eb4643d76b540613a58e5e366a9f25c026f59d0"; // eighteen authored NOPs
constexpr const char* array_sha256="b0f66adc83641586656866813fd9dd0b8ebb63796075661ba45d1aa8089e1d44"; // six authored zeros
constexpr const char* capacity_sha256="15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"; // twelve authored zeros
#else
constexpr const char* call_sha256="76e5885791091de99b664414aa2fa081d791c954a38d8fdf9abef5d6d79f3be5";
constexpr const char* producer_sha256="f1bbdcc55921d19db16070c66c60e473e5de66dffbb0b91bea5dbec3c3aea7bc";
constexpr const char* array_sha256="5875755c1e0b86dab35b026c7624e951a4d2d1d2fa62b7046595167fd0221fd8";
constexpr const char* capacity_sha256="4449a6aec47c6a3dd905786c0ca5943f281d7af5dcca7a71118d7c7545255568";
#endif
std::uintptr_t producer_base{};
void* native_producer{};
std::atomic<BeforeUiProduction> before_callback{};
std::atomic<AfterUiProduction> after_callback{};
std::atomic<std::uint64_t> call_serial{};
std::atomic<DWORD> owner_thread{};
std::atomic<unsigned> diagnostic_before_entries{},diagnostic_after_entries{};
struct CallState {unsigned depth{};bool nested{},callback_failed{};NativeUiProduction before{};};
thread_local CallState current_call;

NativeUiProduction snapshot(int local_client,int refreshed_ui,std::uint64_t serial) noexcept {
    NativeUiProduction result{};
    result.call_serial=serial;result.thread_id=GetCurrentThreadId();
    result.local_client=local_client;result.refreshed_ui=refreshed_ui;
    if (!producer_base || local_client!=0) return result;
    __try {
        const auto base=producer_base;
        result.frontend=*reinterpret_cast<std::uintptr_t*>(base+0x373708c);
        unsigned slot{};
        if (result.frontend==base+0x3740100) slot=0;
        else if (result.frontend==base+0x38c6400) slot=1;
        else return result;
        auto* frontend=reinterpret_cast<unsigned char*>(result.frontend);
        memcpy(&result.command_array,frontend+0x16cc34,4);
        if (result.command_array!=base+0x3737090+slot*0x18) return result;
        std::uint32_t array[6]{},verify[6]{};
        memcpy(array,reinterpret_cast<void*>(result.command_array),sizeof(array));
        result.bytes=array[0];result.used_total=static_cast<int>(array[1]);result.used_critical=static_cast<int>(array[2]);
        result.capacity=static_cast<int>(array[4]);result.warn_size=static_cast<int>(array[5]);
        result.configured_max_views=*reinterpret_cast<int*>(base+0x356a480);
        if (result.configured_max_views<1 || result.configured_max_views>4 ||
            result.capacity!=0x18000*result.configured_max_views ||
            result.used_total<0 || result.used_total>result.capacity || result.used_critical<0 ||
            result.used_critical>result.used_total || (result.used_total&3) ||
            !result.bytes || (result.bytes&3) || result.bytes>UINTPTR_MAX-static_cast<unsigned>(result.capacity)) return result;
        memcpy(&result.native_frame,frontend+0x15d7a0,4);
        memcpy(&result.commands,frontend+0x16cd24,4);
        result.connection_state=*reinterpret_cast<int*>(base+0x2510164);
        result.key_catchers=*reinterpret_cast<int*>(base+0x2510160);
        result.menu_count=*reinterpret_cast<int*>(base+0x216bf4c);
        if (result.connection_state<0 || result.connection_state>10 || result.menu_count<0 ||
            !read_native_menu_page(base,&result.page)) return result;
        memcpy(verify,reinterpret_cast<void*>(result.command_array),sizeof(verify));
        std::uint32_t frame_verify{};std::uintptr_t array_verify{};
        memcpy(&frame_verify,frontend+0x15d7a0,4);memcpy(&array_verify,frontend+0x16cc34,4);
        result.valid=memcmp(array,verify,sizeof(array))==0 && frame_verify==result.native_frame &&
            array_verify==result.command_array && *reinterpret_cast<std::uintptr_t*>(base+0x373708c)==result.frontend;
    } __except(EXCEPTION_EXECUTE_HANDLER) {result.valid=false;}
    return result;
}
void __cdecl before_production(int local_client,int refreshed_ui) noexcept {
    const DWORD previous_error=GetLastError();
    const auto diagnostic_entry=diagnostic_before_entries.fetch_add(1,std::memory_order_relaxed)+1;
    if (diagnostic_entry<=3)
        trace("t5_ui_producer_stage","stage=before_tls entry=%u local=%d refreshed=%d",diagnostic_entry,local_client,refreshed_ui);
    if (current_call.depth++!=0) {current_call.nested=true;SetLastError(previous_error);return;}
    current_call.nested=false;current_call.callback_failed=false;
    current_call.before=snapshot(local_client,refreshed_ui,++call_serial);
    if (diagnostic_entry<=3)
        trace("t5_ui_producer_stage","stage=snapshot_complete entry=%u call=%llu valid=%u",diagnostic_entry,current_call.before.call_serial,current_call.before.valid);
    DWORD expected{};
    if (current_call.before.valid) owner_thread.compare_exchange_strong(expected,current_call.before.thread_id);
    current_call.before.valid=current_call.before.valid && owner_thread.load()==current_call.before.thread_id;
    try {if (const auto callback=before_callback.load()) callback(current_call.before);}
    catch (...) {current_call.callback_failed=true;trace("t5_ui_producer_callback_exception","stage=before");}
    SetLastError(previous_error);
}
void __cdecl after_production() noexcept {
    const DWORD previous_error=GetLastError();
    const auto diagnostic_entry=diagnostic_after_entries.fetch_add(1,std::memory_order_relaxed)+1;
    if (diagnostic_entry<=3)
        trace("t5_ui_producer_stage","stage=after_native_before_tls entry=%u",diagnostic_entry);
    if (!current_call.depth || --current_call.depth!=0) {SetLastError(previous_error);return;}
    const auto& before=current_call.before;
    auto after=snapshot(before.local_client,before.refreshed_ui,before.call_serial);
    after.valid=after.valid && owner_thread.load()==after.thread_id && !current_call.nested && !current_call.callback_failed;
    after.completed_range=same_ui_production_range(before,after);
    if (before.call_serial<=12 || before.call_serial%120==0)
        trace("t5_ui_producer","call=%llu thread=%lu complete=%u frontend=0x%lx frame=%lu array=0x%lx bytes=0x%lx used_before=%d used_after=%d capacity=%d commands=0x%lx state=%d catchers=0x%x original_calls=1",
              before.call_serial,after.thread_id,after.completed_range,static_cast<unsigned long>(after.frontend),after.native_frame,
              static_cast<unsigned long>(after.command_array),static_cast<unsigned long>(after.bytes),before.used_total,after.used_total,
              after.capacity,static_cast<unsigned long>(after.commands),after.connection_state,after.key_catchers);
    try {if (const auto callback=after_callback.load()) callback(before,after);}
    catch (...) {trace("t5_ui_producer_callback_exception","stage=after");}
    SetLastError(previous_error);
}
// Exact call-site ABI: EDI localClient, one cdecl stack argument refreshedUI.
// Extra copied argument is necessary: the wrapper contributes its return address.
// LEA removes it without changing the original return flags.
__declspec(naked) void producer_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+40]
        push dword ptr [ebp]
        call before_production
        lea esp,[esp+8]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        push dword ptr [esp+4]
        call dword ptr [native_producer]
        lea esp,[esp+4]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        call after_production
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
bool install_at(std::uintptr_t base) noexcept {
    if (native_producer) return base==producer_base;
    auto* call=reinterpret_cast<unsigned char*>(base+call_rva);
    if (bovr::native_guard::compare(call,5,call_sha256)||bovr::native_guard::compare(reinterpret_cast<void*>(base+producer_rva),18,producer_sha256)||
        bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2d5bc8),6,array_sha256)||
        bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2d48e2),12,capacity_sha256)) return false;
    DWORD old{},unused{};
    if (!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old)) return false;
    if (bovr::native_guard::compare(call,5,call_sha256)) {VirtualProtect(call,5,old,&unused);return false;}
    producer_base=base;native_producer=reinterpret_cast<void*>(base+producer_rva);
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&producer_detour)-reinterpret_cast<std::uintptr_t>(call)-5);
    call[0]=0xe8;memcpy(call+1,&displacement,4);
    const bool restored=VirtualProtect(call,5,old,&unused)!=FALSE;
    FlushInstructionCache(GetCurrentProcess(),call,5);
    trace("t5_ui_producer_installed","call=%p target=%p span=5 protection_restored=%u",call,native_producer,restored);
    return true;
}
}
bool install_ui_producer_probe(HMODULE image) noexcept {
    try {
        wchar_t enabled[8]{};GetEnvironmentVariableW(L"BOVR_UI_PRODUCER_PROBE",enabled,8);
        if (wcscmp(enabled,L"1") || image!=GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(image)!=0x400000) return false;
        std::string reason;
        return validate_path(executable_path()).accepted && validate_loaded_headers(image,reason) && install_at(reinterpret_cast<std::uintptr_t>(image));
    } catch (...) {return false;}
}
bool set_ui_producer_callbacks(BeforeUiProduction before,AfterUiProduction after) noexcept {
    if ((before || after) && !native_producer) return false;
    before_callback=before;after_callback=after;return true;
}
}
