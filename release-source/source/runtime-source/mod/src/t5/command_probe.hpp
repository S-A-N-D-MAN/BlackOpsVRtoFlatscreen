// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
namespace bovr::t5 {
struct CommandContext {
    int local_client{}, command_number{}, key_catcher{}, connection_state{};
    int native_frame_time{}; // Native absolute key-event clock, not delta time.
    bool native_override{}; // FinishMove may consume/reset it before post hook.
    DWORD native_thread{};
    float viewangles[3]{}, kickangles[3]{};
    bool valid{};
};
struct CommandCallbacks {
    void* context{};
    void (*before_native)(void*,const CommandContext&){};
    // Borrowed native result, exactly0x34 bytes, valid only during this call.
    // The probe never writes it. Any future mutation belongs to the adapter.
    void (*after_native)(void*,const CommandContext&,unsigned char*){};
};
struct PreCommandSnapshot {
    CommandContext context{};
    std::uint64_t sequence{},observed_ms{};
    std::uint64_t gameplay_epoch{};
    std::uintptr_t native_cg_identity{};
    int native_game_time{},native_health{},native_pm_type{};
    std::uint32_t native_pm_flags{},native_weapon_flags{},native_entity_flags{},native_entity_flags2{};
    bool native_gameplay_fields_valid{},gameplay_active{},linked_weapon_active{};
};
// Weapon-only eligibility. A linked scripted firing window must not enable
// locomotion, weapon cycling or manual reload through gameplay_active.
inline bool primary_weapon_active(const PreCommandSnapshot& s) noexcept {
    return s.gameplay_active||s.linked_weapon_active;
}
// Copies the latest actual pre-native command context; retains consumed script
// override information without replacing input callbacks or reading post state.
bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept;
// Callbacks are empty by default; nonempty registration requires installed hook.
// Replacement/unbind fails during a callback;
// context must survive until successful unbind. No lock spans a callback.
// Before/after take independent snapshots; registration should occur at startup.
bool set_command_callbacks(const CommandCallbacks&) noexcept;
// Requires accepted pinned file identity and loaded PE headers. Call only from
// the initial graphics-factory seam, before native command production begins.
// Opt-in BOVR_COMMAND_PROBE=1. Observes the original result without game writes.
bool install_command_probe(HMODULE executable) noexcept;
}
