// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/backend_probe.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t draw_rva = 0x2eb760;
constexpr std::uintptr_t backend_pointer_rva = 0x3a5b5f8;
#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr native_guard::Span entry_guard{draw_rva,6,"e4f521579f039f772ce89c42922dc54152fcb8030b95440c11b2d08eb8866e51"};
#else
constexpr native_guard::Span entry_guard{draw_rva,6,"b29b43a4673ecadd61abbf15be09fc1b9003985bd3ab08f9426fb396fc09d556"};
#endif
using Draw3D = void(__cdecl*)();
Draw3D original_draw{};
std::uintptr_t image_base{};
std::atomic<unsigned long> calls{};
struct BackendSnapshot {
    void* backend{};
    void* views{};
    std::uint32_t frame_index{};
    std::uint32_t count{};
    std::uint32_t index{};
    bool valid{};
};
BackendSnapshot read_backend() noexcept {
    BackendSnapshot s{};
    __try {
        s.backend = *reinterpret_cast<void**>(image_base + backend_pointer_rva);
        if (!s.backend) return s;
        const auto* p = static_cast<const unsigned char*>(s.backend);
        memcpy(&s.frame_index, p + 0x15d7a0, 4);
        memcpy(&s.index, p + 0x16cc38, 4);
        memcpy(&s.count, p + 0x16cc3c, 4);
        memcpy(&s.views, p + 0x16cc40, sizeof(void*));
        s.valid = s.count <= 4 && (!s.count || (s.index < s.count && s.views));
    } __except (EXCEPTION_EXECUTE_HANDLER) { s.valid = false; }
    return s;
}
void __cdecl draw_probe() {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto serial = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool sample = serial <= 12 || serial % 120 == 0;
    BackendSnapshot before{};
    if (sample) before = read_backend();
    const bool stereo=render_stereo_backend(original_draw);
    if (!stereo) original_draw();
    if (!sample) return;
    const auto after = read_backend();
    trace("t5_backend", "call=%lu thread=%lu caller_rva=0x%lx valid=%u buffer=%p frame_index=%lu view_count=%lu view_index=%lu views=%p buffer_after=%p frame_index_after=%lu camera_writes=0",
          serial, GetCurrentThreadId(), static_cast<unsigned long>(caller-image_base), before.valid && after.valid,
          before.backend, before.frame_index, before.count, before.index, before.views,
          after.backend, after.frame_index);
    if (stereo) trace("t5_backend_stereo_dispatch","call=%lu native_frame=%lu",serial,before.frame_index);
}
void jump(unsigned char* bytes, const void* destination) noexcept {
    bytes[0] = 0xe9;
    const auto displacement = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination) -
                                                        (reinterpret_cast<std::uintptr_t>(bytes)+5));
    memcpy(bytes+1, &displacement, 4);
}
}
bool install_backend_probe(HMODULE executable) noexcept {
    wchar_t enabled[8]{};
    GetEnvironmentVariableW(L"BOVR_BACKEND_PROBE", enabled, 8);
    if (wcscmp(enabled,L"1") != 0) return false;
    if (original_draw) return true;
    image_base = reinterpret_cast<std::uintptr_t>(executable);
    auto* entry = reinterpret_cast<unsigned char*>(image_base + draw_rva);
    // Exactly push ebp; mov ebp,esp; and esp,-8. No PC-relative operands.
    if (!native_guard::matches(image_base,entry_guard)) {
        trace("t5_backend_probe_rejected", "reason=loaded_entry_bytes_mismatch"); return false;
    }
    std::array<unsigned char,6> original{};
    memcpy(original.data(),entry,original.size());
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!trampoline) return false;
    memcpy(trampoline,original.data(),original.size());
    jump(trampoline+6,entry+6);
    DWORD old{};
    if (!VirtualProtect(trampoline,11,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(),trampoline,11);
    DWORD protection{};
    if (!VirtualProtect(entry,6,PAGE_EXECUTE_READWRITE,&protection)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    if (!native_guard::matches(image_base,entry_guard)) {
        VirtualProtect(entry,6,protection,&old);
        VirtualFree(trampoline,0,MEM_RELEASE);
        trace("t5_backend_probe_rejected", "reason=entry_changed_before_write"); return false;
    }
    original_draw = reinterpret_cast<Draw3D>(trampoline);
    jump(entry,reinterpret_cast<void*>(&draw_probe)); entry[5]=0x90;
    const BOOL restored = VirtualProtect(entry,6,protection,&old);
    FlushInstructionCache(GetCurrentProcess(),entry,6);
    trace("t5_backend_probe_installed", "entry=%p trampoline=%p bytes=6 protection_restored=%u camera_writes=0", entry,trampoline,restored);
    return true;
}
}
