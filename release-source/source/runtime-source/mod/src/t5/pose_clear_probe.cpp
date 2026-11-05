// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/pose_clear_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstring>
#if !defined(_M_IX86)
#error Pinned SP routine requires x86.
#endif
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t clear_rva=0x2766e0;
constexpr std::array<std::uintptr_t,4> clear_callers{0x5e83b,0x10b838,0x12bd12,0x2563db};
#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr const char* clear_entry_sha256="ca4b9a2ec05863e71b87c84feb71741348a30400daeddedd67bc4cdbca737252"; // seven authored NOPs
constexpr const char* clear_resume_sha256="e42f72d69a754b4bf9773a99bf4845007e93dff90a1968571f55e4a787cdb40f"; // authored MOV prefix
#else
constexpr const char* clear_entry_sha256="3c4bdd5ea12cf3dc3bfb1994837587b7ccca9f0d1e915fdabacbe4afdc38e7bb";
constexpr const char* clear_resume_sha256="34c3124e04b240d473b12fdb6444624e3cf983face5ded552d4e2a27f289574c";
#endif
std::uintptr_t clear_base{};
void* native_clear{};
PoseClearCallback clear_callback{};
std::atomic<std::uint64_t> clear_serial{};
std::atomic<bool> clear_installed{};
SRWLOCK clear_install_lock=SRWLOCK_INIT;
thread_local bool clear_observing{};
void __cdecl after_clear(const std::uint32_t* saved) {
    const auto error=GetLastError();
    // Increment even for unknown/nested/foreign completions. These invalidate
    // previously copied comparison records without publishing another sample.
    const auto serial=clear_serial.fetch_add(1)+1;
    if(clear_observing){SetLastError(error);return;}
    clear_observing=true;
    __try {
        const auto caller=static_cast<std::uintptr_t>(saved[9]);
        PoseClearPoint point{clear_base,caller,saved[10],static_cast<int>(saved[11]),GetCurrentThreadId(),serial,PoseClearCaller::other};
        if(caller==clear_base+0x5e840)point.caller=PoseClearCaller::camera;
        else if(caller==clear_base+0x10b83d)point.caller=PoseClearCaller::normal_weapon;
        if(serial<=8||serial%600==0)trace("t5_native_pose_clear_return","serial=%llu caller=0x%lx kind=%u thread=%lu local=%d original_calls=1 after_only=1",
            serial,static_cast<unsigned long>(caller),static_cast<unsigned>(point.caller),point.thread,point.local_client);
        if(clear_callback)clear_callback(point);
    }__finally {clear_observing=false;SetLastError(error);}
}
__declspec(naked) void pose_clear_detour() {
    __asm {
        // Copy two cdecl words using PUSH only, preserving incoming GP/flags/FP.
        // Original executes before any observer/TLS access; no retained scope
        // exists if it raises a native exception.
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        call dword ptr [native_clear]
        lea esp,[esp+8]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call after_clear
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
bool clear_guards(std::uintptr_t base) noexcept {
    if(!base||base>UINTPTR_MAX-0x276800)return false;
    __try {
        if(bovr::native_guard::compare(reinterpret_cast<void*>(base+clear_rva),7,clear_entry_sha256)||
           bovr::native_guard::compare(reinterpret_cast<void*>(base+clear_rva+7),2,clear_resume_sha256))return false;
        for(const auto caller:clear_callers){const auto* p=reinterpret_cast<const unsigned char*>(base+caller);std::int32_t d{};memcpy(&d,p+1,4);
            if(p[0]!=0xe8||static_cast<std::uint32_t>(base+caller+5+d)!=base+clear_rva)return false;}
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void clear_jump(unsigned char* at,const void* target) noexcept {
    const auto d=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(at)-5);
    at[0]=0xe9;memcpy(at+1,&d,4);
}
bool clear_install_at(std::uintptr_t base,PoseClearCallback callback) noexcept {
    struct Lock{Lock(){AcquireSRWLockExclusive(&clear_install_lock);}~Lock(){ReleaseSRWLockExclusive(&clear_install_lock);}}lock;
    if(clear_installed)return base==clear_base&&callback==clear_callback;
    if(!callback||!clear_guards(base))return false;
    std::array<unsigned char,7> original{};
    std::memcpy(original.data(),reinterpret_cast<const void*>(base+clear_rva),original.size());
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,12,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!trampoline)return false;
    memcpy(trampoline,original.data(),original.size());clear_jump(trampoline+7,reinterpret_cast<void*>(base+clear_rva+7));
    DWORD unused{},previous{};
    if(!VirtualProtect(trampoline,12,PAGE_EXECUTE_READ,&unused)){VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,12);
    auto* site=reinterpret_cast<unsigned char*>(base+clear_rva);
    if(!VirtualProtect(site,7,PAGE_EXECUTE_READWRITE,&previous)){VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    if(!clear_guards(base)){VirtualProtect(site,7,previous,&unused);VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    clear_base=base;native_clear=trampoline;clear_callback=callback;
    clear_jump(site,reinterpret_cast<void*>(&pose_clear_detour));memset(site+5,0x90,2);
    FlushInstructionCache(GetCurrentProcess(),site,7);
    const bool restored=VirtualProtect(site,7,previous,&unused)!=FALSE;clear_installed=true;
    trace("t5_native_pose_clear_installed","entry=%p displaced=7 protection_restored=%u after_only=1 caller_count=4 data_writes=0",site,restored);return true;
}
}
bool install_pose_clear_probe(HMODULE image,PoseClearCallback callback) noexcept {
    const auto error=GetLastError();bool ok{};
    try{wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",flag,8);
        if(!wcscmp(flag,L"1")&&image==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(image)==0x400000){std::string reason;
            ok=validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&clear_install_at(reinterpret_cast<std::uintptr_t>(image),callback);}}
    catch(...){}SetLastError(error);return ok;
}
std::uint64_t pose_clear_serial() noexcept{return clear_serial.load();}
}
