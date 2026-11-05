// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/identity.hpp"
#include <array>
#include <cstring>
#include <limits>
namespace bovr::t5 {
namespace hud_compass_detail {
template<class T>T value(const void* p) noexcept {T out{};std::memcpy(&out,p,sizeof out);return out;}
struct Snapshot {
    std::array<std::uint8_t,192> material{};
    std::array<std::uint8_t,24> name{};
    std::array<std::uint8_t,528> techset{};
    std::array<std::uint8_t,28> technique{};
    std::array<std::uint8_t,16> vertex{},pixel{};
    std::array<std::uint8_t,40> arguments{};
    std::array<std::uint8_t,480> vs{};
    std::array<std::uint8_t,440> ps{};
    friend bool operator==(const Snapshot&,const Snapshot&)=default;
};
template<class Reader>bool capture(std::uint32_t material,Reader& read,Snapshot& s) {
    auto copy=[&](std::uint32_t address,auto& out) {
        return address>=0x10000&&static_cast<std::uint64_t>(address)+out.size()<=UINT32_MAX&&read(address,out.data(),out.size());
    };
    auto u32=[](const auto& a,unsigned at){return value<std::uint32_t>(a.data()+at);};
    if(!copy(material,s.material)||!copy(u32(s.material,0),s.name)||
       std::memcmp(s.name.data(),"hud_border_dpad_compass",23)!=0||s.name[23]!=0||
       !copy(u32(s.material,0xb0),s.techset)||!copy(u32(s.techset,0x18),s.technique)||
       value<std::uint16_t>(s.technique.data()+6)!=1)return false;
    const auto* pass=s.technique.data()+8;
    if(unsigned(pass[12])+pass[13]+pass[14]!=5||
       !copy(value<std::uint32_t>(pass+4),s.vertex)||!copy(value<std::uint32_t>(pass+8),s.pixel)||
       !u32(s.vertex,4)||!u32(s.pixel,4)||
       value<std::uint16_t>(s.vertex.data()+12)!=120||value<std::uint16_t>(s.pixel.data()+12)!=110||
       !copy(value<std::uint32_t>(pass+16),s.arguments))return false;
    if(sha256(s.arguments)!="1bea652b75819263bcc006ee8e8bbb27b323d742482fe02939d571904aaea800")return false;
    return copy(u32(s.vertex,8),s.vs)&&copy(u32(s.pixel,8),s.ps);
}
}
// This is only a current material's coordinate-policy proof. The caller owns
// the command publication/backend lifetime. No pointer survives this call.
template<class Reader>bool is_hud_compass_material(std::uint32_t material,Reader&& read) noexcept {
    try {
        hud_compass_detail::Snapshot a{},b{};
        return hud_compass_detail::capture(material,read,a)&&hud_compass_detail::capture(material,read,b)&&a==b&&
            sha256(a.vs)=="9920bbf1637e17f1fdcb498205039cdb03def35f40b318e422ac565cc86d01e6"&&
            sha256(a.ps)=="fdfb63629966057c335d0af31c91ab3d3c768bd5b586001f46ba2e98c2e49fc4";
    } catch(...) {return false;}
}
}
