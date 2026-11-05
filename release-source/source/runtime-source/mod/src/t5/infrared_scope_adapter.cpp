// SPDX-License-Identifier: GPL-3.0-only
#include "t5/infrared_scope_adapter.hpp"
#include "t5/thermal_scene_scope.hpp"
#include "t5/native_read_recipe.hpp"
#include "t5/identity.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <intrin.h>
#if !defined(_M_IX86)
#error The native infrared consumer requires MSVC x86.
#endif
namespace bovr::t5 {
namespace infrared_scope_detail {
using Native=bool(__cdecl*)(int);
Native original=reinterpret_cast<Native>(0x46d5f0);
std::atomic<bool> ready{};
std::atomic<bool> armed{};
thread_local bool selecting{};
SRWLOCK install_lock=SRWLOCK_INIT;
struct Site {std::uintptr_t address;std::array<std::uint8_t,5> original;};
constexpr std::array<Site,4> sites{{
    {0x45ead0,{0xe8,0x1b,0xeb,0x00,0x00}}, // Authored infrared vision channel.
    {0x58152e,{0xe8,0xbd,0xc0,0xee,0xff}}, // Native actor material/heartbeat.
    {0x581669,{0xe8,0x82,0xbf,0xee,0xff}}, // Matching actor lighting companion.
    {0x5d759f,{0xe8,0x4c,0x60,0xe9,0xff}}  // Native actor highlight eligibility.
}};
std::array<std::array<std::uint8_t,5>,sites.size()> owned{};
bool caller_allowed(std::uintptr_t returned) noexcept {
    for(const auto& site:sites)if(returned==site.address+5)return true;
    return false;
}
// The native answer is never removed. Only the four reviewed scene consumers
// can request IR; fullscreen overlay and saved-screen blur remain unmodified.
bool invoke(int local,std::uintptr_t returned) noexcept {
    const bool native=original(local);
    NativeReadPreserve preserve;
    if(native||!ready.load(std::memory_order_acquire)||selecting||local||!caller_allowed(returned))return native;
    selecting=true;struct Exit{~Exit(){selecting=false;}} exit;
    const bool selected=returned==sites[0].address+5&&thermal_scene_vision_override();
    static thread_local bool previous{};static std::atomic<unsigned> activations{};
    if(returned==sites[0].address+5&&selected!=previous){previous=selected;const auto count=++activations;
        if(count<=8||count%240==0)trace("t5_physical_infrared","count=%u active=%u scoped_vision_only=1 peripheral_ir=0 native_ads_fov_writes=0 fullscreen_overlay_override=0",count,selected);}
    return selected;
}
__declspec(noinline) bool __cdecl bridge(int local) {
    return invoke(local,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
constexpr bovr::native_guard::Span guards[]{
    {0x46d5f0,118,"b1f80411a7009f2c8fcb1b9e240f114bdf5aadf90f9d1e56f140e3fdcdd34659"},
    {0x45eaca,79,"f2461c211ed8c737fa11e8f8388059067a03f7abd3e7574ea503a9108185e161"},
    {0x581512,279,"a9532cd88d6d862cbd64205e6959c351bcc6bb57a26608be36c4235a95df1f47"},
    {0x58162d,92,"f2f31eeb23311c748b174a209cd7957225ffc000b96e6f720e04461b769aa66e"},
    {0x5d7520,740,"ab37071dd1314b5cc0c7005ed03660ca554998a5f2478655c6536f9aec7da8a9"}
};
bool copy(std::uintptr_t address,void* out,std::size_t size) noexcept {
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool main_thread() noexcept {
    DWORD expected{};return copy(0x251ca98,&expected,sizeof expected)&&expected&&expected==GetCurrentThreadId();
}
bool guards_match(bool installed) noexcept {
    for(const auto& guard:guards){std::array<std::uint8_t,1024> bytes{};
        if(guard.size>bytes.size()||!copy(guard.address,bytes.data(),guard.size))return false;
        for(unsigned i=0;i<sites.size();++i){const auto& site=sites[i];
            if(site.address<guard.address||site.address+5>guard.address+guard.size)continue;
            const auto offset=site.address-guard.address;const auto& expected=installed?owned[i]:site.original;
            if(std::memcmp(bytes.data()+offset,expected.data(),5))return false;
            std::memcpy(bytes.data()+offset,site.original.data(),5);
        }
        if(!bovr::native_guard::matches(bytes.data(),guard.size,guard.sha256))return false;
    }
    return true;
}
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire() noexcept {
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
            for(const auto& site:sites)if(c.Eip>=site.address&&c.Eip<site.address+5)return false;
        }
        return true;
    }
};
bool put(std::uintptr_t address,const std::array<std::uint8_t,5>& bytes) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(address),bytes.data(),bytes.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install() noexcept {
    if(ready.load(std::memory_order_acquire))return guards_match(true);
    if(!guards_match(false))return false;
    Peers peers;if(!peers.acquire()||!guards_match(false))return false;
    std::array<DWORD,sites.size()> protection{};unsigned opened{};bool okay=true;
    for(unsigned i=0;i<sites.size();++i){auto patch=sites[i].original;
        const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bridge)-(sites[i].address+5));
        std::memcpy(patch.data()+1,&displacement,4);owned[i]=patch;
        if(!VirtualProtect(reinterpret_cast<void*>(sites[i].address),5,PAGE_EXECUTE_READWRITE,&protection[i])){okay=false;break;}
        ++opened;
    }
    if(okay)for(unsigned i=0;i<sites.size();++i)if(!put(sites[i].address,owned[i]))okay=false;
    if(okay)okay=FlushInstructionCache(GetCurrentProcess(),nullptr,0)!=FALSE&&guards_match(true);
    if(!okay)for(unsigned i=0;i<opened;++i)put(sites[i].address,sites[i].original);
    bool restored=true;for(unsigned i=0;i<opened;++i){DWORD ignored{};
        if(!VirtualProtect(reinterpret_cast<void*>(sites[i].address),5,protection[i],&ignored))restored=false;}
    if(!okay||!restored){
        // Readiness stays false even if the OS refuses rollback: a surviving
        // bridge then executes the native predicate once without an override.
        for(unsigned i=0;i<opened;++i){DWORD previous{},ignored{};
            if(VirtualProtect(reinterpret_cast<void*>(sites[i].address),5,PAGE_EXECUTE_READWRITE,&previous)){
                put(sites[i].address,sites[i].original);
                VirtualProtect(reinterpret_cast<void*>(sites[i].address),5,protection[i],&ignored);}}
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);return false;
    }
    ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
bool configure_infrared_scope_adapter(HMODULE executable,bool input_ready) noexcept {
    NativeReadPreserve preserve;wchar_t option[8]{};
    if(!input_ready||GetEnvironmentVariableW(L"BOVR_COD4_CONTROLS",option,8)!=1||option[0]!=L'1')return false;
    AcquireSRWLockExclusive(&infrared_scope_detail::install_lock);bool okay{};
    try {std::string reason;okay=reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&
        infrared_scope_detail::guards_match(infrared_scope_detail::ready.load(std::memory_order_acquire));}
    catch(...){okay=false;}
    infrared_scope_detail::armed.store(okay,std::memory_order_release);
    ReleaseSRWLockExclusive(&infrared_scope_detail::install_lock);
    trace("t5_infrared_scope_configured","accepted=%u deferred_until_live_thermal_input=1 startup_code_writes=0 native=0046d5f0 scene_consumers=4 original_once=1 fullscreen_blur_overlay_unchanged=1",okay);
    return okay;
}
bool ensure_infrared_scope_adapter() noexcept {
    using namespace infrared_scope_detail;
    if(ready.load(std::memory_order_acquire))return true;
    if(!armed.load(std::memory_order_acquire)||!main_thread())return false;
    NativeReadPreserve preserve;
    AcquireSRWLockExclusive(&install_lock);
    static unsigned attempts{};static std::uint64_t last_attempt{};
    const auto now=GetTickCount64();bool okay=ready.load(std::memory_order_acquire);
    if(!okay&&attempts<3&&(!last_attempt||(now>=last_attempt&&now-last_attempt>=1000))){
        ++attempts;last_attempt=now;okay=install();
        trace("t5_infrared_scope_installed","accepted=%u attempt=%u owner=live_thermal_command native=0046d5f0 scene_consumers=4",okay,attempts);
    }
    ReleaseSRWLockExclusive(&install_lock);return okay;
}
}
