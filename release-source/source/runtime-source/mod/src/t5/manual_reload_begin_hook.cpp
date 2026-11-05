// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_begin_hook.hpp"
#include "t5/manual_reload_native_guards.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <array>
#include <cstring>

#if !defined(_M_IX86)
#error Pinned retail manual reload bridge requires x86.
#endif
namespace bovr::t5 {
void dispatch_manual_reload_begin(void* pm,ManualReloadBeginCallback callback,NativeReloadBegin native) {
    bool suppress{};
    {NativeReadPreserve preserve;if(callback)suppress=callback(pm);}
    // Native exceptions propagate normally; a native call is never retried.
    if(!suppress)native(pm);
}
namespace {
constexpr std::array<std::uintptr_t,2> sites{0x766c84,0x769376};
using Bytes=std::array<unsigned char,5>;
std::atomic<ManualReloadBeginCallback> decision{};
bool installed{};
SRWLOCK installation=SRWLOCK_INIT;
void __cdecl bridge(void* pm) {
    dispatch_manual_reload_begin(pm,decision.load(std::memory_order_acquire),
        reinterpret_cast<NativeReloadBegin>(0x766220));
}
bool same(std::uintptr_t address,const Bytes& bytes) noexcept {
    __try{return !std::memcmp(reinterpret_cast<const void*>(address),bytes.data(),bytes.size());}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool copy_site(std::uintptr_t address,Bytes* bytes) noexcept {
    if(!bytes)return false;
    __try {std::memcpy(bytes->data(),reinterpret_cast<const void*>(address),bytes->size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Acquire all handles before suspending peers; no allocation or callbacks in
// the patch interval. Both native reload routes change as one transaction.
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire() noexcept {
        const auto process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;bool okay=Thread32First(snapshot,&entry)!=FALSE;
        if(okay)do {
            if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){okay=false;break;}
            const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!handle){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;okay=false;break;}
            handles[count++]=handle;
        }while(Thread32Next(snapshot,&entry));
        if(okay&&GetLastError()!=ERROR_NO_MORE_FILES)okay=false;
        CloseHandle(snapshot);if(!okay)return false;
        for(std::size_t i=0;i<count;++i){
            if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c))return false;
            for(const auto site:sites)if(c.Eip>=site&&c.Eip<site+5)return false;
        }
        return true;
    }
};
bool install(ManualReloadBeginCallback callback) noexcept {
    if(installed)return decision.load(std::memory_order_acquire)==callback;
    if(!manual_native::loaded_guards())return false;
    Peers peers;if(!peers.acquire()||!manual_native::loaded_guards())return false;
    // Capture the verified call instructions from this loaded image.  These
    // process-local copies support exact rollback without embedding retail code.
    std::array<Bytes,2> originals{};
    for(unsigned i=0;i<2;++i)if(!copy_site(sites[i],&originals[i]))return false;
    auto patches=originals;
    for(unsigned i=0;i<2;++i){const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bridge)-(sites[i]+5));
        std::memcpy(patches[i].data()+1,&delta,4);}
    std::array<DWORD,2> protections{};unsigned writable{};
    for(;writable<2;++writable)if(!VirtualProtect(reinterpret_cast<void*>(sites[writable]),5,PAGE_EXECUTE_READWRITE,&protections[writable]))break;
    DWORD ignored{};
    if(writable!=2){while(writable){--writable;VirtualProtect(reinterpret_cast<void*>(sites[writable]),5,protections[writable],&ignored);}return false;}
    bool okay=same(sites[0],originals[0])&&same(sites[1],originals[1]);
    if(okay)for(unsigned i=0;i<2;++i){std::memcpy(reinterpret_cast<void*>(sites[i]),patches[i].data(),5);
        okay=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(sites[i]),5)!=FALSE&&okay;}
    for(unsigned i=0;i<2;++i)okay=VirtualProtect(reinterpret_cast<void*>(sites[i]),5,protections[i],&ignored)!=FALSE&&okay;
    okay=okay&&same(sites[0],patches[0])&&same(sites[1],patches[1]);
    if(!okay){
        // A bridge left reachable by a failed rollback has a null decision and
        // therefore always calls the unchanged native function once.
        for(unsigned i=0;i<2;++i){DWORD temporary{};
            if(VirtualProtect(reinterpret_cast<void*>(sites[i]),5,PAGE_EXECUTE_READWRITE,&temporary)){
                if(same(sites[i],patches[i]))std::memcpy(reinterpret_cast<void*>(sites[i]),originals[i].data(),5);
                FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(sites[i]),5);
                VirtualProtect(reinterpret_cast<void*>(sites[i]),5,protections[i],&ignored);}}
        return false;
    }
    decision.store(callback,std::memory_order_release);installed=true;peers.resume();return true;
}
}
bool configure_manual_reload_begin_hook(HMODULE image,ManualReloadBeginCallback callback) noexcept {
    if(!callback||image!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
    NativeReadPreserve preserve;AcquireSRWLockExclusive(&installation);
    const bool okay=install(callback);ReleaseSRWLockExclusive(&installation);
    trace("t5_manual_reload_begin_hook","accepted=%u call=00766c84 tail=00769376 target=00766220 persistent_player_flags=0",okay);
    return okay;
}
}
