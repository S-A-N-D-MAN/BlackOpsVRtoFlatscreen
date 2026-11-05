// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9_observer.hpp"
#include "loader/trace.hpp"
#include "t5/identity.hpp"
#include "t5/scene_probe.hpp"
#include "t5/backend_probe.hpp"
#include "t5/command_probe.hpp"
#include "t5/consumed_command_probe.hpp"
#include "t5/command_rig_adapter.hpp"
#include "t5/quit_gate.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/input_adapter.hpp"
#include "t5/mounted_gun_runtime.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "t5/helicopter_aim_adapter.hpp"
#include "t5/native_use_gaze_adapter.hpp"
#include "t5/infrared_scope_adapter.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/weapon_placement_adapter.hpp"
#include "t5/weapon_barrel_probe.hpp"
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/weapon_profile_probe.hpp"
#include "t5/final_ui_probe.hpp"
#include "t5/menu_input_adapter.hpp"
#include "t5/ui_producer_probe.hpp"
#include "t5/weapon_lifecycle.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/shot_haptic_adapter.hpp"
#include "t5/hud_pass_probe.hpp"
#include "t5/frame_begin_probe.hpp"
#include <windows.h>

extern "C" { void* bovr_exports[23]{}; }
namespace {
INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
INIT_ONCE factory_seams_initialized = INIT_ONCE_STATIC_INIT;
HMODULE proxy_module{};
HMODULE system_d3d9{};
bool instrument{};
bool promote_ex{};
bool scene_probe_installed{};
bool backend_probe_installed{};
bool command_probe_installed{};
bool consumed_command_probe_installed{};
bool weapon_probe_installed{};
bool final_ui_probe_installed{};
bool event_loop_probe_installed{};
bool ui_producer_probe_installed{};
bool weapon_lifecycle_installed{};
bool bullet_probe_installed{};
bool hud_pass_probe_installed{};
bool frame_begin_probe_installed{};
BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) noexcept {
    bovr::initialize_trace(proxy_module);
    wchar_t path[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n + 10 >= MAX_PATH) return FALSE;
    wcscat_s(path, L"\\d3d9.dll");
    system_d3d9 = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!system_d3d9 || system_d3d9 == proxy_module) {
        bovr::trace("system_d3d9_failed", "error=%lu", GetLastError()); return FALSE;
    }
    // Named API exports are resolved by name: Windows versions have different
    // ordinal bases. The unnamed compatibility surface is pinned to this host.
    static constexpr const char* export_names[23] = {
        nullptr, nullptr, nullptr, nullptr, "Direct3DCreate9On12", "Direct3DCreate9On12Ex", nullptr, nullptr,
        "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture", "D3DPERF_BeginEvent", "D3DPERF_EndEvent",
        "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame", "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion",
        "DebugSetLevel", "DebugSetMute", "Direct3D9EnableMaximizedWindowedModeShim", "Direct3DCreate9", "Direct3DCreate9Ex"
    };
    for (unsigned ordinal = 16; ordinal <= 38; ++ordinal) {
        const char* name = export_names[ordinal - 16];
        bovr_exports[ordinal - 16] = reinterpret_cast<void*>(GetProcAddress(system_d3d9, name ? name : MAKEINTRESOURCEA(ordinal)));
    }
    try {
        const auto identity = bovr::t5::validate_path(bovr::t5::executable_path());
        std::string loaded_reason;
        const bool loaded = identity.accepted && bovr::t5::validate_loaded_headers(GetModuleHandleW(nullptr), loaded_reason);
        wchar_t enabled[8]{};
        GetEnvironmentVariableW(L"BOVR_INSTRUMENT", enabled, 8);
        HMODULE pinned{};
        // Opt-in observation; disabled or unknown hosts always get stock D3D9.
        instrument = loaded && wcscmp(enabled, L"1") == 0 &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&initialize), &pinned) != FALSE;
        wchar_t ex_enabled[8]{};
        GetEnvironmentVariableW(L"BOVR_D3D9EX", ex_enabled, 8);
        promote_ex = instrument && wcscmp(ex_enabled, L"1") == 0;
        bovr::trace("identity", "accepted=%u loaded_headers=%u observation=%u hash=%s reason=%s loaded_reason=%s",
                    identity.accepted, loaded, instrument, identity.sha256.c_str(), identity.reason.c_str(), loaded_reason.c_str());
    } catch (...) {
        instrument = false;
        promote_ex = false;
        bovr::trace("identity_failed", "observation=0 reason=exception; stock forwarding retained");
    }
    bovr::set_observation_active(instrument);
    wchar_t recovery_enabled[8]{};
    GetEnvironmentVariableW(L"BOVR_RECOVERY_OBSERVE",recovery_enabled,8);
    const bool recovery_observe=instrument&&wcscmp(recovery_enabled,L"1")==0;
    bovr::set_recovery_diagnostics(recovery_observe);
    bovr::trace("recovery_observation_ready","enabled=%u engine_instruction_patches=0 status_overrides=0",recovery_observe);
    bovr::trace("bootstrap_ready", "system_module=%p engine_instruction_patches=%u promote_ex=%u vr=0", system_d3d9, scene_probe_installed ? 1 : 0, promote_ex);
    return TRUE;
}
BOOL CALLBACK initialize_factory_seams(PINIT_ONCE, PVOID, PVOID*) noexcept {
    // A retail static graphics factory is the bounded startup seam. Never install
    // a code patch from an unrelated D3DPERF export, DllMain, or a running frame.
    bool adapter_configured{};
    bool input_configured{};
    bool menu_input_configured{};
    bool weapon_placement_configured{};
    bool shot_haptics_configured{};
    bool command_rig_configured{};
    if (instrument) {
        scene_probe_installed = bovr::t5::install_scene_probe(GetModuleHandleW(nullptr));
        backend_probe_installed = bovr::t5::install_backend_probe(GetModuleHandleW(nullptr));
        command_probe_installed = bovr::t5::install_command_probe(GetModuleHandleW(nullptr));
        consumed_command_probe_installed = bovr::t5::install_consumed_command_probe(GetModuleHandleW(nullptr));
        weapon_lifecycle_installed = bovr::t5::install_weapon_lifecycle(GetModuleHandleW(nullptr));
        weapon_probe_installed = bovr::t5::install_weapon_probe(GetModuleHandleW(nullptr));
        bovr::t5::configure_weapon_barrel_probe(GetModuleHandleW(nullptr),weapon_probe_installed&&command_probe_installed);
        bovr::t5::configure_weapon_profile_probe(GetModuleHandleW(nullptr),weapon_probe_installed&&command_probe_installed);
        bovr::t5::configure_weapon_animation_probe(GetModuleHandleW(nullptr),weapon_probe_installed&&command_probe_installed);
        bovr::t5::configure_weapon_native_clear_probe(GetModuleHandleW(nullptr),weapon_probe_installed&&command_probe_installed);
        bovr::t5::configure_weapon_borrowed_probe(GetModuleHandleW(nullptr),weapon_probe_installed&&command_probe_installed);
        final_ui_probe_installed = bovr::t5::install_final_ui_probe(GetModuleHandleW(nullptr));
        event_loop_probe_installed = bovr::t5::install_event_loop_probe(GetModuleHandleW(nullptr));
        ui_producer_probe_installed = bovr::t5::install_ui_producer_probe(GetModuleHandleW(nullptr));
        bullet_probe_installed = bovr::t5::install_bullet_probe(GetModuleHandleW(nullptr));
        hud_pass_probe_installed = bovr::t5::install_hud_pass_probe(GetModuleHandleW(nullptr));
        frame_begin_probe_installed = bovr::t5::install_frame_begin_probe(GetModuleHandleW(nullptr));
        bovr::trace("menu_frame_observer_ready","installed=%u additional_instruction_patches=%u policy_configuration_follows=1",frame_begin_probe_installed,frame_begin_probe_installed?1u:0u);
        adapter_configured = bovr::t5::configure_vr_scene_adapter(GetModuleHandleW(nullptr),scene_probe_installed,backend_probe_installed,final_ui_probe_installed,ui_producer_probe_installed,hud_pass_probe_installed,frame_begin_probe_installed,event_loop_probe_installed);
        input_configured = adapter_configured &&
            bovr::t5::configure_input_adapter(GetModuleHandleW(nullptr),command_probe_installed,weapon_probe_installed);
        bovr::t5::configure_manual_reload(GetModuleHandleW(nullptr),input_configured);
        bovr::t5::configure_helicopter_aim_adapter(GetModuleHandleW(nullptr),input_configured);
        bovr::t5::configure_native_use_gaze_adapter(GetModuleHandleW(nullptr),input_configured);
        bovr::t5::configure_infrared_scope_adapter(GetModuleHandleW(nullptr),input_configured);
        bovr::t5::configure_mounted_gun_runtime(GetModuleHandleW(nullptr),input_configured);
        const bool motorcycle_shot=input_configured&&bovr::t5::install_motorcycle_script_shot(GetModuleHandleW(nullptr));
        bovr::trace("motorcycle_script_aim_ready","installed=%u input_configured=%u",motorcycle_shot,input_configured);
        command_rig_configured = adapter_configured &&
            bovr::t5::configure_command_rig_adapter(GetModuleHandleW(nullptr),input_configured,consumed_command_probe_installed);
        menu_input_configured=adapter_configured && final_ui_probe_installed && bovr::t5::menu_input_epoch_ready() &&
            bovr::t5::configure_menu_input_adapter(GetModuleHandleW(nullptr),event_loop_probe_installed);
        if(adapter_configured&&frame_begin_probe_installed&&event_loop_probe_installed)
            bovr::t5::set_event_loop_callback(&bovr::t5::service_menu_frame_after_events);
        else if(menu_input_configured) bovr::t5::set_event_loop_callback(&bovr::t5::service_menu_input_after_events);
        weapon_placement_configured=adapter_configured &&
            bovr::t5::configure_weapon_placement_adapter(GetModuleHandleW(nullptr),weapon_probe_installed);
        shot_haptics_configured=adapter_configured &&
            bovr::t5::configure_shot_haptic_adapter(bullet_probe_installed,command_probe_installed);
        if(adapter_configured&&bovr::t5::configure_predicted_shot_feedback(command_probe_installed))shot_haptics_configured=true;
        bovr::trace("weapon_lifecycle_ready","installed=%u additional_instruction_patches=%u",weapon_lifecycle_installed,weapon_lifecycle_installed?6u:0u);
        bovr::trace("shot_haptic_seams_ready","bullet_probe=%u haptic_adapter=%u additional_instruction_patches=%u runtime_started=0",
                    bullet_probe_installed,shot_haptics_configured,bullet_probe_installed?1u:0u);
        bovr::trace("hud_composition_seams_ready","installed=%u additional_instruction_patches=%u runtime_started=0",
                    hud_pass_probe_installed,hud_pass_probe_installed?2u:0u);
        bovr::trace("command_rig_seams_ready","consumed_probe=%u passive_adapter=%u additional_instruction_patches=%u shot_writes=0",
                    consumed_command_probe_installed,command_rig_configured,consumed_command_probe_installed?1u:0u);
    }
    const bool quit_gate=bovr::t5::quit_gate_installed();
    bovr::trace("factory_seams_ready", "engine_instruction_patches=%u scene_probe=%u backend_probe=%u command_probe=%u weapon_probe=%u final_ui=%u event_loop=%u ui_producer=%u quit_gate=%u vr_adapter_configured=%u input_adapter_configured=%u menu_input_configured=%u weapon_placement_configured=%u runtime_started=0",
        static_cast<unsigned>(scene_probe_installed) + static_cast<unsigned>(backend_probe_installed) + static_cast<unsigned>(command_probe_installed) + static_cast<unsigned>(consumed_command_probe_installed) + static_cast<unsigned>(weapon_probe_installed) + static_cast<unsigned>(final_ui_probe_installed) + static_cast<unsigned>(event_loop_probe_installed) + static_cast<unsigned>(ui_producer_probe_installed) + static_cast<unsigned>(quit_gate) + (weapon_lifecycle_installed?6u:0u) + static_cast<unsigned>(bullet_probe_installed) + (hud_pass_probe_installed?2u:0u) + static_cast<unsigned>(frame_begin_probe_installed),
        scene_probe_installed, backend_probe_installed, command_probe_installed, weapon_probe_installed, final_ui_probe_installed,event_loop_probe_installed,ui_producer_probe_installed,quit_gate,adapter_configured,input_configured,menu_input_configured,weapon_placement_configured);
    return TRUE;
}
void ensure() noexcept {
    if (!InitOnceExecuteOnce(&initialized, initialize, nullptr, nullptr)) {
        // Equivalent to an unavailable system dependency: never fabricate a valid
        // D3D object or silently use this proxy as its own system implementation.
        RaiseFailFastException(nullptr, nullptr, 0);
    }
}
}
extern "C" void __stdcall BovrResolveExport(unsigned ordinal) noexcept {
    const DWORD saved_error = GetLastError();
    ensure();
    if (ordinal < 16 || ordinal > 38 || !bovr_exports[ordinal - 16]) {
        bovr::trace("missing_system_export", "ordinal=%u", ordinal);
        RaiseFailFastException(nullptr, nullptr, 0);
    }
    SetLastError(saved_error);
}

