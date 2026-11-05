// SPDX-License-Identifier: GPL-3.0-only
#include "render/eye_staging.hpp"
#include <algorithm>
#include <limits>

namespace bovr::render {
namespace {
bool same_surface(const D3DSURFACE_DESC& a, const D3DSURFACE_DESC& b) noexcept {
    return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format &&
           a.MultiSampleType == b.MultiSampleType && a.MultiSampleQuality == b.MultiSampleQuality;
}
bool valid_rectangles(const std::array<RECT, 2>& rectangles, const D3DSURFACE_DESC& d) noexcept {
    if (d.Width > static_cast<UINT>(LONG_MAX) || d.Height > static_cast<UINT>(LONG_MAX)) return false;
    for (const auto& r : rectangles) {
        if (r.left < 0 || r.top < 0 || r.right <= r.left || r.bottom <= r.top ||
            r.right > static_cast<LONG>(d.Width) || r.bottom > static_cast<LONG>(d.Height)) return false;
    }
    const auto& a = rectangles[0]; const auto& b = rectangles[1];
    return std::max(a.left, b.left) >= std::min(a.right, b.right) ||
           std::max(a.top, b.top) >= std::min(a.bottom, b.bottom);
}
}
HRESULT EyeStaging::backbuffer(IDirect3DDevice9* device, IDirect3DSurface9** output,
                              D3DSURFACE_DESC* description) noexcept {
    if (!device || !output || !description) return D3DERR_INVALIDCALL;
    *output = nullptr;
    auto hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, output);
    if (FAILED(hr) || !*output) return FAILED(hr) ? hr : E_FAIL;
    hr = (*output)->GetDesc(description);
    if (FAILED(hr)) { (*output)->Release(); *output = nullptr; }
    return hr;
}
HRESULT EyeStaging::begin(IDirect3DDevice9* device, std::uint64_t producer,
                         const std::array<RECT, 2>& rectangles,const std::array<unsigned,2>& copy_order) noexcept {
    if (owner_ && owner_ != GetCurrentThreadId()) return D3DERR_INVALIDCALL;
    invalidate();
    last_source_={};
    if (!device || !producer || copy_order[0]>1 || copy_order[1]>1 || copy_order[0]==copy_order[1]) return D3DERR_INVALIDCALL;
    IDirect3DSurface9* source{}; D3DSURFACE_DESC d{};
    auto hr = backbuffer(device, &source, &d);
    if (FAILED(hr)) return hr;
    last_source_=d;
    source->Release();
    // Copying the packed target back into a multisampled target is not a
    // supported resolve direction. Refuse it rather than publish partial eyes.
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE || !valid_rectangles(rectangles, d)) return D3DERR_INVALIDCALL;
    if (packed_ && (device != device_identity_ || !same_surface(d, description_))) release();
    if (!packed_) {
        hr = device->CreateRenderTarget(d.Width, d.Height, d.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &packed_, nullptr);
        if (FAILED(hr) || !packed_) return FAILED(hr) ? hr : E_FAIL;
        device_identity_ = device; description_ = d;
    }
    owner_ = GetCurrentThreadId(); producer_ = producer; rectangles_ = rectangles;copy_order_=copy_order;
    return S_OK;
}
bool EyeStaging::matches(IDirect3DDevice9* device, std::uint64_t producer) const noexcept {
    return packed_ && device == device_identity_ && producer && producer == producer_ &&
           owner_ == GetCurrentThreadId();
}
HRESULT EyeStaging::save_eye(IDirect3DDevice9* device, std::uint64_t producer, unsigned eye) noexcept {
    if (!matches(device, producer) || eye > 1 ||
        !((mask_==0&&eye==copy_order_[0])||(mask_==(1u<<copy_order_[0])&&eye==copy_order_[1]))) return D3DERR_INVALIDCALL;
    IDirect3DSurface9* source{}; D3DSURFACE_DESC d{};
    auto hr = backbuffer(device, &source, &d);
    if (FAILED(hr)) { invalidate(); return hr; }
    if (!same_surface(d, description_)) hr = D3DERR_INVALIDCALL;
    else hr = device->StretchRect(source, &rectangles_[eye], packed_, &rectangles_[eye], D3DTEXF_NONE);
    source->Release();
    if (SUCCEEDED(hr)) mask_ |= 1u << eye;
    else invalidate();
    return hr;
}
HRESULT EyeStaging::restore_pair(IDirect3DDevice9* device, std::uint64_t producer) noexcept {
    if (!matches(device, producer) || mask_ != 3) return D3DERR_INVALIDCALL;
    IDirect3DSurface9* target{}; D3DSURFACE_DESC d{};
    auto hr = backbuffer(device, &target, &d);
    if (FAILED(hr)) { invalidate(); return hr; }
    if (!same_surface(d, description_)) hr = D3DERR_INVALIDCALL;
    else for (unsigned eye = 0; eye < 2 && SUCCEEDED(hr); ++eye)
        hr = device->StretchRect(packed_, &rectangles_[eye], target, &rectangles_[eye], D3DTEXF_NONE);
    target->Release();
    // Consume the pair even on failure: never reuse pixels from an earlier key.
    invalidate();
    return hr;
}
HRESULT EyeStaging::copy_completed_pair(IDirect3DDevice9* device,std::uint64_t producer,IDirect3DSurface9* destination) const noexcept {
    const DWORD error=GetLastError();HRESULT hr=D3DERR_INVALIDCALL;
    try {
    if(destination&&destination!=packed_&&matches(device,producer)&&mask_==3) {
        D3DSURFACE_DESC d{};hr=destination->GetDesc(&d);
        if(SUCCEEDED(hr)&&!same_surface(d,description_))hr=D3DERR_INVALIDCALL;
        if(SUCCEEDED(hr))hr=device->StretchRect(packed_,nullptr,destination,nullptr,D3DTEXF_NONE);
    }
    }catch(...){hr=E_FAIL;}
    SetLastError(error);return hr;
}
void EyeStaging::invalidate() noexcept { producer_ = 0; mask_ = 0; }
void EyeStaging::release() noexcept {
    invalidate();
    if (packed_) { packed_->Release(); packed_ = nullptr; }
    device_identity_ = nullptr; description_ = {}; owner_ = 0;
}
}
