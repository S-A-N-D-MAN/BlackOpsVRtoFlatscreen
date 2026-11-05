// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cinematic_movie_fixture {
inline constexpr std::uint32_t unrelated_material=0x10203040;
inline constexpr std::uint32_t movie_material=0x50607080;

template<class T>
constexpr void store(std::array<std::uint8_t,136>& bytes,std::size_t at,T value) noexcept {
    if constexpr(sizeof(T)==1)bytes[at]=static_cast<std::uint8_t>(value);
    else {
        const auto bits=[](T v) constexpr {
            if constexpr(std::is_same_v<T,float>)return std::bit_cast<std::uint32_t>(v);
            else return v;
        }(value);
        for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=static_cast<std::uint8_t>(bits>>(8*i));
    }
}
constexpr void image(std::array<std::uint8_t,136>& bytes,std::size_t at,
                     std::uint32_t material,std::uint32_t color) noexcept {
    store<std::uint16_t>(bytes,at,48);store<std::uint8_t>(bytes,at+2,10);
    store<std::uint32_t>(bytes,at+4,material);
    store<float>(bytes,at+8,-.5F);store<float>(bytes,at+12,0.F);store<float>(bytes,at+16,1.F);
    store<float>(bytes,at+20,1281.F);store<float>(bytes,at+24,720.F);
    store<float>(bytes,at+28,0.F);store<float>(bytes,at+32,0.F);
    store<float>(bytes,at+36,1.F);store<float>(bytes,at+40,1.F);store<std::uint32_t>(bytes,at+44,color);
}
constexpr std::array<std::uint8_t,136> make() noexcept {
    std::array<std::uint8_t,136> bytes{};
    // Authored projection, clear, unrelated image, movie image and terminator.
    // The record sizes exercise the parser's public layout contract.
    store<std::uint16_t>(bytes,0,8);store<std::uint8_t>(bytes,2,27);
    store<std::uint16_t>(bytes,8,28);store<std::uint8_t>(bytes,10,5);store<std::uint8_t>(bytes,12,1);
    store<float>(bytes,16,1.F);store<float>(bytes,20,0.F);store<float>(bytes,24,0.F);
    store<float>(bytes,28,0.F);store<float>(bytes,32,1.F);
    image(bytes,36,unrelated_material,0xff000000U);
    image(bytes,84,movie_material,0xffffffffU);
    store<std::uint16_t>(bytes,132,4);
    return bytes;
}
inline constexpr auto authored_movie_commands=make();
}
