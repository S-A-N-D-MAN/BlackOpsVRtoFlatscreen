// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/vpos_world_math.hpp"
#include <array>
#include <cstring>
namespace bovr::t5 {
// Three ray constants, two reflection-UV constants, and the exact PS identity.
// Setters can fail after changing state; record obligations before each call.
struct WaterVPosJournal {
    std::array<VPosVector,5> saved{},written{};
    std::uintptr_t saved_shader{},written_shader{};
    unsigned mask{};
    static constexpr unsigned shader_bit=1u<<5;
    static bool same(const VPosVector& a,const VPosVector& b) noexcept {
        return std::memcmp(a.data(),b.data(),sizeof(VPosVector))==0;
    }
    template<class Set,class SetShader> bool apply(Set&& set,SetShader&& shader) noexcept {
        if(mask||!saved_shader||!written_shader||saved_shader==written_shader)return false;
        for(unsigned i=0;i<5;++i){mask|=1u<<i;if(!set(i,written[i]))return false;}
        mask|=shader_bit;return shader(written_shader);
    }
    template<class Set,class SetShader> bool restore(Set&& set,SetShader&& shader) noexcept {
        if(mask&shader_bit){if(!shader(saved_shader))return false;mask&=~shader_bit;}
        for(int i=4;i>=0;--i)if(mask&(1u<<i)){
            if(!set(unsigned(i),saved[i]))return false;mask&=~(1u<<i);
        }
        return true;
    }
    template<class Get,class Set,class GetShader,class SetShader>
    bool reconcile(Get&& get,Set&& set,GetShader&& read_shader,SetShader&& shader) noexcept {
        if(mask&shader_bit){
            std::uintptr_t actual{};if(!read_shader(actual))return false;
            if(actual==written_shader){
                if(!shader(saved_shader)||!read_shader(actual)||actual!=saved_shader)return false;
            }
            // An original or subsequently installed native shader is preserved.
            mask&=~shader_bit;
        }
        for(int i=4;i>=0;--i)if(mask&(1u<<i)){
            VPosVector actual{};if(!get(unsigned(i),actual))return false;
            if(same(actual,written[i])&&!same(actual,saved[i])){
                if(!set(unsigned(i),saved[i])||!get(unsigned(i),actual)||!same(actual,saved[i]))return false;
            }
            // Either restored already or superseded by a later native write.
            mask&=~(1u<<i);
        }
        return true;
    }
};
}
