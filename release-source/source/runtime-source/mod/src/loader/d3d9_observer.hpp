// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
#include <cstdint>
namespace bovr {
struct DeviceCreationInfo {
    std::uint64_t serial{};
    DWORD thread_id{};
    bool extended{};
};
// Only successful observed factory calls mint this identity. Merely observing
// an existing device does not prove its creation thread or creation order.
// Copies metadata under the observer lock; never calls COM while holding it.
bool read_device_creation(IDirect3DDevice9*, DeviceCreationInfo*) noexcept;
struct GraphicsCallbacks {
    void* context{};
    // Borrowed device valid only for this call. Only implicit swap-chain 0 invokes
    // this seam. No automatic XR initialization or frame completion is implied.
    void (*before_present)(void*, IDirect3DDevice9*){};
    // False/exception defers the real Reset and returns D3DERR_DEVICELOST. The
    // callback must enforce its runtime owner thread and retire GPU ownership.
    bool (*before_reset)(void*, IDirect3DDevice9*){};
    // Called only for an admitted real Reset, with its exact HRESULT. A failed
    // result permits no further device API calls except documented recovery APIs.
    void (*after_reset)(void*, IDirect3DDevice9*, HRESULT){};
    // Post-Release(0) notification: dead identity ONLY, never dereference it. This
    // may run on any release thread. Retire XR before dropping the owner's ref.
    void (*device_released)(void*, std::uintptr_t){};
};
// Requires active observation for nonempty registration. No callbacks by default.
// Replacement/unbind returns false while any snapshot is in flight; caller owns
// context lifetime until a successful unbind. No lock spans a user callback.
bool set_graphics_callbacks(const GraphicsCallbacks&) noexcept;
struct IndexedDrawCall {
    std::uintptr_t caller{};
    D3DPRIMITIVETYPE type{};
    INT base_vertex{};
    UINT min_vertex{},num_vertices{},start_index{},primitive_count{};
};
struct IndexedDrawCallbacks {
    void* context{};
    // Owned diagnostic code must contain its own C++ failures. The observer
    // contains callback SEH, but never catches an exception from native Draw.
    std::uint64_t (*before)(void*,IDirect3DDevice9*,const IndexedDrawCall&) noexcept{};
    void (*after)(void*,std::uint64_t,HRESULT) noexcept{};
    // Native unwind: retire scalar ownership only. No COM or resource release.
    void (*abandon)(void*,std::uint64_t) noexcept{};
};
// Separate startup registration: active + complete tuple enables device slot82
// installation on subsequent observe_device calls. Default adds no Draw hook.
// A copied tuple/context lease spans original Draw and after/abandon. Replacement
// and unbind fail while any lease is in flight. Token0 skips after/abandon.
// Nested draws and untracked shared-vtable devices forward without callbacks.
bool set_indexed_draw_callbacks(const IndexedDrawCallbacks&) noexcept;
// Monotonic witness for this thread only. A resolved slot82 call suppressed by
// an existing indexed callback lease increments it, including nested calls on
// untracked shared-vtable devices or after observation is deactivated. No reset.
// UINT64_MAX is a permanently unusable saturation value: callers must reject
// either snapshot equal to it, even when before/after compare equal. Compare
// snapshots only on the same thread around each owned outbound call.
// Detection does not prevent a nested native draw from seeing temporary state.
inline constexpr std::uint64_t indexed_draw_reentry_exhausted=UINT64_MAX;
std::uint64_t indexed_draw_reentry_serial() noexcept;
// Documented COM interfaces only; no engine address or proprietary data write.
bool observe_factory(IDirect3D9* factory, bool extended, bool promote = false) noexcept;
bool observe_device(IDirect3DDevice9* device, bool extended, bool migrate = false) noexcept;
bool observe_swapchain(IDirect3DSwapChain9* swapchain) noexcept;
void set_observation_active(bool enabled) noexcept;
// Startup-only, after accepted identity enables observation. Adds passive
// TestCooperativeLevel/Reset diagnostics, never a status translation or retry.
void set_recovery_diagnostics(bool enabled) noexcept;
bool recovery_diagnostics_active() noexcept;
}
