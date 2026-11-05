// SPDX-License-Identifier: GPL-3.0-only
#include "t5/render_producer_pacing.hpp"
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace bovr::t5::producer_pacing;
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
Sample example(){
    Sample s{};s.copied=true;s.now_ms=150;
    s.context={0x1000,0x400000,0x7a17de,0x2000,0x3000,0x4000,11,22,102,0,0,5,7,1,9,30,100,100,true,true,true,false};
    s.pixels={9,50,70,0x5000,101,true,true,true,false,false,false,false};
    s.pending={9,50,140,80,90,70,0x5000,0x5000,101,3,true,false,false,false,true,false,true,true,false,false,true,true,false};
    return s;
}
Sample in_flight(const Sample& s){auto n=s;n.flight={17,s.pending};n.pending={};
    n.pending.initialized=true;n.pending.busy=true;return n;}
Sample successor(const Sample& s){auto n=s;n.pending={};n.pending.initialized=true;n.pending.pending=true;
    n.flight={};n.pending.generation=9;n.pending.frame=51;n.pending.published_ms=150;n.pending.render=true;n.pending.views=true;return n;}
template<class Change>void reject(Change change){auto s=example();change(s);check(admit(s)!=Reason::admitted,"unsafe initial sample rejected");}
int main(){
    const auto initial=example();check(admit(initial)==Reason::admitted,"complete exact prior pair admitted");
    auto reused=initial;reused.context.frontend=reused.pixels.buffer;
    reused.context.native_frame=reused.pixels.native_frame+2;
    check(admit(reused)==Reason::admitted,"exact two-frame ring reuse of copied completed HUD pair admitted");
    for(unsigned delta=0;delta<=6;++delta)for(bool same:{false,true}){
        auto s=initial;s.context.native_frame=s.pixels.native_frame+delta;
        s.context.frontend=same?s.pixels.buffer:initial.context.frontend;
        const bool expected=(delta==1&&!same)||(delta==2&&same);
        check((admit(s)==Reason::admitted)==expected,"only immediate alternate or exact second same ring frame admitted");
    }
    auto edge=reused;edge.pixels.native_frame=UINT32_MAX-2;edge.pending.reserved_frame=edge.pixels.native_frame;
    edge.context.native_frame=UINT32_MAX;check(admit(edge)==Reason::admitted,"two-frame boundary without wrap accepted");
    edge.pixels.native_frame=UINT32_MAX-1;edge.pending.reserved_frame=edge.pixels.native_frame;
    edge.context.native_frame=0;check(admit(edge)==Reason::successor,"two-frame unsigned wrap rejected");
    edge=reused;edge.pending={};edge.pending.initialized=true;edge.pending.busy=true;
    check(admit(edge)==Reason::coordinator,"ring reuse does not admit an unproven initial submit/begin gap");
    edge=reused;edge.pixels.completed=false;check(admit(edge)==Reason::pixels,"ring reuse still requires completed pixels");
    edge=reused;edge.pending.producer++;check(admit(edge)==Reason::prior,"ring reuse still requires exact coordinator producer");
    edge=reused;edge.pending.reserved_frame++;check(admit(edge)==Reason::prior,"ring reuse retains exact reserved old native frame");
    edge=reused;edge.context.view_count=1;check(admit(edge)==Reason::successor,"reused ring must still be empty");
    edge=reused;edge.context.parms_count=1;check(admit(edge)==Reason::successor,"reused ring view parms must still be empty");
    check(step(reused,reused)==Reason::admitted,"exact reused ring proof survives repeated copy");
    edge=successor(reused);check(step(reused,edge)==Reason::ready,"reused ring stops only for fresh pristine next XR ticket");
    edge=reused;edge.context.native_frame++;check(step(reused,edge)==Reason::changed,"native frame advance cancels reused-ring wait");
    reject([](Sample& s){s.copied=false;});reject([](Sample& s){s.context.thread=s.context.owner;});
    reject([](Sample& s){s.context.thread=0;});reject([](Sample& s){s.context.known_caller=false;});
    reject([](Sample& s){s.context.caller++;});reject([](Sample& s){s.context.recursive=true;});
    reject([](Sample& s){s.context.ordinary=false;});reject([](Sample& s){s.context.live=false;});
    reject([](Sample& s){s.context.epoch=0;});reject([](Sample& s){s.context.cg=0;});
    reject([](Sample& s){s.context.command_sequence=0;});reject([](Sample& s){s.context.generation++;});
    reject([](Sample& s){s.context.frontend=s.pixels.buffer;});reject([](Sample& s){s.context.native_frame--;});
    reject([](Sample& s){s.context.view_count=1;});reject([](Sample& s){s.context.parms_count=1;});
    reject([](Sample& s){s.pixels.valid=false;});reject([](Sample& s){s.pixels.completed=false;});
    reject([](Sample& s){s.pixels.hud=false;});reject([](Sample& s){s.pixels.menu=true;});
    reject([](Sample& s){s.pixels.auxiliary=true;});reject([](Sample& s){s.pixels.cinematic=true;});
    reject([](Sample& s){s.pixels.thermal=true;});reject([](Sample& s){s.pixels.producer++;});
    reject([](Sample& s){s.pixels.native_frame=UINT32_MAX;s.context.native_frame=0;});
    reject([](Sample& s){s.pending.eye_mask=1;});reject([](Sample& s){s.pending.claimed=false;});
    reject([](Sample& s){s.pending.staged=false;});reject([](Sample& s){s.pending.reservation=0;});
    reject([](Sample& s){s.pending.build=0;});reject([](Sample& s){s.pending.reserved_frame++;});
    reject([](Sample& s){s.pending.backend_buffer++;});reject([](Sample& s){s.pending.invalid=true;});
    reject([](Sample& s){s.pending.busy=true;});reject([](Sample& s){s.pending.reset=true;});
    reject([](Sample& s){s.pending.faulted=true;});reject([](Sample& s){s.pending.awaiting=true;});
    reject([](Sample& s){s.pending.pending=false;});reject([](Sample& s){s.pending.views=false;});
    reject([](Sample& s){s.pending.render=false;});reject([](Sample& s){s.now_ms=139;});
    reject([](Sample& s){s.now_ms=241;});
    const auto flight=in_flight(reused);
    check(admit(flight)==Reason::admitted,"finish-issued exact old proof admits empty busy interval");
    check(step(flight,flight)==Reason::admitted,"same finish operation continues bounded wait");
    check(step(reused,flight)==Reason::admitted,"old pending transitions only to matching finish proof");
    check(step(flight,successor(flight))==Reason::ready,"fresh next ticket can end finish wait without gaining authority");
    auto flight_bad=flight;flight_bad.flight.serial++;
    check(step(flight,flight_bad)==Reason::changed,"changed operation serial cancels");
    flight_bad=flight;flight_bad.flight={};
    check(admit(flight_bad)!=Reason::admitted&&step(flight,flight_bad)==Reason::changed,"proof retirement cancels despite generic busy");
    flight_bad=flight;flight_bad.pending.busy=false;
    check(admit(flight_bad)!=Reason::admitted&&step(flight,flight_bad)==Reason::changed,"idle never extends submitting proof");
    flight_bad=flight;flight_bad.flight.completed.producer++;
    check(admit(flight_bad)!=Reason::admitted&&step(flight,flight_bad)==Reason::changed,"foreign flight producer rejected");
    flight_bad=flight;flight_bad.flight.completed.reserved_frame++;
    check(admit(flight_bad)!=Reason::admitted,"foreign reserved native frame rejected");
    flight_bad=flight;flight_bad.flight.completed.eye_mask=1;
    check(admit(flight_bad)!=Reason::admitted,"incomplete archived eyes rejected");
    flight_bad=flight;flight_bad.flight.completed.invalid=true;
    check(admit(flight_bad)!=Reason::admitted,"invalid archived ticket rejected");
    flight_bad=flight;flight_bad.now_ms=241;
    check(admit(flight_bad)!=Reason::admitted,"original flight publication freshness retained");
    flight_bad=flight;flight_bad.pending.reset=true;
    check(admit(flight_bad)!=Reason::admitted&&step(flight,flight_bad)==Reason::coordinator,"reset cancels retained flight");
    flight_bad=flight;flight_bad.context.generation++;
    check(admit(flight_bad)!=Reason::admitted&&step(flight,flight_bad)==Reason::changed,"new lifecycle cannot inherit flight");
    check(flight.flight.completed==reused.pending,"copied proof leaves original pending metadata unchanged");
    check(step(initial,initial)==Reason::admitted,"same completed prior waits");
    auto changed=initial;changed.pending={};changed.pending.initialized=true;changed.pending.busy=true;
    check(step(initial,changed)==Reason::changed,"generic busy cannot inherit prior completed-pair proof");
    changed.pending.busy=false;check(step(initial,changed)==Reason::changed,"idle missing ticket fails open");
    changed=successor(initial);check(step(initial,changed)==Reason::ready,"fresh pristine successor ends wait only");
    changed.pending.frame++;check(step(initial,changed)==Reason::changed,"skipped frame is ambiguous");
    changed=successor(initial);changed.pending.reservation=1;check(step(initial,changed)==Reason::changed,"already reserved successor fails open");
    changed=successor(initial);changed.pending.awaiting=true;check(step(initial,changed)==Reason::changed,"deferred successor untouched");
    changed=initial;changed.context.generation++;check(step(initial,changed)==Reason::changed,"lifecycle generation cancels during empty gap too");
    changed=initial;changed.context.command_sequence++;check(step(initial,changed)==Reason::changed,"new command epoch cancels");
    changed=initial;changed.context.menu_revision++;check(step(initial,changed)==Reason::changed,"menu change cancels");
    changed=initial;changed.context.frontend++;check(step(initial,changed)==Reason::changed,"native buffer change cancels");
    changed=initial;changed.context.owner++;check(step(initial,changed)==Reason::changed,"owner replacement cancels");
    changed=initial;changed.pixels.producer++;check(step(initial,changed)==Reason::changed,"pixels replacement cancels");
    unsigned reads{},yields{};bool locked=false;
    auto clock=[]()->std::int64_t{return 100;};
    auto read=[&](Sample* s){locked=true;++reads;*s=reads>1?successor(initial):initial;locked=false;return true;};
    auto yield=[&]{check(!locked,"no copied metadata lock spans yield");++yields;};
    auto result=run(initial,false,1000000,clock,read,yield);
    check(result.reason==Reason::diagnostic&&!reads&&!yields,"diagnostic-only mode never yields or reads repeatedly");
    result=run(initial,true,1000000,clock,read,yield);
    check(result.reason==Reason::ready&&reads==2&&yields==1,"ready exits after one bounded yield without claiming ticket");
    reads=yields=0;result=run(initial,true,1000000,clock,[&](Sample* s){++reads;*s=initial;return true;},yield);
    check(maximum_yields==2048&&result.reason==Reason::iterations&&reads==maximum_yields&&yields==maximum_yields,"stalled QPC bounded by explicit2048 iteration cap");
    std::int64_t ticks=100;reads=yields=0;
    result=run(initial,true,1000000,[&]{const auto t=ticks;ticks+=500;return t;},[&](Sample* s){++reads;*s=initial;return true;},yield);
    check(result.reason==Reason::timeout&&result.elapsed_ticks>=2000&&yields<=2,"all copying/yield time counts against deadline");
    ticks=100;result=run(initial,true,1000000,[&]{return --ticks;},read,yield);
    check(result.reason==Reason::clock&&!result.yields,"backwards clock fails open before yield");
    result=run(initial,true,0,clock,read,yield);check(result.reason==Reason::clock,"invalid clock frequency fails open");
    result=run(initial,true,1000000,clock,[](Sample*){return false;},yield);
    check(result.reason==Reason::copy&&!result.yields,"copy failure fails open without yield");
    check(initial.pending==example().pending&&initial.pixels==example().pixels,
        "policy does not mutate pending ownership or completed pixel evidence");
    std::printf("producer_pacing_checks=%u failures=%u native_calls=0 reservations=0\n",checks,failures);
    return failures?1:0;
}
