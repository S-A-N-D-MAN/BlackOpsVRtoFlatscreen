// SPDX-License-Identifier: GPL-3.0-only
#include "input/action_router.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
namespace xr=wawvr::xr;
using namespace bovr::input;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* label) {++checks;if(!ok){++failures;printf("FAIL %s\n",label);}}
xr::ActionSnapshot neutral() {
    xr::ActionSnapshot s{};s.sequence=1;s.focused=true;s.menu.active=true;
    for(auto& h:s.hands) {
        h.grip.active=h.grip.orientation_valid=h.grip.position_valid=h.grip.orientation_tracked=h.grip.position_tracked=true;
        h.trigger.active=h.stick.active=h.primary.active=h.secondary.active=h.stick_click.active=true;
    }
    return s;
}
bool edge(const Output& out,Action action,bool pressed) {
    for(unsigned i=0;i<out.edge_count;++i)if(out.edges[i].action==action&&out.edges[i].pressed==pressed)return true;
    return false;
}
}
int main() {
    {
        ActionRouter router;auto s=neutral();s.hands[1].trigger.active=false;
        router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].trigger.active=true;s.hands[1].trigger.current=1;
        check(!edge(router.update(s,Context::gameplay,2),Action::fire,true),"inactive trigger is not an arming neutral");
        ++s.sequence;s.hands[1].trigger.current=0;router.update(s,Context::gameplay,3);
        ++s.sequence;s.hands[1].trigger.current=1;
        check(edge(router.update(s,Context::gameplay,4),Action::fire,true),"active neutral then fresh trigger press arms");
        ++s.sequence;s.hands[1].trigger.current=std::numeric_limits<float>::quiet_NaN();
        check(edge(router.update(s,Context::gameplay,5),Action::fire,false),"invalid held trigger releases native ownership");
        ++s.sequence;s.hands[1].trigger.current=1;
        check(!edge(router.update(s,Context::gameplay,6),Action::fire,true),"finite held recovery requires a real neutral");
    }
    {
        ActionRouter router;auto s=neutral();router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].primary.current=true;
        check(edge(router.update(s,Context::gameplay,2),Action::use,true),"use control presses when armed");
        ++s.sequence;s.hands[1].primary.active=false;
        check(edge(router.update(s,Context::gameplay,3),Action::use,false),"inactive use action releases");
        ++s.sequence;s.hands[1].primary.active=true;
        check(!edge(router.update(s,Context::gameplay,4),Action::use,true),"active-but-held use recovery remains disarmed");
    }
    for(bool inactive:{false,true}) {
        ActionRouter router;auto s=neutral();router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[0].stick.current={0,1};auto move=router.update(s,Context::gameplay,2);
        check(move.forward==1,"armed movement reaches full scale");
        const auto duplicate=router.update(s,Context::gameplay,3);
        check(duplicate.forward==move.forward&&duplicate.right==move.right&&duplicate.edge_count==0,
              "duplicate valid XR sample retains analog axes without edges");
        ++s.sequence;if(inactive)s.hands[0].stick.active=false;else s.hands[0].stick.current.x=std::numeric_limits<float>::infinity();
        check(router.update(s,Context::gameplay,4).forward==0,"invalid movement immediately zeroes output");
        ++s.sequence;s.hands[0].stick.active=true;s.hands[0].stick.current={0,1};
        check(router.update(s,Context::gameplay,5).forward==0,"held movement cannot rearm from invalid data");
        ++s.sequence;s.hands[0].stick.current={};router.update(s,Context::gameplay,6);
        ++s.sequence;s.hands[0].stick.current={0,1};
        check(router.update(s,Context::gameplay,7).forward==1,"active centered recovery rearms movement");
    }
    {
        ActionRouter router;auto s=neutral();router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].stick.current={1,0};check(router.update(s,Context::gameplay,2).turn==1,"turn arms from valid center");
        ++s.sequence;s.hands[1].stick.active=false;router.update(s,Context::gameplay,3);
        ++s.sequence;s.hands[1].stick.active=true;
        check(router.update(s,Context::gameplay,4).turn==0,"held turn recovery stays disarmed");
    }
    {
        ActionRouter router;auto s=neutral();s.menu.active=false;router.update(s,Context::gameplay,1);
        ++s.sequence;s.menu.active=s.menu.current=true;
        check(!router.update(s,Context::gameplay,2).menu_toggle,"inactive menu action is not neutral");
    }
    {
        ActionRouter router;auto s=neutral();router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].trigger.current=1;s.hands[0].stick.current={0,1};router.update(s,Context::gameplay,2);
        s.hands[0].grip.position_tracked=false; // Explicit invalidation of same cached sequence.
        const auto out=router.update(s,Context::gameplay,3);
        check(edge(out,Action::fire,false)&&out.forward==0,"same-sequence tracking invalidation bypasses no safety gate");
    }
    {
        ActionRouter router;auto s=neutral();router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].trigger.current=1;router.update(s,Context::gameplay,2);
        check(edge(router.update(s,Context::gameplay,253),Action::fire,false),"duplicate sample watchdog releases after250ms");
        check(router.update(s,Context::gameplay,254).edge_count==0,"expired sequence cannot refresh its own timestamp");
        ++s.sequence;check(!edge(router.update(s,Context::gameplay,255),Action::fire,true),"held fresh sample after expiry still requires neutral");
    }
    {
        ActionRouter router(false,true);auto s=neutral();
        router.update(s,Context::gameplay,1);
        ++s.sequence;s.hands[1].trigger.current=1;
        router.update(s,Context::gameplay,2);
        ++s.sequence;s.hands[0].primary.current=true;
        auto out=router.update(s,Context::scripted_use,3);
        check(edge(out,Action::fire,false)&&!edge(out,Action::use,true),
              "frozen struggle entry releases fire and requires neutral use before a new press");
        ++s.sequence;s.hands[0].primary.current=false;router.update(s,Context::scripted_use,4);
        for(unsigned tap=0;tap<6;++tap) {
            ++s.sequence;s.hands[0].primary.current=true;
            s.hands[0].secondary.current=s.hands[0].stick_click.current=true;
            // Right-stick melee is script input too. Hold it across the X taps
            // to prove one independent lease, with no repeated melee edges.
            s.hands[1].primary.current=s.hands[1].secondary.current=s.hands[1].stick_click.current=true;
            s.hands[0].stick.current={1,1};s.hands[1].stick.current={1,1};s.hands[0].trigger.current=0;
            out=router.update(s,Context::scripted_use,5+tap*4,true,true);
            check(out.edge_count==(tap==0?2u:1u)&&edge(out,Action::use,true)&&edge(out,Action::melee,true)==(tap==0)&&
                  out.forward==0&&out.right==0&&out.turn==0&&
                  !out.weapon_next&&!out.weapon_alternate&&!out.lower_stance&&out.look_edge_count==0,
                  "each fresh X tap admits use independently of one held melee lease while firing and movement stay blocked");
            check(router.update(s,Context::scripted_use,6+tap*4,true,true).edge_count==0,
                  "duplicate struggle sample cannot repeat use press");
            ++s.sequence;check(router.update(s,Context::scripted_use,7+tap*4).edge_count==0,
                  "holding X has no synthetic repeat or release");
            ++s.sequence;s.hands[0].primary.current=false;
            out=router.update(s,Context::scripted_use,8+tap*4);
            check(out.edge_count==1&&edge(out,Action::use,false),"release balances each struggle X tap");
        }
        ++s.sequence;s.hands[0].primary.current=true;router.update(s,Context::scripted_use,40);
        ++s.sequence;out=router.update(s,Context::gameplay,41);
        check(out.edge_count==2&&edge(out,Action::use,false)&&edge(out,Action::melee,false)&&out.forward==0&&out.turn==0,
              "struggle exit balances both use and melee without carrying held buttons or movement into gameplay");
        ++s.sequence;out=router.update(s,Context::gameplay,42);
        check(out.edge_count==0&&out.forward==0&&out.turn==0,"still-held gameplay controls require neutral after struggle");
    }
    {
        ActionRouter router(false,true);auto s=neutral();router.update(s,Context::vehicle_weapon,1);
        ++s.sequence;s.hands[1].trigger.current=1;s.hands[0].trigger.current=1;
        s.hands[0].stick.current={1,1};s.hands[1].stick.current={1,1};
        s.hands[0].primary.current=s.hands[0].secondary.current=true;
        s.hands[1].primary.current=s.hands[1].secondary.current=true;
        auto out=router.update(s,Context::vehicle_weapon,2,true,true);
        check(out.edge_count==2&&edge(out,Action::fire,true)&&edge(out,Action::use,true)&&
              out.forward==0&&out.right==0&&out.turn==0&&!out.weapon_next&&!out.weapon_alternate&&
              !out.lower_stance&&out.look_edge_count==0,
              "vehicle weapon context admits only dominant fire and X use");
        ++s.sequence;out=router.update(s,Context::gameplay,3);
        check(edge(out,Action::fire,false)&&edge(out,Action::use,false)&&out.forward==0&&out.turn==0,
              "vehicle weapon exit balances owned buttons and requires neutral ordinary controls");
    }
    printf("input_transition_checks=%u failures=%u native_commands=0\n",checks,failures);
    return failures?1:0;
}
