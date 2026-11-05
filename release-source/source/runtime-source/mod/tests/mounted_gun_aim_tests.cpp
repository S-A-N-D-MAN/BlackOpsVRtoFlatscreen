// SPDX-License-Identifier: GPL-3.0-only
#include "input/mounted_gun_aim.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
namespace aim = bovr::input::mounted_gun_aim;
namespace xr = wawvr::xr;
using bovr::input::CommandAimProposal;
using bovr::input::CommandAimStatus;
unsigned checks{}, failures{};
void check(bool ok, const char* text) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", text); }
}
bool near(float a, float b) { return std::fabs(a - b) < .001f; }
bool near(xr::Vec3f a, xr::Vec3f b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
xr::Quaternionf yaw(float degrees) {
    const float half = degrees * .008726646259971648f;
    return {0, std::sin(half), 0, std::cos(half)};
}
struct Fixture {
    bovr::input::CommandRigSample rig{};
    xr::Basis3f stock{};
    Fixture() {
        rig.xr_generation = rig.anchor_generation = 7;
        rig.published_ms = 1000;
        rig.frame.frame_id = 12;
        rig.frame.predicted_display_time = 100000;
        rig.frame.predicted_display_period = 11111111;
        rig.frame.should_render = rig.frame.views_valid = true;
        rig.frame.actions.focused = true;
        rig.frame.actions.sequence = 17;
        auto& pose = rig.frame.actions.hands[1].aim;
        pose.active = pose.orientation_valid = pose.orientation_tracked = true;
    }
    CommandAimStatus run(CommandAimProposal* out, std::uint64_t now = 1010) const {
        return aim::right_hand(rig, stock, now, out);
    }
};
void controller_geometry() {
    Fixture f;
    CommandAimProposal p{}, baseline{};
    check(f.run(&baseline) == CommandAimStatus::accepted &&
        near(baseline.forward, {1,0,0}), "mounted right aim works without any grip action");
    f.rig.frame.head_center.orientation = yaw(80);
    f.rig.frame.head_center.position = {1,2,3};
    f.rig.frame.actions.hands[1].aim.pose.position = {-7,6,5};
    auto& support = f.rig.frame.actions.hands[0];
    support.aim = f.rig.frame.actions.hands[1].aim;
    support.aim.pose.orientation = yaw(-70);
    support.squeeze = {true, 1.f, false};
    check(f.run(&p) == CommandAimStatus::accepted && near(p.forward, baseline.forward) &&
        !p.support.paired, "head movement, hand translation and support hand cannot redirect mounted aim");
    f.rig.frame.actions.hands[1].aim.pose.orientation = yaw(30);
    check(f.run(&p) == CommandAimStatus::accepted && near(p.yaw_degrees, 30),
        "right-controller yaw controls mounted world direction");
    f.stock = {{0,1,0},{-1,0,0},{0,0,1}};
    check(f.run(&p) == CommandAimStatus::accepted && near(p.yaw_degrees, 120),
        "native stock body basis is composed exactly once");
    f.rig.tracking_anchor.orientation = yaw(30);
    check(f.run(&p) == CommandAimStatus::accepted && near(p.yaw_degrees, 90),
        "a matching recentered controller and anchor preserve body-forward aim");
    f.rig.anchor_generation++;
    check(f.run(&p) == CommandAimStatus::identity,
        "mixed recenter generations cannot authorize aim");
    f = Fixture{};
    check(f.run(&p,1100) == CommandAimStatus::accepted,
        "original mounted rig is valid at the inclusive 100 ms age boundary");
    check(f.run(&p,1101) == CommandAimStatus::stale && p.packed == 0,
        "retained mounted rig cannot refresh its original expiry");
    check(f.run(&p,999) == CommandAimStatus::stale,
        "future-dated mounted rig is rejected");
    f.rig.frame.actions.hands[1].aim.active = false;
    check(f.run(&p) == CommandAimStatus::tracking, "inactive right aim is rejected");
    f = Fixture{}; f.rig.frame.actions.hands[1].aim.orientation_tracked = false;
    check(f.run(&p) == CommandAimStatus::tracking, "lost right aim tracking is rejected");
}
void mechanical_limits() {
    aim::ClampedAim p{};
    check(aim::clamp({-40,70},{0,0},{-10,-20},{25,45},&p) &&
        near(p.relative.pitch,-10) && near(p.relative.yaw,45) && p.forward.z > 0,
        "asymmetric arcs clamp upward pitch and rightward yaw with IW pitch sign");
    check(aim::clamp({50,-60},{0,0},{-10,-20},{25,45},&p) &&
        near(p.relative.pitch,25) && near(p.relative.yaw,-20) && p.forward.z < 0,
        "opposite asymmetric limits retain their independent ranges");
    check(aim::clamp({0,5},{0,350},{-30,-20},{30,20},&p) &&
        near(p.relative.yaw,15) && near(p.world.yaw,5),
        "yaw seam chooses the short path across 360 degrees");
    check(aim::clamp({0,350},{0,5},{-30,-20},{30,20},&p) &&
        near(p.relative.yaw,-15) && near(p.world.yaw,-10),
        "reverse yaw seam preserves negative relative angle");
    check(aim::clamp({0,-160},{0,170},{-30,-10},{30,20},&p) &&
        near(p.relative.yaw,20) && near(p.world.yaw,-170),
        "wrapped request still stops at the asymmetric native yaw limit");
    check(aim::clamp({-80,80},{0,0},{0,0},{0,0},&p) &&
        near(p.forward,{1,0,0}), "zero-width native arc locks the barrel at its pivot");
    check(aim::clamp({0,80},{0,0},{-10,-20},{25,45},&p) &&
        near(p.forward,{.70710678f,.70710678f,0}) &&
        !near(p.forward,{.17364818f,.98480775f,0}),
        "barrel ray follows clamped 45 degree aim rather than requested 80 degrees");
    check(aim::clamp({15,25},{10,20},{-10,-20},{25,45},&p) &&
        near(p.relative.pitch,5) && near(p.relative.yaw,5) &&
        near(p.world.pitch,15) && near(p.world.yaw,25),
        "nonzero mount pitch and yaw are removed and restored exactly once");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (unsigned field = 0; field != 8; ++field) {
        aim::Angles requested{}, base{}, minimum{-10,-20}, maximum{25,45};
        float* components[]{&requested.pitch,&requested.yaw,&base.pitch,&base.yaw,
            &minimum.pitch,&minimum.yaw,&maximum.pitch,&maximum.yaw};
        *components[field] = field % 2 ? infinity : nan;
        p.forward = {8,9,10};
        check(!aim::clamp(requested,base,minimum,maximum,&p) && near(p.forward,{}),
            "nonfinite native/requested component rejects and clears proposal");
    }
    check(!aim::clamp({},{},{5,-20},{4,45},&p), "reversed pitch arc is rejected");
    check(!aim::clamp({},{},{-10,46},{25,45},&p), "reversed yaw arc is rejected");
    check(!aim::clamp({},{},{-181,-20},{25,45},&p), "pitch arc outside native signed range is rejected");
    check(!aim::clamp({},{},{-10,-20},{25,181},&p), "yaw arc outside native signed range is rejected");
    check(!aim::clamp({},{},{-10,-20},{25,45},nullptr), "missing destination is rejected");
}
}

int main() {
    controller_geometry();
    mechanical_limits();
    std::printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
