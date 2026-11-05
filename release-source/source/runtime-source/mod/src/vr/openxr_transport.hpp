// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "frame_coordinator.hpp"
#include "d3d9ex_bridge.h"
#include "d3d11_compositor.h"

namespace bovr::vr {
using QuadReticleProvider=bool (*)(void*,xr::CompositorReticle*) noexcept;
namespace openxr_transport_detail {
bool select_quad_reticle(const xr::QuadLayer*,QuadReticleProvider,void*,
                         xr::CompositorReticle*) noexcept;
}
// Concrete address-free OpenXR/D3D9Ex transport. The device passed to submit is
// borrowed for that call; its creation, Ex promotion and ResetEx interception
// remain the BO adapter's responsibility.
class OpenXrTransport final : public FrameTransport {
public:
    // Optional diagnostics/test validator. Invoked on the owner thread while
    // the exact capture is borrowed, before either eye is composited. It must
    // not retain resources or call the coordinator recursively. Install only
    // before initialization; false rejects the capture with a zero-layer end.
    using CaptureValidator=bool (*)(void*, ID3D11Device*, ID3D11DeviceContext*,
                                   const xr::FrameState&, const xr::D3D11SourceFrame&);
    bool set_capture_validator(CaptureValidator validator,void* user) noexcept {
        if (initialized_) return false;
        capture_validator_=validator; capture_validator_user_=user; return true;
    }
    bool initialize(const xr::RuntimeConfig&, const xr::HostCallbacks&) override;
    BeginStatus begin(xr::FrameState*) override;
    TransportResult discard(const xr::FrameState&) override;
    TransportResult submit(IDirect3DDevice9*, const xr::FrameState&,
                           const xr::StereoSourceLayout&) override;
    TransportResult submit_quad(IDirect3DDevice9*,const xr::FrameState&,const xr::QuadLayer&) override;
    bool before_reset(bool already_lost) override;
    bool after_reset(bool was_lost, bool succeeded) override;
    bool shutdown() override;
    xr::OpenXrRuntime& runtime() noexcept { return runtime_; }
    using ScopeLayoutProvider=void (*)(void*,const xr::FrameState&,xr::StereoSourceLayout*) noexcept;
    void set_scope_layout_provider(ScopeLayoutProvider callback,void* context=nullptr) noexcept {scope_layout_=callback;scope_context_=context;}
    // The provider validates the exact native render completion/frame and
    // returns a borrowed surface valid for the duration of this submit call.
    // It is queried only when a physical scope explicitly requires it.
    using ScopeSourceProvider=IDirect3DSurface9* (*)(void*,IDirect3DDevice9*,const xr::FrameState&) noexcept;
    void set_scope_source_provider(ScopeSourceProvider callback,void* context=nullptr) noexcept {
        scope_source_=callback;scope_source_context_=context;
    }
    using EyeOverlay=bool (*)(void*,ID3D11Device*,ID3D11DeviceContext*,const xr::FrameState&,unsigned,const xr::EyeRenderTarget&);
    void set_eye_overlay(EyeOverlay callback,void* context=nullptr) noexcept {eye_overlay_=callback;overlay_context_=context;}
    void set_quad_reticle_provider(QuadReticleProvider callback,void* context=nullptr) noexcept {
        quad_reticle_provider_=callback;quad_reticle_context_=context;
    }
private:
    ScopeLayoutProvider scope_layout_{};void* scope_context_{};
    ScopeSourceProvider scope_source_{};void* scope_source_context_{};
    EyeOverlay eye_overlay_{};
    void* overlay_context_{};
    QuadReticleProvider quad_reticle_provider_{};
    void* quad_reticle_context_{};
    TransportResult submit_layer(IDirect3DDevice9*,const xr::FrameState&,
                                 const xr::StereoSourceLayout&,const xr::QuadLayer*);
    xr::OpenXrRuntime runtime_;
    xr::D3D9ExSharedTextureBridge bridge_;
    xr::D3D9ExSharedTextureBridge scope_bridge_;
    xr::D3D11Compositor compositor_;
    bool initialized_{};
    CaptureValidator capture_validator_{};
    void* capture_validator_user_{};
};
} // namespace bovr::vr
