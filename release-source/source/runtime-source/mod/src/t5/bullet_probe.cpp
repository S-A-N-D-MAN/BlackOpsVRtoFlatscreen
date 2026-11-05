// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/command_rig_adapter.hpp"
#include "t5/shot_body_probe.hpp"
#include "t5/consumed_command_probe.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/identity.hpp"
#include "t5/motorcycle_shot.hpp"
#include "t5/dual_pistol_shot.hpp"
#include "t5/dual_pistol_effects.hpp"
#include "t5/dual_pistol_projectile.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/ordinary_consumed_aim.hpp"
#include "t5/ordinary_event_fx.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <tlhelp32.h>
#if !defined(_M_IX86)
#error Pinned native bullet observer requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t bullet_call_rva=0xcfdd2,bullet_native_rva=0x625c0;


std::uintptr_t bullet_base{};void* bullet_original{};
std::atomic<std::uint64_t> bullet_serial{};
SRWLOCK bullet_callback_lock=SRWLOCK_INIT;
LocalShotCallbacks bullet_callbacks{};unsigned bullet_callbacks_in_flight{};
SRWLOCK bullet_selector_lock=SRWLOCK_INIT;
BulletSelector bullet_selector{};unsigned bullet_selectors_in_flight{};
bool bullet_selection_enabled{};
bool ordinary_call_patched{},script_call_patched{};
bool dual_pistol_selection_enabled{};
std::atomic<bool> dual_hooks_ready{};
std::atomic<bool> ordinary_consumed_ready{};
std::atomic<bool> ordinary_event_ready{};
SRWLOCK dual_install_lock=SRWLOCK_INIT;
enum class InstallKind { dual,consumed,event_witness,event_angles };
struct DualInstallPeers {
    explicit DualInstallPeers(InstallKind selected=InstallKind::dual) noexcept:kind(selected){}
    InstallKind kind{};
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~DualInstallPeers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
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
            if(kind==InstallKind::consumed){if(c.Eip>=0x513e58&&c.Eip<0x513e5d)return false;}
            else if(kind==InstallKind::event_witness||kind==InstallKind::event_angles){
                if(c.Eip>=0x8908c5&&c.Eip<0x8908ca)return false;
                if(kind==InstallKind::event_angles&&c.Eip>=0x656260&&c.Eip<0x656268)return false;}
            else for(const auto site:{std::uintptr_t{0x4cfdd2},std::uintptr_t{0x799c35},std::uintptr_t{0x4cfe8e}})
                    if(c.Eip>=site&&c.Eip<site+5)return false;
        }
        return true;
    }
};
template<class T> T bullet_load(std::uintptr_t address) noexcept {T value{};memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));return value;}
struct LinkedBulletFields {
    primary_weapon_tracking::Player player{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server server{};
    friend bool operator==(const LinkedBulletFields&,const LinkedBulletFields&)=default;
};
primary_weapon_tracking::Player bullet_player(std::uintptr_t ps) noexcept {
    return {bullet_load<int>(ps+4),bullet_load<int>(ps+0x1c4),bullet_load<std::uint32_t>(ps+0xc),
        bullet_load<std::uint32_t>(ps+0x10),bullet_load<std::uint32_t>(ps+0xe0),bullet_load<std::uint32_t>(ps+0xe4)};
}
LinkedBulletFields linked_bullet_fields(std::uintptr_t cg,std::uintptr_t client,std::uintptr_t entity) noexcept {
    LinkedBulletFields f{};f.player=bullet_player(cg+0x8a364);
    f.view={bullet_load<int>(cg+0x8a360),bullet_load<int>(bullet_base+0x2510160),bullet_load<int>(bullet_base+0x2510164),
        bullet_load<int>(bullet_base+0x2511e2c)!=0,bullet_load<int>(cg+0xc)!=0,bullet_load<int>(cg+0xa9d44)!=0};
    f.server={bullet_player(client),true,bullet_load<int>(entity+0x184),bullet_load<int>(client+0x1a2c),
        bullet_load<int>(client+0x1a18),bullet_load<std::uint32_t>(client+0x1c0c),bullet_load<std::uint32_t>(entity+0x298)};
    return f;
}
bool linked_bullet_scope(const NativeLocalShot& shot,NativeConsumedCommand* out) noexcept {
    NativeConsumedCommand scope{};int time{};
    if(!read_consumed_command_scope(&scope))return false;
    memcpy(&time,scope.command.data(),4);
    if(!scope.active||!scope.readable||!scope.local_identity||scope.native_returned||scope.nesting_invalidated||scope.mismatches||
       !scope.scope_serial||scope.image_base!=bullet_base||scope.native_thread!=shot.native_thread||scope.local_client_index||scope.server_state!=4||
       scope.entity_identity!=shot.attacker_identity||scope.game_client_identity!=shot.client_identity||
       !scope.source_command_identity||scope.command[0x18]!=shot.weapon_index||scope.command_time!=time||
       !scope.observed_ms||shot.observed_ms<scope.observed_ms||shot.observed_ms-scope.observed_ms>100)return false;
    *out=scope;return true;
}
NativeLocalShot read_local_shot(const std::uint32_t* args,std::uint64_t serial) noexcept {
    NativeLocalShot s{};s.serial=serial;s.observed_ms=GetTickCount64();s.native_thread=GetCurrentThreadId();
    s.attacker_identity=args[0];memcpy(&s.spread,args+1,4);s.game_time=static_cast<int>(args[4]);
    const auto entity=bullet_base+0x16796f8;
    if(args[0]!=entity||args[3]!=entity||!args[2]) return s;
    __try {
        const auto client=bullet_load<std::uintptr_t>(entity+0x13c),clients=bullet_load<std::uintptr_t>(bullet_base+0x1803140);
        s.client_identity=client;
        s.local_identity=client&&client==clients&&bullet_load<int>(entity)==0&&bullet_load<unsigned char>(client+0x130)==0;
        if(!s.local_identity) return s;
        const auto cg=bullet_load<std::uintptr_t>(bullet_base+0x2bf5354);if(!cg) return s;
        const auto ps=cg+0x8a364;if(bullet_load<unsigned char>(ps+0x130)!=0) return s;
        const auto wp=static_cast<std::uintptr_t>(args[2]);
        memcpy(s.forward,reinterpret_cast<void*>(wp),12);memcpy(s.right,reinterpret_cast<void*>(wp+12),12);
        memcpy(s.up,reinterpret_cast<void*>(wp+24),12);memcpy(s.muzzle,reinterpret_cast<void*>(wp+36),12);memcpy(s.gun_forward,reinterpret_cast<void*>(wp+48),12);
        s.variant_identity=bullet_load<std::uintptr_t>(wp+0x3c);s.definition_identity=bullet_load<std::uintptr_t>(wp+0x40);
        s.weapon_index=bullet_load<unsigned char>(entity+0xd4);
        s.weapon_identity=s.weapon_index>0&&s.weapon_index==bullet_load<unsigned char>(client+0x144)&&s.weapon_index==bullet_load<unsigned char>(ps+0x144)&&
            s.variant_identity&&s.definition_identity&&s.variant_identity==bullet_load<std::uintptr_t>(bullet_base+0x7e19a8+s.weapon_index*4)&&
            s.definition_identity==bullet_load<std::uintptr_t>(s.variant_identity+8)&&bullet_load<int>(s.definition_identity+0x1c)==0;
        s.server_pm_type=bullet_load<int>(client+4);s.server_health=bullet_load<int>(entity+0x184);s.server_weapon_state=bullet_load<int>(client+0x158);
        s.normal_context=s.server_pm_type==0&&s.server_health>0&&bullet_load<int>(client+0x1c4)>0&&
            bullet_load<int>(client+0x1a2c)==2&&bullet_load<int>(client+0x1a18)==0&&!bullet_load<std::uintptr_t>(entity+0x298)&&
            !(bullet_load<std::uint32_t>(client+0x1c0c)&0x17)&&!(bullet_load<std::uint32_t>(client+0xc)&0x800)&&
            !(bullet_load<std::uint32_t>(client+0x10)&0x180)&&!(bullet_load<std::uint32_t>(client+0xe0)&0x4300)&&!(bullet_load<std::uint32_t>(client+0xe4)&0x40040000)&&
            bullet_load<int>(ps+4)==0&&bullet_load<int>(ps+0x1c4)>0&&!(bullet_load<std::uint32_t>(ps+0xc)&0x800)&&
            !(bullet_load<std::uint32_t>(ps+0x10)&0x180)&&!(bullet_load<std::uint32_t>(ps+0xe0)&0x4300)&&!(bullet_load<std::uint32_t>(ps+0xe4)&0x40040000);
        if(!s.normal_context&&s.weapon_identity&&s.server_pm_type==1) {
            const auto first=linked_bullet_fields(cg,client,entity);
            NativeConsumedCommand before{},after{};
            if(primary_weapon_tracking::paired_mode(primary_weapon_tracking::client_mode(first.player,first.view),
               primary_weapon_tracking::server_mode(first.server))==primary_weapon_tracking::Mode::linked&&linked_bullet_scope(s,&before)) {
                const auto second=linked_bullet_fields(cg,client,entity);
                s.linked_context=first==second&&bullet_load<std::uintptr_t>(bullet_base+0x2bf5354)==cg&&
                    bullet_load<std::uintptr_t>(entity+0x13c)==client&&bullet_load<std::uintptr_t>(bullet_base+0x1803140)==client&&
                    bullet_load<int>(entity)==0&&bullet_load<unsigned char>(client+0x130)==0&&bullet_load<unsigned char>(ps+0x130)==0&&
                    bullet_load<unsigned char>(entity+0xd4)==s.weapon_index&&bullet_load<unsigned char>(client+0x144)==s.weapon_index&&
                    bullet_load<unsigned char>(ps+0x144)==s.weapon_index&&
                    bullet_load<std::uintptr_t>(bullet_base+0x7e19a8+s.weapon_index*4)==s.variant_identity&&
                    bullet_load<std::uintptr_t>(s.variant_identity+8)==s.definition_identity&&bullet_load<int>(s.definition_identity+0x1c)==0&&
                    linked_bullet_scope(s,&after)&&before.scope_serial==after.scope_serial&&before.command==after.command&&
                    before.server_client_identity==after.server_client_identity&&before.source_command_identity==after.source_command_identity&&
                    before.observed_ms==after.observed_ms;
                if(s.linked_context)s.server_link=first.server.link;
            }
        }
        s.readable=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return s;}
    s.finite=std::isfinite(s.spread)&&s.spread>=0;
    // The exact ordinary local type0 path initializes/consumes basis+muzzle
    // (WP0..2f). WP30..3b gunForward remains opaque unused stack bytes here;
    // requiring its float interpretation to be finite rejects legitimate shots.
    for(const auto* vector:{s.forward,s.right,s.up,s.muzzle}) for(unsigned i=0;i<3;++i) s.finite=s.finite&&std::isfinite(vector[i]);
    s.local_shot_confirmed=s.readable&&s.local_identity&&(s.normal_context||s.linked_context)&&s.weapon_identity&&s.finite;return s;
}
bool copy_bullet_parameters(std::uintptr_t address,void* output) noexcept {
    if(!address) return false;
    __try {memcpy(output,reinterpret_cast<const void*>(address),68);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool invoke_selector_cpp(const BulletSelector& selector,const NativeBulletSelectionContext& context,NativeBulletGeometry* output) noexcept {
    try {return selector.select(selector.context,context,output);} catch(...) {return false;}
}
bool invoke_selector_contained(const BulletSelector& selector,const NativeBulletSelectionContext& context,NativeBulletGeometry* output) noexcept {
    // Only the mod's copied-value callback is contained. No native game call is
    // inside this boundary, and all mod locks are released before callback entry.
    __try {return invoke_selector_cpp(selector,context,output);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool valid_bullet_geometry(const NativeBulletGeometry& value) noexcept {
    const float* basis[]{value.forward,value.right,value.up};
    for(const auto* vector:{value.forward,value.right,value.up,value.muzzle})
        for(unsigned i=0;i<3;++i)if(!std::isfinite(vector[i]))return false;
    const auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    for(unsigned i=0;i<3;++i) {
        if(std::fabs(dot(basis[i],basis[i])-1.F)>0.001F)return false;
        for(unsigned j=i+1;j<3;++j)if(std::fabs(dot(basis[i],basis[j]))>0.001F)return false;
    }
    // Native AngleVectors supplies F/R/U: cross(forward,right) == -up.
    // A visual F/L/U basis must be converted before this boundary.
    for(unsigned i=0;i<3;++i) {
        const auto j=(i+1)%3,k=(i+2)%3;
        if(std::fabs(value.forward[j]*value.right[k]-value.forward[k]*value.right[j]+value.up[i])>0.001F)return false;
    }
    return true;
}
void __cdecl before_bullet(std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();
    const auto* args=saved+28;
    saved[26]=args[2];
    if(saved[27]==bullet_base+0x16f818) {
        if(select_motorcycle_script_shot(bullet_base,args,reinterpret_cast<unsigned char*>(saved+9)))
            saved[26]=reinterpret_cast<std::uint32_t>(saved+9);
        // Every other MagicBullet keeps the exact original arguments and policy.
        SetLastError(error);return;
    }
    if(dual_pistol_selection_enabled){
        // pushad's EBP is the actual ClientEvents event; the selector also proves
        // the original enclosing stack and the event ring before using it.
        if(select_dual_pistol_shot(bullet_base,saved+27,saved[2],reinterpret_cast<unsigned char*>(saved+9)))
            saved[26]=reinterpret_cast<std::uint32_t>(saved+9);
        SetLastError(error);return;
    }
    AcquireSRWLockExclusive(&bullet_selector_lock);
    const auto selector=bullet_selector;
    const bool active=bullet_selection_enabled&&selector.select&&!bullet_selectors_in_flight;
    if(active) ++bullet_selectors_in_flight;
    ReleaseSRWLockExclusive(&bullet_selector_lock);
    const bool body_active=command_rig_enabled();
    if(active||body_active) {
        NativeBulletSelectionContext context{};
        const auto entity=bullet_base+0x16796f8;
        const bool copied=args[0]==entity&&args[3]==entity&&copy_bullet_parameters(args[2],context.native_parameters.data());
        if(copied) {
            // Derive context from this exact owned68-byte snapshot, never two
            // separate reads of the caller's WP across an intervening change.
            std::uint32_t copied_args[5]{};memcpy(copied_args,args,sizeof(copied_args));
            copied_args[2]=reinterpret_cast<std::uint32_t>(context.native_parameters.data());
            context.native=read_local_shot(copied_args,0);
            context.eligible=context.native.local_shot_confirmed;
            context.native.local_shot_confirmed=false; // This call has not fired yet.
        }
        if(body_active)trace_native_shot_body(bullet_base,context);
        if(active&&context.eligible) {
            NativeBulletGeometry geometry{};
            memcpy(&geometry,context.native_parameters.data(),sizeof(geometry));
            const bool selected=invoke_selector_contained(selector,context,&geometry)&&valid_bullet_geometry(geometry);
            if(selected) {
                auto* scratch=reinterpret_cast<unsigned char*>(saved+9);
                memcpy(scratch,context.native_parameters.data(),68);
                memcpy(scratch,&geometry,sizeof(geometry));
                saved[26]=reinterpret_cast<std::uint32_t>(scratch);
            }
        }
        if(active) {AcquireSRWLockExclusive(&bullet_selector_lock);--bullet_selectors_in_flight;ReleaseSRWLockExclusive(&bullet_selector_lock);}
    }
    SetLastError(error);
}
void __cdecl observe_bullet(const std::uint32_t* saved) noexcept {
    const DWORD error=GetLastError();
    if(saved[27]==bullet_base+0x16f818) {
        if(saved[26]!=saved[30])
            trace("t5_motorcycle_script_shot_completed","native_calls=1 argument_substituted=1");
        SetLastError(error);return;
    }
    const auto serial=bullet_serial.fetch_add(1,std::memory_order_relaxed)+1;
    std::uint32_t actual_args[5]{};memcpy(actual_args,saved+28,sizeof(actual_args));actual_args[2]=saved[26];
    const auto shot=read_local_shot(actual_args,serial);
    trace_native_shot_command_rig(shot);
    AcquireSRWLockExclusive(&bullet_callback_lock);const auto callbacks=bullet_callbacks;
    if(callbacks.after_native) ++bullet_callbacks_in_flight;ReleaseSRWLockExclusive(&bullet_callback_lock);
    if(callbacks.after_native) {
        try {callbacks.after_native(callbacks.context,shot);} catch(...) {trace("t5_bullet_callback_exception","serial=%llu",serial);}
        AcquireSRWLockExclusive(&bullet_callback_lock);--bullet_callbacks_in_flight;ReleaseSRWLockExclusive(&bullet_callback_lock);
    }
    if(shot.local_shot_confirmed||serial<=3||serial%240==0)
        trace("t5_native_bullet","serial=%llu thread=%lu observed_ms=%llu game_time=%d local_confirmed=%u readable=%u local_identity=%u normal=%u weapon_identity=%u finite=%u weapon=%d attacker=%p client=%p variant=%p definition=%p spread=%.6f muzzle=%.6f,%.6f,%.6f forward=%.6f,%.6f,%.6f original_calls=1 original_argument_writes=0 argument_substituted=%u hit_confirmed=0 linked=%u link=0x%x",
            serial,shot.native_thread,shot.observed_ms,shot.game_time,shot.local_shot_confirmed,shot.readable,shot.local_identity,shot.normal_context,shot.weapon_identity,shot.finite,shot.weapon_index,
            reinterpret_cast<void*>(shot.attacker_identity),reinterpret_cast<void*>(shot.client_identity),reinterpret_cast<void*>(shot.variant_identity),reinterpret_cast<void*>(shot.definition_identity),shot.spread,
            shot.muzzle[0],shot.muzzle[1],shot.muzzle[2],shot.forward[0],shot.forward[1],shot.forward[2],saved[26]!=saved[30],shot.linked_context,shot.server_link);
    SetLastError(error);
}
__declspec(naked) void bullet_detour() {
    __asm {
        // GP0..7,flags8,scratch9..25,selected26,return27,args28..32.
        // Scratch remains on this invocation's stack throughout native execution.
        lea esp,[esp-72]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call before_bullet
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        push dword ptr [esp+92]
        push dword ptr [esp+92]
        push dword ptr [esp+76]
        push dword ptr [esp+92]
        push dword ptr [esp+92]
        call dword ptr [bullet_original]
        lea esp,[esp+20]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call observe_bullet
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        lea esp,[esp+72]
        ret
    }
}
bool commit_bullet_call(std::uintptr_t base) noexcept {
    if(ordinary_call_patched) return bullet_base==base;
    if(bullet_original&&bullet_base!=base)return false;
    constexpr unsigned char expected[]{0xe8,0xe9,0x27,0xf9,0xff};
    auto* site=reinterpret_cast<unsigned char*>(base+bullet_call_rva);if(memcmp(site,expected,5))return false;
    std::int32_t displacement{};memcpy(&displacement,site+1,4);if(reinterpret_cast<std::uintptr_t>(site+5)+displacement!=base+bullet_native_rva) return false;
    DWORD protection{},old{};if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    if(memcmp(site,expected,5)) {VirtualProtect(site,5,protection,&old);return false;}
    bullet_base=base;bullet_original=reinterpret_cast<void*>(base+bullet_native_rva);
    const auto offset=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bullet_detour)-reinterpret_cast<std::uintptr_t>(site+5));memcpy(site+1,&offset,4);
    const BOOL restored=VirtualProtect(site,5,protection,&old);FlushInstructionCache(GetCurrentProcess(),site,5);
    ordinary_call_patched=true;
    return restored!=FALSE;
}
bool patch_bullet_call(std::uintptr_t base) noexcept {
    if(ordinary_call_patched)return bullet_base==base;
    if(native_guard::compare(reinterpret_cast<void*>(base+bullet_call_rva),5,
       "a3939f360275533a5b097554780fbd1ca7cfce2b4d91272d37267a5769be3a4b"))return false;
    const bool okay=commit_bullet_call(base);
    if(okay)trace("t5_bullet_probe_installed","site=%p target=%p bytes=5 protection_restored=1 native_calls=1 argument_writes=0",reinterpret_cast<void*>(base+bullet_call_rva),bullet_original);
    return okay;
}
bool patch_motorcycle_script_call(std::uintptr_t base) noexcept {
    if(script_call_patched)return bullet_base==base;
    if(bullet_original&&bullet_base!=base)return false;
    auto* caller=reinterpret_cast<unsigned char*>(base+0x16f4a0);
    auto* site=reinterpret_cast<unsigned char*>(base+0x16f813);
    if(bovr::native_guard::compare(caller,0x380,
       "fcdf08afad9e72a9df29268987699ccd45e477cebdd9c29ac36fd9e4b673d73a"))return false;
    DWORD protection{},old{};if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection))return false;
    bullet_base=base;bullet_original=reinterpret_cast<void*>(base+bullet_native_rva);
    const auto offset=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&bullet_detour)-reinterpret_cast<std::uintptr_t>(site+5));
    memcpy(site+1,&offset,4);const BOOL restored=VirtualProtect(site,5,protection,&old);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    script_call_patched=true;
    trace("t5_motorcycle_script_shot_hook","site=%p installed=1 protection_restored=%u exact_weapon_only=1",site,restored);return true;
}
}
bool set_bullet_selector(const BulletSelector& selector) noexcept {
    const DWORD error=GetLastError();
    wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_BULLET_SELECTION",flag,8);
    const bool enabled=!wcscmp(flag,L"1");
    AcquireSRWLockExclusive(&bullet_selector_lock);
    const bool allowed=!bullet_selectors_in_flight&&(!selector.select||(enabled&&bullet_original));
    if(allowed) {bullet_selector=selector;bullet_selection_enabled=enabled;}
    ReleaseSRWLockExclusive(&bullet_selector_lock);
    SetLastError(error);return allowed;
}
bool set_local_shot_callbacks(const LocalShotCallbacks& callbacks) noexcept {
    AcquireSRWLockExclusive(&bullet_callback_lock);const bool allowed=!bullet_callbacks_in_flight&&(!callbacks.after_native||bullet_original);
    if(allowed) bullet_callbacks=callbacks;ReleaseSRWLockExclusive(&bullet_callback_lock);return allowed;
}
bool install_bullet_probe(HMODULE executable) noexcept {
    wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_BULLET_PROBE",flag,8);if(wcscmp(flag,L"1")) return false;
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000) return false;
    try {std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason)) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);if(bovr::native_guard::compare(reinterpret_cast<void*>(base+bullet_native_rva), 8, "589dae351264bef13a10d98febef43bda87fb00b90662ece321791a3735add61")) return false;
        return patch_bullet_call(base);
    } catch(...) {return false;}
}
bool install_motorcycle_script_shot(HMODULE executable) noexcept {
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    try {std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);
        if(bovr::native_guard::compare(reinterpret_cast<void*>(base+bullet_native_rva),8,
           "589dae351264bef13a10d98febef43bda87fb00b90662ece321791a3735add61"))return false;
        return patch_motorcycle_script_call(base);
    }catch(...){return false;}
}
bool install_dual_pistol_shot(HMODULE executable) noexcept {
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    try {std::string reason;if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);
        if(!dual_pistol_shot_guards(base)||native_guard::compare(reinterpret_cast<void*>(base+bullet_native_rva),8,
           "589dae351264bef13a10d98febef43bda87fb00b90662ece321791a3735add61"))return false;
        if(!patch_bullet_call(base))return false;
        dual_pistol_selection_enabled=true;trace("t5_dual_pistol_shot_hook","installed=1 event_hands=29,30:right,31,32:left exact_command_match=1 reload_writes=0");return true;
    }catch(...){return false;}
}
bool ensure_dual_pistol_hooks(const CommandContext& context) noexcept {
    const auto error=GetLastError();PreCommandSnapshot pre{};
    const bool dual=native_command_dual_current(context);
    const bool ordinary=dual_pistol_effects::ordinary_mode()!=dual_pistol_effects::OrdinaryMode::disabled&&
        native_command_hitscan_current(context);
    const auto fx_mode=ordinary_event_fx::mode();
    const bool effects=fx_mode!=ordinary_event_fx::Mode::disabled&&native_command_hitscan_current(context);
    if(context.native_thread!=GetCurrentThreadId()||(!dual&&!ordinary&&!effects)||
       !read_pre_command_snapshot(&pre)||!pre.gameplay_active||pre.native_pm_type!=0||
       pre.context.native_thread!=context.native_thread||pre.context.command_number!=context.command_number){SetLastError(error);return false;}
    if(!dual){
        if((!ordinary||ordinary_consumed_ready.load(std::memory_order_acquire))&&
           (!effects||ordinary_event_ready.load(std::memory_order_acquire))){SetLastError(error);return true;}
        AcquireSRWLockExclusive(&dual_install_lock);
        static unsigned attempts{};static std::uint64_t last_attempt{};
        const auto now=GetTickCount64();bool okay=!ordinary||ordinary_consumed_ready.load(std::memory_order_acquire);
        if(ordinary&&!okay&&attempts<3&&(!last_attempt||(now>=last_attempt&&now-last_attempt>=1000))){
            ++attempts;last_attempt=now;
            const auto image=GetModuleHandleW(nullptr);
            const bool prepared=ordinary_consumed_aim::prepare(reinterpret_cast<std::uintptr_t>(image))&&
                prepare_consumed_command_probe(image);
            {DualInstallPeers peers(InstallKind::consumed);
                if(prepared&&peers.acquire())okay=commit_consumed_command_probe();}
            if(okay){ordinary_consumed_aim::enable();ordinary_consumed_ready.store(true,std::memory_order_release);}
            trace("t5_ordinary_consumed_aim_hook","accepted=%u attempt=%u command=%d call=00513e58 existing_wrapper=1 shot_call_sites_changed=0 client_fx_correction=0",okay,attempts,context.command_number);
        }
        if(effects){static unsigned fx_attempts{};static std::uint64_t fx_last_attempt{};
            bool fx_okay=ordinary_event_ready.load(std::memory_order_acquire);
            if(!fx_okay&&fx_attempts<3&&(!fx_last_attempt||(now>=fx_last_attempt&&now-fx_last_attempt>=1000))){
                ++fx_attempts;fx_last_attempt=now;const auto image=GetModuleHandleW(nullptr);
                const bool prepared=prepare_ordinary_event_fx(image);
                {DualInstallPeers peers(fx_mode==ordinary_event_fx::Mode::witness?InstallKind::event_witness:InstallKind::event_angles);
                    if(prepared&&peers.acquire())fx_okay=commit_ordinary_event_fx();}
                if(fx_okay)ordinary_event_ready.store(true,std::memory_order_release);
                trace("t5_ordinary_event_fx_hook","accepted=%u attempt=%u command=%d mode=%u pmove_call=008908c5 event_entry=00656260 event_entry_installed=%u old_three_sites_changed=0",fx_okay,fx_attempts,context.command_number,unsigned(fx_mode),fx_okay&&fx_mode!=ordinary_event_fx::Mode::witness);
            }
            okay=okay&&fx_okay;
        }
        ReleaseSRWLockExclusive(&dual_install_lock);SetLastError(error);return okay;
    }
    if(dual_hooks_ready.load(std::memory_order_acquire)){SetLastError(error);return true;}
    AcquireSRWLockExclusive(&dual_install_lock);
    static unsigned attempts{};static std::uint64_t last_attempt{};
    const auto now=GetTickCount64();bool okay=dual_hooks_ready.load(std::memory_order_acquire);
    if(!okay&&attempts<3&&(!last_attempt||(now>=last_attempt&&now-last_attempt>=1000))){
        ++attempts;last_attempt=now;
        try {
            const auto image=GetModuleHandleW(nullptr);std::string reason;
            const bool prepared=reinterpret_cast<std::uintptr_t>(image)==0x400000&&validate_path(executable_path()).accepted&&
                validate_loaded_headers(image,reason)&&dual_pistol_shot_guards(0x400000)&&
                !native_guard::compare(reinterpret_cast<void*>(0x4625c0),8,"589dae351264bef13a10d98febef43bda87fb00b90662ece321791a3735add61")&&
                prepare_dual_pistol_effects(image)&&prepare_dual_pistol_projectile(image);
            DualInstallPeers peers;
            // Both sites are quiescent while their four-byte displacements change.
            // File/crypto/trace work is complete before any peer is suspended.
            if(prepared&&peers.acquire()){
                okay=commit_bullet_call(0x400000)&&commit_dual_pistol_effects()&&commit_dual_pistol_projectile();
                if(okay){dual_pistol_selection_enabled=true;dual_hooks_ready.store(true,std::memory_order_release);}
            }
        }catch(...){okay=false;}
        trace("t5_dual_pistol_hooks_deferred","accepted=%u attempt=%u command=%d dual=%u ordinary_effects_mode=%u owner=live_exact_input peer_instruction_pointers_checked=1 startup_code_writes=0",okay,attempts,context.command_number,dual,unsigned(dual_pistol_effects::ordinary_mode()));
    }
    ReleaseSRWLockExclusive(&dual_install_lock);SetLastError(error);return okay;
}
}
