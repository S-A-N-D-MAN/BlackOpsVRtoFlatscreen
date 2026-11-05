// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>

namespace bovr::render {
// Serialized render-owner calls only. Holds only its own default-pool render
// target; never binds a target or changes viewport, shader, or scene state.
// Release on the owner before Reset / device release, never from DllMain.
class EyeStaging final {
public:
    EyeStaging() = default;
    ~EyeStaging() { release(); }
    EyeStaging(const EyeStaging&) = delete;
    EyeStaging& operator=(const EyeStaging&) = delete;
    HRESULT begin(IDirect3DDevice9* device, std::uint64_t producer,
                  const std::array<RECT, 2>& rectangles,
                  const std::array<unsigned,2>& copy_order={0,1}) noexcept;
    HRESULT save_eye(IDirect3DDevice9* device, std::uint64_t producer, unsigned eye) noexcept;
    HRESULT restore_pair(IDirect3DDevice9* device, std::uint64_t producer) noexcept;
    // Read-only diagnostic export into a caller-owned surface; keeps the pair live.
    HRESULT copy_completed_pair(IDirect3DDevice9*,std::uint64_t,IDirect3DSurface9*) const noexcept;
    void invalidate() noexcept;
    void release() noexcept;
    unsigned completed_eye_mask() const noexcept { return mask_; }
    const D3DSURFACE_DESC& last_source_description() const noexcept { return last_source_; }
private:
    HRESULT backbuffer(IDirect3DDevice9* device, IDirect3DSurface9** output,
                       D3DSURFACE_DESC* description) noexcept;
    bool matches(IDirect3DDevice9* device, std::uint64_t producer) const noexcept;
    IDirect3DSurface9* packed_{};
    IDirect3DDevice9* device_identity_{}; // borrowed; packed_ owns its D3D reference
    D3DSURFACE_DESC description_{};
    D3DSURFACE_DESC last_source_{};
    std::array<RECT, 2> rectangles_{};
    DWORD owner_{};
    std::uint64_t producer_{};
    unsigned mask_{};
    std::array<unsigned,2> copy_order_{0,1};
};
}
