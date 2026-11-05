// SPDX-License-Identifier: GPL-3.0-only
#include "input/turn_controller.hpp"
#include <cstdio>
#include <limits>
int main() {
    unsigned failures{};
    auto check=[&](bool value,const char* label) { if(!value) { ++failures; printf("FAIL %s\n",label); } };
    bovr::input::TurnController turn;
    check(turn.update(1,100)==0,"startup held stick cannot snap");
    check(turn.update(0,110)==0,"neutral arms without turning");
    check(turn.update(1,120)==-30,"right stick turns engine yaw right");
    check(turn.update(1,130)==0 && turn.update(-1,140)==0,"held or reversed stick cannot repeat without neutral");
    turn.update(0,150);
    check(turn.update(-1,160)==30,"left snap after neutral");
    turn.reset(); check(turn.update(1,170)==0,"focus/reset requires neutral");
    turn.update(0,180); turn.update(std::numeric_limits<float>::quiet_NaN(),190);
    check(turn.update(1,200)==0,"invalid sample disarms snapping");
    turn.update(0,210);
    check(turn.update(1,200)==0,"clock regression disarms held stick");
    turn.reset(); turn.update(0,1000,bovr::input::TurnMode::smooth);
    check(turn.update(1,1025,bovr::input::TurnMode::smooth)==-3,"smooth turn integrates elapsed seconds");
    check(turn.update(1,2025,bovr::input::TurnMode::smooth)==-6,"long gap clamps rotation step");
    turn.reset();turn.update(0,3000,bovr::input::TurnMode::smooth,45.f,90.f);
    check(std::fabs(turn.update(1,3020,bovr::input::TurnMode::smooth,45.f,90.f)+1.8f)<.00001f,"launch option smooth speed is90 degrees per second");
    check(std::fabs(turn.update(-.5f,3040,bovr::input::TurnMode::smooth,45.f,90.f)-.9f)<.00001f,"smooth turn preserves proportional opposite direction");
    const float observed_native_yaw=-578.05f,observed_smooth_step=-1.44f;
    const float continuous=bovr::input::apply_turn_delta(observed_native_yaw,observed_smooth_step,bovr::input::TurnMode::smooth);
    check(std::fabs((continuous-observed_native_yaw)-observed_smooth_step)<.0001f&&continuous<-579.f,
          "smooth turn preserves unbounded native yaw without a full-revolution jump");
    check(std::fabs(bovr::input::apply_turn_delta(observed_native_yaw,-45.f,bovr::input::TurnMode::snap)-96.95f)<.0001f,
          "snap turn retains established bounded yaw write");
    turn.reset();check(turn.update(1,3060,bovr::input::TurnMode::smooth,45.f,90.f)==0,"smooth loss recovery also requires neutral");
    turn.update(0,3080,bovr::input::TurnMode::snap,45.f,90.f);
    check(turn.update(1,3100,bovr::input::TurnMode::snap,45.f,90.f)==-45.f,"default COD4 snap remains45 degrees");
    printf("turn_controller_failures=%u\n",failures); return failures?1:0;
}
