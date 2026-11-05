// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/weapon_lifecycle.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <array>
#include <cstring>
#if !defined(_M_IX86)
#error Pinned SP lifecycle observers require MSVC x86.
#endif
namespace bovr::t5 {
namespace {
std::uintptr_t lifecycle_base{};
bool lifecycle_installed{};
SRWLOCK lifecycle_lock=SRWLOCK_INIT;
detail::WeaponLifetimeState lifecycle_state{};
void *create_original{},*free_original{},*owner_memset_original{},*pool_memset_original{},*owner_free_original{};
constexpr std::uintptr_t owner_rva=0x81c6d8,pool_rva=0x2089148,map_rva=0x20e6150,allocated_rva=0x2088548;
template<class T> T life_load(std::uintptr_t p) noexcept {T v{};memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return v;}
std::uintptr_t current_owner() noexcept {
    __try {return life_load<std::uintptr_t>(lifecycle_base+owner_rva);} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
bool pool_matches(std::uintptr_t dobj) noexcept {
    __try {
        const auto index=life_load<short>(lifecycle_base+map_rva+1536*2);
        return index>0&&index<0xc00&&dobj==lifecycle_base+pool_rva+static_cast<unsigned>(index)*0x7c&&
            life_load<unsigned char>(lifecycle_base+allocated_rva+index)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void invalidate_lifetime(const char* event) noexcept {
    AcquireSRWLockExclusive(&lifecycle_lock);lifecycle_state.invalidate();const auto serial=lifecycle_state.serial;ReleaseSRWLockExclusive(&lifecycle_lock);
    trace("t5_weapon_lifetime","event=%s generation=%llu thread=%lu valid=0 native_calls=unchanged game_writes=0",event,serial,GetCurrentThreadId());
}
void __cdecl create_before(const std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();const auto* a=saved+10;
    if(a[3]==1536&&a[4]==0) {
        const auto owner=current_owner();AcquireSRWLockExclusive(&lifecycle_lock);
        lifecycle_state.begin(owner,GetCurrentThreadId());ReleaseSRWLockExclusive(&lifecycle_lock);
    } else if(a[3]==1536) invalidate_lifetime("unsupported_client_create");
    SetLastError(error);
}
void __cdecl create_after(const std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();const auto* a=saved+10;
    if(a[3]==1536&&a[4]==0) {
        const auto owner=current_owner(),dobj=static_cast<std::uintptr_t>(saved[7]);const bool matches=pool_matches(dobj);
        AcquireSRWLockExclusive(&lifecycle_lock);lifecycle_state.finish(owner,dobj,GetCurrentThreadId(),matches);
        const auto state=lifecycle_state.live;const auto serial=lifecycle_state.serial;ReleaseSRWLockExclusive(&lifecycle_lock);
        trace("t5_weapon_lifetime","event=create generation=%llu thread=%lu viewmodel=%p dobj=%p handle=1536 local=0 pool_match=%u verified=%u native_calls=1 game_writes=0",serial,GetCurrentThreadId(),reinterpret_cast<void*>(owner),reinterpret_cast<void*>(dobj),matches,state.verified);
    }
    SetLastError(error);
}
void __cdecl free_before(const std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();const auto* a=saved+10;
    // The pinned SP callee ignores localClient: handle1536 is the same slot
    // even for an unsupported second argument. Never preserve stale ownership.
    if(a[0]==1536) invalidate_lifetime("free");
    SetLastError(error);
}
void __cdecl owner_reset_before(const std::uint32_t*) noexcept {
    const DWORD error=GetLastError();invalidate_lifetime("viewmodel_owner_reset");SetLastError(error);
}
void __cdecl pool_reset_before(const std::uint32_t*) noexcept {
    const DWORD error=GetLastError();invalidate_lifetime("client_pool_reset");SetLastError(error);
}
void __cdecl owner_free_before(const std::uint32_t*) noexcept {
    const DWORD error=GetLastError();invalidate_lifetime("viewmodel_owner_free");SetLastError(error);
}
// Every observer saves the complete native input or result state. Tail wrappers
// retain the caller's original arguments/return address and native cleanup.
#define LIFE_NOTIFY(F) __asm pushfd __asm pushad __asm mov ebp,esp __asm sub esp,528 __asm and esp,0xfffffff0 __asm fxsave [esp] __asm cld __asm push ebp __asm call F __asm lea esp,[esp+4] __asm fxrstor [esp] __asm mov esp,ebp __asm popad __asm popfd
__declspec(naked) void create_detour() {
    LIFE_NOTIFY(create_before)
    __asm {
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        call dword ptr [create_original]
        lea esp,[esp+20]
    }
    LIFE_NOTIFY(create_after)
    __asm ret
}
__declspec(naked) void free_detour() {LIFE_NOTIFY(free_before) __asm jmp dword ptr [free_original]}
__declspec(naked) void owner_reset_detour() {LIFE_NOTIFY(owner_reset_before) __asm jmp dword ptr [owner_memset_original]}
__declspec(naked) void pool_reset_detour() {LIFE_NOTIFY(pool_reset_before) __asm jmp dword ptr [pool_memset_original]}
__declspec(naked) void owner_free_detour() {LIFE_NOTIFY(owner_free_before) __asm jmp dword ptr [owner_free_original]}
#undef LIFE_NOTIFY
struct LifePatch {std::uintptr_t rva{};unsigned size{};const char* sha256{};void* detour{};bool entry{};DWORD protection{};};
std::array<LifePatch,6> life_patches{{
#if defined(BOVR_TEST_NATIVE_GUARDS)
    {0xcf420,6,"b0f66adc83641586656866813fd9dd0b8ebb63796075661ba45d1aa8089e1d44",reinterpret_cast<void*>(&create_detour),true},
    {0x3cfc0,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4",reinterpret_cast<void*>(&free_detour),true},
    {0x13359e,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4",reinterpret_cast<void*>(&owner_reset_detour),false},
    {0x24f7ae,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4",reinterpret_cast<void*>(&owner_reset_detour),false},
    {0xd2697,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4",reinterpret_cast<void*>(&pool_reset_detour),false},
    {0x23c883,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4",reinterpret_cast<void*>(&owner_free_detour),false}
#else
    {0xcf420,6,"d024984859d5fa24578ef4cea5b7eb19fe76dd64a6d9434b49d189c9bc2e9fb6",reinterpret_cast<void*>(&create_detour),true},
    {0x3cfc0,5,"4740aeadc7165174238c47a81fcb97826c1c52021c9d1384898840ef45bd9d7c",reinterpret_cast<void*>(&free_detour),true},
    {0x13359e,5,"958004427b97013a27b1fea6a0fed852241d8be7160db1aca62ae754c3831ed8",reinterpret_cast<void*>(&owner_reset_detour),false},
    {0x24f7ae,5,"c8f3400b2d3d9ad4484ffbf39c60664ca71f8493289066ecf7326a36053fdff5",reinterpret_cast<void*>(&owner_reset_detour),false},
    {0xd2697,5,"3246eb5d28e7384aff9cc69d3831dda489cf7cd38ab7b34331544055c45b2b6b",reinterpret_cast<void*>(&pool_reset_detour),false},
    {0x23c883,5,"b50eff320b328466fd30aaf10859c84080132d946ddd83b93d3b8ca14dba647b",reinterpret_cast<void*>(&owner_free_detour),false}
#endif
}};
void* life_trampoline(std::uintptr_t address,unsigned count) noexcept {
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,count+5,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!p) return nullptr;
    memcpy(p,reinterpret_cast<void*>(address),count);p[count]=0xe9;
    const auto displacement=static_cast<std::uint32_t>(address+count-reinterpret_cast<std::uintptr_t>(p+count+5));memcpy(p+count+1,&displacement,4);
    DWORD old{};if(!VirtualProtect(p,count+5,PAGE_EXECUTE_READ,&old)) {VirtualFree(p,0,MEM_RELEASE);return nullptr;}
    FlushInstructionCache(GetCurrentProcess(),p,count+5);return p;
}
bool patch_lifecycle(std::uintptr_t base) noexcept {
    if(lifecycle_installed) return lifecycle_base==base;
    for(const auto& p:life_patches) if(!native_guard::matches(reinterpret_cast<void*>(base+p.rva),p.size,p.sha256)) return false;
    auto* create=life_trampoline(base+life_patches[0].rva,6);if(!create) return false;
    auto* free=life_trampoline(base+life_patches[1].rva,5);if(!free) {VirtualFree(create,0,MEM_RELEASE);return false;}
    unsigned opened=0;DWORD old{};
    for(auto& p:life_patches) {if(!VirtualProtect(reinterpret_cast<void*>(base+p.rva),p.size,PAGE_EXECUTE_READWRITE,&p.protection)) break;++opened;}
    bool valid=opened==life_patches.size();
    if(valid) for(const auto& p:life_patches) if(!native_guard::matches(reinterpret_cast<void*>(base+p.rva),p.size,p.sha256)) valid=false;
    if(!valid) {
        for(unsigned i=0;i<opened;++i) VirtualProtect(reinterpret_cast<void*>(base+life_patches[i].rva),life_patches[i].size,life_patches[i].protection,&old);
        VirtualFree(create,0,MEM_RELEASE);VirtualFree(free,0,MEM_RELEASE);return false;
    }
    lifecycle_base=base;create_original=create;free_original=free;
    owner_memset_original=reinterpret_cast<void*>(base+0x565480);pool_memset_original=reinterpret_cast<void*>(base+0x1a41f0);owner_free_original=reinterpret_cast<void*>(base+0x64b00);
    for(const auto& p:life_patches) {
        auto* site=reinterpret_cast<unsigned char*>(base+p.rva);site[0]=p.entry?0xe9:0xe8;
        const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p.detour)-reinterpret_cast<std::uintptr_t>(site+5));memcpy(site+1,&relative,4);
        if(p.size==6) site[5]=0x90;
        const BOOL restored=VirtualProtect(site,p.size,p.protection,&old);FlushInstructionCache(GetCurrentProcess(),site,p.size);
        trace("t5_weapon_lifecycle_hook","site=%p bytes=%u protection_restored=%u",site,p.size,restored);
    }
    lifecycle_installed=true;return true;
}
}
bool read_weapon_lifetime(std::uintptr_t owner,std::uintptr_t dobj,WeaponLifetime* out) noexcept {
    if(out) *out={};if(!out||!lifecycle_installed||!owner||!dobj||current_owner()!=owner||!pool_matches(dobj)) return false;
    AcquireSRWLockShared(&lifecycle_lock);const bool valid=lifecycle_state.read(owner,dobj,GetCurrentThreadId(),out);ReleaseSRWLockShared(&lifecycle_lock);return valid;
}
bool install_weapon_lifecycle(HMODULE executable) noexcept {
    wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_LIFECYCLE_PROBE",flag,8);if(wcscmp(flag,L"1")) return false;
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000) return false;
    try {std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason)) return false;return patch_lifecycle(reinterpret_cast<std::uintptr_t>(executable));} catch(...) {return false;}
}
}
