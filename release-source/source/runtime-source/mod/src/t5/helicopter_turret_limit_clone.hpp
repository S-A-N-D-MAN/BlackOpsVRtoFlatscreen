// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bovr::t5::helicopter_turret_limits {
using Info=std::array<std::uint8_t,0x1dc8>;
inline constexpr float yaw_limit_degrees=179.f;
inline constexpr float pitch_limit_degrees=89.f;
namespace detail {
inline bool disjoint(const void* a,const void* b) noexcept {
    const auto left=reinterpret_cast<std::uintptr_t>(a),right=reinterpret_cast<std::uintptr_t>(b);
    return left<=right?right-left>=sizeof(Info):left-right>=sizeof(Info);
}
template<class T>inline T read(const Info& bytes,std::size_t offset) noexcept {
    T value{};std::memcpy(&value,bytes.data()+offset,sizeof value);return value;
}
}
// Value-only, separately owned copies from the exact current native info.
// This grants no vehicle/thread/stack lifetime or callback authority. The
// adapter validates the native rate/dt arguments separately and retains the
// private clone until the original caller has consumed its four clamp fields.
// Failure leaves every supplied byte untouched. Aliasing copies cannot stand
// in for two independent observations, and output may overlap neither copy.
inline bool clone(const Info& first,const Info& rechecked,Info* output) noexcept {
    static_assert(sizeof(float)==4&&sizeof(Info)==0x1dc8);
    if(!output||!detail::disjoint(&first,&rechecked)||
       !detail::disjoint(&first,output)||!detail::disjoint(&rechecked,output)||
       first!=rechecked)return false;
    const auto type=detail::read<std::int16_t>(first,0x40);
    if(type!=4&&type!=6)return false;
    for(const auto offset:{0x1ecu,0x1f0u,0x1f4u,0x1f8u}) {
        const float limit=detail::read<float>(first,offset);
        if(!std::isfinite(limit)||limit<=0.f||limit>180.f)return false;
    }
    Info proposed=first;
    constexpr std::array<float,4> limits{yaw_limit_degrees,yaw_limit_degrees,pitch_limit_degrees,pitch_limit_degrees};
    std::memcpy(proposed.data()+0x1ec,limits.data(),sizeof limits);
    *output=proposed;return true;
}
}
