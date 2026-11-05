// SPDX-License-Identifier: GPL-3.0-only
#include "input/action_router.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
namespace xr=wawvr::xr;
using namespace bovr::input;
constexpr std::array<Action,5> monitor_actions{Action::fire,Action::ads,Action::use,Action::jump,Action::melee};
unsigned checks{},failures{};
void check(bool ok,const char* label) {
    ++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}
}
bool edge(const Output& out,Action action,bool down=true) {
    unsigned count{};
    for(unsigned i=0;i<out.edge_count;++i)if(out.edges[i].action==action&&out.edges[i].pressed==down)++count;
    return count==1;
}
bool all_edges(const Output& out,bool down) {
    if(out.edge_count!=monitor_actions.size())return false;
    for(auto action:monitor_actions)if(!edge(out,action,down))return false;
    return true;
}
bool no_weapon_requests(const Output& out) {
    if(out.weapon_next||out.weapon_alternate||out.lower_stance||out.turn!=0||out.look_edge_count)return false;
    for(unsigned i=0;i<out.edge_count;++i)if(out.edges[i].pressed) {
        const auto action=out.edges[i].action;
        if(action!=Action::fire&&action!=Action::ads&&action!=Action::use&&action!=Action::jump&&action!=Action::melee)return false;
    }
    return true;
}
struct Fixture {
    ActionRouter router;
    xr::ActionSnapshot sample{};
    std::uint64_t now{1000};
    unsigned dominant{},support{};
    explicit Fixture(bool left=false,bool cod4=true):router(left,cod4),dominant(left?0u:1u),support(dominant^1u) {
        sample.focused=true;sample.sequence=1;sample.menu.active=true;
        for(auto& hand:sample.hands) {
            hand.grip.active=hand.grip.orientation_valid=hand.grip.position_valid=true;
            hand.grip.orientation_tracked=hand.grip.position_tracked=true;
            hand.trigger.active=hand.stick.active=hand.primary.active=hand.secondary.active=true;
            hand.squeeze.active=hand.stick_click.active=true;
        }
    }
    Output step(Context context=Context::aircraft_monitor) {
        ++sample.sequence;now+=16;
        // Even accidental gameplay-only permissions cannot change this role.
        return router.update(sample,context,now,true,true);
    }
    void set(Action action,bool held) {
        switch(action) {
        case Action::fire:sample.hands[dominant].trigger.current=held?1.f:0.f;break;
        case Action::ads:sample.hands[support].trigger.current=held?1.f:0.f;break;
        case Action::use:sample.hands[support].primary.current=held;break;
        case Action::jump:sample.hands[dominant].primary.current=held;break;
        case Action::melee:sample.hands[dominant].secondary.current=held;break;
        default:break;
        }
    }
    void all(bool held) {for(auto action:monitor_actions)set(action,held);}
    void rearm() {all(false);sample.hands[support].stick.current={};step();all(true);}
};
}

