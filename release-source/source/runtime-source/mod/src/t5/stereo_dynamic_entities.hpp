// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>

namespace bovr::t5 {
// Startup verification only. No hook, native call or executable-code write.
// BOVR_STEREO_DYNAMIC_ENTITIES=0 retains the accepted baseline for comparison.
bool configure_stereo_dynamic_entities(HMODULE executable) noexcept;
bool stereo_dynamic_entities_enabled() noexcept;
bool validate_stereo_dynamic_native_guards(HMODULE executable) noexcept;

struct DynamicEntitiesFailure {
    const char* stage="none";
    const char* field="none";
    std::uintptr_t address{};
    std::uint32_t expected{},actual{};
};

// R_DrawAllDynEnt appends camera-local compact records to shared counters. Its
// native skin/brush paths fully overwrite each successful compact record and
// allocate fresh buffer-local surfaces. Between the two ordinary cameras we
// reset ONLY those two counters; the left eye's emitted surfaces stay intact.
class StereoDynamicEntitiesScope {
public:
    StereoDynamicEntitiesScope()=default;
    StereoDynamicEntitiesScope(const StereoDynamicEntitiesScope&)=delete;
    StereoDynamicEntitiesScope& operator=(const StereoDynamicEntitiesScope&)=delete;
    bool begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
               std::uint64_t producer) noexcept;
    bool prepare_right() noexcept;
    bool finish() noexcept;
    bool good() const noexcept{return good_;}
    bool prepared() const noexcept{return prepared_;}
    bool enabled() const noexcept{return enabled_;}
    const DynamicEntitiesFailure& failure() const noexcept{return failure_;}
    const std::array<std::uint32_t,2>& left_counts() const noexcept{return left_;}
    const std::array<std::uint32_t,2>& right_counts() const noexcept{return right_;}
private:
    bool validate(unsigned cameras,const char* stage) noexcept;
    bool reject(const char* stage,const char* field,std::uintptr_t address=0,
                std::uint32_t expected=0,std::uint32_t actual=0) noexcept;
    std::uintptr_t base_{},buffer_{},views_{},world_{};
    std::uint32_t frame_{};std::uint64_t producer_{};DWORD thread_{};
    std::array<std::uint32_t,2> arrays_{},capacities_{},left_{},right_{};
    bool enabled_{},active_{},prepared_{},finished_{},good_{true};
    DynamicEntitiesFailure failure_{};
};
}
