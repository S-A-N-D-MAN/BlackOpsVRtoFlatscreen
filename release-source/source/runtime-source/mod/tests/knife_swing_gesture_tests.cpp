// SPDX-License-Identifier: GPL-3.0-only
#include "input/knife_swing_gesture.hpp"
#include "t5/primary_weapon_variant.hpp"
#include <cstdlib>
#include <iostream>

namespace xr = wawvr::xr;
namespace input = bovr::input;
namespace {
int failures{};
void check(bool value,const char* label){if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
xr::ActionSnapshot sample(std::uint64_t sequence,float x){xr::ActionSnapshot a{};a.focused=true;a.sequence=sequence;auto& p=a.hands[1].grip;p.active=p.position_valid=p.position_tracked=true;p.pose.position={x,0,0};return a;}
}
int main(){
    check(bovr::t5::primary_knife_variant("vorkuta_knife_sp","WEAPON_KNIFE_PRISON"),"Vorkuta AK-based knife variant enters gesture mode");
    check(bovr::t5::primary_knife_variant("knife_sp"),"ordinary knife variant enters gesture mode");
    check(bovr::t5::primary_knife_variant("scripted_item","WEAPON_KNIFE_PRISON"),"localized knife identity is sufficient");
    check(!bovr::t5::primary_knife_variant("ak47_sp"),"ordinary rifle is not a knife");
    check(!bovr::t5::primary_knife_variant("ak47_bayonet_sp"),"bayonet rifle keeps trigger fire");
    check(!bovr::t5::primary_knife_variant("ballistic_knife_sp","WEAPON_BALLISTIC_KNIFE"),"ballistic knife keeps ranged trigger fire");
    check(!bovr::t5::primary_knife_variant("knife_launcher","WEAPON_KNIFE_LAUNCHER"),"knife launcher keeps ranged trigger fire");
    input::KnifeSwingGesture g;
    check(!g.update(sample(1,0),1000,true).pulse,"first tracked sample seeds history");
    check(!g.update(sample(2,.002f),1020,true).pulse,"slow motion arms without attacking");
    const auto swing=g.update(sample(3,.032f),1040,true);
    check(swing.pulse&&swing.speed_meters_per_second>=1.25f,"deliberate right-controller swing attacks once");
    check(!g.update(sample(3,.032f),1040,true).pulse,"duplicate XR publication cannot repeat an attack");
    check(!g.update(sample(4,.062f),1060,true).pulse,"continued fast motion stays disarmed");
    check(!g.update(sample(5,.063f),1080,true).pulse,"slow sample rearms after the swing");
    check(!g.update(sample(6,.093f),1100,true).pulse,"cooldown blocks an immediate second swing");
    check(!g.update(sample(7,.094f),1400,true).pulse,"stale sample gap resets instead of attacking");
    check(!g.update(sample(8,.095f),1420,true).pulse,"fresh slow sample rearms after reset");
    check(g.update(sample(9,.125f),1440,true).pulse,"new swing attacks after cooldown and rearm");
    auto lost=sample(10,.125f);lost.hands[1].grip.position_tracked=false;
    check(!g.update(lost,1460,true).pulse,"tracking loss clears the gesture");
    check(!g.update(sample(11,.5f),1480,true).pulse,"tracking reacquisition seeds without attack");
    check(!g.update(sample(12,.9f),1500,true).pulse,"implausible discontinuity cannot attack");
    auto unfocused=sample(13,.9f);unfocused.focused=false;
    check(!g.update(unfocused,1520,true).pulse,"focus loss clears the gesture");
    check(!g.update(sample(14,.9f),1540,false).pulse,"disabled knife mode cannot attack");
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
