// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_checkpoint_lease.hpp"
#include <cstdio>
#include <initializer_list>

namespace lease=bovr::t5::checkpoint_lease;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* label) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}
}
constexpr lease::Key ak{0x29c69430,0x29cf3794,0x1c08b40,0x6500000,11,0xe81000,0xe91000,0,0,0};
// These role command times are deliberately independent of each other and of
// the input clock; ownership is recorded in each native time domain separately.
lease::Times times(std::int64_t game) {return {game-25,game-50,game};}
struct Run {
    lease::History history{};
    lease::Key key{ak};
    std::int64_t game{249300};
    std::uint64_t clock{900000},wall{1000};
    explicit Run(lease::Key selected=ak,std::int64_t initial_game=249300,
                 std::uint64_t initial_clock=900000,std::uint64_t initial_wall=1000):
        key(selected),game(initial_game),clock(initial_clock),wall(initial_wall) {
        history.observe_lifecycle(lease::identity(key),game,clock,true,false,wall);
    }
    void sample(std::int64_t next,std::uint64_t elapsed=25,bool dead=false,bool connected=true) {
        game=next;clock+=elapsed;wall+=elapsed;
        history.observe_lifecycle(lease::identity(key),game,clock,connected,dead,wall);
    }
    void own() {check(history.record_owned(key,times(game),clock,wall),"fresh owned sample is recorded");}
    void own_to(std::int64_t end) {
        own();
        while(game<end){const auto step=end-game<25?end-game:25;sample(game+step,static_cast<std::uint64_t>(step));own();}
    }
    void die(bool retire_first=false) {
        if(retire_first)history.retire();
        sample(game+25,25,true);
        if(!retire_first)history.retire();
    }
    void rewind(std::int64_t restored=249300,std::uint64_t wait=6000) {sample(restored,wait);}
    bool claim(bool normal=true,bool bit=true) {return history.claim(key,times(game),clock,wall,normal,bit);}
    void ready() {own_to(249500);die();rewind();}
};

void observed_ak_retry() {
    for(bool retire_first:{false,true}) {
        Run r(ak,249300,935736-6975,256876828-6975);
        // The owned AK interval precedes the death at native time 256275.
        r.own_to(256250);r.die(retire_first);
        // Exact observed clock domains from the user trace. Command/input time
        // advances while the native game clock rewinds to its RAM checkpoint.
        check(r.clock==935736&&r.wall==256876828,"recorded AK death has exact independent input and wall stamps");
        r.game=249300;r.clock=941834;r.wall=256882968;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        // Pre55649 ->55650 ->55651 ->55652. Only the last one is normal.
        r.game=249316;r.clock=941850;r.wall=256882973;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        r.game=249347;r.clock=941881;r.wall=256882978;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        r.game=249363;r.clock=941898;r.wall=256882984;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        check(r.claim(),"recorded AK death/rewind admits first normal after three loading observations");
        check(!r.claim(),"successful recovery is consumed exactly once");
    }
}

