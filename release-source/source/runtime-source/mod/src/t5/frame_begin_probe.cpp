// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/frame_begin_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <array>
#include <array>
#include <atomic>
#include <cstring>
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t entry_rva=0x2d7ad0;
constexpr std::array<std::uintptr_t,3> callers{0x9505c,0x2ceedf,0x3a17d9};
std::uintptr_t module_base{};
void* native_begin{};
std::atomic<bool> installed{};
std::atomic<std::uint64_t> next_serial{},next_pre_serial{};
// Aligned interlocked publication; x86 assembly reads this one LONG.
alignas(4) volatile LONG pre_callback_enabled{};
SRWLOCK install_lock=SRWLOCK_INIT,callback_lock=SRWLOCK_INIT;
FrameBeginCallback registered_callback{},registered_pre_callback{};
thread_local bool observing{};
struct Lock {SRWLOCK& lock;explicit Lock(SRWLOCK& value):lock(value){AcquireSRWLockExclusive(&lock);}~Lock(){ReleaseSRWLockExclusive(&lock);}};
FrameBeginCallback read_callback() noexcept {
    AcquireSRWLockShared(&callback_lock);const auto callback=registered_callback;ReleaseSRWLockShared(&callback_lock);return callback;
}
FrameBeginCallback read_pre_callback() noexcept {
    AcquireSRWLockShared(&callback_lock);const auto callback=registered_pre_callback;ReleaseSRWLockShared(&callback_lock);return callback;
}
void callback_cpp(FrameBeginCallback callback,const FrameBeginPoint& point) noexcept {
    try {if(callback)callback(point);}catch(...){trace("t5_frame_begin_callback_error","call=%llu kind=cpp",point.call_serial);}
}
void callback_boundary(FrameBeginCallback callback,const FrameBeginPoint& point) noexcept {
    __try {callback_cpp(callback,point);}
    __except(EXCEPTION_EXECUTE_HANDLER){trace("t5_frame_begin_callback_error","call=%llu kind=seh",point.call_serial);}
}
void __cdecl before_native(std::uintptr_t caller_return) noexcept {
    const DWORD saved=GetLastError();
    if(!observing){
        observing=true;
        FrameBeginPoint point{module_base,caller_return,GetCurrentThreadId(),next_pre_serial.fetch_add(1)+1};
        for(const auto caller:callers)point.known_direct_caller|=caller_return==module_base+caller+5;
        point.ordinary_screen_frame=caller_return==module_base+0x3a17de;
        callback_boundary(read_pre_callback(),point);
        observing=false;
    }
    SetLastError(saved);
}
void __cdecl after_native(std::uintptr_t caller_return) noexcept {
    const DWORD saved=GetLastError();
    if(!observing) {
        observing=true;
        FrameBeginPoint point{module_base,caller_return,GetCurrentThreadId(),next_serial.fetch_add(1)+1};
        for(const auto caller:callers)point.known_direct_caller|=caller_return==module_base+caller+5;
        point.ordinary_screen_frame=caller_return==module_base+0x3a17de;
        if(point.call_serial<=6||point.call_serial%120==0)
            trace("t5_frame_begin_return","call=%llu caller_return=0x%lx thread=%lu known_direct=%u ordinary=%u original_calls=1 result_interpreted=0",
                  point.call_serial,static_cast<unsigned long>(caller_return),point.native_thread,point.known_direct_caller,point.ordinary_screen_frame);
        callback_boundary(read_callback(),point);
        observing=false;
    }
    SetLastError(saved);
}
__declspec(naked) void frame_begin_detour() {
    __asm {
        // Native has no arguments. Both original returns pop its saved ECX.
        // No observer/TLS state is active while native executes or unwinds.
        // Disabled path still preserves incoming flags around the enable read.
        pushfd
        cmp dword ptr [pre_callback_enabled],0
        je pre_disabled
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+36]
        call before_native
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
    pre_disabled:
        popfd
        call dword ptr [native_begin]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+36]
        call after_native
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
bool guards_match(std::uintptr_t base) noexcept {
    if(!base||base>UINTPTR_MAX-callers.back()-5)return false;
    __try {
#if defined(BOVR_TEST_NATIVE_GUARDS)
        constexpr auto entry_hash="60029aed29905024e57979bf76c6f195c133e2d8997e515c915436b676d69c74";
        constexpr auto resume_hash="2f1034ecc24ebc947b5e77ab455d811e526c3f5ea2fb05573a455fd47dd4b5d9";
#else
        constexpr auto entry_hash="2af48b0951bd7b043661c0cd3f0237b5d199454d72cd892b5dfe86c317b2390e";
        constexpr auto resume_hash="aeddf798b4b8836a873bd2ce413d0b7e45f65ad41e239a910a740dcd7fc11c57";
#endif
        if(bovr::native_guard::compare(reinterpret_cast<void*>(base+entry_rva),8,entry_hash)||
           bovr::native_guard::compare(reinterpret_cast<void*>(base+entry_rva+8),6,resume_hash))return false;
        for(const auto caller:callers) {
            const auto* bytes=reinterpret_cast<const unsigned char*>(base+caller);
            std::int32_t relative{};memcpy(&relative,bytes+1,4);
            if(bytes[0]!=0xe8||static_cast<std::uint32_t>(base+caller+5+relative)!=base+entry_rva)return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return true;
}
void jump(unsigned char* site,void* target) noexcept {
    const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(site)-5);
    site[0]=0xe9;memcpy(site+1,&relative,4);
}
bool install_at(std::uintptr_t base) noexcept {
    Lock serialized(install_lock);
    if(installed.load())return base==module_base;
    if(!guards_match(base))return false;
    std::array<unsigned char,8> original{};
    std::memcpy(original.data(),reinterpret_cast<const void*>(base+entry_rva),original.size());
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,13,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!trampoline)return false;
    memcpy(trampoline,original.data(),original.size());jump(trampoline+8,reinterpret_cast<void*>(base+entry_rva+8));
    DWORD previous{},unused{};
    if(!VirtualProtect(trampoline,13,PAGE_EXECUTE_READ,&unused)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,13);
    auto* entry=reinterpret_cast<unsigned char*>(base+entry_rva);
    if(!VirtualProtect(entry,8,PAGE_EXECUTE_READWRITE,&previous)){VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    if(!guards_match(base)) {VirtualProtect(entry,8,previous,&unused);VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    module_base=base;native_begin=trampoline;
    jump(entry,reinterpret_cast<void*>(&frame_begin_detour));memset(entry+5,0x90,3);
    FlushInstructionCache(GetCurrentProcess(),entry,8);
    const bool restored=VirtualProtect(entry,8,previous,&unused)!=FALSE;
    installed=true;
    trace("t5_frame_begin_installed","entry=%p trampoline=%p displaced=8 protection_restored=%u caller_count=3 optional_pre_default=0 native_calls_unchanged=1 data_writes=0",entry,trampoline,restored);
    return true;
}
}
bool install_frame_begin_probe(HMODULE image) noexcept {
    try {
        wchar_t option[8]{},legacy[8]{};GetEnvironmentVariableW(L"BOVR_MENU_FRAME_EPOCH_PROBE",option,8);GetEnvironmentVariableW(L"BOVR_UI_PRODUCER_PROBE",legacy,8);
        if(wcscmp(option,L"1")||!wcscmp(legacy,L"1")||image!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
        std::string reason;
        return validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&install_at(reinterpret_cast<std::uintptr_t>(image));
    }catch(...){return false;}
}
bool set_frame_begin_callback(FrameBeginCallback callback) noexcept {
    if(callback&&!installed.load())return false;
    Lock serialized(callback_lock);registered_callback=callback;return true;
}
bool set_frame_begin_pre_callback(FrameBeginCallback callback) noexcept {
    if(callback&&!installed.load())return false;
    Lock serialized(callback_lock);registered_pre_callback=callback;
    InterlockedExchange(&pre_callback_enabled,callback?1L:0L);return true;
}
}
