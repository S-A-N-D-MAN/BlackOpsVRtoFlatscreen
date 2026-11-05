// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_math.h"
#include <cstddef>
#include <cstdint>

namespace bovr::input {
namespace xr = wawvr::xr;

// Values only. A producer must create this identity by reserving the actual
// pending XR frame for one native scene build. A cache read cannot mint it.
struct WeaponFrameIdentity final {
    std::uint64_t generation{}, frame_id{}, reservation_serial{}, native_build_serial{};
    friend bool operator==(const WeaponFrameIdentity&, const WeaponFrameIdentity&) = default;
};
struct ReservedWeaponFrame final {
    WeaponFrameIdentity identity{};
    xr::FrameState prediction{};
    xr::Posef tracking_anchor{};
    std::uint64_t anchor_generation{}, published_ms{};
    bool exclusively_reserved_for_scene{};
    // Visual-only compatibility metadata. Predicted grip/palm/action poses
    // remain unchanged; only an independent free left glove consumes this.
    bool pimax_free_glove_grip_basis{};
    // Visual-only world translation from this reservation's raw head center to
    // its collision-approved camera center. Actions and native body stay raw.
    xr::Vec3f camera_translation{};
};
enum class BodyCameraSource : std::uint8_t { unknown, completed_native_refdef };
struct WeaponBodySample final {
    WeaponFrameIdentity identity{};
    // The current native refdef origin and unmodified stock camera axis.
    // No previous scene, stereo eye, or persistent weapon matrix is accepted.
    xr::EnginePose stock_camera{};
    BodyCameraSource source{BodyCameraSource::unknown};
};
struct WeaponCalibration final {
    bool confirmed{};
    std::uint32_t weapon_index{};
    std::uint64_t model_generation{}, revision{};
    xr::Hand hand{xr::Hand::Right};
    // From current grip position to model origin, rotated by CURRENT aim.
    // OpenXR metres: +X right, +Y up, -Z forward. Zero is not auto-calibration.
    xr::Vec3f origin_from_grip_aim_local_meters{};
    // Model IW local axes to aim IW local axes; Hamilton (x,y,z,w).
    xr::Quaternionf model_to_aim_iw{};
};
struct WeaponPlacementContext final {
    WeaponFrameIdentity expected{};
    std::uint64_t now_ms{}, model_generation{}, calibration_revision{};
    std::uint32_t weapon_index{};
    bool gameplay_allowed{};
};
// Exact proven BO scratch argument layout. This is owned output, never a cast
// over game memory. Native scale is preserved rather than recalibrated here.
struct ScaledWeaponPlacement final {
    xr::Quaternionf quaternion{};
    xr::Vec3f origin{};
    float scale{1.0F};
};
static_assert(sizeof(ScaledWeaponPlacement)==32);
static_assert(offsetof(ScaledWeaponPlacement, origin)==0x10);
static_assert(offsetof(ScaledWeaponPlacement, scale)==0x1c);

enum class WeaponPlacementStatus : std::uint8_t {
    accepted, invalid_output, context_rejected, reservation_mismatch,
    stale_prediction, invalid_prediction, invalid_tracking, calibration_mismatch,
    invalid_calibration, invalid_body, invalid_native_placement, invalid_result
};
struct WeaponPlacementProposal final {
    bool valid{};
    WeaponFrameIdentity identity{};
    std::int64_t predicted_display_time{};
    std::uint64_t action_sequence{}, calibration_revision{};
    xr::EnginePose body_world{}, head_world{}, weapon_world{};
    ScaledWeaponPlacement placement{};
};
constexpr std::uint64_t kMaximumWeaponPredictionAgeMs=100;

// Computes a proposed value only; no pointer retention, clocks, runtime calls,
// game writes, tags or shot overrides. Output is invalidated on every rejection.
[[nodiscard]] WeaponPlacementStatus propose_weapon_placement(
    const ReservedWeaponFrame&, const WeaponBodySample&, const WeaponCalibration&,
    const WeaponPlacementContext&, const ScaledWeaponPlacement& original,
    WeaponPlacementProposal*, float units_per_meter=xr::kIwUnitsPerMeter) noexcept;
// Experimental diagnostic alignment: derive the explicit offset/rotation that
// makes this reserved grip+aim sample reproduce the current native placement.
// This does not identify a physical grip tag, muzzle or comfortable hand fit.
[[nodiscard]] WeaponPlacementStatus derive_experimental_native_alignment(
    const ReservedWeaponFrame&, const WeaponBodySample&, const WeaponPlacementContext&,
    const ScaledWeaponPlacement& original, xr::Hand, WeaponCalibration*,
    float units_per_meter=xr::kIwUnitsPerMeter) noexcept;
}
