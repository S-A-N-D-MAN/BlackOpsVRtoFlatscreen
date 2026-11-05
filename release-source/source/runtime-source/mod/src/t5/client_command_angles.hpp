// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace bovr::t5::client_command_angles {
// This is a current authored-command refresh, not an event/consumption witness.
enum class Mode : unsigned { disabled, observe, correct };
using Words=std::array<std::uint32_t,2>;
struct Facts {
    bool exact{},ordinary{},dual{};
    int type{},kind{},fire_type{};
    unsigned weapon{};
    std::uint64_t now{},published_ms{};
    std::uint32_t packed{};
    std::array<unsigned char,52> command{};
};
inline bool select(const Facts& f,Words* out) noexcept {
    if(!out||!f.exact||!f.ordinary||f.dual||f.type!=0||f.kind<0||f.kind>4||
       f.fire_type<0||f.fire_type>6||f.fire_type==5||!f.weapon||f.weapon>=256||
       f.command[0x18]!=f.weapon||!f.published_ms||f.now<f.published_ms||f.now-f.published_ms>100)return false;
    std::uint32_t actual{};std::memcpy(&actual,f.command.data()+0x20,4);if(actual!=f.packed)return false;
    constexpr float degrees_per_word=360.f/65536.f;
    const float pitch=static_cast<std::int16_t>(f.packed&65535u)*degrees_per_word;
    const float yaw=static_cast<std::int16_t>(f.packed>>16)*degrees_per_word;
    *out={std::bit_cast<std::uint32_t>(pitch),std::bit_cast<std::uint32_t>(yaw)};return true;
}
struct Host {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
    // Return true only when the operation completed; observed is the old word.
    bool (*compare_exchange)(void*,std::uintptr_t,std::uint32_t,std::uint32_t,std::uint32_t*) noexcept{};
};
inline bool finite(const Words& bits) noexcept {
    return std::isfinite(std::bit_cast<float>(bits[0]))&&std::isfinite(std::bit_cast<float>(bits[1]));
}
inline void restore(const Host& h,std::uintptr_t address,const Words& before,const Words& selected,unsigned owned) noexcept {
    if(!h.compare_exchange)return;
    for(unsigned i=0;i<2;++i)if(owned&(1u<<i)) {
        std::uint32_t observed{};h.compare_exchange(h.context,address+4*i,selected[i],before[i],&observed);
    }
}
inline bool transfer(const Host& h,std::uintptr_t address,const Words& before,const Words& selected,unsigned* owned) noexcept {
    if(!owned)return false;*owned=0;
    if(!h.compare_exchange||!address||(address&3)||address>UINTPTR_MAX-8||!finite(before)||!finite(selected))return false;
    for(unsigned i=0;i<2;++i) {
        std::uint32_t observed{};
        if(!h.compare_exchange(h.context,address+4*i,before[i],selected[i],&observed)||observed!=before[i]) {
            restore(h,address,before,selected,*owned);*owned=0;return false;
        }
        if(before[i]!=selected[i])*owned|=1u<<i;
    }
    return true;
}
}
