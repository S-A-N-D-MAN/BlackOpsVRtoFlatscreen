// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "render/hud_eye_commands.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
namespace bovr::t5 {
struct AuxiliaryViewSlot {
    std::uintptr_t address{};
    render::HudEyeViewport scene{},display{},scissor{};
    std::array<std::uint8_t,8> targets{}; // Native35F0..35F7; copied bytes.
    std::uint8_t extra{};
    friend bool operator==(const AuxiliaryViewSlot&,const AuxiliaryViewSlot&)=default;
};
struct AuxiliaryTarget {
    std::uint32_t image{},color{},depth{};
    std::uint16_t width{},height{};
    friend bool operator==(const AuxiliaryTarget&,const AuxiliaryTarget&)=default;
};
struct AuxiliaryViewLayout {
    std::uintptr_t buffer{},view_array{};
    std::uint32_t frame{},count{},selected{};
    std::array<AuxiliaryViewSlot,3> slots{};
    std::array<AuxiliaryTarget,2> targets{}; // Exact native22/23 table metadata.
    friend bool operator==(const AuxiliaryViewLayout&,const AuxiliaryViewLayout&)=default;
};
struct StereoDrawPlan {
    std::uint32_t original_index{};
    std::array<std::uint32_t,2> native_indices{0,1};
    std::array<unsigned,2> eyes{0,1};
    bool auxiliary{};
};
// End-exclusive bound of second SMP store, slot2 extra flag. All earlier
// buffer/global/target/viewport reads fall below this exact native offset.
inline constexpr std::uintptr_t auxiliary_read_extent=0x420f730+0xe380+2*0x38e0+0x38c4+1;
static_assert(auxiliary_read_extent==0x4228535);
bool read_auxiliary_layout(std::uintptr_t base,bool frontend,AuxiliaryViewLayout*) noexcept;
bool validate_auxiliary_native_guards(HMODULE) noexcept;
inline bool regular_eye_slot(const AuxiliaryViewSlot& s,const render::HudEyeViewport& eye) noexcept {
    return s.address&&s.extra==0&&s.scene==eye&&s.display==eye&&s.scissor==eye&&
        s.targets[0]==3&&s.targets[1]==3&&s.targets[4]==22&&s.targets[5]==20&&s.targets[6]==19&&s.targets[7]==2;
}
inline bool exact_auxiliary_layout(const AuxiliaryViewLayout& s,const std::array<render::HudEyeViewport,2>& eyes,
        const std::array<AuxiliaryViewSlot,2>& produced) noexcept {
    if(s.count!=3||s.selected!=2||!s.buffer||!s.view_array||s.slots[0]!=produced[0]||s.slots[1]!=produced[1]||
       !regular_eye_slot(s.slots[0],eyes[0])||!regular_eye_slot(s.slots[1],eyes[1]))return false;
    for(unsigned i=0;i<3;++i)if(s.slots[i].address!=s.view_array+i*0x38e0)return false;
    const auto& a=s.slots[2];const auto& target=s.targets[0];const auto& depth=s.targets[1];
    const render::HudEyeViewport expected{0,0,target.width,target.height};
    return a.extra==1&&a.targets[0]==22&&a.targets[1]==22&&a.targets[4]==22&&a.targets[5]==20&&a.targets[6]==19&&a.targets[7]==2&&
        target.image&&target.color&&target.depth&&depth.image&&depth.color&&depth.depth&&
        target.color!=depth.color&&target.width&&target.height&&target.width<=5120&&target.height<=4096&&
        target.width==depth.width&&target.height==depth.height&&a.scene==expected&&a.display==expected&&a.scissor==expected;
}
inline StereoDrawPlan auxiliary_draw_plan() noexcept {return {2,{2,0},{1,0},true};}
// The ordinary packed scene has two native eye slots and retains its 0,1
// execution order. The auxiliary route still requires its complete third view.
inline bool exact_postfx_layout(const AuxiliaryViewLayout& s,const std::array<render::HudEyeViewport,2>& eyes,
        const std::array<AuxiliaryViewSlot,2>& produced) noexcept {
    if(s.count==3)return exact_auxiliary_layout(s,eyes,produced);
    if(s.count!=2||s.selected!=1||!s.buffer||!s.view_array||s.slots[2]!=AuxiliaryViewSlot{}||
       s.slots[0]!=produced[0]||s.slots[1]!=produced[1])return false;
    for(unsigned i=0;i<2;++i)if(s.slots[i].address!=s.view_array+i*0x38e0||!regular_eye_slot(s.slots[i],eyes[i]))return false;
    return true;
}
inline constexpr bool postfx_view_count(unsigned count) noexcept {return count==2||count==3;}
inline constexpr unsigned postfx_first_eye(unsigned count) noexcept {return count==2?0u:1u;}
inline constexpr unsigned postfx_native_index(unsigned count,unsigned eye) noexcept {return eye?(count==2?1u:2u):0u;}
// This is the exact adapter invocation sequence, not a model of an unrelated
// implementation. Native draws continue after a copy rejection, once each.
// C++ native unwinds restore the selected index and are never handled here.
template<class SetIndex,class Native,class Copy>
bool execute_stereo_draw_plan(const StereoDrawPlan& plan,SetIndex set,Native native,Copy copy,
                             bool* native_started,bool capture=true) {
    struct Restore {SetIndex& set;std::uint32_t value;~Restore(){set(value);}} restore{set,plan.original_index};
    for(unsigned step=0;step<2;++step){set(plan.native_indices[step]);*native_started=true;native();
        if(capture)capture=copy(plan.eyes[step]);}
    return capture;
}
}
