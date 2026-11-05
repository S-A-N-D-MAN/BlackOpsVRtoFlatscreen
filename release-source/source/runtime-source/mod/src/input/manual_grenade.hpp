// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"
#include "mod/manual_grenade_logic.hpp"
namespace bovr::input::grenade {
namespace xr=wawvr::xr;
namespace g=wawvr::mod;
struct Context {
    std::uint64_t now{},identity{},generation{};
    bool valid{},blocked{},frag_available{},tactical_available{};
    bool button_mode{};
};
struct Output {
    g::ManualGrenadeStage stage{};
    g::ManualGrenadeSlot slot{};
    g::ManualGrenadeEvent event{};
    std::uint64_t transaction{},updated_ms{},generation{};
    bool consume_trigger{},valid{};
    xr::Posef tracking_pose{};
    xr::Vec3f local_origin{},local_velocity{},local_forward{};
};
// Uses the existing COD4/WaW belt, hysteresis and velocity rules. The caller
// alone owns native inventory, cooking, spawning and exactly-once delivery.
class Controller {
public:
    Output update(const CommandRigSample&,const Context&) noexcept;
    void retire() noexcept;
    void reset() noexcept;
private:
    g::ManualGrenadeGestureState gesture_{};
    g::ManualGrenadeVelocityHistory history_{};
    std::uint64_t identity_{},generation_{},sequence_{},published_{},transaction_{};
    xr::Vec3f previous_{};
    bool button_mode_{},button_decided_{},button_reserved_{};
};
// Native speed and world-up arc, directed by the current common-head ray.
// This does not authorize a throw or change its native origin, fuse or ammo.
bool button_launch_velocity(xr::Vec3f forward,const g::ManualGrenadeLaunchCalibration&,
                            xr::Vec3f* out) noexcept;
}
