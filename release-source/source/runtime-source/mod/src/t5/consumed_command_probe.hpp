// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

namespace bovr::t5 {
enum ConsumedCommandMismatch : std::uint32_t {
    consumed_wrong_return=1u<<0, consumed_other_client=1u<<1,
    consumed_server_identity=1u<<2, consumed_server_inactive=1u<<3,
    consumed_entity_identity=1u<<4, consumed_client_identity=1u<<5,
    consumed_command_time=1u<<6, consumed_source_changed=1u<<7,
    consumed_unreadable=1u<<8, consumed_nested=1u<<9,
    consumed_callback_failed=1u<<10, consumed_native_unwind=1u<<11
};
struct NativeConsumedCommand {
    std::uintptr_t image_base{},server_client_identity{},entity_identity{},game_client_identity{};
    std::uintptr_t source_command_identity{};
    std::uint64_t scope_serial{},observed_ms{};
    DWORD native_thread{};
    int local_client_index{},server_state{},command_time{},server_time{},level_time{},last_server_time{};
    std::array<std::uint8_t,52> command{};
    std::uint32_t mismatches{};
    bool readable{},local_identity{},active{},native_returned{},nesting_invalidated{};
};
struct ConsumedCommandCallbacks {
    void* context{};
    void(*before_native)(void*,const NativeConsumedCommand&){};
    void(*after_native)(void*,const NativeConsumedCommand&){};
};
// Explicit BOVR_CONSUMED_COMMAND_PROBE=1, pinned SP CALL513E58 only. Passive:
// no command, shot, runtime, authentication or application argument writes.
bool install_consumed_command_probe(HMODULE executable) noexcept;
// Runtime coordinator: prepare validates/copies pins before peer suspension;
// commit performs only fixed comparisons and the existing CALL replacement.
bool prepare_consumed_command_probe(HMODULE executable) noexcept;
bool commit_consumed_command_probe() noexcept;
// Registration cannot replace a callback context while an outer scope owns it.
bool set_consumed_command_callbacks(const ConsumedCommandCallbacks&) noexcept;
// Copies values only during this thread's exact native ClientThink execution.
// Hidden during callbacks; nesting poisons the whole outer scope. Native unwind
// retires the scope in __finally without handling/changing the native exception.
// active is native local command identity, not permission to alter gameplay.
bool read_consumed_command_scope(NativeConsumedCommand*) noexcept;
}
