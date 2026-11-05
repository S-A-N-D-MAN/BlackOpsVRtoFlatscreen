// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace bovr::t5 {
// Strictly observational. Disabled unless both explicit environment options
// select the diagnostic and a bounded list of static-model instance indices.
bool configure_foliage_lod_probe(HMODULE executable) noexcept;
struct FoliageLodOwner {void* context{};bool(*current)(void*) noexcept{};};
struct FoliageLodRecord {
    std::uint32_t index{},model{},instance{},world{};
    float adjusted{},raw{},placement_scale{},corrected_scale{},corrected_bias{},applied_inverse{};
    int result{};unsigned no_cull{};std::int16_t lod_count{};
    std::array<float,4> ranges{};std::array<float,3> origin{};
    std::array<char,96> name{};
    bool captured{};
};
class FoliageLodProbeScope {
public:
    FoliageLodProbeScope()=default;
    FoliageLodProbeScope(const FoliageLodProbeScope&)=delete;
    FoliageLodProbeScope& operator=(const FoliageLodProbeScope&)=delete;
    ~FoliageLodProbeScope(){finish();}
    bool begin(std::uintptr_t base,std::uintptr_t buffer,unsigned frame,std::uintptr_t refdef,
               std::uint64_t producer,unsigned eye,FoliageLodOwner owner) noexcept;
    void finish() noexcept;
private:
    friend void observe_foliage_lod(std::uint32_t*) noexcept;
    friend void invoke_foliage_lod_native(FoliageLodProbeScope*,void(*)(void*),void*);
    bool current(bool built) const noexcept;
    std::uintptr_t base_{},buffer_{},view_{},refdef_{};
    std::uint64_t producer_{};unsigned frame_{},eye_{};DWORD thread_{};
    FoliageLodOwner owner_{};
    std::array<std::uint32_t,3> origin_{};std::array<int,4> viewport_{};
    std::array<FoliageLodRecord,64> records_{};
    unsigned observed_{},rejected_{};bool active_{},blocked_{};
};
void observe_foliage_lod(std::uint32_t* saved) noexcept;
void invoke_foliage_lod_native(FoliageLodProbeScope*,void(*body)(void*),void*);
}
