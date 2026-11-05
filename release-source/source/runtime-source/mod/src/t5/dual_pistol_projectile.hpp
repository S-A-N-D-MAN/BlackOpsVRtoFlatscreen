// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
namespace bovr::t5 {
// Identity and hash work happens before the shared deferred installer pauses
// peer threads; commit contains only bounded code-memory operations.
bool prepare_dual_pistol_projectile(HMODULE executable) noexcept;
bool commit_dual_pistol_projectile() noexcept;
}