// Generic ABI-preserving tail forwarding for documented and ordinal-only exports.
// Save GP registers, EFLAGS and aligned x87/SSE state around lazy resolution. Once
// restored, the system function sees the caller's exact stack and register state.
#define FORWARD_ORDINAL(N, BYTE_OFFSET) \
extern "C" __declspec(naked) void ProxyOrdinal##N() { \
    __asm { pushfd } \
    __asm { pushad } \
    __asm { mov ebp, esp } \
    __asm { sub esp, 528 } \
    __asm { and esp, 0xfffffff0 } \
    __asm { fxsave [esp] } \
    __asm { push N } \
    __asm { call BovrResolveExport } \
    __asm { fxrstor [esp] } \
    __asm { mov esp, ebp } \
    __asm { popad } \
    __asm { popfd } \
    __asm { jmp dword ptr [bovr_exports + BYTE_OFFSET] } \
}
FORWARD_ORDINAL(16, 0)
FORWARD_ORDINAL(17, 4)
FORWARD_ORDINAL(18, 8)
FORWARD_ORDINAL(19, 12)
FORWARD_ORDINAL(20, 16)
FORWARD_ORDINAL(21, 20)
FORWARD_ORDINAL(22, 24)
FORWARD_ORDINAL(23, 28)
FORWARD_ORDINAL(24, 32)
FORWARD_ORDINAL(25, 36)
FORWARD_ORDINAL(26, 40)
FORWARD_ORDINAL(27, 44)
FORWARD_ORDINAL(28, 48)
FORWARD_ORDINAL(29, 52)
FORWARD_ORDINAL(30, 56)
FORWARD_ORDINAL(31, 60)
FORWARD_ORDINAL(32, 64)
FORWARD_ORDINAL(33, 68)
FORWARD_ORDINAL(34, 72)
FORWARD_ORDINAL(35, 76)
FORWARD_ORDINAL(36, 80)

