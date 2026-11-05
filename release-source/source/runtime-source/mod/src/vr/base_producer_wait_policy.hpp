// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string_view>

namespace bovr::vr::openxr_transport_detail {
struct BaseProducerWaitPolicy final {
    std::uint32_t microseconds{10000};
    bool override_accepted{};
};
// Exact startup comparison values only. Do not parse, trim, clamp or partially
// accept user text: malformed/missing settings use the bounded default.
constexpr BaseProducerWaitPolicy select_base_producer_wait(std::wstring_view value) noexcept {
    if(value==L"6000")return {6000,true};
    if(value==L"8000")return {8000,true};
    if(value==L"10000")return {10000,true};
    return {};
}
// Reads only at transport startup; preserves the caller's Windows last error.
BaseProducerWaitPolicy read_base_producer_wait_policy() noexcept;
}