int main() {
    for(bool left:{false,true})for(bool cod4:{false,true}) {
        for(auto action:monitor_actions) {
            Fixture f(left,cod4);f.step();f.set(action,true);auto out=f.step();
            check(out.edge_count==1&&edge(out,action),"each hand layout delivers its independently assigned native action");
            check(no_weapon_requests(out),"monitor action has no gameplay-only requests");
            check(f.step().edge_count==0,"held monitor button never repeats a press");
            check(f.router.update(f.sample,Context::aircraft_monitor,f.now,true,true).edge_count==0,
                  "duplicate XR publication never repeats a press");
            f.set(action,false);out=f.step();
            check(out.edge_count==1&&edge(out,action,false),"assigned action releases exactly its own lease");
        }
        {
            Fixture f(left,cod4);f.step();f.all(true);
            for(auto& hand:f.sample.hands){hand.secondary.current=hand.stick_click.current=true;hand.squeeze.current=1;}
            f.sample.hands[f.dominant].stick.current={0,1};
            auto out=f.step();
            check(all_edges(out,true)&&no_weapon_requests(out),"simultaneous commands remain independent and excluded buttons stay silent");
            f.set(Action::ads,false);out=f.step();
            check(out.edge_count==1&&edge(out,Action::ads,false),"releasing ADS does not release troop command or attack");
            f.set(Action::jump,false);out=f.step();
            check(out.edge_count==1&&edge(out,Action::jump,false),"releasing troop command does not release mouse buttons");
        }
        {
            Fixture f(left,cod4);f.step();f.sample.hands[f.dominant].stick.current={0,1};
            check(f.step().edge_count==0,"right stick up cannot accidentally command troops");
            f.sample.hands[f.dominant].stick.current={};
            f.sample.hands[f.support].stick.current={.6f,.8f};auto out=f.step();
            check(std::fabs(out.forward-.8f)<.00001f&&std::fabs(out.right-.6f)<.00001f&&out.turn==0,
                  "support stick keeps native monitor navigation axes");
            f.sample.hands[f.support].grip.pose.orientation={0,.70710678f,0,.70710678f};
            f.sample.hands[f.dominant].grip.pose.orientation={0,1,0,0};
            out=f.step();
            check(std::fabs(out.forward-.8f)<.00001f&&std::fabs(out.right-.6f)<.00001f,
                  "controller orientation does not rotate monitor navigation");
            f.sample.hands[f.support].stick.current.x=std::numeric_limits<float>::quiet_NaN();
            check(f.step().forward==0,"nonfinite monitor navigation is rejected");
            f.sample.hands[f.support].stick.current={0,1};check(f.step().forward==0,"axis recovery while deflected requires neutral");
            f.sample.hands[f.support].stick.current={};f.step();f.sample.hands[f.support].stick.current={0,1};
            check(f.step().forward==1,"fresh neutral restores monitor navigation");
        }
        {
            Fixture f(left,cod4);f.step();f.sample.hands[f.dominant].stick.current={.6f,.8f};
            auto out=f.step();
            check(std::fabs(out.forward-.8f)<.00001f&&std::fabs(out.right-.6f)<.00001f&&out.turn==0,
                  "dominant stick supplies native movement axes consumed by the monitor camera script");
            check(out.edge_count==0&&no_weapon_requests(out),"camera motion cannot command troops or request gameplay turning");
            f.sample.hands[f.dominant].stick.current.y=std::numeric_limits<float>::infinity();out=f.step();
            check(out.forward==0&&out.right==0,"invalid camera axis stops both camera outputs");
            f.sample.hands[f.dominant].stick.current={0,1};check(f.step().forward==0,"held camera axis requires neutral after invalidation");
            f.sample.hands[f.dominant].stick.current={};f.step();f.sample.hands[f.dominant].stick.current={0,1};
            check(f.step().forward==1,"neutral rearms monitor camera motion");
            f.sample.menu.current=true;out=f.step();check(out.forward==0&&out.right==0,"pause edge stops monitor camera axes");
            f.sample.menu.current=false;check(f.step().forward==0,"resume cannot restore a held camera axis");
        }
        {
            Fixture f(left,cod4);f.step();
            f.sample.hands[f.support].stick.current={-.6f,-.8f};
            f.sample.hands[f.dominant].stick.current={.6f,.8f};auto out=f.step();
            check(std::fabs(out.forward-.8f)<.00001f&&std::fabs(out.right-.6f)<.00001f,
                  "deflected dominant camera stick takes precedence over support navigation");
            f.sample.hands[f.dominant].stick.current={};out=f.step();
            check(std::fabs(out.forward+.8f)<.00001f&&std::fabs(out.right+.6f)<.00001f,
                  "centered dominant stick leaves support navigation available");
            f.sample.hands[f.dominant].stick.active=false;f.sample.hands[f.dominant].stick.current={.6f,.8f};out=f.step();
            check(std::fabs(out.forward+.8f)<.00001f&&std::fabs(out.right+.6f)<.00001f,
                  "inactive dominant stick cannot override valid support navigation");
            f.sample.hands[f.dominant].stick.active=true;out=f.step();
            check(std::fabs(out.forward+.8f)<.00001f&&std::fabs(out.right+.6f)<.00001f,
                  "reactivated deflected stick stays disarmed while support navigation remains usable");
            f.sample.hands[f.dominant].stick.current={};f.step();
            f.sample.hands[f.dominant].stick.current={.6f,.8f};out=f.step();
            check(std::fabs(out.forward-.8f)<.00001f&&std::fabs(out.right-.6f)<.00001f,
                  "fresh dominant neutral restores its camera precedence");
        }
        {
            Fixture f(left,cod4);f.step();f.all(true);f.sample.hands[f.support].stick.current={0,1};f.step();
            f.sample.menu.current=true;const auto out=f.step();
            check(out.menu_toggle&&all_edges(out,false)&&out.forward==0,"pause edge immediately balances every held monitor command");
            check(!f.step().menu_toggle,"held pause does not toggle the menu repeatedly");
            f.sample.menu.current=false;
            check(f.step().edge_count==0&&f.step().forward==0,"resume with held controls cannot replay command or cursor motion");
            f.rearm();check(all_edges(f.step(),true),"neutral after pause rearms each command");
        }
        for(unsigned invalid=0;invalid<7;++invalid) {
            Fixture f(left,cod4);f.step();f.all(true);f.step();Output out{};
            switch(invalid) {
            case 0:f.sample.focused=false;out=f.step();f.sample.focused=true;break;
            case 1:f.sample.hands[f.dominant].grip.orientation_tracked=false;out=f.step();
                   f.sample.hands[f.dominant].grip.orientation_tracked=true;break;
            case 2:f.sample.hands[f.support].grip.position_tracked=false;out=f.step();
                   f.sample.hands[f.support].grip.position_tracked=true;break;
            case 3:out=f.step(Context::suspended);break;
            case 4:out=f.step(Context::menu);break;
            case 5:out=f.router.release_all();break; // Adapter generation invalidation.
            case 6:f.now+=251;out=f.router.expire(f.now);break;
            }
            check(all_edges(out,false),"focus tracking pause role generation or expiry loss balances all leases");
            check(f.step().edge_count==0&&f.step().edge_count==0,"recovered held buttons cannot bypass neutral rearm");
            f.rearm();check(all_edges(f.step(),true),"valid neutral restores controls after invalidation");
        }
        {
            Fixture f(left,cod4);f.step();f.set(Action::fire,true);f.step();
            f.sample.hands[f.dominant].trigger.current=std::numeric_limits<float>::quiet_NaN();
            auto out=f.step();check(out.edge_count==1&&edge(out,Action::fire,false),"nonfinite trigger immediately releases its action");
            f.set(Action::fire,true);check(f.step().edge_count==0,"invalid trigger cannot rearm while held");
            f.set(Action::fire,false);f.step();f.set(Action::fire,true);
            check(edge(f.step(),Action::fire),"trigger neutral restores its action");
        }
    }
    for(bool left:{false,true}) {
        Fixture f(left);f.step(Context::scripted_linked);f.set(Action::ads,true);
        auto out=f.step(Context::scripted_linked);
        check(out.edge_count==1&&edge(out,Action::ads),"existing car linked trigger remains ADS without troop command");
        f.set(Action::jump,true);out=f.step(Context::scripted_linked);
        check(out.edge_count==0,"ordinary linked role still rejects jump and reload");
        f.all(false);f.step(Context::gameplay);f.set(Action::jump,true);out=f.step(Context::gameplay);
        check(out.edge_count==1&&edge(out,Action::reload),"ordinary COD4 A remains reload");
        f.all(false);f.step(Context::gameplay);f.set(Action::ads,true);out=f.step(Context::gameplay);
        check(edge(out,Action::jump)&&!edge(out,Action::ads),"ordinary COD4 support trigger remains jump");
    }
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
