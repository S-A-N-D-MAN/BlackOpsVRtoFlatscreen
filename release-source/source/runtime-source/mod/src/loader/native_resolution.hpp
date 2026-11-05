// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
namespace bovr {
// Called only for an identity-approved promoted factory before native renderer
// configuration. No game instructions, dvars, window, or OS mode are written.
bool configure_native_resolution(IDirect3D9* factory,bool trusted_promoted) noexcept;
void observe_native_resolution(IDirect3DDevice9* device,const D3DPRESENT_PARAMETERS& parameters,HRESULT result) noexcept;
}
