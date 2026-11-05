// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/borrowed_profile_shape.hpp"
#include "t5/weapon_animation_math.hpp"
#include "t5/native_transform_precision.hpp"
#include <cstring>

namespace bovr::t5::borrowed_profile {
enum class BarrelGeometryStatus { invalid, fixed, animated_translation };
// This is a geometry check after exact current profile and locked-matrix
// admission. It does not authorize a profile, pointer, command, or scene lease.
inline BarrelGeometryStatus barrel_geometry(unsigned row,const animation_detail::LocalGeometry& local) noexcept {
    namespace a=animation_detail;
    const auto* profile=get(row);const auto* selected=shape(row);
    if(!profile||!selected||selected->total!=profile->bone.size()||
       selected->gun>=profile->bone.size()||selected->flash>=profile->bone.size()||
       !a::finite(local.flash_in_gun.origin)||!a::rigid(local.flash_in_gun.axis)||
       !a::finite(local.gun_in_dobj.origin)||!a::rigid(local.gun_in_dobj.axis)||
       !std::isfinite(local.roundtrip_position)||local.roundtrip_position>0.02F||
       !std::isfinite(local.roundtrip_axis)||local.roundtrip_axis>0.01F)return BarrelGeometryStatus::invalid;
    a::NativeMat32 gun_raw{},flash_raw{};a::Transform gun{},flash{};
    std::memcpy(&gun_raw,profile->bone[selected->gun].bind.data(),sizeof(gun_raw));
    std::memcpy(&flash_raw,profile->bone[selected->flash].bind.data(),sizeof(flash_raw));
    if(!a::convert_native(gun_raw,&gun)||!a::convert_native(flash_raw,&flash))return BarrelGeometryStatus::invalid;
    const auto bind=a::relative(gun,flash);
    if(a::axis_error(bind.axis,local.flash_in_gun.axis)>0.002F)return BarrelGeometryStatus::invalid;
    const bool fixed=native_precision::local_position_close(bind.origin,local.flash_in_gun.origin,
        gun.origin,flash.origin,local.gun_world.origin,local.flash_world.origin);
    // Native alternate-mode animation can translate the muzzle away from its
    // asset bind position. Use the current locked translation in the proposal;
    // the existing camera/normal/after checks still require the same transform
    // through that occurrence. Some stock assets (including the FAL) author a
    // small fixed muzzle rotation. The exact copied bind comparison above owns
    // that rotation; placement solves it out so command aim and tag_flash agree.
    if(selected->revision!=runtime_revision)return BarrelGeometryStatus::invalid;
    return fixed?BarrelGeometryStatus::fixed:BarrelGeometryStatus::animated_translation;
}
}
