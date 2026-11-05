// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/grenade_throw_hook.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#if !defined(_M_IX86)
#error The pinned grenade CALL requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t call_va=0x4c58c0, target_va=0x6318d0;
constexpr native_guard::Span call_guard{call_va,5,"2be4cef802a8f376221409bfb826041c05f64b6b6a411fe959d4659a22cb9ea1"};
constexpr native_guard::Span offhand_guard{0x4054d0,116,"fcb489dce7fa4da15d54f2599525044f1327eb806761bd2f8aabfaf54a8aafb9"};
constexpr native_guard::Span caller_guard{0x4c5760,579,"6186c40dbea67e9f9e0c0f9abbf966e414e1d8fdbd88387543b3c68567040739"};
constexpr native_guard::Span target_guard{target_va,466,"9908d89790e7a906405ea1c9f850b4c39092791093d60083eab46515ddabdad3"};
constexpr native_guard::Span timer_guard{0x7e6b50,156,"3c587f2ce4efc6e22ebd14f5ab20e487373bdecd70d34a4231d4ca676a0e224a"};
using NativeThrow=void*(__cdecl*)(void*,float*,float*,unsigned,unsigned,int,int);
std::atomic<NativeThrow> native_throw{};
std::atomic<GrenadeThrowCallback> throw_callback{};
SRWLOCK install_lock=SRWLOCK_INIT;
bool installed{};


