#pragma once

#include "xr_types.h"

#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr std::int32_t kPhysicalScopePanelPixels = 1024;
inline constexpr std::int32_t kNativeStereoRegionWidth = 4992;
inline constexpr std::int32_t kPhysicalScopePackedWidth =
    kNativeStereoRegionWidth + kPhysicalScopePanelPixels;

struct PhysicalScopeProfile final {
    const char* internal_weapon_name{};
    wawvr::xr::Vec3f lens_from_grip_weapon_local{};
    // Generic tag_scope/tag_scope_animate bones identify the optic assembly,
    // not necessarily its rear glass. This model-local correction places the
    // physical quad at the actual eyepiece. Exact rear-lens tags pass a zero
    // correction instead.
    wawvr::xr::Vec3f lens_from_scope_tag_weapon_local{};
    float zoom_fov_degrees{20.0F};
    float lens_radius_meters{0.032F};
    // Some viewmodels have a very different receiver/optic layout from the
    // legacy grip-relative fallback. Do not draw a floating lens if their
    // evaluated optic tag is unavailable during a scripted weapon change.
    bool requires_model_tag{};
};

struct PhysicalScopeSnapshot final {
    bool active{};
    bool anchored_to_model_tag{};
    std::uint64_t controller_generation{};
    std::uint64_t publication_milliseconds{};
    std::int32_t weapon_index{};
    wawvr::xr::Vec3f lens_origin_world{};
    wawvr::xr::Basis3f lens_axis_world{};
    // CoD camera-local coordinates: +forward, +left, +up, in IW units.
    wawvr::xr::Vec3f lens_origin_camera_local{};
    wawvr::xr::Basis3f lens_axis_camera_local{};
    float zoom_fov_degrees{};
    float lens_radius_meters{};
};

[[nodiscard]] const PhysicalScopeProfile* find_physical_scope_profile(
    std::string_view internal_weapon_name) noexcept;

[[nodiscard]] bool build_physical_scope_snapshot(
    const PhysicalScopeProfile* profile,
    std::int32_t weapon_index,
    std::uint64_t controller_generation,
    std::uint64_t publication_milliseconds,
    bool right_gripping,
    bool left_gripping,
    bool support_pose_latched,
    const wawvr::xr::Vec3f* lens_tag_world,
    const wawvr::xr::Vec3f* lens_tag_weapon_local_offset,
    const wawvr::xr::Vec3f& grip_world,
    const wawvr::xr::Basis3f& weapon_axis_world,
    const wawvr::xr::Vec3f& camera_origin_world,
    const wawvr::xr::Basis3f& camera_axis_world,
    PhysicalScopeSnapshot* output) noexcept;

void publish_physical_scope_snapshot(
    const PhysicalScopeSnapshot& snapshot) noexcept;
void invalidate_physical_scope_snapshot() noexcept;

[[nodiscard]] bool read_fresh_physical_scope_snapshot(
    std::uint64_t now_milliseconds,
    PhysicalScopeSnapshot* output) noexcept;

}  // namespace wawvr::mod
