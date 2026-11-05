// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace bovr::t5 {
bool configure_sun_shadow_lod_hook(HMODULE executable) noexcept;
// The existing frontend wrapper owns this scope; it never changes a native LOD
// record, rendering view index, local-client index, or caster partition.
class SunShadowLodScope {
public:
    SunShadowLodScope()=default;
    SunShadowLodScope(const SunShadowLodScope&)=delete;
    SunShadowLodScope& operator=(const SunShadowLodScope&)=delete;
    ~SunShadowLodScope(){finish();}
    bool begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
               std::uintptr_t refdef,std::uint64_t producer,unsigned view_slot=1) noexcept;
    void finish() noexcept;
    unsigned substitutions() const noexcept{return substituted_;}
    unsigned spot_substitutions() const noexcept{return spot_substituted_;}
    bool safe() const noexcept {
        return (substituted_==0&&spot_substituted_==0)||
            (!active_&&!blocked_&&(substituted_==0||substituted_==2)&&spot_seen_==spot_expected_);
    }
private:
    friend unsigned select_sun_shadow_lod_index(std::uintptr_t,unsigned,unsigned,unsigned) noexcept;
    friend unsigned select_spot_shadow_lod_index(std::uintptr_t,unsigned,unsigned,unsigned) noexcept;
    friend void invoke_sun_shadow_lod_native(SunShadowLodScope*,void(*)(void*),void*);
    std::uintptr_t base_{},buffer_{},refdef_{},view_{};
    std::uint32_t frame_{};std::uint64_t producer_{};DWORD thread_{};
    std::array<std::uint32_t,3> origin_{};
    std::array<std::uint8_t,0x34> used_lod_{};
    unsigned view_slot_{1},substituted_{},spot_substituted_{},spot_seen_{},spot_expected_{};
    bool active_{},blocked_{};
};
// Callback has copied arguments only. The exact assembly bridge applies its
// chosen index to the consumed argument word and tail-jumps to native once.
unsigned select_sun_shadow_lod_index(std::uintptr_t return_address,unsigned partition,
                                   unsigned original_index,unsigned max_count) noexcept;
unsigned select_spot_shadow_lod_index(std::uintptr_t return_address,unsigned primary_light,
                                    unsigned original_index,unsigned spot_slot) noexcept;
void invoke_sun_shadow_lod_native(SunShadowLodScope*,void(*body)(void*),void* context);
}
