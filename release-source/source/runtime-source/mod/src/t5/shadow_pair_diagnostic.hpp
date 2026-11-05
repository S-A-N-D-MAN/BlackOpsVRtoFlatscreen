// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/native_shadow_state.hpp"
#include <memory>

namespace bovr::t5 {
struct ShadowPairDiagnosticEye {
    std::uintptr_t view{};
    std::uint32_t local_client{},shadow_type{};
    std::uint8_t extra{};
    std::array<std::uint8_t,0x140> camera{};
    std::array<std::uint8_t,0x30> viewports{};
    std::array<std::uint8_t,32> constants{};
    std::array<std::array<std::uint8_t,0x34>,2> corrected_lod{};
    std::array<std::array<std::uint8_t,0x50>,2> sun_lists{};
    std::array<std::array<std::uint8_t,0x50>,4> spot_lists{};
    std::array<std::uint8_t,shadow_sun_bytes> sun{};
    std::array<std::uint8_t,4*shadow_spot_bytes> spots{};
    std::array<std::uint32_t,8> mask{};
    std::uint32_t sun_index{},spot_count{};
    bool valid{};
    friend bool operator==(const ShadowPairDiagnosticEye&,const ShadowPairDiagnosticEye&)=default;
};
struct ShadowPairDiagnosticSample {
    std::uintptr_t base{},buffer{};
    std::uint32_t frame{};
    std::uint64_t producer{};
    DWORD thread{};
    std::array<ShadowPairDiagnosticEye,2> eyes{};
};
// Optional observation only. Failure never alters normal shadow admission.
// Each emitted JSON is one accepted frontend pair, not GPU acceptance evidence.
class ShadowPairDiagnostic {
public:
    ShadowPairDiagnostic() noexcept;
    ~ShadowPairDiagnostic();
    void configure(std::uintptr_t base) noexcept;
    std::unique_ptr<ShadowPairDiagnosticSample> begin(std::uintptr_t buffer,
        std::uint32_t frame,std::uint64_t producer) noexcept;
    void capture(ShadowPairDiagnosticSample*,const EyeShadowState&) noexcept;
    void complete(ShadowPairDiagnosticSample*,bool accepted) noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
