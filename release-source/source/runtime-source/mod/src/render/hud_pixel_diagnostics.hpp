// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <span>
#include <string>
namespace bovr::render {
struct HudPixelIdentity {
    std::uint64_t generation{},xr_frame{},producer{};
    std::uintptr_t backend{};
    std::uint32_t native_frame{};
};
// Optional synchronous readback only; no COM resources/native pointers survive
// a method. Serialized XR-owner use, max three producers spaced at least1.5s.
// Requires HUD_PIXEL_PROBE=1 and a preexisting absolute output directory.
class HudPixelDiagnostics final {
public:
    bool begin(IDirect3DDevice9*,const HudPixelIdentity&,std::span<const std::uint8_t> source,
               std::span<const std::uint8_t> left,std::span<const std::uint8_t> right) noexcept;
    void capture(IDirect3DDevice9*,std::uint64_t producer,const char* stage,bool final=false) noexcept;
    void abandon(std::uint64_t producer,const char* reason) noexcept;
private:
    bool configured_{},enabled_{},active_{};
    unsigned captured_{};
    DWORD owner_{};
    std::uint64_t last_begin_{};
    HudPixelIdentity identity_{};
    std::wstring directory_,prefix_;
};
}
