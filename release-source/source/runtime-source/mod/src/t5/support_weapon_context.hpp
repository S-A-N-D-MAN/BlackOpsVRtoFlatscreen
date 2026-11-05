// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace bovr::t5::support_weapon {
// Exact mission marker asset only. The caller separately establishes whether
// this index is currently equipped or belongs to the native fourth action slot.
using Copy=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
struct Descriptor {
    unsigned weapon{};
    std::uintptr_t variant{},definition{},name{};
    int type{},weapon_class{},capacity{};
    friend bool operator==(const Descriptor&,const Descriptor&)=default;
};
namespace detail {
inline bool bounded(std::uintptr_t p,std::size_t n) noexcept {
    return p>=0x10000&&n&&p<=(std::numeric_limits<std::uint32_t>::max)()-n+1;
}
template<class T>bool value(Copy copy,void* context,std::uintptr_t p,T& out) noexcept {
    return bounded(p,sizeof out)&&copy(context,p,&out,sizeof out);
}
inline bool descriptor(Copy copy,void* context,unsigned weapon,Descriptor* out) noexcept {
    if(!copy||!out||!weapon||weapon>255)return false;
    Descriptor d{};d.weapon=weapon;std::uint32_t variant{},definition{},name{};
    constexpr char expected[]="rocket_barrage_sp";char actual[sizeof expected]{};
    if(!value(copy,context,0xbe19a8+4*weapon,variant)||!bounded(variant,0x24)||
       !value(copy,context,variant+8,definition)||!bounded(definition,0x24)||
       !value(copy,context,variant,name)||!bounded(name,sizeof actual)||
       !copy(context,name,actual,sizeof actual)||std::memcmp(actual,expected,sizeof expected)||
       !value(copy,context,definition+0x1c,d.type)||d.type!=2||
       !value(copy,context,definition+0x20,d.weapon_class)||d.weapon_class!=4||
       !value(copy,context,variant+0x20,d.capacity)||d.capacity!=1)return false;
    d.variant=variant;d.definition=definition;d.name=name;*out=d;return true;
}
}
inline bool read_descriptor(Copy copy,void* context,unsigned weapon,Descriptor* out) noexcept {
    if(!out)return false;*out={};Descriptor first{},last{};
    if(!detail::descriptor(copy,context,weapon,&first)||
       !detail::descriptor(copy,context,weapon,&last)||first!=last)return false;
    *out=first;return true;
}
}
