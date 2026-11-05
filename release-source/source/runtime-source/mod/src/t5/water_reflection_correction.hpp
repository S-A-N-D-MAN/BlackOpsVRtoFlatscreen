// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "loader/d3d9_observer.hpp"
#include <memory>
namespace bovr::t5 {
// Exact recognized water shaders only; current selected-eye ownership is
// supplied by the VPOS draw scope. No independent game calls or extra draws.
class WaterReflectionCorrection {
public:
    WaterReflectionCorrection() noexcept;
    ~WaterReflectionCorrection();
    void configure(std::uintptr_t base) noexcept;
    std::uint64_t before(IDirect3DDevice9*,const IndexedDrawCall&) noexcept;
    void after(std::uint64_t,HRESULT) noexcept;
    void abandon(std::uint64_t=0) noexcept;
    bool reconcile_normal(IDirect3DDevice9*,bool device_usable) noexcept;
    bool before_reset(IDirect3DDevice9*) noexcept;
    void after_reset(IDirect3DDevice9*,HRESULT) noexcept;
    bool retire_terminal(IDirect3DDevice9*) noexcept;
    bool retire() noexcept;
    bool pending() const noexcept;
    bool busy() const noexcept;
    bool rejected() const noexcept;
private:struct State;std::unique_ptr<State> state_;
};
}
