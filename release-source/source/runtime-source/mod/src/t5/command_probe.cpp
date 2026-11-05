// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/command_probe.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/shot_haptic_adapter.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#if !defined(_M_IX86)
#error The pinned command ABI requires MSVC x86.
#endif

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t create_cmd_rva=0x4822d0;
#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr native_guard::Span entry_guard{create_cmd_rva,8,"d3cbaa211e9194dc7ef1d35cb55bbdde660c064832e3bff281047b656d902e74"};
#else
constexpr native_guard::Span entry_guard{create_cmd_rva,8,"cd57d2584e9f22d8f1fe25fa1e1c7e08966171cdd4b773e173487aa49a8049b6"};
#endif
void* original_create_cmd{};
std::uintptr_t image_base{};
std::atomic<unsigned long> calls{};
SRWLOCK callback_lock=SRWLOCK_INIT;
CommandCallbacks registered_callbacks{};
unsigned callbacks_in_flight{};
SRWLOCK pre_snapshot_lock=SRWLOCK_INIT;
PreCommandSnapshot pre_snapshot{};
struct Snapshot {
    unsigned char command[0x34]{};
    float viewangles[3]{}, kickangles[3]{};
    int command_number{}, key_catcher{}, connection_state{};
    bool valid{};
};
Snapshot snapshot(const void* result) noexcept {
    Snapshot s{};
    __try {
        memcpy(s.command,result,sizeof(s.command));
        memcpy(s.viewangles,reinterpret_cast<const void*>(image_base+0x2511e20),sizeof(s.viewangles));
        memcpy(s.kickangles,reinterpret_cast<const void*>(image_base+0x2511da8),sizeof(s.kickangles));
        memcpy(&s.command_number,reinterpret_cast<const void*>(image_base+0x2593850),4);
        // Key_IsCatcherActive0x509780 directly masks this integer. Connection
        // state is the exact integer tested >=9 by the native ring writer.
        memcpy(&s.key_catcher,reinterpret_cast<const void*>(image_base+0x2510160),4);
        memcpy(&s.connection_state,reinterpret_cast<const void*>(image_base+0x2510164),4);
        s.valid=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { s.valid=false; }
    return s;
}
CommandContext read_context(int local_client) noexcept {
    CommandContext context{};
    context.local_client=local_client; context.native_thread=GetCurrentThreadId();
    __try {
        memcpy(context.viewangles,reinterpret_cast<const void*>(image_base+0x2511e20),sizeof(context.viewangles));
        memcpy(context.kickangles,reinterpret_cast<const void*>(image_base+0x2511da8),sizeof(context.kickangles));
        memcpy(&context.command_number,reinterpret_cast<const void*>(image_base+0x2593850),4);
        memcpy(&context.key_catcher,reinterpret_cast<const void*>(image_base+0x2510160),4);
        memcpy(&context.connection_state,reinterpret_cast<const void*>(image_base+0x2510164),4);
        memcpy(&context.native_frame_time,reinterpret_cast<const void*>(image_base+0x2081764),4);
        int native_override{};
        memcpy(&native_override,reinterpret_cast<const void*>(image_base+0x2511e2c),4);
        context.native_override=native_override!=0;
        context.valid=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { context.valid=false; }
    return context;
}
template<class T> T command_load(std::uintptr_t p) noexcept {T value{};memcpy(&value,reinterpret_cast<const void*>(p),sizeof(value));return value;}
void copy_native_gameplay(PreCommandSnapshot& s) noexcept {
    const auto& c=s.context;if(!c.valid||c.local_client!=0) return;
    __try {
        s.native_cg_identity=command_load<std::uintptr_t>(image_base+0x2bf5354);
        // Reject the base before adding field offsets. An invalid small base
        // can otherwise land in unrelated readable memory after relocation.
        if(s.native_cg_identity<0x10000||s.native_cg_identity>UINT32_MAX-0xa9d48)return;
        const auto cg=s.native_cg_identity,ps=cg+0x8a364;
        if(command_load<unsigned char>(ps+0x130)!=0) return;
        s.native_game_time=command_load<int>(cg+0x8a348);s.native_health=command_load<int>(ps+0x1c4);s.native_pm_type=command_load<int>(ps+4);
        s.native_pm_flags=command_load<std::uint32_t>(ps+0xc);s.native_weapon_flags=command_load<std::uint32_t>(ps+0x10);
        s.native_entity_flags=command_load<std::uint32_t>(ps+0xe0);s.native_entity_flags2=command_load<std::uint32_t>(ps+0xe4);
        const bool camera_native=native_first_person_camera(command_load<int>(cg+0x8a360))&&!command_load<int>(cg+0xc)&&!command_load<int>(cg+0xa9d44);
        s.native_gameplay_fields_valid=true;
        const auto weapon_mode=primary_weapon_tracking::player_mode({s.native_pm_type,s.native_health,
            s.native_pm_flags,s.native_weapon_flags,s.native_entity_flags,s.native_entity_flags2});
        // Use the same primary-weapon state policy as the placement and render
        // probes. Black Ops leaves offhand-disable 0x100 set after the breach;
        // treating the old combined 0x180 mask as a gameplay rejection starves
        // the tracked scene transaction and leaves only one native world view.
        s.gameplay_active=c.connection_state>=9&&!c.key_catcher&&!c.native_override&&
            weapon_mode==primary_weapon_tracking::Mode::ordinary&&camera_native;
        s.linked_weapon_active=c.connection_state>=9&&!c.key_catcher&&!c.native_override&&camera_native&&
            weapon_mode==primary_weapon_tracking::Mode::linked;
    } __except(EXCEPTION_EXECUTE_HANDLER) {s.native_gameplay_fields_valid=false;s.gameplay_active=false;s.linked_weapon_active=false;}
}
bool update_gameplay_epoch(const PreCommandSnapshot& previous,PreCommandSnapshot& next) noexcept {
    next.sequence=previous.sequence+1;next.gameplay_epoch=previous.gameplay_epoch;
    const bool regression=previous.sequence&&((previous.context.valid&&next.context.valid&&
        (next.context.command_number<previous.context.command_number||next.context.native_frame_time<previous.context.native_frame_time))||
        (previous.native_gameplay_fields_valid&&next.native_gameplay_fields_valid&&
        (next.native_cg_identity!=previous.native_cg_identity||next.native_game_time<previous.native_game_time)));
    const bool transition=previous.sequence&&(previous.gameplay_active!=next.gameplay_active||previous.linked_weapon_active!=next.linked_weapon_active||previous.context.valid!=next.context.valid||
        previous.native_gameplay_fields_valid!=next.native_gameplay_fields_valid);
    if(regression||transition||(!next.gameplay_epoch&&primary_weapon_active(next))) {++next.gameplay_epoch;return true;}
    return false;
}
void dispatch_callback(bool before,int local_client,unsigned char* result) noexcept {
    AcquireSRWLockExclusive(&callback_lock);
    const auto callbacks=registered_callbacks;
    const bool active=before?callbacks.before_native!=nullptr:callbacks.after_native!=nullptr;
    if(active) ++callbacks_in_flight;
    ReleaseSRWLockExclusive(&callback_lock);
    if(!active) return;
    const auto context=read_context(local_client);
    try {
        if(before) callbacks.before_native(callbacks.context,context);
        else callbacks.after_native(callbacks.context,context,result);
    } catch(...) { trace("t5_command_callback_exception","before=%u",before); }
    AcquireSRWLockExclusive(&callback_lock);
    --callbacks_in_flight;
    ReleaseSRWLockExclusive(&callback_lock);
}
void __cdecl before_command(int local_client) noexcept {
    const DWORD error=GetLastError();
    PreCommandSnapshot next{};next.context=read_context(local_client);next.observed_ms=GetTickCount64();copy_native_gameplay(next);
    AcquireSRWLockExclusive(&pre_snapshot_lock);
    const bool changed=update_gameplay_epoch(pre_snapshot,next);pre_snapshot=next;
    ReleaseSRWLockExclusive(&pre_snapshot_lock);
    observe_predicted_shot_feedback(next);
    if(changed) trace("t5_native_gameplay_epoch","sequence=%llu epoch=%llu active=%u fields_valid=%u thread=%lu command=%d native_clock=%d cg=%p game_time=%d health=%d pm=%d override=%u catcher=0x%x connection=%d linked_weapon=%u pm_flags=0x%x weapon_flags=0x%x",
        next.sequence,next.gameplay_epoch,next.gameplay_active,next.native_gameplay_fields_valid,next.context.native_thread,next.context.command_number,next.context.native_frame_time,
        reinterpret_cast<void*>(next.native_cg_identity),next.native_game_time,next.native_health,next.native_pm_type,next.context.native_override,next.context.key_catcher,next.context.connection_state,
        next.linked_weapon_active,next.native_pm_flags,next.native_weapon_flags);
    dispatch_callback(true,local_client,nullptr);SetLastError(error);
}
std::uint32_t word(const unsigned char* p) noexcept {
    std::uint32_t value{}; memcpy(&value,p,4); return value;
}
void __cdecl observe_command(const void* result, std::uintptr_t caller, int local_client) noexcept {
    const auto serial=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(serial>12 && serial%120!=0) return;
    const auto s=snapshot(result);
    trace("t5_command", "call=%lu thread=%lu caller_rva=0x%lx result=%p local_client=%d valid=%u command_number=%d key_catcher=0x%x connection_state=%d server_time=%lu buttons=%08lx,%08lx angles=%lu,%lu,%lu weapon=%u offhand=%u alt_weapon=%u forward=%d right=%d native_view=%.6f,%.6f,%.6f kick=%.6f,%.6f,%.6f original_calls=1 command_writes=0",
        serial,GetCurrentThreadId(),static_cast<unsigned long>(caller-image_base),result,local_client,s.valid,
        s.command_number,s.key_catcher,s.connection_state,
        static_cast<unsigned long>(word(s.command)),static_cast<unsigned long>(word(s.command+4)),
        static_cast<unsigned long>(word(s.command+8)),static_cast<unsigned long>(word(s.command+0xc)),
        static_cast<unsigned long>(word(s.command+0x10)),static_cast<unsigned long>(word(s.command+0x14)),
        static_cast<unsigned>(s.command[0x18]),static_cast<unsigned>(s.command[0x19]),static_cast<unsigned>(s.command[0x1a]),
        static_cast<int>(static_cast<std::int8_t>(s.command[0x1b])),static_cast<int>(static_cast<std::int8_t>(s.command[0x1c])),
        s.viewangles[0],s.viewangles[1],s.viewangles[2],s.kickangles[0],s.kickangles[1],s.kickangles[2]);
}
void __cdecl after_command(unsigned char* result,std::uintptr_t caller,int local_client) noexcept {
    const DWORD error=GetLastError();
    observe_command(result,caller,local_client);
    dispatch_callback(false,local_client,result);
    SetLastError(error);
}

// Entry: EAX=result, [ESP+4]=localClient, [ESP]=native return address.
// Re-push the sole stack argument before calling the original exactly once.
// No result buffer is exposed before native input construction. Before/after
// callbacks default empty. After native returns, save
// its complete register/flags/x87/SSE state around observation, including EAX.
// At saved ESP (EBP), GP EAX is +28, flags+32, native return+36, client+40.
__declspec(naked) void command_detour() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+40]
        call before_command
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        push dword ptr [esp+4]
        call dword ptr [original_create_cmd]
        lea esp,[esp+4]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push dword ptr [ebp+40]
        push dword ptr [ebp+36]
        push dword ptr [ebp+28]
        call after_command
        lea esp,[esp+12]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
void jump(unsigned char* output,const void* target) noexcept {
    output[0]=0xe9;
    const auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-
                                                (reinterpret_cast<std::uintptr_t>(output)+5));
    memcpy(output+1,&relative,4);
}
}
bool read_pre_command_snapshot(PreCommandSnapshot* output) noexcept {
    if(!output) return false;*output={};
    AcquireSRWLockShared(&pre_snapshot_lock);const auto copy=pre_snapshot;ReleaseSRWLockShared(&pre_snapshot_lock);
    if(!original_create_cmd||!copy.sequence||!copy.context.valid) return false;
    *output=copy;return true;
}
bool set_command_callbacks(const CommandCallbacks& callbacks) noexcept {
    AcquireSRWLockExclusive(&callback_lock);
    const bool nonempty=callbacks.before_native || callbacks.after_native;
    const bool allowed=callbacks_in_flight==0 && (!nonempty || original_create_cmd!=nullptr);
    if(allowed) registered_callbacks=callbacks;
    ReleaseSRWLockExclusive(&callback_lock);
    return allowed;
}
bool install_command_probe(HMODULE executable) noexcept {
    wchar_t enabled[8]{};
    GetEnvironmentVariableW(L"BOVR_COMMAND_PROBE",enabled,8);
    if(wcscmp(enabled,L"1")!=0) return false;
    if(original_create_cmd) return image_base==reinterpret_cast<std::uintptr_t>(executable);
    if(!executable) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(executable);
    auto* entry=reinterpret_cast<unsigned char*>(base+create_cmd_rva);
    // sub esp,0xc; push ebx; mov ebx,[esp+0x14]. Three complete instructions,
    // eight bytes, with no PC-relative operands. Stack effects remain exact.
    if(!native_guard::matches(base,entry_guard)) {
        trace("t5_command_probe_rejected","reason=loaded_entry_bytes_mismatch"); return false;
    }
    std::array<unsigned char,8> original{};
    memcpy(original.data(),entry,original.size());
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,13,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!trampoline) return false;
    memcpy(trampoline,original.data(),original.size()); jump(trampoline+8,entry+8);
    DWORD old{};
    if(!VirtualProtect(trampoline,13,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(),trampoline,13);
    DWORD protection{};
    if(!VirtualProtect(entry,8,PAGE_EXECUTE_READWRITE,&protection)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    if(!native_guard::matches(base,entry_guard)) {
        VirtualProtect(entry,8,protection,&old); VirtualFree(trampoline,0,MEM_RELEASE);
        trace("t5_command_probe_rejected","reason=entry_changed_before_write"); return false;
    }
    image_base=base; original_create_cmd=trampoline;
    jump(entry,reinterpret_cast<void*>(&command_detour)); memset(entry+5,0x90,3);
    const BOOL restored=VirtualProtect(entry,8,protection,&old);
    FlushInstructionCache(GetCurrentProcess(),entry,8);
    trace("t5_command_probe_installed","entry=%p trampoline=%p bytes=8 protection_restored=%u original_calls_per_dispatch=1 command_writes=0",entry,trampoline,restored);
    return true;
}
}
