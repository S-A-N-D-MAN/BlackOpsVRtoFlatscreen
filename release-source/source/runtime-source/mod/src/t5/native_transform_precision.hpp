// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_animation_math.hpp"
#include <limits>

namespace bovr::t5::native_precision {
namespace a=animation_detail;
constexpr double minimum_position_error=0.002F;
// Native units are inches: even at distant map coordinates no position check
// can accept more than 0.508 mm. Direction and occurrence identity are separate.
constexpr double maximum_position_error=0.02F;
inline double spacing(float value) noexcept {
    if(!std::isfinite(value))return std::numeric_limits<double>::infinity();
    const auto infinity=std::numeric_limits<float>::infinity();
    return std::max(double(std::nextafter(value,infinity))-double(value),
                    double(value)-double(std::nextafter(value,-infinity)));
}
inline double rounding_radius(a::Vec value) noexcept {
    const auto x=spacing(value.x),y=spacing(value.y),z=spacing(value.z);
    return 0.5*std::sqrt(x*x+y*y+z*z);
}
inline bool within_position(a::Vec first,a::Vec second,double rounding_budget) noexcept {
    if(!a::finite(first)||!a::finite(second)||!std::isfinite(rounding_budget)||rounding_budget<0)return false;
    const auto limit=std::clamp(rounding_budget,minimum_position_error,maximum_position_error);
    const auto x=double(first.x)-double(second.x),y=double(first.y)-double(second.y),z=double(first.z)-double(second.z);
    return x*x+y*y+z*z<=limit*limit;
}
inline bool world_position_close(a::Vec expected,a::Vec actual) noexcept {
    // Independently composed float32 world transforms can differ by two ULPs
    // per component. Keep the original floor near the origin and budget only
    // the spacing at the current coordinates.
    const auto x=std::max(spacing(expected.x),spacing(actual.x));
    const auto y=std::max(spacing(expected.y),spacing(actual.y));
    const auto z=std::max(spacing(expected.z),spacing(actual.z));
    return within_position(expected,actual,2*std::sqrt(x*x+y*y+z*z));
}
struct ComposedPositionBudget {
    double gun{},barrel{};
    bool valid{};
};
inline ComposedPositionBudget composed_position_budget(const a::LocalGeometry& camera,
    a::Vec solved_pose,a::Vec expected_gun,a::Vec expected_barrel,
    a::Vec native_gun,a::Vec native_barrel) noexcept {
    // The tracked writer is not one independent world composition. Its local
    // j_gun was recovered by subtracting rounded camera pose/gun origins, then
    // it solved a rounded DObj translation and native code recomposed j_gun.
    // Keep the expected target's own rounding radius as well: treating shared
    // rounding errors as independent is conservative, not a claim of tightness.
    // Rigid rotations preserve the Euclidean radius of each operand error.
    const auto camera_pose=rounding_radius(camera.pose_world.origin);
    const auto camera_gun=rounding_radius(camera.gun_world.origin);
    const auto camera_barrel=rounding_radius(camera.flash_world.origin);
    ComposedPositionBudget result{};
    result.gun=camera_pose+camera_gun+rounding_radius(solved_pose)+
        rounding_radius(expected_gun)+rounding_radius(native_gun);
    // Recovering flash-in-gun subtracts a second pair of rounded camera
    // origins. The expected and native muzzle then each round their world
    // composition. Carry the gun's accumulated error into that second edge.
    result.barrel=result.gun+camera_gun+camera_barrel+
        rounding_radius(expected_barrel)+rounding_radius(native_barrel);
    result.valid=std::isfinite(result.gun)&&std::isfinite(result.barrel)&&
        result.gun>=0&&result.barrel>=0;
    return result;
}
inline bool local_position_close(a::Vec first,a::Vec second,
                                 a::Vec first_parent,a::Vec first_child,
                                 a::Vec second_parent,a::Vec second_child) noexcept {
    // A local translation subtracts two independently rounded world origins.
    // Comparing two such translations has four half-ULP radii. Rigid rotation
    // preserves this norm, so the bound is independent of camera/gun heading.
    return within_position(first,second,rounding_radius(first_parent)+rounding_radius(first_child)+
                           rounding_radius(second_parent)+rounding_radius(second_child));
}
inline bool same_local_geometry(const a::LocalGeometry& first,const a::LocalGeometry& second) noexcept {
    return a::rigid(first.gun_in_dobj.axis)&&a::rigid(second.gun_in_dobj.axis)&&
        a::rigid(first.flash_in_gun.axis)&&a::rigid(second.flash_in_gun.axis)&&
        local_position_close(first.gun_in_dobj.origin,second.gun_in_dobj.origin,
            first.pose_world.origin,first.gun_world.origin,second.pose_world.origin,second.gun_world.origin)&&
        a::axis_error(first.gun_in_dobj.axis,second.gun_in_dobj.axis)<=0.002F&&
        local_position_close(first.flash_in_gun.origin,second.flash_in_gun.origin,
            first.gun_world.origin,first.flash_world.origin,second.gun_world.origin,second.flash_world.origin)&&
        a::axis_error(first.flash_in_gun.axis,second.flash_in_gun.axis)<=0.002F;
}
}
