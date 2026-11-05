// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace bovr::t5 {
struct StereoLodFovMath {
    float applied{},temporary_scale{},temporary_bias{},expected_scale{},expected_bias{};
};
bool stereo_lod_fov_math(float tangent,float previous,float threshold,float scale,float bias,StereoLodFovMath*) noexcept;
bool configure_stereo_lod_fov_scope(HMODULE) noexcept;
bool stereo_lod_fov_scope_enabled() noexcept;
struct StereoLodFovOwner {void* context{};bool(*current)(void*) noexcept{};};
// No instruction patch. Only two registered dvar current floats are journaled;
// original metadata, preferences, native ramp/history and model ranges survive.
class StereoLodFovScope {
public:
    StereoLodFovScope()=default;
    ~StereoLodFovScope(){finish();}
    StereoLodFovScope(const StereoLodFovScope&)=delete;
    StereoLodFovScope& operator=(const StereoLodFovScope&)=delete;
    bool begin(std::uintptr_t base,std::uintptr_t buffer,unsigned frame,std::uintptr_t refdef,
               std::uint64_t producer,unsigned eye,StereoLodFovOwner) noexcept;
    void after_native(bool completed) noexcept;
    bool finish() noexcept;
    bool safe() const noexcept{return good_&&!pending_;}
private:
    struct Dvar {std::uintptr_t slot{},pointer{};std::array<unsigned char,0x70> bytes{};};
    bool frame_current(bool built) const noexcept;
    bool dvar_current(unsigned index,bool temporary) const noexcept;
    bool dvars_current(bool temporary) const noexcept;
    bool restore() noexcept;
    void observe() noexcept;
    bool active_{},good_{true},changed_{},reported_{},admitted_{},actual_valid_{};unsigned pending_{};
    std::uintptr_t base_{},buffer_{},view_{},refdef_{};std::uint64_t producer_{};
    unsigned frame_{},eye_{};DWORD thread_{};StereoLodFovOwner owner_{};
    std::array<unsigned,3> origin_{};std::array<int,4> viewport_{};unsigned tangent_bits_{};
    std::array<Dvar,2> dvars_{};std::array<unsigned,2> before_{},temporary_{};
    StereoLodFovMath math_{};std::array<float,3> actual_{};
    const char* reason_{"none"};
};
void invoke_stereo_lod_fov_native(StereoLodFovScope*,void(*body)(void*),void*);
}
