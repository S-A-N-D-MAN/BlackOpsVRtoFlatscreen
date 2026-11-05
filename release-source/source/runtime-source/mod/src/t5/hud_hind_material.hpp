// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace bovr::t5 {
namespace hud_hind_detail {
struct Snapshot {
    std::array<std::uint8_t,192> material{};
    std::array<char,sizeof("hind_reticle")> name{};
    friend bool operator==(const Snapshot&,const Snapshot&)=default;
};
template<class Reader>bool capture(std::uint32_t material,Reader& read,Snapshot& out) {
    auto copy=[&](std::uint32_t address,auto& destination) {
        return address>=0x10000&&
            static_cast<std::uint64_t>(address)+destination.size()<=
                std::numeric_limits<std::uint32_t>::max()&&
            read(address,destination.data(),destination.size());
    };
    if(!copy(material,out.material))return false;
    std::uint32_t name{};
    std::memcpy(&name,out.material.data(),sizeof name);
    return copy(name,out.name)&&
        std::memcmp(out.name.data(),"hind_reticle",sizeof("hind_reticle"))==0;
}
}
// Coordinate-only policy for the exact current Hind reticle material. The
// caller separately owns the admitted helicopter frame and copied HUD list.
// Neither a native pointer nor a material decision is retained across calls.
template<class Reader>bool is_hud_hind_material(std::uint32_t material,Reader&& read) noexcept {
    try {
        hud_hind_detail::Snapshot first{},second{};
        return hud_hind_detail::capture(material,read,first)&&
            hud_hind_detail::capture(material,read,second)&&first==second;
    } catch(...) {return false;}
}
}
