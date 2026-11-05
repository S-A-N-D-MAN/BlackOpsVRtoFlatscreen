// SPDX-License-Identifier: GPL-3.0-only
#include "input/vehicle_aim_convergence.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace xr = wawvr::xr;
namespace test {

unsigned checks{};
unsigned failures{};

void check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::printf("FAIL %s\n", message);
    }
}

double length(const xr::Vec3f& value)
{
    const double x = static_cast<double>(value.x);
    const double y = static_cast<double>(value.y);
    const double z = static_cast<double>(value.z);
    return std::sqrt(x * x + y * y + z * z);
}

double length(const xr::Vec3f& lhs, const xr::Vec3f& rhs)
{
    const double x = static_cast<double>(lhs.x) - static_cast<double>(rhs.x);
    const double y = static_cast<double>(lhs.y) - static_cast<double>(rhs.y);
    const double z = static_cast<double>(lhs.z) - static_cast<double>(rhs.z);
    return std::sqrt(x * x + y * y + z * z);
}

bool close(float a, float b, double tolerance = 1e-6)
{
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= tolerance;
}

bool close(const xr::Vec3f& lhs, const xr::Vec3f& rhs, double tolerance = 1e-5)
{
    return close(lhs.x, rhs.x, tolerance) && close(lhs.y, rhs.y, tolerance) &&
        close(lhs.z, rhs.z, tolerance);
}

void translated_muzzle_convergence()
{
    const xr::Vec3f eye{2.0F, -1.25F, 4.5F};
    const xr::Vec3f muzzle{1.5F, -2.2F, 7.7F};
    const xr::Vec3f forward{0.4F, 0.6F, 0.8F};
    const float distance{4.2F};

    xr::Vec3f target{};
    xr::Vec3f direction{};
    check(bovr::input::vehicle_aim_convergence(eye, forward, muzzle, distance, &target, &direction),
        "translated muzzle convergence accepts geometric input");

    const double len = std::sqrt(0.4 * 0.4 + 0.6 * 0.6 + 0.8 * 0.8);
    const double scale = static_cast<double>(distance) / len;
    const xr::Vec3f expected_target{
        static_cast<float>(static_cast<double>(eye.x) + 0.4 * scale),
        static_cast<float>(static_cast<double>(eye.y) + 0.6 * scale),
        static_cast<float>(static_cast<double>(eye.z) + 0.8 * scale),
    };
    const xr::Vec3f expected_direction = {
        static_cast<float>(static_cast<double>(expected_target.x - muzzle.x)),
        static_cast<float>(static_cast<double>(expected_target.y - muzzle.y)),
        static_cast<float>(static_cast<double>(expected_target.z - muzzle.z)),
    };
    const double expected_direction_len = length(expected_direction);
    const xr::Vec3f expected_direction_unit{
        static_cast<float>(static_cast<double>(expected_direction.x) / expected_direction_len),
        static_cast<float>(static_cast<double>(expected_direction.y) / expected_direction_len),
        static_cast<float>(static_cast<double>(expected_direction.z) / expected_direction_len),
    };

    check(close(target, expected_target), "translated convergence target uses normalized forward distance");
    check(close(direction, expected_direction_unit), "translated convergence direction is normalized target minus muzzle");
}

void perpendicular_nonunit_forward()
{
    const xr::Vec3f eye{1.0F, 2.0F, 3.0F};
    const xr::Vec3f forward{2.0F, 0.0F, 0.0F};
    const float distance{2.0F};
    const xr::Vec3f muzzle{3.0F, 3.0F, 3.0F};

    xr::Vec3f target{};
    xr::Vec3f direction{};
    check(bovr::input::vehicle_aim_convergence(eye, forward, muzzle, distance, &target, &direction),
        "nonunit forward vector is accepted and normalized");
    check(close(target, {3.0F, 2.0F, 3.0F}), "nonunit forward changes target in unit direction");
    check(close(direction, {0.0F, -1.0F, 0.0F}), "perpendicular geometry preserves unit direction to muzzle");
}

void vertical_and_negative_direction()
{
    const xr::Vec3f eye{0.0F, 4.0F, 0.0F};
    const xr::Vec3f forward{0.0F, -2.0F, 0.0F};
    const float distance{5.0F};
    const xr::Vec3f muzzle{-1.0F, -1.0F, 2.0F};

    xr::Vec3f target{};
    xr::Vec3f direction{};
    check(bovr::input::vehicle_aim_convergence(eye, forward, muzzle, distance, &target, &direction),
        "negative vertical forward is accepted");
    check(close(target, {0.0F, -1.0F, 0.0F}), "negative vertical target uses normalized forward");
    const double expected_direction_len = length(target, muzzle);
    const xr::Vec3f expected_direction{
        static_cast<float>(static_cast<double>(target.x - muzzle.x) / expected_direction_len),
        static_cast<float>(static_cast<double>(target.y - muzzle.y) / expected_direction_len),
        static_cast<float>(static_cast<double>(target.z - muzzle.z) / expected_direction_len),
    };
    check(close(direction, expected_direction), "direction follows translated target to muzzle geometry");
}

