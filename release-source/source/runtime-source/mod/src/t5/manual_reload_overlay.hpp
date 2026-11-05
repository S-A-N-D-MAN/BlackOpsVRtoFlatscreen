// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <d3d11.h>
namespace bovr::t5 {
bool render_manual_reload_overlay(void*,ID3D11Device*,ID3D11DeviceContext*,const wawvr::xr::FrameState&,unsigned,const wawvr::xr::EyeRenderTarget&);
}
