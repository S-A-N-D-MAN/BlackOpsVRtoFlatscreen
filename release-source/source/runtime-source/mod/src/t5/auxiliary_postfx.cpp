// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/auxiliary_postfx.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <cstring>
namespace bovr::t5 {
static_assert(sizeof(AuxiliaryTarget)==16);
namespace {
constexpr std::array<unsigned,5> target_ids{1,3,6,22,23};
template<class T>void copy_at(std::uintptr_t p,T* out) noexcept {native_read_copy(p,out);}
bool layout_at(std::uintptr_t base,const AuxiliaryViewLayout& expected,std::uint32_t selected,AuxiliaryViewLayout* out) noexcept {
    AuxiliaryViewLayout s{};
    copy_at(base+0x3a5b5f8,&s.buffer);if(s.buffer!=expected.buffer)return false;
    copy_at(s.buffer+0x15d7a0,&s.frame);copy_at(s.buffer+0x16cc38,&s.selected);
    copy_at(s.buffer+0x16cc3c,&s.count);copy_at(s.buffer+0x16cc40,&s.view_array);
    if(s.frame!=expected.frame||s.selected!=selected||s.count!=expected.count||!postfx_view_count(s.count)||s.view_array!=expected.view_array)return false;
    for(unsigned i=0;i<s.count;++i){auto& v=s.slots[i];v.address=s.view_array+i*0x38e0;
        copy_at(v.address+0x140,&v.scene);copy_at(v.address+0x150,&v.display);copy_at(v.address+0x160,&v.scissor);
        copy_at(v.address+0x35f0,&v.targets);copy_at(v.address+0x38c4,&v.extra);}
    for(unsigned i=0;i<2;++i)copy_at(base+0x41eb1e8+(22+i)*20,&s.targets[i]);
    auto compare=s;compare.selected=expected.count-1;
    if(compare!=expected)return false;*out=s;return true;
}
bool snapshot_once(std::uintptr_t base,const AuxiliaryViewLayout& expected,std::uint32_t selected,AuxiliaryPostfxSnapshot* out) noexcept {
    AuxiliaryPostfxSnapshot s{};
    __try {
        if(!layout_at(base,expected,selected,&s.layout))return false;
        for(unsigned i=0;i<2;++i){auto& v=s.views[i];const auto p=expected.slots[i].address;
            copy_at(p+0x170,&v.scene_def);copy_at(p+0x18c,&v.full_screen);copy_at(p+0x3620,&v.rendering_mode);copy_at(p+0x21cc,&v.effects);}
        copy_at(base+0x356a32c,&s.saved_times);copy_at(base+0x356a33c,&s.saved_params);
        for(unsigned i=0;i<target_ids.size();++i)copy_at(base+0x41eb1e8+target_ids[i]*20,&s.targets[i]);
        copy_at(base+0x3ccbfa8,&s.source_scene_def);copy_at(base+0x3ccbfbc,&s.source_viewport);copy_at(base+0x417ff28,&s.source_target);
        AuxiliaryViewLayout end{};if(!layout_at(base,expected,selected,&end)||end!=s.layout)return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=s;return true;
}
template<class T>auto hex_bytes(const T& value) noexcept {
    std::array<char,sizeof(T)*2+1> out{};constexpr char digits[]="0123456789abcdef";
    const auto* p=reinterpret_cast<const unsigned char*>(&value);
    for(unsigned i=0;i<sizeof(T);++i){out[2*i]=digits[p[i]>>4];out[2*i+1]=digits[p[i]&15];}return out;
}
}
bool read_stereo_postfx(std::uintptr_t base,const AuxiliaryViewLayout& expected,std::uint32_t selected,AuxiliaryPostfxSnapshot* out) noexcept {
    const auto error=GetLastError();if(out)*out={};bool result=false;
    if(out&&base&&base<=UINT32_MAX-auxiliary_read_extent&&postfx_view_count(expected.count)&&(selected==0||selected==expected.count-1)){
        const unsigned slot=expected.buffer==base+0x3740100?0:expected.buffer==base+0x38c6400?1:2;
        if(slot<2&&expected.view_array==base+0x420f730+slot*0xe380&&
           exact_postfx_layout(expected,{expected.slots[0].scene,expected.slots[1].scene},{expected.slots[0],expected.slots[1]})){
            result=copy_stable_auxiliary_postfx([&](AuxiliaryPostfxSnapshot* value) noexcept {
                return snapshot_once(base,expected,selected,value);},out);
        }
    }
    SetLastError(error);return result;
}
bool read_auxiliary_postfx(std::uintptr_t base,const AuxiliaryViewLayout& expected,std::uint32_t selected,AuxiliaryPostfxSnapshot* out) noexcept {
    if(expected.count==3)return read_stereo_postfx(base,expected,selected,out);
    if(out)*out={};return false;
}
void trace_auxiliary_postfx(std::uintptr_t base,const AuxiliaryViewLayout& expected,const AuxiliaryPostfxKey& key,unsigned stage) noexcept {
    const auto error=GetLastError();
    if(!key.attempt||key.attempt>16||!key.producer||!key.generation||!key.xr_frame||key.owner!=GetCurrentThreadId()||stage>2){SetLastError(error);return;}
    AuxiliaryPostfxSnapshot s{};const unsigned selected=stage==2?0:2;
    const auto observed=GetTickCount64();const bool copied=read_auxiliary_postfx(base,expected,selected,&s);
    // Each line is bounded below the existing1536-byte trace payload. Raw
    // targets are address values only; no canonical COM identity is inferred.
    trace("t5_auxiliary_postfx_snapshot","attempt=%llu stage=%u producer=%llu generation=%llu xr_frame=%llu buffer=0x%lx native_frame=%u selected=%u observed=%llu copied=%u saved_times_hex=%s saved_params_hex=%s targets_1_3_6_22_23_hex=%s source_scene_def_hex=%s source_viewport=%d,%d,%d,%d source_target=%u raw_pointer_equality_only=1 across_stage_atomicity=0",
        key.attempt,stage,key.producer,key.generation,key.xr_frame,static_cast<unsigned long>(expected.buffer),expected.frame,selected,observed,copied,
        hex_bytes(s.saved_times).data(),hex_bytes(s.saved_params).data(),hex_bytes(s.targets).data(),hex_bytes(s.source_scene_def).data(),
        s.source_viewport.x,s.source_viewport.y,s.source_viewport.width,s.source_viewport.height,s.source_target);
    if(copied)for(unsigned eye=0;eye<2;++eye){const auto& v=s.views[eye];const auto parsed=auxiliary_postfx_effects(v);const auto& l=s.layout.slots[eye];
        trace("t5_auxiliary_postfx_view","attempt=%llu stage=%u producer=%llu eye=%u scene_def_hex=%s full_screen=%u rendering_mode=%u effects_hex=%s save_mode=%u save_timer=%u blur_enabled=%u blur_timer=%u flash_enabled=%u flash_timer=%u timer_outside_copied_history_mask=%u full_filter_condition=%u scene=%d,%d,%d,%d display=%d,%d,%d,%d scissor=%d,%d,%d,%d",
            key.attempt,stage,key.producer,eye,hex_bytes(v.scene_def).data(),v.full_screen,v.rendering_mode,hex_bytes(v.effects).data(),
            parsed.save_mode,parsed.save_timer,parsed.blur_enabled,parsed.blur_timer,parsed.flash_enabled,parsed.flash_timer,parsed.timer_outside_copied_history_mask,parsed.full_filter,
            l.scene.x,l.scene.y,l.scene.width,l.scene.height,l.display.x,l.display.y,l.display.width,l.display.height,l.scissor.x,l.scissor.y,l.scissor.width,l.scissor.height);}
    SetLastError(error);
}
// Generated exact native guards live in a separate byte-only include.
#include "t5/auxiliary_postfx_guards.inc"
}
