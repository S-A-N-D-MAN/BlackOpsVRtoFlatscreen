// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_values.hpp"
#include "t5/bloom_episode.hpp"
#include "t5/auxiliary_postfx.hpp"
#include <d3d9.h>
#include <memory>
#include <string>
namespace bovr::t5 {
struct BloomShaderSource {
    void* context{};
    bool(*current)(void*) noexcept{};
    bool(*read)(void*,BloomDrawSnapshot*,BloomReadStatus*) noexcept{};
    std::uint64_t creation_serial{};DWORD creation_thread{};bool creation_extended{};
    bool(*read_episode)(void*,const BloomEpisodeCursor&,BloomDrawSnapshot*,BloomEpisodePlan*,BloomReadStatus*) noexcept{};
    std::uintptr_t native_draw_return{};
    // Optional active read-only query guard. It checks owned entry identity,
    // not native memory. A null callback retains complete source-read checks.
    bool(*query_guard)(void*,const BloomEpisodeCursor&,const BloomDrawSnapshot&,const BloomDrawCall&) noexcept{};
    unsigned initial_eye{1}; // Ordinary native order0,1; auxiliary order1,0.
    // Optional callback-free owned-identity guard. The active replay invokes
    // it only inside current/guard/current at every existing query boundary.
    // Unlike query_guard it must not invoke an owner callback internally.
    bool(*query_guard_owned)(void*,const BloomEpisodeCursor&,const BloomDrawSnapshot&,const BloomDrawCall&) noexcept{};
};
// State belongs to the retained renderer owner. The adapter keeps its device
// and this object alive through callbacks and defers reset/stop/replacement.
// Normal owner retirement is required; never destroy this object on native unwind.
class BloomShaderProbe {
public:
    BloomShaderProbe();~BloomShaderProbe();
    // Existing absolute case directory; no environment access while drawing.
    void configure(bool,const std::wstring&,BloomCaptureContract=BloomCaptureContract::selected_four) noexcept;
    void begin(const AuxiliaryPostfxKey&,std::uint64_t epoch,IDirect3DDevice9*,BloomShaderSource) noexcept;
    std::uint64_t before(IDirect3DDevice9*,const BloomDrawCall& ={}) noexcept;
    void after(std::uint64_t,HRESULT) noexcept;
    void abandon(std::uint64_t=0) noexcept; // Mod state only, never COM/file work.
    void finish(bool ordinary_staged) noexcept;
    void retire() noexcept;
    std::uint64_t bytes_used() const noexcept;
    unsigned attempts() const noexcept;
    bool configured() const noexcept;
private:struct State;std::unique_ptr<State> state_;
};
}
