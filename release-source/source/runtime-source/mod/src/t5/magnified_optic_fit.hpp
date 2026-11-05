// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/crossbow_optic.hpp"
#include "t5/weapon_animation_math.hpp"
#include <algorithm>
namespace bovr::t5::magnified_optic {
// Lens vertices are authored in primary-model bind space. Placement removes
// the native muzzle rotation; apply the same correction to the metric lens.
inline bool fit_bounds(animation_detail::Vec low,animation_detail::Vec high,
    const animation_detail::Transform& gun,const animation_detail::Transform& flash,
    bool compact,crossbow::LensFit* out) noexcept {
    namespace a=animation_detail;
    if(!out||!a::finite(low)||!a::finite(high)||!a::rigid(gun.axis)||!a::rigid(flash.axis)||
       high.x<low.x||high.y<=low.y||high.z<=low.z||high.x-low.x>4.f)return false;
    const auto local=a::relative(gun,{{low.x-.03f,(low.y+high.y)*.5f,(low.z+high.z)*.5f},gun.axis});
    const auto barrel=a::relative(gun,flash);
    const a::Transform identity{{},{{1,0,0},{0,1,0},{0,0,1}}};
    const auto inverse=a::relative({{},barrel.axis},identity);
    const auto offset=a::world(inverse.axis,local.origin);
    constexpr float metric=1.f/wawvr::xr::kIwUnitsPerMeter;
    const float radius=std::max(high.y-low.y,high.z-low.z)*.5f*metric;
    if(radius<crossbow::kMinimumLensRadiusMeters||radius>.06f||a::dot(offset,offset)>1600.f)return false;
    *out={{offset.x*metric,offset.y*metric,offset.z*metric},radius,.045f,compact?2.5f:4.f};
    return true;
}
}
