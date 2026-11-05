// SPDX-License-Identifier: GPL-3.0-only
#include "input/predicted_shot_feedback.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace bovr::input;
namespace {
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
PredictedFireSample sample(unsigned sequence=1,std::uint64_t time=1000) {
    PredictedFireSample s{};s.valid=true;s.generation=1;s.epoch=2;s.player=3;s.weapon=32;
    s.occurrence=time;s.observed_ms=s.published_ms=time;s.native_time=static_cast<int>(time);s.sequence=sequence;return s;
}
void next(PredictedFireSample& s,unsigned event,std::uint64_t time) {
    s.events[s.sequence&3]=static_cast<int>(event);s.sequence=(s.sequence+1)&255;
    s.occurrence=time;s.observed_ms=s.published_ms=time;s.native_time=static_cast<int>(time);
}
}
int main(){
    {
        PredictedShotFeedback p;auto s=sample();s.dual=true;p.observe(s,1000);
        next(s,29,1001);next(s,31,1002);next(s,32,1003);auto r=p.observe(s,1003);
        check(r.shots==3&&r.hand_shots[0]==2&&r.hand_shots[1]==1,"dual events route rumble to each firing hand");
        check(!p.observe(s,1003).shots,"dual batch cannot replay");
        s.dual=false;next(s,31,1004);check(!p.observe(s,1004).shots,"dual identity change silently rebaselines");
        next(s,32,1005);check(!p.observe(s,1005).shots,"left events cannot pulse ordinary weapon");
    }
    {
        PredictedShotFeedback p;auto s=sample();s.events[0]=29;
        check(p.observe(s,1000).shots==0,"initial ring never replays old fire");
        next(s,29,1001);check(p.observe(s,1001).shots==1,"native ordinary fire accepted once");
        check(std::fabs(p.kickback(1,2,32,1001)-.018F)<.000001F,"kick is bounded 18mm");
        check(p.observe(s,1001).shots==0,"same callback cannot replay fire");
        s.occurrence=1002;s.observed_ms=s.published_ms=1002;
        check(p.observe(s,1002).shots==0,"new command same ring emits no fire");
        const auto half=p.kickback(1,2,32,1061);
        check(half>0&&half<.018F,"kick returns smoothly without changing an aim axis");
        check(p.kickback(1,2,32,1121)==0,"expired kick cannot persist");
        check(p.kickback(2,2,32,1003)==0&&p.kickback(1,3,32,1003)==0&&p.kickback(1,2,14,1003)==0,
            "generation epoch and weapon mismatch cannot inherit recoil");
    }
    {
        PredictedShotFeedback p;auto s=sample(254);p.observe(s,1000);
        next(s,29,1001);check(p.observe(s,1001).shots==1,"prewrap shot accepted");
        next(s,29,1002);check(p.observe(s,1002).shots==1,"255 to zero wraps at eight bits");
        next(s,30,1003);check(p.observe(s,1003).shots==1,"last burst round accepted without trigger input");
        auto rollback=s;rollback.sequence=255;rollback.occurrence=1004;rollback.observed_ms=rollback.published_ms=1004;
        check(p.observe(rollback,1004).shots==0,"prediction backward ring cannot pulse");
        s.occurrence=1005;s.observed_ms=s.published_ms=1005;
        check(p.observe(s,1005).shots==0,"prediction recovery cannot replay prior shot");
        next(s,29,1006);check(p.observe(s,1006).shots==1,"new postrollback shot recovers");
        rollback=s;rollback.sequence=1;rollback.native_time=999;rollback.occurrence=1007;
        rollback.observed_ms=rollback.published_ms=1007;
        check(!p.observe(rollback,1007).shots,"native time and event rollback emits nothing");
        s.occurrence=1008;s.observed_ms=s.published_ms=1008;
        check(!p.observe(s,1008).shots,"native time and ring recovery cannot duplicate shot");
        next(s,29,1009);check(p.observe(s,1009).shots==1,"new event recovers after combined rollback");
    }
    {
        PredictedShotFeedback p;auto s=sample();p.observe(s,1000);
        next(s,29,1001);next(s,23,1002);next(s,30,1003);next(s,31,1004);
        check(p.observe(s,1004).shots==2,"four entry batch counts only ordinary right fire");
        for(unsigned event:{23u,31u,32u,47u,49u,50u,0u}) {
            next(s,event,s.occurrence+1);
            check(p.observe(s,s.occurrence).shots==0,"reload offhand dual and unrelated events stay silent");
        }
        for(unsigned i=0;i<5;++i)next(s,29,1020+i);
        auto missed=p.observe(s,1024);check(missed.reset&&missed.shots==0,"ring overflow drops unprovable shots");
        next(s,29,1025);check(p.observe(s,1025).shots==1,"overflow rebaselines then resumes");
    }
    for(unsigned change=0;change<10;++change) {
        PredictedShotFeedback p;auto s=sample();p.observe(s,1000);next(s,29,1001);p.observe(s,1001);
        next(s,29,1002);
        switch(change){
        case 0:s.valid=false;break;case 1:s.generation=2;break;case 2:s.epoch=3;break;
        case 3:s.player=4;break;case 4:s.weapon=14;break;case 5:s.published_ms=1;break;
        case 6:s.native_time=999;break;case 7:s.sequence=256;break;
        case 8:s.observed_ms=1;break;case 9:s.observed_ms=s.published_ms=s.occurrence=1200;break;
        }
        const auto now=change==9?1200u:1002u;const auto r=p.observe(s,now);
        check(!r.shots&&r.reset,"invalid changed stale or regressed source drops feedback");
        check(p.kickback(s.generation,s.epoch,s.weapon,now)==0,"rejected source clears recoil");
    }
    {
        PredictedShotFeedback p;auto s=sample();p.observe(s,1000);next(s,29,1001);p.observe(s,1001);
        p.invalidate();check(p.kickback(1,2,32,1002)==0,"owner reset clears recoil immediately");
        next(s,29,1003);check(p.observe(s,1003).shots==0,"focus or tracking recovery establishes silent baseline");
        next(s,29,1004);check(p.observe(s,1004).shots==1,"new fire after recovery works");
    }
    std::printf("predicted_shot_feedback_checks=%u failures=0 native_calls=0 xr_calls=0\n",checks);
}
