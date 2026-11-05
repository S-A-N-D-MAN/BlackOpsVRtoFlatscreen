// SPDX-License-Identifier: GPL-3.0-only
#include "t5/level_tracking_anchor.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
unsigned checks{}, failures{};
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
constexpr float pi = 3.14159265358979323846F;
bool close(float a, float b, float tolerance = 1.0e-5F) {
    return std::fabs(a - b) <= tolerance;
}
bool same_position(const wawvr::xr::Vec3f& a, const wawvr::xr::Vec3f& b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
}
wawvr::xr::Quaternionf axis_angle(float x, float y, float z, float radians) {
    const float half = radians * 0.5F;
    const float sine = std::sin(half);
    return {x * sine, y * sine, z * sine, std::cos(half)};
}
bool same_rotation(
    const wawvr::xr::Quaternionf& a,
    const wawvr::xr::Quaternionf& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= 1.0e-5F;
}
}

int main() {
    using namespace wawvr::xr;
    using bovr::t5::level_tracking_anchor;
    Posef output{};
    check(level_tracking_anchor({{0, 0, 0, 1}, {1, 2, 3}}, &output) &&
          same_rotation(output.orientation, {0, 0, 0, 1}) &&
          same_position(output.position, {1, 2, 3}),
          "identity keeps the exact position and level orientation");

    const auto yaw = axis_angle(0, 1, 0, 73.0F * pi / 180.0F);
    check(level_tracking_anchor({yaw, {-4, 5, 6}}, &output) &&
          same_rotation(output.orientation, yaw) &&
          same_position(output.position, {-4, 5, 6}),
          "pure yaw is preserved");

    const auto pitch = axis_angle(1, 0, 0, 24.0F * pi / 180.0F);
    const auto roll = axis_angle(0, 0, 1, -19.0F * pi / 180.0F);
    const auto mixed = Normalize(Multiply(Multiply(yaw, pitch), roll));
    check(level_tracking_anchor({mixed, {0.25F, 1.7F, -0.5F}}, &output) &&
          same_rotation(output.orientation, yaw) &&
          close(output.orientation.x, 0) && close(output.orientation.z, 0) &&
          same_position(output.position, {0.25F, 1.7F, -0.5F}),
          "mixed pose retains yaw while removing anchor pitch and roll");

    const auto vertical = axis_angle(1, 0, 0, 90.0F * pi / 180.0F);
    check(!level_tracking_anchor({vertical, {}}, &output),
          "near-vertical forward defers anchor capture");
    check(!level_tracking_anchor({{0, 0, 0, 0}, {}}, &output),
          "zero quaternion is rejected");
    Posef invalid{{0, 0, 0, 1}, {std::numeric_limits<float>::infinity(), 0, 0}};
    check(!level_tracking_anchor(invalid, &output),
          "non-finite position is rejected");
    check(!level_tracking_anchor({{0, 0, 0, 1}, {}}, nullptr),
          "null output is rejected");

    std::printf("level_tracking_anchor_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
