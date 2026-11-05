// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/helicopter_turret_limit_hook.hpp"
#include "t5/identity.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <cstring>
#if !defined(_M_IX86)
#error The pinned helicopter trace bridge requires MSVC x86.
#endif
namespace bovr::t5 {
namespace helicopter_turret_limit_detail {
#include "t5/helicopter_turret_limit_guards.inc"
constexpr std::uintptr_t call_rva=0x40c49a,caller_rva=0x40c050,target_rva=0x290950,normalize_rva=0x1f8370;
constexpr std::size_t call_offset=call_rva-caller_rva;
std::uintptr_t hook_base{},native_target{};
std::array<std::uint8_t,5> owned_call{},original_call{};
std::atomic<bool> hook_ready{};
SRWLOCK install_lock=SRWLOCK_INIT;
template<class T>bool get(std::uintptr_t at,T* out) noexcept {
    __try{std::memcpy(out,reinterpret_cast<const void*>(at),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool put(std::uintptr_t at,const T& value) noexcept {
    __try{std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool guards_match(std::uintptr_t base,bool owned) noexcept {
    std::array<std::uint8_t,limit_caller_guard.size> caller{};
    if(!base||base>UINT32_MAX-call_rva-limit_caller_guard.size||
       !get(base+caller_rva,&caller)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+target_rva),
           limit_callee_guard.size,limit_callee_guard.sha256)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+normalize_rva),
           limit_normalize_guard.size,limit_normalize_guard.sha256))return false;
    if(owned){
        if(!hook_ready.load(std::memory_order_acquire)||base!=hook_base||
           std::memcmp(caller.data()+call_offset,owned_call.data(),owned_call.size()))return false;
        std::memcpy(caller.data()+call_offset,original_call.data(),original_call.size());
    }
    return bovr::native_guard::matches(caller.data(),caller.size(),limit_caller_guard.sha256);
}
// Installation only: no callbacks/allocations while peer instruction pointers
// are held away from the five-byte patch. All peers resume on every exit.
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire(std::uintptr_t base) noexcept {
        const auto process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;bool okay=Thread32First(snapshot,&entry)!=FALSE;
        if(okay)do {if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){okay=false;break;}
            const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!handle){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;okay=false;break;}handles[count++]=handle;
        }while(Thread32Next(snapshot,&entry));
        if(okay&&GetLastError()!=ERROR_NO_MORE_FILES)okay=false;CloseHandle(snapshot);if(!okay)return false;
        for(std::size_t i=0;i<count;++i){if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c))return false;
            if(c.Eip>=base+call_rva&&c.Eip<base+call_rva+5)return false;}
        return true;
    }
};

std::uintptr_t dispatch(std::uintptr_t ret,std::uintptr_t vehicle,std::uintptr_t scr,
                        std::uintptr_t info,std::uintptr_t pilot,std::uintptr_t stack) noexcept {
    NativeReadPreserve preserve;
    return select_helicopter_turret_limits(ret,vehicle,scr,info,pilot,stack);
}
// Preserve the original four-float call stack and return address. The sole
// admitted register change is the caller-local EBX info pointer. Native rate
// calculation returns its original ST0 result directly to the native caller;
// the caller consumes the private limits and eventually restores outer EBX.
__declspec(naked) void helicopter_turret_limit_bridge(){
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        lea eax,[ebp+36]
        push eax
        push dword ptr [ebp+8]
        push dword ptr [ebp+16]
        push dword ptr [ebp+4]
        push dword ptr [ebp]
        push dword ptr [ebp+36]
        call dispatch
        add esp,24
        test eax,eax
        jz no_selection
        mov dword ptr [ebp+16],eax
    no_selection:
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [native_target]
    }
}
bool install(std::uintptr_t base) noexcept {
    if(hook_ready.load(std::memory_order_acquire))return guards_match(base,true);
    if(!guards_match(base,false))return false;
    Peers peers;if(!peers.acquire(base)||!guards_match(base,false))return false;
    std::array<std::uint8_t,5> original{};
    if(!get(base+call_rva,&original))return false;
    auto patch=original;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&helicopter_turret_limit_bridge)-(base+call_rva+5));
    std::memcpy(patch.data()+1,&displacement,4);
    auto* site=reinterpret_cast<void*>(base+call_rva);DWORD previous{},ignored{};
    if(!VirtualProtect(site,patch.size(),PAGE_EXECUTE_READWRITE,&previous))return false;
    std::array<std::uint8_t,5> before{};
    if(!get(base+call_rva,&before)||before!=original){VirtualProtect(site,patch.size(),previous,&ignored);return false;}
    // Even uncertain rollback retains a safe original native tail target.
    hook_base=base;native_target=base+target_rva;original_call=original;owned_call=patch;
    const bool wrote=put(base+call_rva,patch);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,patch.size())!=FALSE;
    const bool restored=VirtualProtect(site,patch.size(),previous,&ignored)!=FALSE;
    if(!wrote||!flushed||!restored||!get(base+call_rva,&before)||before!=patch){
        DWORD rollback{};if(VirtualProtect(site,patch.size(),PAGE_EXECUTE_READWRITE,&rollback)){
            put(base+call_rva,original);FlushInstructionCache(GetCurrentProcess(),site,patch.size());
            VirtualProtect(site,patch.size(),previous,&ignored);}
        return false;
    }
    hook_ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
bool configure_helicopter_turret_limit_hook(HMODULE executable) noexcept {
    NativeReadPreserve preserve;AcquireSRWLockExclusive(&helicopter_turret_limit_detail::install_lock);bool okay{};
    try{std::string reason;okay=reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&
        helicopter_turret_limit_detail::install(reinterpret_cast<std::uintptr_t>(executable));}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&helicopter_turret_limit_detail::install_lock);
    trace("t5_helicopter_turret_limits_configured","accepted=%u call=0080c49a native=00690950 original_once=1 native_exit_checks_unchanged=1",okay);
    return okay;
}
}
