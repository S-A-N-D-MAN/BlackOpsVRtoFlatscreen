// SPDX-License-Identifier: GPL-3.0-only
#include "input/helicopter_axes.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
unsigned checks{};
unsigned failures{};
void check(bool ok,const char* label) {
    ++checks;
    if(!ok){++failures;printf("FAIL %s\n",label);}
}
bool approx(float lhs,float rhs) { return std::fabs(lhs-rhs)<=1e-6F; }
}

int main() {
    using bovr::input::helicopter_axes;
    {
        const auto a = helicopter_axes(0.0F,0.0F,0.0F,0.0F,true);
        check(a.valid&&a.forward==0.0F&&a.right==0.0F&&a.yaw==0.0F&&a.pitch==0.0F,"neutral axes remain centered and valid");
    }
    {
        const auto a = helicopter_axes(0.20F, -0.20F, 0.20F, -0.20F, true);
        check(a.valid && a.forward == 0.0F && a.right == 0.0F && a.yaw == 0.0F && a.pitch == 0.0F,
              "deadzone boundary inputs return zero");
    }
    {
        const auto a = helicopter_axes(0.6F, 0.6F, -0.6F, -0.6F, true);
        check(approx(a.forward,0.5F) && approx(a.right,0.5F) && approx(a.yaw,-0.5F) && approx(a.pitch,-0.5F),
              ".6 sticks linearly remap to .5 with sign preserved");
    }
    {
        const auto a = helicopter_axes(-3.0F, 2.0F, 1.0F, -1.0F, true);
        check(a.valid && approx(a.forward,1.0F) && approx(a.right,-1.0F) && approx(a.yaw,1.0F) && approx(a.pitch,-1.0F),
              "axes clamp to [-1,1] and stay valid");
    }
    {
        const auto a = helicopter_axes(std::numeric_limits<float>::quiet_NaN(),0.5F,0.4F,0.5F,true);
        const auto b = helicopter_axes(0.5F,std::numeric_limits<float>::infinity(),0.4F,0.5F,true);
        check(!a.valid && a.forward==0.0F && a.right==0.0F && a.yaw==0.0F && a.pitch==0.0F
             && !b.valid && b.forward==0.0F && b.right==0.0F && b.yaw==0.0F && b.pitch==0.0F,
              "nonfinite input disables output and marks invalid");
    }
    {
        const auto a = helicopter_axes(0.9F,-0.9F,-0.9F,0.3F,false);
        check(!a.valid && a.forward==0.0F && a.right==0.0F && a.yaw==0.0F && a.pitch==0.0F,"inactive sample never publishes axes");
    }
    printf("helicopter_axes_checks=%u failures=%u native_commands=0\n",checks,failures);
    return failures?1:0;
}
