// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/consumed_command_probe.hpp"
#include "t5/identity.hpp"
#include "t5/ordinary_consumed_aim.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstring>
#if !defined(_M_IX86)
#error The consumed-command observer requires MSVC x86.
#endif

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t call_rva=0x113e58,native_rva=0x13b430;





std::uintptr_t consumed_base{};
void* consumed_original{};
std::atomic<bool> installed{};
std::atomic<std::uint64_t> next_scope_serial{};
SRWLOCK install_lock=SRWLOCK_INIT,callback_lock=SRWLOCK_INIT;
ConsumedCommandCallbacks callbacks{};
unsigned callbacks_in_flight{};
struct Scope {
    unsigned depth{};
    bool in_native{},poisoned{};
    NativeConsumedCommand value{};
    ConsumedCommandCallbacks selected{};
};
thread_local Scope scope;

template<class T> T load(std::uintptr_t address) noexcept {
    T result{};std::memcpy(&result,reinterpret_cast<const void*>(address),sizeof(result));return result;
}
NativeConsumedCommand snapshot(const std::uint32_t* saved,std::uint64_t serial) noexcept {
    NativeConsumedCommand value{};value.image_base=consumed_base;value.scope_serial=serial;
    value.native_thread=GetCurrentThreadId();value.observed_ms=GetTickCount64();
    // Saved GP0..7, flags8, return9. Native ClientThink index10; the two
    // G_SetLastServerTime arguments remain at11/12. Saved caller EDI/EBX/ESI
    // occupy13..15, SV_ClientThink return16, actual serverClient17, cmd52ptr18.
    __try {
        if(saved[9]!=consumed_base+call_rva+5) {value.mismatches=consumed_wrong_return;return value;}
        value.local_client_index=static_cast<int>(saved[10]);
        if(saved[10]||saved[11]) {value.mismatches=consumed_other_client;return value;}
        value.server_client_identity=saved[17];value.source_command_identity=saved[18];
        const auto server=load<std::uintptr_t>(consumed_base+0x246d01c);
        if(!server||value.server_client_identity!=server) {value.mismatches=consumed_server_identity;return value;}
        value.server_state=load<int>(server);
        if(value.server_state!=4) {value.mismatches=consumed_server_inactive;return value;}
        value.entity_identity=consumed_base+0x16796f8;
        if(load<int>(value.entity_identity)!=0) {value.mismatches=consumed_entity_identity;return value;}
        const auto client=load<std::uintptr_t>(value.entity_identity+0x13c);
        value.game_client_identity=client;
        if(!client||client!=load<std::uintptr_t>(consumed_base+0x1803140)||load<unsigned char>(client+0x130)!=0) {
            value.mismatches=consumed_client_identity;return value;
        }
        value.local_identity=true;
        std::memcpy(value.command.data(),reinterpret_cast<void*>(server+0x10f38),value.command.size());
        std::memcpy(&value.command_time,value.command.data(),4);
        value.server_time=load<int>(consumed_base+0x246d014);
        value.level_time=load<int>(consumed_base+0x1804890);
        value.last_server_time=load<int>(client+0x1d00);
        if(value.command_time!=static_cast<int>(saved[12]))value.mismatches|=consumed_command_time;
        if(!value.source_command_identity||std::memcmp(value.command.data(),reinterpret_cast<void*>(value.source_command_identity),52))
            value.mismatches|=consumed_source_changed;
        // A current pointer is not enough: reject a mutation during the copy.
        if(server!=load<std::uintptr_t>(consumed_base+0x246d01c)||load<int>(server)!=4||
           client!=load<std::uintptr_t>(value.entity_identity+0x13c)||client!=load<std::uintptr_t>(consumed_base+0x1803140)||
           std::memcmp(value.command.data(),reinterpret_cast<void*>(server+0x10f38),52))value.mismatches|=consumed_source_changed;
        value.readable=true;value.active=value.mismatches==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {value.mismatches|=consumed_unreadable;}
    return value;
}
bool callback_cpp(void(*callback)(void*,const NativeConsumedCommand&),void* context,const NativeConsumedCommand& value) noexcept {
    try {if(callback)callback(context,value);return true;}
    catch(...) {return false;}
}
bool notify(void(*callback)(void*,const NativeConsumedCommand&),void* context,const NativeConsumedCommand& value) noexcept {
    __try {return callback_cpp(callback,context,value);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void enter_scope(const std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();
    if(scope.depth++) {
#if !defined(BOVR_TEST_NATIVE_GUARDS) || defined(BOVR_TEST_ORDINARY_CONSUMED_INTEGRATION)
        ordinary_consumed_aim::cancel_for_nested();
#endif
        scope.in_native=false;scope.poisoned=true;scope.value.nesting_invalidated=true;
        scope.value.mismatches|=consumed_nested;SetLastError(error);return;
    }
    scope.in_native=false;scope.poisoned=false;
    scope.value=snapshot(saved,next_scope_serial.fetch_add(1,std::memory_order_relaxed)+1);
    AcquireSRWLockExclusive(&callback_lock);scope.selected=callbacks;++callbacks_in_flight;ReleaseSRWLockExclusive(&callback_lock);
    if(!notify(scope.selected.before_native,scope.selected.context,scope.value)) {
        scope.poisoned=true;scope.value.mismatches|=consumed_callback_failed;
    }
#if !defined(BOVR_TEST_NATIVE_GUARDS) || defined(BOVR_TEST_ORDINARY_CONSUMED_INTEGRATION)
    if(!scope.poisoned&&scope.value.active)ordinary_consumed_aim::before(scope.value);
#endif
    scope.in_native=!scope.poisoned&&scope.value.active;SetLastError(error);
}
void leave_scope(bool abnormal) noexcept {
    const DWORD error=GetLastError();
    scope.in_native=false;
    if(!scope.depth) {SetLastError(error);return;}
    if(scope.depth>1) {--scope.depth;SetLastError(error);return;}
    scope.value.native_returned=!abnormal;
    if(abnormal)scope.value.mismatches|=consumed_native_unwind;
#if !defined(BOVR_TEST_NATIVE_GUARDS) || defined(BOVR_TEST_ORDINARY_CONSUMED_INTEGRATION)
    ordinary_consumed_aim::after(scope.value);
#endif
    // Keep the outer depth occupied during notification. Re-entry poisons the
    // outer scope and executes native code once, without replacing this tuple.
    notify(scope.selected.after_native,scope.selected.context,scope.value);
    if(scope.value.scope_serial<=3||scope.value.scope_serial%240==0||abnormal)
        trace("t5_native_consumed_command","scope=%llu thread=%lu observed_ms=%llu client_index=%d server=%p entity=%p game_client=%p command_time=%d server_time=%d level_time=%d last_server_time=%d active=%u readable=%u mismatch=0x%x returned=%u nested=%u command_bytes=52 command_writes=0 native_calls=1",
              scope.value.scope_serial,scope.value.native_thread,scope.value.observed_ms,scope.value.local_client_index,
              reinterpret_cast<void*>(scope.value.server_client_identity),reinterpret_cast<void*>(scope.value.entity_identity),reinterpret_cast<void*>(scope.value.game_client_identity),
              scope.value.command_time,scope.value.server_time,scope.value.level_time,scope.value.last_server_time,scope.value.active,scope.value.readable,
              scope.value.mismatches,scope.value.native_returned,scope.value.nesting_invalidated);
    scope.depth=0;scope.poisoned=false;scope.selected={};
    AcquireSRWLockExclusive(&callback_lock);--callbacks_in_flight;ReleaseSRWLockExclusive(&callback_lock);
    SetLastError(error);
}

// The compiler-owned __finally below supplies the real SEH unwind record.
// This bridge saves its C++ caller state, restores the complete native input,
// calls original once with one copied stack argument, and copies the complete
// native output back to the outer saved images before restoring its C++ caller.
__declspec(naked) void __cdecl native_preserved(std::uint32_t*,void*) {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        mov eax,esp
        push ebp
        push eax
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        mov esi,[esp]
        push dword ptr [esi+40]
        sub esp,36
        mov edi,esp
        mov ecx,9
        cld
        rep movsd
        mov eax,[esp+44]
        fxrstor [eax]
        popad
        popfd
        call dword ptr [consumed_original]
        lea esp,[esp+4]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov esi,ebp
        mov edi,[ebp+36]
        mov ecx,9
        rep movsd
        mov esi,esp
        mov edi,[ebp+40]
        mov ecx,128
        rep movsd
        mov eax,[ebp+44]
        fxrstor [eax]
        mov esp,[ebp+48]
        popad
        popfd
        ret
    }
}
void __cdecl dispatch_preserved(std::uint32_t* saved,void* fx) {
    enter_scope(saved);
    __try {native_preserved(saved,fx);}
    __finally {leave_scope(AbnormalTermination()!=FALSE);}
}
__declspec(naked) void consumed_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov eax,esp
        push eax
        push ebp
        call dispatch_preserved
        lea esp,[esp+8]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
struct GuardSpec {std::uintptr_t rva;std::size_t size;const char* hash;};
std::array<GuardSpec,7> guard_specs() noexcept {
#if defined(BOVR_TEST_NATIVE_GUARDS)
        constexpr auto call_hash="e8dde0bd101f7fdcb64b9246c39376da8112baa9035b28cff8a3c5f525650f18";
        constexpr auto native_hash="837885c8f8091aeaeb9ec3c3f85a6ff470a415e610b8ba3e49f9b33c9cf9d619";
        constexpr auto caller_hash="dd46c3eebb1884ff3b5258c0a2fc9398e560a29e0780d4b53869b6254aa46a96";
        constexpr auto copy_hash="659d36ca563ba4622daabb36a71dafaf6060cdcbf89bb12e75426198496d272c";
        constexpr auto setter_hash="eb142b0cae0baa72a767ebc0823d1be94e14c5bfc52d8e417fc4302fceb6240c";
        constexpr auto before_hash="9d908ecfb6b256def8b49a7c504e6c889c4b0e41fe6ce3e01863dd7b61a20aa0";
        constexpr auto after_hash=native_hash;
#else
        constexpr auto call_hash="85f9df365ae59aee1b9b6be480c761f961f8abac34d4e328e3872e28cc4b0eb6";
        constexpr auto native_hash="f6b4601b01daaf46c7777bcebe1bb9b71cec152f15fa84d3fba37b115d1af572";
        constexpr auto caller_hash="caa6ed78e636ed4b49dd04cd7e6d6d5e0dda13e5d7752f11c09007960d0cb7d1";
        constexpr auto copy_hash="e5e7ebfe3bdf83103362ca7b1f6cb4fa97efd9fb8647cad372722a53351e1daa";
        constexpr auto setter_hash="a4aa376a0681335a53fb0c8119fde9c5db2f9f9e9795efaa0f318973fb556284";
        constexpr auto before_hash="05955906ccb34ef2980b66d85dd64c7324856c808234b15bb9a23b3781fbc8a4";
        constexpr auto after_hash="8394038a63faf5e1ce0b53f40646939f691b5e0baceb4ae4161159add4a42d9c";
#endif
    return {{{call_rva,5,call_hash},{native_rva,7,native_hash},{0x113dc0,13,caller_hash},
        {0x113e04,26,copy_hash},{0x113e1e,34,setter_hash},{0x113e40,24,before_hash},{call_rva+5,7,after_hash}}};
}
std::array<std::array<unsigned char,34>,7> prepared_guards{};bool prepared{};
bool guards_match(std::uintptr_t base) noexcept {
    if(!base||base>UINTPTR_MAX-native_rva-7)return false;
    __try {
        for(const auto& s:guard_specs())if(bovr::native_guard::compare(reinterpret_cast<void*>(base+s.rva),s.size,s.hash))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool install_at(std::uintptr_t base) noexcept {
    AcquireSRWLockExclusive(&install_lock);
    bool result=false;
    if(installed.load(std::memory_order_acquire)) result=base==consumed_base;
    else if(guards_match(base)) {
        auto* site=reinterpret_cast<unsigned char*>(base+call_rva);DWORD old{},unused{};
        if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)) {
            if(guards_match(base)) {
                consumed_base=base;consumed_original=reinterpret_cast<void*>(base+native_rva);
                const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&consumed_detour)-reinterpret_cast<std::uintptr_t>(site+5));
                memcpy(site+1,&delta,4);FlushInstructionCache(GetCurrentProcess(),site,5);
                installed.store(true,std::memory_order_release);result=true;
            }
            const bool restored=VirtualProtect(site,5,old,&unused)!=FALSE;
            if(result)trace("t5_consumed_command_installed","call=%p target=%p protection_restored=%u command_writes=0 shot_writes=0",site,consumed_original,restored);
        }
    }
    ReleaseSRWLockExclusive(&install_lock);return result;
}
}
bool install_consumed_command_probe(HMODULE executable) noexcept {
    try {
        wchar_t option[8]{};GetEnvironmentVariableW(L"BOVR_CONSUMED_COMMAND_PROBE",option,8);
        if(wcscmp(option,L"1")||executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
        std::string reason;
        return validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&install_at(reinterpret_cast<std::uintptr_t>(executable));
    } catch(...) {return false;}
}
bool prepare_consumed_command_probe(HMODULE executable) noexcept {
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    if(installed.load(std::memory_order_acquire))return consumed_base==0x400000;
    prepared=false;
    try {std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason)||!guards_match(0x400000))return false;
        const auto specs=guard_specs();
        for(unsigned i=0;i<specs.size();++i){const auto& s=specs[i];
            std::memcpy(prepared_guards[i].data(),reinterpret_cast<void*>(0x400000+s.rva),s.size);
            if(!native_guard::matches(prepared_guards[i].data(),s.size,s.hash))return false;}
        prepared=true;return true;
    }catch(...){return false;}
}
bool commit_consumed_command_probe() noexcept {
    if(installed.load(std::memory_order_acquire))return consumed_base==0x400000;
    if(!prepared)return false;
    // Coordinator suspends/checks peers. No hashing, allocation, locks or log.
    __try {
        const auto specs=guard_specs();for(unsigned i=0;i<specs.size();++i)
            if(std::memcmp(reinterpret_cast<void*>(0x400000+specs[i].rva),prepared_guards[i].data(),specs[i].size))return false;
        auto* site=reinterpret_cast<unsigned char*>(0x400000+call_rva);DWORD protection{},ignored{};
        if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        consumed_base=0x400000;consumed_original=reinterpret_cast<void*>(0x400000+native_rva);
        const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&consumed_detour)-reinterpret_cast<std::uintptr_t>(site+5));
        std::memcpy(site+1,&delta,4);const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
        const bool restored=VirtualProtect(site,5,protection,&ignored)!=FALSE;
        if(!flushed||!restored){DWORD writable{};if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&writable)){
            std::memcpy(site,prepared_guards[0].data(),5);FlushInstructionCache(GetCurrentProcess(),site,5);VirtualProtect(site,5,protection,&ignored);}return false;}
        installed.store(true,std::memory_order_release);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool set_consumed_command_callbacks(const ConsumedCommandCallbacks& selected) noexcept {
    AcquireSRWLockExclusive(&callback_lock);
    const bool allowed=!callbacks_in_flight&&(!(selected.before_native||selected.after_native)||installed.load(std::memory_order_acquire));
    if(allowed)callbacks=selected;ReleaseSRWLockExclusive(&callback_lock);return allowed;
}
bool read_consumed_command_scope(NativeConsumedCommand* output) noexcept {
    if(!output||scope.depth!=1||!scope.in_native||scope.poisoned||!scope.value.active||scope.value.native_thread!=GetCurrentThreadId())return false;
    *output=scope.value;return true;
}
}
