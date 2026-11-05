// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace bovr::t5 {
// These are independently verified retail byte spans, not a copied engine struct.
inline constexpr std::size_t shadow_sun_bytes=0x450,shadow_spot_bytes=0x1d0;
inline constexpr std::size_t shadow_technique_bytes=0x4000,shadow_light_bytes=0x170;
struct ShadowLightSlot {
    std::uint32_t index{},value{};
    std::array<std::uint8_t,shadow_light_bytes> proof{};
    friend bool operator==(const ShadowLightSlot&,const ShadowLightSlot&)=default;
};
struct NativeShadowState {
    std::array<std::uint8_t,shadow_sun_bytes> sun{};
    std::array<std::uint32_t,8> mask{};
    std::array<std::uint8_t,shadow_technique_bytes> techniques{};
    std::uint32_t spot_count{},light_count{};
    std::array<std::uint8_t,4*shadow_spot_bytes> spots{};
    std::array<ShadowLightSlot,4> light_slots{};
    friend bool operator==(const NativeShadowState&,const NativeShadowState&)=default;
};
struct EyeShadowState {
    std::uintptr_t buffer{},view{};
    std::uint32_t frame{},eye{},sun_index{};
    NativeShadowState state{};
    bool valid{},thermal_scope{};
};
struct EyeShadowPair {std::array<EyeShadowState,2> eyes{};};
struct ShadowFailure {
    const char* stage="none";
    const char* field="none";
    std::uintptr_t address{};
    std::uint32_t expected{},actual{};
};

bool validate_shadow_native_guards(HMODULE executable) noexcept;
// Startup-only, process-lifetime five-byte CALL replacement. Outside an armed
// auxiliary scope the bridge preserves the existing native inner call.
bool install_shadow_inner_dispatch(HMODULE executable) noexcept;
// No game writes. The reader requires the selected, just-built frontend eye.
bool capture_eye_shadow(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
                        unsigned eye,EyeShadowState* output) noexcept;

bool capture_thermal_shadow(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,EyeShadowState* output) noexcept;

class NativeShadowScope {
public:
    NativeShadowScope()=default;
    NativeShadowScope(const NativeShadowScope&)=delete;
    NativeShadowScope& operator=(const NativeShadowScope&)=delete;
    ~NativeShadowScope(){finish();}
    bool begin(std::uintptr_t base,const EyeShadowState& eye) noexcept;
    bool finish() noexcept;
    bool good() const noexcept {return good_;}
    bool restored() const noexcept {return restored_;}
    bool pending() const noexcept {return active_;}
    const ShadowFailure& failure() const noexcept {return failure_;}
private:
    friend bool prepare_shadow_inner_view(std::uintptr_t) noexcept;
    friend void complete_shadow_inner_view() noexcept;
    bool begin_journal(std::uintptr_t base,const EyeShadowState&,bool inner) noexcept;
    bool finish_journal() noexcept;
    std::uintptr_t base_{},buffer_{},view_{};
    std::uint32_t frame_{};
    DWORD thread_{};
    NativeShadowState before_{},selected_{};
    std::array<ShadowLightSlot,4> touched_{};
    std::uint32_t touched_count_{};
    bool active_{},good_{},restored_{};
    bool inner_identity_{},deferred_{},thermal_identity_{};
    unsigned inner_phase_{};
    unsigned inner_callback_depth_{};
    const EyeShadowState* deferred_eye_{};
    ShadowFailure failure_{};
};
// The exact bridge callbacks are also exercised by copied in-process fixtures.
// They never call a native renderer themselves.
bool prepare_shadow_inner_view(std::uintptr_t view) noexcept;
void complete_shadow_inner_view() noexcept;
// Runs the existing native draw thunk once. __finally restores the journal for
// both C++ exceptions and native SEH; it never catches/suppresses the exception.
void invoke_shadow_native(NativeShadowScope*,void(*body)(void*),void* context);
}
