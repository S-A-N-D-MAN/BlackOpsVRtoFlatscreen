// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/quit_gate.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <array>
#include <cstdint>
#include <cstring>
namespace bovr::t5 {
bool QuitRetirementGate::may_call_native(QuitRetirementCallback callback, void* context) noexcept {
    auto state = state_.load();
    for (;;) {
        if (state == State::retired) return true;
        if (state == State::requesting || state == State::waiting) return false;
        if (!callback) return true;
        if (state_.compare_exchange_weak(state, State::requesting)) break;
    }
    bool ready{};
    try { ready = callback(context); }
    catch (...) { ready = false; }
    state_.store(ready ? State::idle : State::waiting);
    return ready;
}
bool QuitRetirementGate::acknowledge_retired() noexcept {
    auto expected = State::waiting;
    return state_.compare_exchange_strong(expected, State::retired);
}
bool QuitRetirementGate::retirement_requested() const noexcept {
    const auto state = state_.load();
    return state == State::requesting || state == State::waiting;
}
namespace {
constexpr std::uintptr_t quit_rva = 0x1df2b0, registration_rva = 0x42d042, cbuf_rva = 0x9b930;
constexpr native_guard::Span quit_guard{quit_rva,5,"b39d74e1e1a986c1b99788ccbc690a3e50f71afe368f691fa8af3441e7034a05"};


using NativeQuit = void(__cdecl*)();
using CbufAddText = void(__cdecl*)(int, const char*);
NativeQuit original_quit{};
CbufAddText cbuf_add_text{};
std::atomic<bool> installed{};
QuitRetirementGate gate;
SRWLOCK callback_lock = SRWLOCK_INIT;
QuitRetirementCallback retirement_callback{};
void* retirement_context{};
std::atomic<unsigned> in_flight{};
void __cdecl quit_hook() noexcept {
    AcquireSRWLockShared(&callback_lock);
    const auto callback = retirement_callback;
    void* const context = retirement_context;
    ++in_flight;
    ReleaseSRWLockShared(&callback_lock);
    const bool forward = gate.may_call_native(callback, context);
    --in_flight;
    trace("quit_gate", "native_forwarded=%u retirement_requested=%u thread=%lu", forward, gate.retirement_requested(), GetCurrentThreadId());
    if (forward) original_quit(); // Exact cdecl void() ABI, once per admitted call.
}
void jump(unsigned char* from, const void* target) noexcept {
    from[0] = 0xe9;
    const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) -
                                                (reinterpret_cast<std::uintptr_t>(from) + 5));
    memcpy(from + 1, &delta, 4);
}
bool enabled(const wchar_t* name) noexcept {
    wchar_t value[8]{}; GetEnvironmentVariableW(name, value, 8); return wcscmp(value,L"1") == 0;
}
}
bool install_quit_gate(HMODULE executable) noexcept {
    if (!enabled(L"BOVR_QUIT_GATE") || !enabled(L"BOVR_INSTRUMENT")) return false;
    if (installed.load()) return true;
    if (executable != GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(executable) != 0x400000) return false;
    try {
        const auto identity = validate_path(executable_path());
        std::string reason;
        if (!identity.accepted || !validate_loaded_headers(executable, reason)) return false;
    } catch (...) { return false; }
    auto* image = reinterpret_cast<unsigned char*>(executable);
    auto* entry = image + quit_rva;
    if (!native_guard::matches(reinterpret_cast<std::uintptr_t>(image),quit_guard) ||
        bovr::native_guard::compare(image + registration_rva, 20, "e26012dcecbfe732c9ebbdccc1119dbe3a2219cd3b59820e13542025947db424") ||
        bovr::native_guard::compare(image + cbuf_rva, 7, "42d075922a1c12b266a456ef31a2366bae1b1f701205444f1725074df51e8684") ||
        memcmp(image + 0x5a9c58, "quit", 5)) {
        trace("quit_gate_rejected", "reason=loaded_mapping_bytes_mismatch"); return false;
    }
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&install_quit_gate), &pinned)) return false;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!trampoline) return false;
    std::array<unsigned char,5> original{};
    memcpy(original.data(),entry,original.size());
    // One absolute PUSH immediate; no PC-relative operand requires relocation.
    memcpy(trampoline,original.data(),original.size()); jump(trampoline+5,entry+5);
    DWORD old{};
    if (!VirtualProtect(trampoline,10,PAGE_EXECUTE_READ,&old)) { VirtualFree(trampoline,0,MEM_RELEASE); return false; }
    FlushInstructionCache(GetCurrentProcess(),trampoline,10);
    DWORD protection{};
    if (!VirtualProtect(entry,5,PAGE_EXECUTE_READWRITE,&protection)) { VirtualFree(trampoline,0,MEM_RELEASE); return false; }
    if (!native_guard::matches(reinterpret_cast<std::uintptr_t>(image),quit_guard)) {
        VirtualProtect(entry,5,protection,&old); VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    original_quit = reinterpret_cast<NativeQuit>(trampoline);
    cbuf_add_text = reinterpret_cast<CbufAddText>(image + cbuf_rva);
    jump(entry,reinterpret_cast<void*>(&quit_hook));
    const BOOL restored = VirtualProtect(entry,5,protection,&old);
    FlushInstructionCache(GetCurrentProcess(),entry,5);
    installed.store(true);
    trace("quit_gate_installed", "entry=%p trampoline=%p bytes=5 protection_restored=%u callback_default=none", entry,trampoline,restored);
    return true;
}
bool set_quit_retirement_callback(QuitRetirementCallback callback, void* context) noexcept {
    if (callback && !installed.load()) return false;
    AcquireSRWLockExclusive(&callback_lock);
    const bool allowed = in_flight.load() == 0;
    if (allowed) { retirement_callback = callback; retirement_context = context; }
    ReleaseSRWLockExclusive(&callback_lock); return allowed;
}
bool quit_gate_installed() noexcept { return installed.load(); }
bool quit_retirement_requested() noexcept { return gate.retirement_requested(); }
bool acknowledge_quit_retired_and_queue() noexcept {
    if (!installed.load() || !cbuf_add_text || !gate.acknowledge_retired()) return false;
    cbuf_add_text(0,"quit\n");
    trace("quit_gate_retired", "native_quit_queue_invoked=1 thread=%lu", GetCurrentThreadId());
    return true;
}
}
