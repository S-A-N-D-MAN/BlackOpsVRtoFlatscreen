// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace bovr::t5::rebirth_prop {
// Rebirth's opening hands and scripted hatchet use firearm metadata but lack
// firearm tags. These two exact live assets retain their native presentation.
using Copy=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Current {
    unsigned weapon{};
    std::uintptr_t cg{},server{},variant{},definition{};
};
namespace detail {
inline bool bounded(std::uintptr_t p,std::size_t n) noexcept {
    return p>=0x10000&&n&&p<=(std::numeric_limits<std::uint32_t>::max)()-n+1;
}
template<class T>bool value(Copy copy,void* context,std::uintptr_t p,T& out) noexcept {
    return bounded(p,sizeof out)&&copy(context,p,&out,sizeof out);
}
struct Descriptor {
    std::uint32_t name{};
    int type{},weapon_class{},capacity{};
    unsigned char dual{};
    enum class Kind { unknown,hands,hatchet } kind{};
    friend bool operator==(const Descriptor&,const Descriptor&)=default;
};
inline bool current(Copy copy,void* context,const Current& c,Descriptor& d) noexcept {
    if(!copy||!c.weapon||c.weapon>255||!bounded(c.cg,0x8a4a9)||
       !bounded(c.server,0x145)||!bounded(c.variant,0x24)||!bounded(c.definition,0x56b))return false;
    std::uint32_t cg{},server{},clients{},variant{},definition{};
    unsigned char primary{},server_primary{},local{},server_local{};
    constexpr char hands[]="rebirth_hands_sp",hatchet[]="rebirth_hatchet_sp";
    char actual[sizeof hatchet]{};
    const auto exact_name=[&] {
        if(!value(copy,context,c.variant,d.name)||!bounded(d.name,sizeof actual))return false;
        for(unsigned i=0;i<sizeof actual;++i){
            if(!value(copy,context,d.name+i,actual[i]))return false;
            if(!actual[i]){
                if(!std::memcmp(actual,hands,sizeof hands))d.kind=Descriptor::Kind::hands;
                else if(!std::memcmp(actual,hatchet,sizeof hatchet))d.kind=Descriptor::Kind::hatchet;
                return d.kind!=Descriptor::Kind::unknown;
            }
        }
        return false;
    };
    return value(copy,context,0x2ff5354,cg)&&cg==c.cg&&
        value(copy,context,0x1a796f8+0x13c,server)&&server==c.server&&
        value(copy,context,0x1c03140,clients)&&clients==server&&
        value(copy,context,c.cg+0x8a364+0x130,local)&&!local&&
        value(copy,context,c.server+0x130,server_local)&&!server_local&&
        value(copy,context,c.cg+0x8a364+0x144,primary)&&primary==c.weapon&&
        value(copy,context,c.server+0x144,server_primary)&&server_primary==c.weapon&&
        value(copy,context,0xbe19a8+4*c.weapon,variant)&&variant==c.variant&&
        value(copy,context,c.variant+8,definition)&&definition==c.definition&&
        exact_name()&&
        value(copy,context,c.definition+0x1c,d.type)&&d.type==0&&
        value(copy,context,c.definition+0x20,d.weapon_class)&&d.weapon_class==0&&
        value(copy,context,c.variant+0x20,d.capacity)&&d.capacity==0&&
        value(copy,context,c.definition+0x56a,d.dual)&&!d.dual;
}
}
inline bool read_current(Copy copy,void* context,const Current& c) noexcept {
    detail::Descriptor first{},last{};
    return detail::current(copy,context,c,first)&&detail::current(copy,context,c,last)&&first==last;
}
}