void lifecycle_refusals() {
    {Run r;check(!r.claim(),"unknown startup bit has no ownership proof");}
    {Run r;r.own_to(249500);r.rewind();check(!r.claim()&&r.history.spans()==0,"rewind without observed death clears history");}
    {Run r;r.own_to(249500);r.sample(249300,25,true);check(!r.claim(),"death first reported at rewind cannot manufacture an arm");}
    {Run r;r.own_to(249500);r.history.retire();r.sample(249525);r.sample(249550,25,true);r.rewind();
        check(!r.claim(),"menu or rejected healthy observation between ownership and death prevents arming");}
    {Run r;r.own_to(249500);r.die();r.rewind(249300,lease::History::death_window_ms+1);
        check(!r.claim(),"wall-aged death arm cannot authorize a later load");}
    {Run r;r.ready();check(!r.claim(false)&&!r.claim(),"first nonnormal decision consumes the recovery arm");}
    {Run r;r.ready();check(!r.claim(true,false)&&!r.claim(),"clean native bit needs no recovery and consumes the arm");}
    {Run r;r.ready();r.sample(249316,16,false,false);r.sample(249332,16);
        check(!r.claim()&&r.history.spans()==0,"connection drop clears old history even if pointers return");}
    for(unsigned member=0;member<3;++member) {
        Run r;r.ready();
        if(member==0)r.key.cg+=0x1000;else if(member==1)r.key.server+=0x1000;else r.key.world+=0x1000;
        r.sample(249316,16);
        check(!r.claim()&&r.history.spans()==0,"cg server or native world replacement clears history");
    }
    {Run r;r.ready();--r.clock;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        check(!r.claim()&&r.history.spans()==0,"input-clock rollback is not a permitted game-time rewind");}
    {Run r;r.ready();--r.wall;
        r.history.observe_lifecycle(lease::identity(r.key),r.game,r.clock,true,false,r.wall);
        check(!r.claim()&&r.history.spans()==0,"wall-clock rollback clears all ownership proof");}
    {Run r;r.ready();r.sample(249299,1);
        check(!r.claim()&&r.history.spans()==0,"a second native rewind cannot reuse the first death arm");}
    {Run r;r.ready();check(r.claim(),"initial recovery accepted before repeated restore test");
        r.sample(249400);r.sample(249300);check(!r.claim(),"another restore needs new owned healthy-to-death evidence");}
}

void exact_key_and_roles() {
    for(unsigned member=0;member<10;++member) {
        Run r;r.ready();auto other=r.key;
        switch(member) {
        case 0:other.cg+=4;break;case 1:other.predicted+=4;break;case 2:other.server+=4;break;
        case 3:other.world+=4;break;case 4:++other.weapon;break;case 5:other.variant+=4;break;
        case 6:other.definition+=4;break;case 7:other.partner=12;break;
        case 8:other.pairvariant=0x1234;break;case 9:other.pairdef=0x1234;break;
        }
        check(!r.history.claim(other,times(r.game),r.clock,r.wall,true,true),"restored key must match every owned identity component");
        check(!r.claim(),"wrong first key cannot catch up to a later matching weapon");
    }
    for(unsigned member=0;member<4;++member) {
        Run r;r.ready();auto t=times(r.game);
        if(member==0)--t.predicted;else if(member==1)--t.server;
        else if(member==2)t.predicted=249501;else t.server=249501;
        check(!r.history.claim(r.key,t,r.clock,r.wall,true,true),"both independent role times must lie within the same prior span");
    }
    {Run r;r.ready();auto t=times(r.game);t.predicted=static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())+1;
        check(!r.history.claim(r.key,t,r.clock,r.wall,true,true),"out-of-range native role time cannot be history evidence");}
    {Run r;r.ready();auto t=times(r.game);--t.native_game;
        check(!r.history.claim(r.key,t,r.clock,r.wall,true,true),"claim must use the current lifecycle native game stamp");}
    {Run r;r.ready();check(!r.history.claim(r.key,times(r.game),r.clock+1,r.wall,true,true),"claim input occurrence clock must remain exact");}
    auto dual=ak;dual.partner=12;dual.pairvariant=0xea1000;dual.pairdef=0xeb1000;
    {Run r(dual);r.ready();check(r.claim(),"paired weapon ownership may recover only as its complete exact key");}
    {auto shared=dual;shared.pairdef=shared.definition;Run r(shared);r.ready();
        check(r.claim(),"pure history preserves exact shared-definition identity without overriding native pair admission");}
    for(unsigned member=0;member<3;++member) {
        Run r(dual);r.ready();
        if(member==0)++r.key.partner;else if(member==1)r.key.pairvariant+=4;else r.key.pairdef+=4;
        check(!r.claim(),"partner number variant and definition changes independently reject recovery");
    }
    {auto optional=ak;optional.world=0;Run r(optional);r.ready();check(r.claim(),"optional absent map identity still requires all other exact identities");}
    {Run r(ak,200097,154950);r.own_to(200197);r.die();r.rewind(200097);
        check(r.claim(),"observed Khe Sanh game200097 above input154950 is valid across independent clock origins");}
}

