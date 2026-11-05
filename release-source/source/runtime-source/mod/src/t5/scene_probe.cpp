// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/scene_probe.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t scene_rva = 0x2c8cd0;
constexpr std::uintptr_t frontend_pointer_rva = 0x373708c;
constexpr native_guard::Span scene_guard{scene_rva,5,"2549e49f874bec22cfaf9fef81bcd2bc7f6ecdff72f543b4fbe38db592dc9fd3"};
using RenderScene = void(__cdecl*)(void*, int);
RenderScene original{};
std::uintptr_t image_base{};
std::atomic<unsigned long> calls{};
struct Snapshot {
    int viewport[4]{};
    float fov[2]{};
    float origin[3]{};
    float axis[9]{};
    int time{};
    int local_client{};
    void* frontend{};
    int view_count{-1};
    std::uint32_t native_frame{}, render_view_count{}, render_view_index{};
    void* render_views{};
    bool valid{};
};
// The retail SP field offsets below were independently disassembled from the
// pinned BlackOps.exe. LinkerMod's refdef declaration differs at origin/axis;
// copying that declaration would sample the wrong fields. No game data writes.
Snapshot snapshot(const void* refdef) noexcept {
    Snapshot s{};
    __try {
        const auto* p = static_cast<const unsigned char*>(refdef);
        memcpy(s.viewport, p, sizeof(s.viewport));
        memcpy(s.fov, p + 0x10, sizeof(s.fov));
        memcpy(s.origin, p + 0x20, sizeof(s.origin));
        memcpy(s.axis, p + 0x34, sizeof(s.axis));
        // Time is provisional until the runtime samples agree with game frames.
        memcpy(&s.time, p + 0x58, sizeof(s.time));
        memcpy(&s.local_client, p + 0x183a4, sizeof(s.local_client));
        s.frontend = *reinterpret_cast<void**>(image_base + frontend_pointer_rva);
        if (s.frontend) {
            memcpy(&s.view_count, static_cast<const unsigned char*>(s.frontend) + 0x16cbe0, sizeof(s.view_count));
            const auto* frame=static_cast<const unsigned char*>(s.frontend);
            memcpy(&s.native_frame,frame+0x15d7a0,4);
            memcpy(&s.render_view_count,frame+0x16cc3c,4);
            memcpy(&s.render_view_index,frame+0x16cc38,4);
            memcpy(&s.render_views,frame+0x16cc40,sizeof(void*));
        }
        s.valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.valid = false;
    }
    return s;
}
void __cdecl scene_probe(void* refdef, int frame_time) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const unsigned long serial = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool sampled = serial <= 12 || serial % 120 == 0;
    Snapshot before{};
    if (sampled) before = snapshot(refdef);
    const bool stereo=render_stereo_scene(refdef,frame_time,original);
    if (!stereo) original(refdef, frame_time);
    if (!sampled) return;
    const Snapshot after = snapshot(refdef);
    trace("t5_scene", "call=%lu thread=%lu caller_rva=0x%lx refdef=%p valid=%u viewport=%d,%d,%d,%d tanfov=%.5f,%.5f origin=%.4f,%.4f,%.4f axis=%.4f,%.4f,%.4f;%.4f,%.4f,%.4f;%.4f,%.4f,%.4f candidate_time=%d frame_time=%d local_client=%d frontend_before=%p frontend_after=%p views_before=%d views_after=%d",
          serial, GetCurrentThreadId(), static_cast<unsigned long>(caller - image_base), refdef,
          before.valid && after.valid, before.viewport[0], before.viewport[1], before.viewport[2], before.viewport[3],
          before.fov[0], before.fov[1], before.origin[0], before.origin[1], before.origin[2],
          before.axis[0], before.axis[1], before.axis[2], before.axis[3], before.axis[4], before.axis[5],
          before.axis[6], before.axis[7], before.axis[8], before.time, frame_time, before.local_client,
          before.frontend, after.frontend, before.view_count, after.view_count);
    trace("t5_scene_frame", "call=%lu native_frame_before=%lu native_frame_after=%lu render_count_before=%lu render_count_after=%lu render_index_before=%lu render_index_after=%lu views_before=%p views_after=%p stereo_dispatched=%u",
          serial,before.native_frame,after.native_frame,before.render_view_count,after.render_view_count,
          before.render_view_index,after.render_view_index,before.render_views,after.render_views,stereo);
}
void write_jump(unsigned char* output, const void* destination) noexcept {
    output[0] = 0xe9;
    const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination) -
                                                    (reinterpret_cast<std::uintptr_t>(output) + 5));
    memcpy(output + 1, &relative, 4);
}
}
bool install_scene_probe(HMODULE executable) noexcept {
    wchar_t enabled[8]{};
    GetEnvironmentVariableW(L"BOVR_SCENE_PROBE", enabled, 8);
    if (wcscmp(enabled, L"1") != 0) return false;
    if (original) return true;
    image_base = reinterpret_cast<std::uintptr_t>(executable);
    auto* entry = reinterpret_cast<unsigned char*>(image_base + scene_rva);
    // Five bytes are exactly one mov eax,0x14e8 instruction: no relative operand,
    // no split instruction, and the following stack-probe call stays in place.
    if (!native_guard::matches(image_base,scene_guard)) {
        trace("t5_scene_probe_rejected", "reason=loaded_entry_bytes_mismatch rva=0x%lx", scene_rva);
        return false;
    }
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 10, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline) return false;
    std::array<unsigned char,5> original_bytes{};
    memcpy(original_bytes.data(),entry,original_bytes.size());
    memcpy(trampoline, original_bytes.data(), original_bytes.size());
    write_jump(trampoline + 5, entry + 5);
    DWORD previous{};
    if (!VirtualProtect(trampoline, 10, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, 10);
    DWORD entry_protection{};
    if (!VirtualProtect(entry, 5, PAGE_EXECUTE_READWRITE, &entry_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    // Immediate pre-write check also rejects a hook installed since our first check.
    if (!native_guard::matches(image_base,scene_guard)) {
        VirtualProtect(entry, 5, entry_protection, &previous);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        trace("t5_scene_probe_rejected", "reason=entry_changed_before_write"); return false;
    }
    original = reinterpret_cast<RenderScene>(trampoline);
    write_jump(entry, reinterpret_cast<void*>(&scene_probe));
    const BOOL restored = VirtualProtect(entry, 5, entry_protection, &previous);
    FlushInstructionCache(GetCurrentProcess(), entry, 5);
    trace("t5_scene_probe_installed", "entry=%p trampoline=%p bytes=5 protection_restored=%u camera_writes=0", entry, trampoline, restored);
    return true;
}
}
