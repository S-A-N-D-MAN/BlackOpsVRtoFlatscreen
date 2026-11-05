// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
namespace bovr {
enum class ResourceKind { texture, volume_texture, cube_texture, vertex_buffer, index_buffer };
struct ResourcePolicy { DWORD usage; D3DPOOL pool; bool changed; bool unsupported_managed; };
// Narrow migration for the valid static managed requests used by the reference
// engines. This is not a complete software implementation of managed resources.
ResourcePolicy ex_resource_policy(ResourceKind kind, DWORD usage, D3DPOOL pool) noexcept;
D3DDISPLAYMODEEX ex_display_mode(const D3DPRESENT_PARAMETERS& parameters) noexcept;
}
