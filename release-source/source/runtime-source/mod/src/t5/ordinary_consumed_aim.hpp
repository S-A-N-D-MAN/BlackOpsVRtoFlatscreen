// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/consumed_command_probe.hpp"
#include <cmath>
#include <cstring>
namespace bovr::t5::ordinary_consumed_aim {
struct Angles {float pitch{},yaw{};friend bool operator==(const Angles&,const Angles&)=default;};
inline Angles decode(std::uint32_t packed) noexcept {
    // Exact native SHORT2ANGLE multiplier at009cdec4, used by0069db43.
    return {static_cast<std::int16_t>(packed&65535u)*.0054931640625f,
            static_cast<std::int16_t>(packed>>16)*.0054931640625f};
}
inline bool finite(Angles a) noexcept {return std::isfinite(a.pitch)&&std::isfinite(a.yaw);}
enum class Completion {native_result,restore_original,owner_retired};
bool prepare(std::uintptr_t base) noexcept;
void enable() noexcept;
void before(const NativeConsumedCommand&) noexcept;
void after(const NativeConsumedCommand&) noexcept;
// Called before a nested native invocation, while the outer scope is still
// available. Retires the outer preload before inner native code can observe it.
void cancel_for_nested() noexcept;
}