extern "C" IDirect3D9* WINAPI BovrDirect3DCreate9(UINT sdk_version) noexcept {
    BovrResolveExport(37);
    InitOnceExecuteOnce(&factory_seams_initialized, initialize_factory_seams, nullptr, nullptr);
    if (promote_ex && bovr_exports[22]) {
        const auto ex_fn = reinterpret_cast<HRESULT(WINAPI*)(UINT, IDirect3D9Ex**)>(bovr_exports[22]);
        IDirect3D9Ex* extended{};
        const HRESULT hr = ex_fn(sdk_version, &extended);
        if (SUCCEEDED(hr) && extended) {
            if (bovr::observe_factory(extended, true, true)) {
                bovr::trace("factory_promoted_ex", "sdk=%u factory=%p hr=0x%08lx", sdk_version, extended, static_cast<unsigned long>(hr));
                return extended;
            }
            extended->Release();
        }
        bovr::trace("factory_ex_fallback", "hr=0x%08lx reason=creation_or_required_hooks_failed stock=1", static_cast<unsigned long>(hr));
    }
    const auto fn = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(bovr_exports[21]);
    IDirect3D9* const factory = fn(sdk_version);
    bovr::trace("direct3d_create9", "sdk=%u factory=%p observation=%u", sdk_version, factory, instrument);
    if (instrument && factory) bovr::observe_factory(factory, false);
    return factory;
}
extern "C" HRESULT WINAPI BovrDirect3DCreate9Ex(UINT sdk_version, IDirect3D9Ex** output) noexcept {
    BovrResolveExport(38);
    InitOnceExecuteOnce(&factory_seams_initialized, initialize_factory_seams, nullptr, nullptr);
    const auto fn = reinterpret_cast<HRESULT(WINAPI*)(UINT, IDirect3D9Ex**)>(bovr_exports[22]);
    const HRESULT hr = fn(sdk_version, output);
    bovr::trace("direct3d_create9_ex", "sdk=%u hr=0x%08lx observation=%u", sdk_version, static_cast<unsigned long>(hr), instrument);
    if (instrument && SUCCEEDED(hr) && output && *output) bovr::observe_factory(*output, true);
    return hr;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        proxy_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    // No loader-lock I/O, hashing, graphics calls, worker creation, patching or waits.
    return TRUE;
}