void boundary_values()
{
    xr::Vec3f target{};
    xr::Vec3f direction{};
    check(bovr::input::vehicle_aim_convergence({0, 0, 0}, {0.5F, 0.0F, 0.0F}, {-1, 0, 0}, 1.0F, &target, &direction),
        "distance lower bound is accepted");
    check(close(target, {1.0F, 0.0F, 0.0F}), "distance lower bound target is geometric");

    check(bovr::input::vehicle_aim_convergence({0, 0, 0}, {0.0F, 2.0F, 0.0F}, {0.0F, 99999.0F, 0.0F}, 100000.0F,
        &target, &direction),
        "distance upper bound is accepted");
    check(close(static_cast<float>(length(target, {0.0F, 99999.0F, 0.0F})), 1.0F), "upper bound keeps target separated from muzzle");

    check(bovr::input::vehicle_aim_convergence({0, 0, 0}, {1.0F, 0.0F, 0.0F}, {1.999F, 0.0F, 0.0F}, 2.0F, &target, &direction),
        "target to muzzle minimum length boundary is accepted");
    check(close(static_cast<float>(length(target, {1.999F, 0.0F, 0.0F})), 0.001F), "target/muzzle minimum geometry is preserved");
}

void forward_and_distance_rejections()
{
    const xr::Vec3f eye{1.0F, 2.0F, 3.0F};
    const xr::Vec3f muzzle{4.0F, 5.0F, 6.0F};
    xr::Vec3f target{9.0F, 9.0F, 9.0F};
    xr::Vec3f direction{8.0F, 8.0F, 8.0F};
    const xr::Vec3f target_before{target};
    const xr::Vec3f direction_before{direction};

    check(!bovr::input::vehicle_aim_convergence(eye, {0.4F, 0.0F, 0.0F}, muzzle, 0.5F, &target, &direction),
        "distance below minimum is rejected");
    check(!bovr::input::vehicle_aim_convergence(eye, {0.4F, 0.0F, 0.0F}, muzzle, 100000.1F, &target, &direction),
        "distance above maximum is rejected");
    check(!bovr::input::vehicle_aim_convergence(eye, {0.4F, 0.0F, 0.0F}, muzzle, std::numeric_limits<float>::quiet_NaN(), &target, &direction),
        "nonfinite distance is rejected");

    check(!bovr::input::vehicle_aim_convergence(eye, {0.0F, 0.0F, 0.0F}, muzzle, 2.0F, &target, &direction),
        "zero forward vector is rejected");
    check(!bovr::input::vehicle_aim_convergence(eye, {2.1F, 0.0F, 0.0F}, muzzle, 2.0F, &target, &direction),
        "forward above maximum is rejected");
    check(!bovr::input::vehicle_aim_convergence(eye, {std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, muzzle, 2.0F, &target, &direction),
        "nonfinite forward component is rejected");
    check(!bovr::input::vehicle_aim_convergence(eye, {0.2F, 0.1F, 0.1F}, {1.0F, 2.0F, 3.0F}, 2.0F, &target, &direction),
        "nonfinite target-muzzle separation is rejected");

    const xr::Vec3f too_close_target{2.0F, 2.0F, 3.0F};
    check(!bovr::input::vehicle_aim_convergence(eye, {0.5F, 0.0F, 0.0F}, too_close_target, 1.0F, &target, &direction),
        "target/muzzle distance below minimum is rejected");
    check(close(target, target_before) && close(direction, direction_before), "invalid run leaves outputs untouched");
}

void bad_inputs_and_output_requirements()
{
    xr::Vec3f target{9.0F, 9.0F, 9.0F};
    xr::Vec3f direction{8.0F, 8.0F, 8.0F};
    const xr::Vec3f target_before{target};
    const xr::Vec3f direction_before{direction};

    check(!bovr::input::vehicle_aim_convergence({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
            {1.0F, 0.0F, 0.0F}, {1.0F, 2.0F, 3.0F}, 1.0F, &target, &direction),
        "nonfinite eye rejects computation");
    check(!bovr::input::vehicle_aim_convergence({1.0F, 2.0F, 3.0F},
            {1.0F, 0.0F, 0.0F}, {1.0F, std::numeric_limits<float>::infinity(), 3.0F}, 1.0F, &target, &direction),
        "nonfinite muzzle rejects computation");
    check(close(target, target_before) && close(direction, direction_before), "nonfinite inputs preserve output on failure");

    target = xr::Vec3f{7.0F, 7.0F, 7.0F};
    direction = xr::Vec3f{6.0F, 6.0F, 6.0F};
    check(!bovr::input::vehicle_aim_convergence({1.0F, 2.0F, 3.0F}, {1.0F, 0.0F, 0.0F},
            {4.0F, 5.0F, 6.0F}, 2.0F, nullptr, &direction),
        "null target output is rejected");
    check(close(direction, xr::Vec3f{6.0F, 6.0F, 6.0F}), "null target does not mutate direction");

    xr::Vec3f aliased{7.0F, 7.0F, 7.0F};
    check(!bovr::input::vehicle_aim_convergence({1.0F, 2.0F, 3.0F}, {1.0F, 0.0F, 0.0F},
            {4.0F, 5.0F, 6.0F}, 2.0F, &aliased, &aliased),
        "aliased outputs are rejected");
    check(close(aliased, xr::Vec3f{7.0F, 7.0F, 7.0F}), "aliased outputs do not mutate values");
}

} // namespace test

int main()
{
    test::translated_muzzle_convergence();
    test::perpendicular_nonunit_forward();
    test::vertical_and_negative_direction();
    test::boundary_values();
    test::forward_and_distance_rejections();
    test::bad_inputs_and_output_requirements();

    std::printf("checks=%u failures=%u\n", test::checks, test::failures);
    return static_cast<int>(test::failures);
}
