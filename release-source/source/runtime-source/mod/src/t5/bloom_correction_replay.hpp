// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_probe.hpp"
#include "t5/bloom_quad_native.hpp"
#include <memory>
namespace bovr::t5 {
struct BloomQuadSource {
    void* context{};
    bool (*read)(void*,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot*) noexcept{};
};
// Owner-held, default-off state. before() only reads. after(D3D_OK) performs a
// separate, contained UP replay, then restores every attempted field normally.
// No resource destructor or COM method is invoked by native unwind abandon().
class BloomCorrectionReplay {
public:
    BloomCorrectionReplay();~BloomCorrectionReplay();
    void configure(bool) noexcept;
    bool configured() const noexcept;
    // True only after this invocation installs the current owned source scope.
    bool begin(const AuxiliaryPostfxKey&,std::uint64_t successful_reset_serial,
               IDirect3DDevice9*,BloomShaderSource,BloomQuadSource) noexcept;
    std::uint64_t before(IDirect3DDevice9*,const BloomDrawCall&) noexcept;
    void after(std::uint64_t,HRESULT) noexcept;
    void abandon(std::uint64_t=0) noexcept;
    bool finish(bool ordinary_staged) noexcept;
    void end_scope() noexcept; // Every normal backend exit; drops borrowed callbacks.
    // Normal owner callbacks only. Refuse release while a journal is pending.
    bool retire() noexcept;
    // Completed ResetEx is a normal boundary, not proof that state disappeared.
    // usable=false must not query a lost device. Never used during native unwind.
    bool reconcile_normal(IDirect3DDevice9*,bool usable) noexcept;
    bool busy() const noexcept;
    bool pending() const noexcept;
    bool rejected() const noexcept;
    unsigned replayed() const noexcept;
private:struct State;std::unique_ptr<State> state_;
};
}
