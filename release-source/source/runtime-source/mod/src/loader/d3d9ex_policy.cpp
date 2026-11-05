// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9ex_policy.hpp"
namespace bovr {
ResourcePolicy ex_resource_policy(ResourceKind kind, DWORD usage, D3DPOOL pool) noexcept {
    ResourcePolicy result{usage, pool, false, false};
    if (pool != D3DPOOL_MANAGED) return result;
    const bool buffer = kind == ResourceKind::vertex_buffer || kind == ResourceKind::index_buffer;
    const DWORD supported_usage = buffer ? D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING : 0;
    if ((usage & ~supported_usage) != 0) { result.unsupported_managed = true; return result; }
    result.usage |= D3DUSAGE_DYNAMIC;
    result.pool = D3DPOOL_DEFAULT;
    result.changed = true;
    return result;
}
D3DDISPLAYMODEEX ex_display_mode(const D3DPRESENT_PARAMETERS& p) noexcept {
    D3DDISPLAYMODEEX mode{};
    mode.Size = sizeof(mode);
    mode.Width = p.BackBufferWidth;
    mode.Height = p.BackBufferHeight;
    mode.RefreshRate = p.FullScreen_RefreshRateInHz;
    mode.Format = p.BackBufferFormat;
    mode.ScanLineOrdering = D3DSCANLINEORDERING_UNKNOWN;
    return mode;
}
}