bool guards_match() noexcept {
    // Each complete function includes its native stack convention and all
    // outgoing call displacements. No saved runtime snapshot is trusted here.
    return native_guard::matches_absolute(offhand_guard)&&
        native_guard::matches_absolute(caller_guard)&&
        native_guard::matches_absolute(target_guard)&&
        native_guard::matches_absolute(timer_guard);
}
bool finite(const std::array<float,3>& v) noexcept {
    return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);
}
bool stack_vector(const float* p) noexcept {
    const auto at=reinterpret_cast<std::uintptr_t>(p);
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto low=reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    const auto high=reinterpret_cast<std::uintptr_t>(tib->StackBase);
    if(!p||at%alignof(float)||at<low||at>=high||high-at<3*sizeof(float))return false;
    MEMORY_BASIC_INFORMATION page{};
    if(VirtualQuery(p,&page,sizeof(page))!=sizeof(page)||page.State!=MEM_COMMIT||
       page.Protect&(PAGE_GUARD|PAGE_NOACCESS))return false;
    const auto access=page.Protect&0xff;
    if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return false;
    const auto region=reinterpret_cast<std::uintptr_t>(page.BaseAddress);
    return at>=region&&at-region<=page.RegionSize&&page.RegionSize-(at-region)>=3*sizeof(float);
}
bool copy_arguments(void* parent,float* start,float* velocity,unsigned weapon,unsigned model,int rotate,int fuse,
                    GrenadeThrowArguments* out) noexcept {
    if(!out||!stack_vector(start)||!stack_vector(velocity))return false;
    const auto a=reinterpret_cast<std::uintptr_t>(start),b=reinterpret_cast<std::uintptr_t>(velocity);
    if((a<=b?b-a:a-b)<3*sizeof(float))return false;
    __try {
        GrenadeThrowArguments value{};value.parent=reinterpret_cast<std::uintptr_t>(parent);
        value.weapon=weapon;value.model=model;value.rotate=rotate;value.fuse_time=fuse;
        std::memcpy(value.origin.data(),start,sizeof(value.origin));
        std::memcpy(value.velocity.data(),velocity,sizeof(value.velocity));
        if(!finite(value.origin)||!finite(value.velocity))return false;
        *out=value;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool invoke_callback(GrenadeThrowCallback callback,const GrenadeThrowArguments& in,GrenadeThrowReplacement* out) noexcept {
    __try {return callback&&callback(in,out);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool replace_vectors(float* start,float* velocity,const GrenadeThrowArguments& before,
                     const GrenadeThrowReplacement& after) noexcept {
    if(!finite(after.origin)||!finite(after.velocity)||!stack_vector(start)||!stack_vector(velocity))return false;
    __try {
        if(std::memcmp(start,before.origin.data(),sizeof(before.origin))||
           std::memcmp(velocity,before.velocity.data(),sizeof(before.velocity)))return false;
        // These are disjoint, committed, writable vectors on THIS thread's
        // stack. Preserve the accepted velocity in the caller's own scratch:
        // native code normalizes it after G_FireGrenade and inherits movement.
        std::memcpy(start,after.origin.data(),sizeof(after.origin));
        std::memcpy(velocity,after.velocity.data(),sizeof(after.velocity));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {
            std::memcpy(start,before.origin.data(),sizeof(before.origin));
            std::memcpy(velocity,before.velocity.data(),sizeof(before.velocity));
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}
void* __cdecl throw_bridge(void* parent,float* start,float* velocity,unsigned weapon,unsigned model,int rotate,int fuse) {
    const auto original=native_throw.load(std::memory_order_acquire);
    // Published before the native CALL is patched and never cleared thereafter.
    if(!original)return nullptr;
    const DWORD native_error=GetLastError();
    GrenadeThrowArguments values{};GrenadeThrowReplacement replacement{};
    bool accepted{},applied{};
    if(copy_arguments(parent,start,velocity,weapon,model,rotate,fuse,&values)) {
        replacement={values.origin,values.velocity};
        accepted=invoke_callback(throw_callback.load(std::memory_order_acquire),values,&replacement);
        if(accepted)applied=replace_vectors(start,velocity,values,replacement);
    }
    SetLastError(native_error);
    // Pass original pointers and every scalar unchanged. Do not catch a native
    // exception or call again: all fuse/ammo/entity/script ownership is native.
    void* const projectile=original(parent,start,velocity,weapon,model,rotate,fuse);
    const DWORD result_error=GetLastError();
    if(accepted) {
        // Only an accepted host release emits evidence. NPC and unmatched
        // native throws remain silent; an accepted-but-rejected write is clear.
        const auto& sent_origin=applied?replacement.origin:values.origin;
        const auto& sent_velocity=applied?replacement.velocity:values.velocity;
        trace("t5_grenade_throw_native",
              "callback_accepted=1 applied=%u native_calls=1 projectile=%p parent=%p weapon=%u model=%u rotate=%d fuse=%d "
              "native_origin=(%.3f,%.3f,%.3f) native_velocity=(%.3f,%.3f,%.3f) "
              "sent_origin=(%.3f,%.3f,%.3f) sent_velocity=(%.3f,%.3f,%.3f)",
              unsigned(applied),projectile,parent,weapon,model,rotate,fuse,
              double(values.origin[0]),double(values.origin[1]),double(values.origin[2]),
              double(values.velocity[0]),double(values.velocity[1]),double(values.velocity[2]),
              double(sent_origin[0]),double(sent_origin[1]),double(sent_origin[2]),
              double(sent_velocity[0]),double(sent_velocity[1]),double(sent_velocity[2]));
    }
    SetLastError(result_error);
    return projectile;
}

// Bounded installation-only quiescence. All handles are acquired before any
// suspension; no C++ allocation or callbacks run while peers are suspended.
struct Peers {
    std::array<HANDLE,512> handles{};
    std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire() noexcept {
        const DWORD process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
        bool ok=Thread32First(snapshot,&entry)!=FALSE;
        if(ok)do {
            if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){ok=false;break;}
            const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!handle){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;ok=false;break;}
            handles[count++]=handle;
        } while(Thread32Next(snapshot,&entry));
        if(ok&&GetLastError()!=ERROR_NO_MORE_FILES)ok=false;
        CloseHandle(snapshot);if(!ok)return false;
        for(std::size_t i=0;i<count;++i) {
            if(SuspendThread(handles[i])==DWORD(-1))return false;
            ++suspended;
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(handles[i],&context))return false;
            if(context.Eip>=call_va&&context.Eip<call_va+call_guard.size)return false;
        }
        return true;
    }
};
bool install_at(GrenadeThrowCallback callback) noexcept {
    if(installed)return throw_callback.load(std::memory_order_acquire)==callback;
    if(!guards_match())return false;
    Peers peers;if(!peers.acquire()||!guards_match())return false;
    auto* site=reinterpret_cast<unsigned char*>(call_va);
    const auto bridge=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&throw_bridge));
    const std::uint32_t delta=bridge-static_cast<std::uint32_t>(call_va+5);
    unsigned char patch[5]{0xe8};std::memcpy(patch+1,&delta,sizeof(delta));
    DWORD old{},unused{};
    if(!VirtualProtect(site,sizeof(patch),PAGE_EXECUTE_READWRITE,&old))return false;
    std::array<unsigned char,5> original{};
    if(!native_guard::matches(site,call_guard.size,call_guard.sha256)){VirtualProtect(site,sizeof(patch),old,&unused);return false;}
    std::memcpy(original.data(),site,original.size());
    native_throw.store(reinterpret_cast<NativeThrow>(target_va),std::memory_order_release);
    // Keep the callback disabled until both the patch and page protection are
    // verified. A failure that leaves a bridge reachable still passes through.
    std::memcpy(site,patch,sizeof(patch));
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,sizeof(patch))!=FALSE;
    const bool restored=VirtualProtect(site,sizeof(patch),old,&unused)!=FALSE;
    if(!flushed||!restored||std::memcmp(site,patch,sizeof(patch))) {
        DWORD rollback_protection{};
        if(VirtualProtect(site,sizeof(patch),PAGE_EXECUTE_READWRITE,&rollback_protection)) {
            std::memcpy(site,original.data(),original.size());
            FlushInstructionCache(GetCurrentProcess(),site,original.size());
            VirtualProtect(site,original.size(),old,&unused);
        }
        return false;
    }
    throw_callback.store(callback,std::memory_order_release);installed=true;
    peers.resume();return true;
}
}
bool configure_grenade_throw_hook(HMODULE image,GrenadeThrowCallback callback) noexcept {
    if(!callback||reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
    AcquireSRWLockExclusive(&install_lock);bool accepted{};
    try {
        if(installed)accepted=throw_callback.load(std::memory_order_acquire)==callback;
        else {std::string reason;accepted=validate_path(executable_path()).accepted&&
            validate_loaded_headers(image,reason)&&install_at(callback);}
    } catch(...) {accepted=false;}
    ReleaseSRWLockExclusive(&install_lock);
    trace("t5_grenade_throw_hook","accepted=%u call=004c58c0 target=006318d0 callback_values_only=1 native_calls=1 original_stack_buffers=1",accepted);
    return accepted;
}
}