void interval_and_freshness() {
    {Run r;r.own_to(249400);r.history.retire();r.sample(249500);r.own_to(249600);r.die();r.rewind(249450);
        check(r.history.spans()==2&&!r.claim(),"retired ownership intervals cannot merge across a gap");
        r.sample(249500,25);check(!r.claim(),"later catchup into an owned interval cannot revive rejected rewind");}
    {Run r;r.own_to(249400);r.sample(249450);r.sample(249500);r.own_to(249600);r.die();r.rewind(249450);
        check(r.history.spans()==2&&!r.claim(),"a missed record closes the continuous interval without explicit retire");}
    {Run r;r.own_to(249400);r.sample(249500,101);r.own_to(249600);r.die();r.rewind(249450);
        check(r.history.spans()==2&&!r.claim(),"long ownership sampling gap cannot become a wide native-time interval");}
    {Run r;r.own_to(249400);++r.key.weapon;r.sample(249500);r.own_to(249600);r.die();r.rewind(249350);
        check(r.history.spans()==2&&!r.claim(),"different weapon intervals cannot lend their time range to current key");}
    {Run r;r.ready();r.sample(249316,16);r.sample(249332,16);r.sample(249348,16);r.sample(249364,16);
        check(!r.claim(),"four following observations exceed the proven short restore handoff");}
    {Run r;r.ready();r.wall+=101;
        check(!r.claim(),"claim cannot outlive the original rewind wall age without lifecycle observations");}
    {Run r;r.ready();r.sample(249401,16);check(!r.claim(),"native-time catchup beyond100ms expires even under a short wall interval");}
    {Run r;r.own_to(249500);r.die();r.rewind(249250);r.sample(249300,25);
        check(!r.claim(),"first rewind outside ownership remains invalid even after native time catches up inside");}
    {Run r;r.ready();auto t=times(r.game);
        check(!r.history.claim(r.key,t,r.clock,r.wall-1,true,true),"claim cannot predate its observed lifecycle");}
    {Run r;r.own_to(249500);r.die();r.rewind(249501);
        check(!r.claim(),"restored time just outside the owned interval is not rounded into it");}
}

void bounded_storage_and_recording() {
    {Run r;r.own();check(!r.history.record_owned(r.key,times(r.game),r.clock,r.wall),"duplicate occurrence cannot create extra history");}
    {Run r;r.own_to(249400);r.sample(249425);auto t=times(r.game);t.server=249000;
        check(!r.history.record_owned(r.key,t,r.clock,r.wall)&&r.history.spans()==0,"role-clock rollback invalidates continuous history");}
    {Run r;r.own_to(249400);r.die();
        check(!r.history.record_owned(r.key,times(r.game),r.clock,r.wall),"dead observations cannot record ownership");}
    {Run r;r.ready();check(!r.history.record_owned(r.key,times(r.game),r.clock,r.wall),"unclaimed recovery cannot manufacture owned history");}
    for(bool newest:{false,true}) {
        Run r;
        for(std::size_t i=0;i<=lease::History::capacity;++i) {
            if(i)r.sample(r.game+25);
            r.own();r.history.retire();
        }
        const auto last=r.game;r.die();r.rewind(newest?last:249300);
        check(r.history.spans()==lease::History::capacity,"ownership history has fixed bounded capacity");
        check(r.claim()==newest,"overflow evicts only oldest exact span without merging or rejecting retained newest proof");
    }
    {Run r;r.ready();r.history.reset();check(!r.claim()&&r.history.spans()==0,"explicit map/load retirement clears history and arm");}
}
}
int main() {
    observed_ak_retry();lifecycle_refusals();exact_key_and_roles();interval_and_freshness();bounded_storage_and_recording();
    std::printf("manual_reload_checkpoint_lease_checks=%u failures=%u native_calls=0 native_writes=0\n",checks,failures);
    return failures?1:0;
}
