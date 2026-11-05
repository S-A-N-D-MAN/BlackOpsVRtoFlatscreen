// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/crossbow_adapter.hpp"
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
namespace crossbow_projectile_detail {
constexpr native_guard::Span trace_caller_guard{0xcfe25,113,"c8f902088a5c44e10a7ad4532351ce838b9413ed3eba86466a27e61167db8e9e"};
constexpr native_guard::Span trace_callee_guard{0x281ea0,502,"f490f0e4295e8d16b02af6e38d1c471b6cd8984285866b7671c5a356979d0e21"};
constexpr std::uintptr_t call_rva=0xcfe8e,caller_rva=0xcfe25,target_rva=0x281ea0;
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
    std::array<std::uint8_t,trace_caller_guard.size> caller{};
    if(!base||base>UINT32_MAX-call_rva-trace_caller_guard.size||
       !get(base+caller_rva,&caller)||
       !bovr::native_guard::matches(reinterpret_cast<const void*>(base+target_rva),
           trace_callee_guard.size,trace_callee_guard.sha256))return false;
    if(owned){
        if(!hook_ready.load(std::memory_order_acquire)||base!=hook_base||
           std::memcmp(caller.data()+call_offset,owned_call.data(),owned_call.size()))return false;
        std::memcpy(caller.data()+call_offset,original_call.data(),original_call.size());
    }
    return bovr::native_guard::matches(caller.data(),caller.size(),trace_caller_guard.sha256);
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

void dispatch_guarded(std::uintptr_t stack) noexcept {
    __try {
        auto* args=reinterpret_cast<std::uint32_t*>(stack);
        float spread{};std::memcpy(&spread,args+3,4);
        spread=select_crossbow_spread(args[1],args[2],args[4],spread);
        std::memcpy(args+3,&spread,4);
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
void __cdecl dispatch(std::uintptr_t stack) noexcept {NativeReadPreserve preserve;dispatch_guarded(stack);}
// Preserve all registers and floating point state, then tail-enter the original
// native projectile function exactly once with its original stack shape.
__declspec(naked) void crossbow_projectile_bridge(){
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        lea eax,[ebp+36]
        push eax
        call dispatch
        add esp,4
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
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&crossbow_projectile_bridge)-(base+call_rva+5));
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
bool configure_crossbow_projectile_hook(HMODULE executable) noexcept {
    NativeReadPreserve preserve;AcquireSRWLockExclusive(&crossbow_projectile_detail::install_lock);bool okay{};
    try{std::string reason;okay=reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&
        crossbow_projectile_detail::install(reinterpret_cast<std::uintptr_t>(executable));}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&crossbow_projectile_detail::install_lock);
    trace("t5_crossbow_projectile_configured","accepted=%u call=004cfe8e native=00681ea0 original_once=1 native_exit_checks_unchanged=1",okay);
    return okay;
}
}
