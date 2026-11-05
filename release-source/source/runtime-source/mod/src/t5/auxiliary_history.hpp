// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/auxiliary_postfx.hpp"
#include "render/eye_staging.hpp"
#include <memory>
#include <string>
namespace bovr::t5 {
enum class HistoryKind { unsupported, active, inactive };
inline HistoryKind history_kind(const AuxiliaryPostfxSnapshot& s) noexcept {
    const auto& a=s.views[0];const auto& b=s.views[1];
    if(a.effects!=b.effects||a.scene_def[0]!=b.scene_def[0]||a.rendering_mode||b.rendering_mode||a.full_screen||b.full_screen)return HistoryKind::unsupported;
    const auto e=auxiliary_postfx_effects(a);
    if(!e.save_mode&&!e.blur_enabled&&!e.flash_enabled)return HistoryKind::inactive;
    if(e.save_mode!=1||e.blur_enabled!=1||e.flash_enabled||e.save_timer||e.blur_timer||
       !a.effects[6]||a.effects[6]>10000||a.effects[7]||a.effects[8]||a.effects[9]!=0x3f800000||a.effects[10]!=0x3f800000)return HistoryKind::unsupported;
    return HistoryKind::active;
}
// Process/adapter lifetime budget: failed attempts are spent, never refunded.
class AuxiliaryHistoryBudget {
public:
    std::uint64_t claim(HistoryKind kind,std::uint64_t now) noexcept {
        if(!now||kind==HistoryKind::unsupported||(used_&&(now<last_||now-last_<500)))return 0;
        auto& count=kind==HistoryKind::active?active_:inactive_;
        if(count>=(kind==HistoryKind::active?2u:1u))return 0;
        ++count;last_=now;return ++used_;
    }
    bool exhausted() const noexcept {return active_==2&&inactive_==1;}
private:unsigned active_{},inactive_{};std::uint64_t used_{},last_{};
};
struct HistoryScalars {
    std::uint32_t view_mode{},viewport_behavior{},target_width{},target_height{};
    std::uint8_t viewport_dirty{},scissor_dirty{};
    render::HudEyeViewport scissor{};
    friend bool operator==(const HistoryScalars&,const HistoryScalars&)=default;
};
struct AuxiliaryHistorySnapshot {
    AuxiliaryPostfxSnapshot postfx{};HistoryScalars source{};
    friend bool operator==(const AuxiliaryHistorySnapshot&,const AuxiliaryHistorySnapshot&)=default;
};
bool validate_auxiliary_history_guards(HMODULE) noexcept;
bool read_auxiliary_history(std::uintptr_t,const AuxiliaryViewLayout&,unsigned selected,AuxiliaryHistorySnapshot*) noexcept;
struct HistoryCurrent {
    void* context{};bool(*check)(void*) noexcept{};
    bool valid() const noexcept {return check&&check(context);}
};
// Capture resources belong to one backend invocation. The C10 owner-held form
// defers their release on native faults until normal owner retirement; it never
// reuses a faulted capture. File/GPU capture failures stay observational.
class AuxiliaryHistoryCapture {
public:
    AuxiliaryHistoryCapture() noexcept;
    ~AuxiliaryHistoryCapture();
    AuxiliaryHistoryCapture(const AuxiliaryHistoryCapture&)=delete;
    AuxiliaryHistoryCapture& operator=(const AuxiliaryHistoryCapture&)=delete;
    void begin(bool enabled,AuxiliaryHistoryBudget&,std::uintptr_t,const AuxiliaryViewLayout&,
               const AuxiliaryPostfxKey&,IDirect3DDevice9*,HistoryCurrent) noexcept;
    void stage(unsigned) noexcept;
    void finish(bool native_complete,const render::EyeStaging&) noexcept;
    // C10 owner-held diagnostic: fault retirement has no COM/resource work.
    void abandon_native_fault() noexcept;
    void retire() noexcept;
private:struct State;std::unique_ptr<State> state_;
};
}
