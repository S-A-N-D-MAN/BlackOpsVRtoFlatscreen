// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "openxr_runtime.h"

namespace blackopsvr {

// Retain the inspected core's namespace until integration merits an explicit
// reviewed adaptation. This header contains no game structures or addresses.
namespace xr = wawvr::xr;

inline xr::RuntimeConfig default_runtime_config() noexcept {
    xr::RuntimeConfig config{};
    config.application_name = "Black Ops VR";
    config.engine_name = "Black Ops VR";
    return config;
}

} // namespace blackopsvr
