// SPDX-License-Identifier: GPL-3.0-only
#include "input/motorcycle_router.hpp"
#include "input/vehicle_look_delta.hpp"
#include <cstdio>

namespace {
using namespace bovr::input;
namespace xr=wawvr::xr;
unsigned checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
bool held(const MotorcycleOutput& o,MotorcycleAction a){return o.held[static_cast<unsigned>(a)];}
xr::ActionSnapshot neutral(){
    xr::ActionSnapshot s{};s.focused=true;s.sequence=1;
    for(auto& h:s.hands){
        h.grip.active=h.grip.orientation_valid=h.grip.position_valid=true;
        h.grip.orientation_tracked=h.grip.position_tracked=true;
        h.trigger.active=h.trigger_click.active=h.squeeze.active=h.stick.active=true;
        h.primary.active=h.secondary.active=true;
    }
    return s;
}
struct Fixture{
    MotorcycleRouter router{};xr::ActionSnapshot s=neutral();std::uint64_t now=1000;
    MotorcycleOutput fresh(bool active=true){++s.sequence;now+=10;return router.update(s,active,now);}
    MotorcycleOutput duplicate(bool active=true){++now;return router.update(s,active,now);}
};
}

int main(){
    {
        Fixture f;f.fresh();
        f.s.hands[0].stick.current={.6f,.8f};auto o=f.fresh();
        check(o.valid&&held(o,MotorcycleAction::gas)&&!held(o,MotorcycleAction::reverse),"forward stick owns native gas");
        check(o.drive>.5f&&o.steer>.3f,"forward and steering magnitudes survive deadzone mapping");
        f.s.hands[0].stick.current={.6f,-.8f};o=f.fresh();
        check(!held(o,MotorcycleAction::gas)&&held(o,MotorcycleAction::reverse),"back stick balances gas and owns reverse");
        f.s.hands[0].stick.current={};o=f.fresh();
        check(!held(o,MotorcycleAction::gas)&&!held(o,MotorcycleAction::reverse)&&o.drive==0&&o.steer==0,"center releases drive commands");
    }
    {
        Fixture f;f.fresh();
        f.s.hands[1].trigger.current=1;auto o=f.fresh();
        check(held(o,MotorcycleAction::attack),"right trigger routes vehicle primary fire");
        check(held(o,MotorcycleAction::script_attack),"right trigger also supplies ordinary attack for motorcycle script and truck gunner");
        f.s.hands[0].trigger.current=1;o=f.fresh();
        check(held(o,MotorcycleAction::attack)&&held(o,MotorcycleAction::attack_second),"left trigger independently routes secondary fire");
        f.s.hands[1].primary.current=true;f.s.hands[1].squeeze.current=1;f.s.hands[0].primary.current=true;o=f.fresh();
        check(held(o,MotorcycleAction::handbrake)&&held(o,MotorcycleAction::reload)&&held(o,MotorcycleAction::use),"A grip and X route vehicle utilities");
        f.s.focused=false;o=f.duplicate();
        bool any{};for(bool b:o.held)any=any||b;
        check(!o.valid&&!any&&o.drive==0&&o.steer==0,"focus loss releases every motorcycle control");
    }
    {
        Fixture f;f.s.hands[0].stick.current={0,1};auto o=f.fresh();
        check(held(o,MotorcycleAction::gas)&&o.drive==1,"fresh entry-held analog throttle drives immediately");
        f.s.hands[0].stick.current={};f.fresh();f.s.hands[0].stick.current={0,1};o=f.fresh();
        check(held(o,MotorcycleAction::gas)&&o.drive==1,"fresh center rearms full throttle");
        f.s.hands[0].stick.current={};o=f.duplicate();
        check(!held(o,MotorcycleAction::gas)&&o.drive==0,"changed duplicate may release but cannot rearm");
    }
    {
        Fixture f;f.fresh();
        f.s.hands[0].stick.current={.6f,.8f};
        f.s.hands[1].trigger.current=1;f.fresh();
        auto o=f.fresh(false);
        check(!o.valid&&o.drive==0&&!held(o,MotorcycleAction::attack),"missing landing snapshot releases movement and fire");
        o=f.duplicate();
        check(o.drive==0&&o.steer==0&&!held(o,MotorcycleAction::gas),"reentry cannot reacquire movement from a duplicate");
        o=f.fresh();
        check(o.drive>.5f&&o.steer>.3f&&held(o,MotorcycleAction::gas),"fresh held stick resumes driving and steering after landing gap");
        check(!held(o,MotorcycleAction::attack)&&!held(o,MotorcycleAction::script_attack),"both fire paths stay disarmed across the same gap");
        f.s.hands[1].trigger.current=0;f.fresh();
        f.s.hands[1].trigger.current=1;o=f.fresh();
        check(held(o,MotorcycleAction::attack),"fresh trigger release and press restores motorcycle shooting");
        f.router.reset();o=f.duplicate();
        check(o.drive==0&&!held(o,MotorcycleAction::gas),"explicit reset preserves duplicate watermark");
        o=f.fresh();
        check(o.drive>.5f&&held(o,MotorcycleAction::gas),"fresh stick recovers after explicit adapter reset");
        f.s.focused=false;o=f.fresh();
        check(!o.valid&&o.drive==0&&!held(o,MotorcycleAction::gas),"fresh analog input cannot bypass focus loss");
        f.s.focused=true;f.s.hands[0].grip.position_tracked=false;o=f.fresh();
        check(!o.valid&&o.drive==0,"fresh analog input cannot bypass tracking loss");
    }
    {
        VehicleLook look;VehicleLookDelta delta{};
        check(!look.update(10,5,179,&delta),"first tracked orientation calibrates without snapping native aim");
        check(look.update(11,15,-176,&delta)&&delta.pitch==10&&delta.yaw==5,"physical pitch and wraparound yaw produce one-for-one mouse-like aim");
        check(!look.update(11,15,-176,&delta)&&delta.pitch==0&&delta.yaw==0,"duplicate XR samples cannot repeatedly turn the weapon");
        check(look.update(12,15,-176,&delta)&&delta.pitch==0&&delta.yaw==0,"steady controller adds no camera feedback or aim drift");
        look.reset();
        check(!look.update(13,-30,20,&delta),"reentry calibrates a moved controller without a jump");
        check(!look.update(2,40,70,&delta),"rewound sequence establishes a new baseline without moving aim");
    }
    std::printf("motorcycle_router_checks=%u failures=%u native_commands=0\n",checks,failures);
    return failures?1:0;
}
