// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/auxiliary_view.hpp"
#include <array>
#include <cstdint>
namespace bovr::t5 {
struct AuxiliaryPostfxView {
    std::array<std::uint32_t,5> scene_def{}; // Native view+170: time, floatTime, viewOffset.
    std::uint32_t full_screen{},rendering_mode{};
    std::array<std::uint32_t,21> effects{}; // Exact native21CC..221F raw bytes.
    friend bool operator==(const AuxiliaryPostfxView&,const AuxiliaryPostfxView&)=default;
};
struct AuxiliaryPostfxSnapshot {
    AuxiliaryViewLayout layout{};
    std::array<AuxiliaryPostfxView,2> views{};
    std::array<std::uint32_t,4> saved_times{};
    std::array<std::uint32_t,20> saved_params{};
    std::array<AuxiliaryTarget,5> targets{}; // Native1,3,6,22,23; raw identities.
    std::array<std::uint32_t,5> source_scene_def{};
    render::HudEyeViewport source_viewport{};
    std::uint8_t source_target{};
    friend bool operator==(const AuxiliaryPostfxSnapshot&,const AuxiliaryPostfxSnapshot&)=default;
};
// Exact current ordinary or auxiliary layout, used only by the complete bloom
// episode. The historical history/probe entry point remains auxiliary-only.
bool read_stereo_postfx(std::uintptr_t,const AuxiliaryViewLayout&,std::uint32_t,AuxiliaryPostfxSnapshot*) noexcept;
struct AuxiliaryPostfxKey {
    std::uint64_t attempt{},producer{},generation{},xr_frame{};
    DWORD owner{};
};
template<class Reader>bool copy_stable_auxiliary_postfx(Reader reader,AuxiliaryPostfxSnapshot* out) noexcept {
    if(out)*out={};if(!out)return false;AuxiliaryPostfxSnapshot a{},b{};
    if(!reader(&a)||!reader(&b)||a!=b)return false;*out=b;return true;
}
struct AuxiliaryPostfxEffects {
    unsigned save_mode{},blur_enabled{},flash_enabled{},save_timer{},blur_timer{},flash_timer{};
    unsigned timer_outside_copied_history_mask{};
    bool full_filter{};
};
inline AuxiliaryPostfxEffects auxiliary_postfx_effects(const AuxiliaryPostfxView& v) noexcept {
    AuxiliaryPostfxEffects s{};s.save_mode=v.effects[5]&255;s.blur_enabled=v.effects[12]&255;s.flash_enabled=v.effects[20]&255;
    s.save_timer=v.effects[4];s.blur_timer=v.effects[11];s.flash_timer=v.effects[19];
    // Four fixed copied records follow the reference layout. Until a native
    // length proof is available, this describes our copy bound, not whether
    // a larger index is valid for the game. Never use it to index native data.
    if(s.save_mode&&s.save_timer>=4)s.timer_outside_copied_history_mask|=1;
    if(s.blur_enabled&&s.blur_timer>=4)s.timer_outside_copied_history_mask|=2;
    if(s.flash_enabled&&s.flash_timer>=4)s.timer_outside_copied_history_mask|=4;
    s.full_filter=(v.rendering_mode&7)!=0||v.full_screen!=0;return s;
}
// At most16 attempts, including partial/copy-failed invocations. It never
// renews on native frame regression or XR reset, so failures stay bounded.
class AuxiliaryPostfxBudget {
public:
    std::uint64_t claim(std::uint64_t now) noexcept {
        if(!now||used_>=16||(used_&&(now<last_||now-last_<250)))return 0;
        last_=now;return ++used_;
    }
private:std::uint64_t last_{},used_{};
};
bool validate_auxiliary_postfx_guards(HMODULE) noexcept;
bool read_auxiliary_postfx(std::uintptr_t base,const AuxiliaryViewLayout& expected,
                          std::uint32_t selected,AuxiliaryPostfxSnapshot*) noexcept;
void trace_auxiliary_postfx(std::uintptr_t base,const AuxiliaryViewLayout& expected,
                           const AuxiliaryPostfxKey&,unsigned stage) noexcept;
}
