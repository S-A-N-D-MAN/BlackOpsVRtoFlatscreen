// SPDX-License-Identifier: GPL-3.0-only
#include "input/shot_haptics.hpp"
#include <iostream>
#include <thread>
#include <atomic>
using namespace bovr::input;
unsigned checks{},failures{};
void check(bool ok,const char* name) {++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
HapticContext context(std::uint64_t now=1000,std::uint64_t sequence=1) {
    return {2,7,sequence,now,true,true,{true,true}};
}
ConfirmedLocalShot shot(std::uint64_t serial=1,std::uint64_t now=1001,unsigned hand=1) {
    return {2,7,serial,now,hand,true};
}
int main() {
    ShotHaptics h;
    check(!h.queue(shot(),1001),"unarmed rejects");
    check(h.service(context(),1000).shots[1]==0,"service never invents a shot");
    auto dry=shot();dry.local_shot_confirmed=false;
    check(!h.queue(dry,1001),"trigger or dry fire excluded");
    auto ai=shot();ai.gameplay_epoch=8;
    check(!h.queue(ai,1001),"wrong native lifetime excluded");
    check(h.queue(shot(),1001),"confirmed local shot accepted");
    check(!h.queue(shot(),1001),"duplicate native dispatch rejected");
    check(!h.queue(shot(1,1001,0),1001),"same shot cannot switch hands");
    auto p=h.service(context(1002,2),1002);
    check(p.shots[1]==1 && !p.shots[0],"pulse only firing hand");
    check(p.amplitude>0 && p.amplitude<=1 && p.duration_seconds<=.05f,"bounded pulse");
    check(h.service(context(1002,2),1003).shots[1]==0,"drain once including duplicate publication");
    check(h.queue(shot(2,1004,0),1004),"other hand separate pulse");
    auto lost=context(1005,3);lost.focused=false;
    p=h.service(lost,1005);
    check(p.stop_mask==2 && p.shots[0]==0,"focus loss stops active and discards pending");
    check(!h.queue(shot(3,1006),1006),"no queue while unfocused");
    check(h.service(context(1010,4),1010).shots[0]==0,"focus recovery no replay");
    check(!h.queue(shot(4,1009),1010),"old shot predating new permission rejected");
    check(h.queue(shot(4,1011),1011),"new shot after recovery");
    auto gen=context(1012,5);gen.xr_generation=3;
    check(h.service(gen,1012).shots[1]==0,"reset drops pending old generation");
    check(!h.queue(shot(5,1013),1013),"old generation producer rejected");
    h.reset();h.service(context(2000,1),2000);
    check(!h.queue(shot(10,1999),2001),"event before publication rejected");
    check(!h.queue(shot(10,2002),2001),"future event rejected");
    check(h.queue(shot(10,2001),2001),"valid event before hand tracking loss");
    auto tracking=context(2002,2);tracking.tracked[1]=false;
    check(h.service(tracking,2002).shots[1]==0,"tracking loss drops pending");
    check(!h.queue(shot(11,2003),2003),"untracked firing hand excluded");
    h.reset();h.service(context(3000,1),3000);
    check(h.queue(shot(20,3001),3001),"queue before expiry");
    check(h.service(context(3000,1),3101).shots[1]==0,"duplicate cannot refresh old source");
    check(!h.queue(shot(21,3102),3102),"expired permission stays disarmed");
    h.service(context(4000,1),4000);h.queue(shot(30,4001),4001);
    check(h.service(context(4102,2),4102).shots[1]==0,"long service gap drops batch");
    h.queue(shot(31,4103),4103);
    auto back=context(4104,1);
    check(h.service(back,4104).shots[1]==0,"backwards sequence disarms");
    h.service(context(5000,1),5000);h.queue(shot(40,5001),5001);
    check(h.service(context(5000,1),4999).shots[1]==0,"backwards clock disarms");
    h.reset();h.service(context(6000,1),6000);
    for(unsigned i=1;i<=64;++i) check(h.queue(shot(100+i,6001),6001),"bounded burst accepted/coalesced");
    check(h.service(context(6002,2),6002).shots[1]==32,"burst has one capped batch");
    check(h.service(context(6003,3),6003).shots[1]==0,"no burst replay");
    check(h.reset().stop_mask==2 && h.reset().stop_mask==0,"reset stop occurs once");
    // Exercise the actual cross-thread mailbox with a single monotonic producer.
    h.service(context(7000,1),7000);std::atomic<unsigned> queued{};
    std::thread producer([&]{for(unsigned i=1;i<=20;++i) if(h.queue(shot(200+i,7001),7001)) ++queued;});
    producer.join();p=h.service(context(7002,2),7002);
    check(queued==20 && p.shots[1]==20,"native producer to XR consumer mailbox");
    h.reset();h.service(context(7002,2),7002);
    check(!h.queue(shot(220,7002),7002),"reset at same clock cannot replay a native shot");
    h.queue(shot(221,7003),7003);
    check(h.service(context(7004,2),7004).shots[1]==0,"same sequence with rewritten age is invalid");
    std::cout<<checks-failures<<'/'<<checks<<" checks passed\n";
    return failures?1:0;
}
