// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace bovr::t5 {
// The reviewed infrared vision callsite may return this value in addition to
// its native answer. It is true only inside the explicit native vision copy,
// never during ordinary client actor assembly or the two headset world views.
bool thermal_scene_vision_override() noexcept;
bool validate_thermal_scene_native_guards() noexcept;

class ThermalSceneScope {
public:
    ThermalSceneScope()=default;
    ThermalSceneScope(const ThermalSceneScope&)=delete;
    ThermalSceneScope& operator=(const ThermalSceneScope&)=delete;
    ~ThermalSceneScope(){restore();}
    bool begin(std::uintptr_t refdef,std::uintptr_t frontend,std::uint32_t native_frame) noexcept;
    bool restore() noexcept;
    bool active() const noexcept{return active_;}
    bool safe() const noexcept{return !active_&&restored_;}
    unsigned actor_count() const noexcept{return actor_count_;}
private:
    std::uintptr_t cg_{},buffer_{};
    std::uint32_t frame_{},thread_{};
    std::array<std::uint8_t,0x288> vision_{};
    std::uint32_t post_brightening_{};
    std::array<std::uint32_t,256> flags_{};
    std::array<bool,256> changed_{};
    unsigned actor_count_{};
    bool active_{},restored_{true};
};

// Reserve the last native view slot without exposing it to the normal HUD or
// optional native auxiliary-camera allocator. The draw-surface allocator is
// append-only; it is deliberately not rewound after this view is built.
class ThermalViewSlotScope {
public:
    ThermalViewSlotScope()=default;
    ThermalViewSlotScope(const ThermalViewSlotScope&)=delete;
    ThermalViewSlotScope& operator=(const ThermalViewSlotScope&)=delete;
    ~ThermalViewSlotScope(){restore();}
    bool begin(std::uintptr_t frontend,std::uint32_t native_frame) noexcept;
    bool built() const noexcept;
    bool restore() noexcept;
    std::uintptr_t slot() const noexcept{return slot_;}
    bool safe() const noexcept{return !active_&&restored_;}
private:
    std::uintptr_t buffer_{},slot_{};
    std::uint32_t frame_{},thread_{};
    bool active_{},restored_{true},built_{};
};
// Also restores temporary native data on Windows structured exceptions, which
// do not run ordinary C++ destructors under the project's /EHsc setting.
void invoke_thermal_scene_native(ThermalSceneScope*,ThermalViewSlotScope*,void(*body)(void*),void* context);
bool invoke_thermal_backend_native(std::uintptr_t buffer,void(*body)(void*),void* context);
}
