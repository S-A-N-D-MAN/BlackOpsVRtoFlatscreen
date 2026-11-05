// SPDX-License-Identifier: GPL-3.0-only
#include "input/manual_reload.hpp"
#include "input/two_hand_aim.hpp"
#include "xr_math.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include <cmath>
namespace m=bovr::input::manual;
namespace xr=wawvr::xr;
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Fixture {
    m::Controller control;xr::FrameState f{};m::Context c{};m::Output out{};
    Fixture(bool empty=false,bool left=false){
        c.valid=true;c.left_handed=left;c.weapon=32;c.clip=empty?0:20;c.reserve=120;c.capacity=30;c.epoch=c.generation=1;c.now=1000;
        f.views_valid=true;f.actions.focused=true;f.head_center.position={0,1.7f,0};
        for(auto& h:f.actions.hands){h.grip.active=h.grip.position_valid=h.grip.position_tracked=h.grip.orientation_valid=h.grip.orientation_tracked=true;
            h.grip.pose.position={.2f,1.3f,-.35f};h.aim=h.grip;h.secondary.active=h.squeeze.active=h.trigger.active=true;}
        tick();
    }
    xr::HandActionState& main(){return f.actions.hands[c.left_handed?0:1];}
    xr::HandActionState& off(){return f.actions.hands[c.left_handed?1:0];}
    void tick(){++f.actions.sequence;c.now+=16;out=control.update(f,c);}
    void begin(){main().secondary.current=true;tick();main().secondary.current=false;tick();}
    void grab(){off().grip.pose.position=out.supply;off().squeeze.current=.8f;tick();}
    void insert(){off().grip.pose.position=out.receiver;tick();off().squeeze.current=0;tick();}
    void charge(float distance=.08f){off().grip.pose.position=out.handle;off().trigger.current=1;tick();off().grip.pose.position.z+=distance;tick();off().trigger.current=0;tick();}
    void mechanism(m::ReloadKind kind,unsigned rounds,bool charge_empty=false,bool cycle=false){
        c.weapon=55;c.mechanism={true,kind,rounds,charge_empty,cycle};tick();}
    void pump_mechanism(){c.weapon=55;c.mechanism={true,m::ReloadKind::internal_rounds,1,true,true};
        c.mechanism.grip_pump=true;c.mechanism.handle_offset={0,-.03f,-.19f};tick();}
    void pump_position(float rearward=0){const auto delta=xr::Rotate(main().aim.pose.orientation,{0,0,rearward});
        off().grip.pose.position={out.handle.x+delta.x,out.handle.y+delta.y,out.handle.z+delta.z};}
    void pump(float distance=.08f){pump_position();off().squeeze.current=.8f;tick();pump_position(distance);tick();pump_position();tick();}
    void ack(){const int amount=static_cast<int>(out.commit_rounds);check(amount>0,"ack requires a bounded physical request");
        c.clip+=amount;c.reserve-=amount;c.commit_completed=true;tick();c.commit_completed=false;tick();}
};
void pump_controls(){
    for(bool left:{false,true}){
        Fixture t(false,left);t.c.clip=t.c.capacity=5;t.c.reserve=9;t.off().squeeze.current=.8f;t.pump_mechanism();
        t.pump_position();t.tick();check(t.out.pump_gripped&&!t.out.cycle_pending,"held shotgun grip attaches visual hand before a shot or pump stroke");
        --t.c.clip;t.tick();
        check(t.out.cycle_pending&&t.out.block_attack&&!t.out.held_device,"shot with already-held support grip starts only a pump gate");
        check(t.out.pump_gripped,"busy pump gate retains the authored grip instead of releasing visual support");
        for(unsigned i=0;i<5;++i)t.tick();
        check(t.out.cycle_pending&&!t.out.request_commit,"stationary held grip cannot cycle or load ammunition");
        t.pump(.03f);check(t.out.cycle_pending,"short rearward-return stroke cannot cycle pump");
        // Moving the complete gun/hand pair does not move its sliding action.
        t.main().grip.pose.position.x+=.2f;t.off().grip.pose.position.x+=.2f;t.tick();
        check(t.out.cycle_pending,"common hand and weapon translation cannot fake a pump");
        t.pump_position(.08f);t.tick();
        check(t.out.cycle_pending&&t.out.charge_fraction>=.9f&&t.out.block_attack,"full rearward grip stroke remains blocked until return");
        check(t.out.pump_gripped,"authored visual hand remains attached at full rear stroke");
        for(unsigned i=0;i<3;++i)t.tick();
        check(t.out.cycle_pending,"holding the pump rearward cannot chamber by elapsed frames");
        t.pump_position();t.out=t.control.update(t.f,t.c);
        check(t.out.cycle_pending&&t.out.charge_fraction>=.9f,"same action sequence cannot supply unobserved return or reset accepted pump pose");
        t.tick();check(!t.out.cycle_pending&&!t.out.block_attack&&!t.out.request_commit&&t.c.clip==4&&t.c.reserve==9&&t.off().squeeze.current>.65f,
            "fresh complete pump returns ready with grip still held and unchanged ammunition");
        check(t.out.pump_gripped&&t.out.charge_fraction==0,"completed return retains visual grip at forward pump position");
        for(unsigned i=0;i<3;++i)t.tick();check(!t.out.cycle_pending,"held grip cannot replay a completed pump");
        --t.c.clip;t.tick();t.off().squeeze.current=0;t.tick();
        check(!t.out.pump_gripped&&t.out.cycle_pending,"grip release detaches visual hand without clearing required cycle");t.charge();
        check(t.out.cycle_pending,"left trigger alone cannot operate a grip-classified shotgun pump");
        t.pump_position();t.off().squeeze.current=.8f;t.tick();t.pump_position(.08f);t.tick();t.off().squeeze.current=0;t.tick();
        t.pump_position();t.off().squeeze.current=.8f;t.tick();
        check(t.out.cycle_pending,"releasing an opened pump cancels stroke instead of completing old trigger semantics");
        t.pump();check(!t.out.cycle_pending,"fresh back-and-return stroke succeeds after interrupted grip");
    }
    {
        Fixture t;t.c.clip=4;t.c.capacity=5;t.c.reserve=9;t.pump_mechanism();--t.c.clip;t.tick();
        t.off().grip.pose.position=t.out.supply;t.off().squeeze.current=.8f;t.tick();
        check(t.out.cycle_pending&&!t.out.held_device,"hip grip during pending pump is neither a stroke nor ammo pickup");
        check(!t.out.pump_gripped,"holding grip at hip cannot attach to the pump");
        t.begin();check(t.out.stage==m::Stage::available&&!t.out.held_device,"reload starts with held hip grip without inventing pickup edge");
        t.off().squeeze.current=0;t.tick();t.grab();t.insert();
        check(!t.out.pump_gripped,"shell loading and insertion keep independent hand instead of pump attachment");
        check(t.out.request_commit&&t.out.commit_rounds==1,"fresh hip grip and receiver release preserve individual shell loading");
        t.ack();t.begin();check(t.out.cycle_pending&&t.out.stage==m::Stage::charge,"partial tube finish preserves the pending shot cycle");
        t.pump();check(!t.out.block_attack&&t.c.clip==4&&t.c.reserve==8,"pump after loading changes no additional ammunition");
    }
    {
        Fixture t(true);t.c.capacity=5;t.c.reserve=2;t.pump_mechanism();t.begin();t.grab();t.insert();t.ack();t.begin();
        check(t.out.cycle_pending&&t.out.stage==m::Stage::charge,"empty tube loading requires same grip pump after A finish");
        t.charge();check(t.out.cycle_pending,"empty shotgun cannot fall back to trigger charging");
        t.pump();check(!t.out.block_attack&&t.c.clip==1&&t.c.reserve==1,"empty tube grip pump enables inserted shell without another commit");
    }
    for(unsigned loss=0;loss<3;++loss){
        Fixture t;t.c.clip=t.c.capacity=5;t.pump_mechanism();--t.c.clip;t.tick();
        t.pump_position();t.off().squeeze.current=.8f;t.tick();t.pump_position(.08f);t.tick();
        if(loss==0){t.off().grip.position_tracked=false;t.tick();}
        else if(loss==1){t.f.actions.focused=false;t.tick();}
        else {t.c.now+=101;t.out=t.control.update(t.f,t.c);}
        check(t.out.block_attack&&t.out.cycle_pending,"pose, focus or stale sample loss retains pending pump gate");
        check(!t.out.pump_gripped,"tracking loss drops visual attachment while retaining the pump requirement");
        t.off().grip.position_tracked=true;t.f.actions.focused=true;t.pump_position();t.tick();t.pump_position();t.tick();
        check(t.out.cycle_pending,"tracking recovery with grip held cannot inherit opened stroke or force a release");
        t.pump();check(!t.out.cycle_pending&&!t.out.request_commit,"fresh grip stroke after recovery clears only cycle gate");
    }
    {
        Fixture t;t.c.clip=t.c.capacity=5;t.pump_mechanism();--t.c.clip;t.tick();t.pump_position();t.off().squeeze.current=.8f;t.tick();
        t.pump_position(.08f);t.tick();t.off().grip.pose.position=t.out.supply;t.tick();t.pump_position();t.tick();
        check(t.out.cycle_pending,"leaving pump axis for hip cancels stroke before a return can finish it");
        t.pump();check(!t.out.cycle_pending,"returning to pump allows a new deliberate stroke");
        --t.c.clip;t.tick();++t.c.weapon;t.tick();check(!t.out.cycle_pending,"weapon change does not carry previous shotgun pump");
    }
    {
        Fixture t;t.c.clip=t.c.capacity=5;t.pump_mechanism();
        constexpr float half=.70710678118f;t.main().aim.pose.orientation={0,half,0,half};t.tick();--t.c.clip;t.tick();t.pump();
        check(!t.out.cycle_pending&&t.c.clip==4,"pump travel follows current dominant weapon axis after rotation");
        Fixture bolt;bolt.c.clip=bolt.c.capacity=5;bolt.mechanism(m::ReloadKind::magazine,0,true,true);--bolt.c.clip;bolt.tick();bolt.pump();
        check(bolt.out.cycle_pending,"bolt rifle retains trigger control and ignores grip pump stroke");
        check(!bolt.out.pump_gripped,"ordinary rifle charging cannot acquire a shotgun visual grip");
        bolt.off().squeeze.current=0;bolt.tick();bolt.charge();check(!bolt.out.cycle_pending,"unchanged trigger pull and release still cycles bolt rifle");
    }
}
void held_trigger_charge(){
    for(bool left:{false,true}){
        Fixture t(true,left);t.c.weapon=14;t.c.capacity=8;t.c.reserve=96;
        t.c.cod4_controls=true;t.main().primary.active=true;t.tick();
        t.main().primary.current=true;t.tick();t.main().primary.current=false;t.tick();
        t.grab();t.insert();
        check(t.out.stage==m::Stage::charge&&t.out.block_attack&&!t.out.request_commit,
            "empty eight-round rifle insertion waits for charging while owning the offhand");
        auto duplicate=[&]{++t.c.now;t.out=t.control.update(t.f,t.c);};
        auto reach_held=[&]{
            t.off().grip.pose.position=t.out.handle;t.off().grip.pose.position.x+=.25f;
            t.off().trigger.current=1;t.tick();
            check(t.out.stage==m::Stage::charge&&!t.out.request_commit,
                "trigger press away from charging handle cannot grab or transfer ammo");
            t.off().grip.pose.position=t.out.handle;duplicate();
            check(t.out.stage==m::Stage::charge&&t.out.charge_fraction==0,
                "reused action sample cannot capture a newly reached handle");
            t.tick();t.tick();
            check(t.out.stage==m::Stage::charging&&t.out.block_attack&&!t.out.request_commit&&t.out.charge_fraction==0,
                "held trigger enters charging zone without a second press or an automatic chamber");
        };
        reach_held();
        for(unsigned i=0;i<4;++i)t.tick();
        check(t.out.charge_fraction==0&&t.out.block_attack&&!t.out.request_commit,
            "stationary held trigger cannot complete the charging stroke");
        t.off().trigger.current=0;t.tick();
        check(!t.out.request_commit&&t.out.block_attack,"release without a pull cannot load the empty rifle");
        t.tick();reach_held();
        t.off().grip.pose.position.z+=.02f;t.tick();t.off().trigger.current=0;t.tick();
        check(!t.out.request_commit&&t.out.block_attack,"partial pull and release keeps the charging gate closed");
        t.tick();reach_held();
        t.off().grip.pose.position.z+=.08f;duplicate();
        check(t.out.charge_fraction==0&&!t.out.request_commit,
            "reused action sample cannot supply an unobserved charging pull");
        t.tick();
        check(t.out.charge_fraction>=.9f&&t.out.block_attack&&!t.out.request_commit,
            "full fresh pull still requires support-trigger release");
        t.off().trigger.current=0;duplicate();
        check(t.out.charge_fraction>=.9f&&t.out.block_attack&&!t.out.request_commit,
            "reused action sample cannot release a completed charging pull");
        t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==8&&t.out.block_attack,
            "fresh release after the full pull requests exactly the missing native rounds");
        t.ack();
        check(t.out.stage==m::Stage::ready&&!t.out.block_attack&&t.c.clip==8&&t.c.reserve==88,
            "native acknowledgement unlocks firing with conserved rifle ammunition");
        t.tick();check(!t.out.request_commit,"completed charging gesture cannot duplicate the transfer");
    }
}
void partial_tube_support_return(){
    const auto reload=[](Fixture& t){t.main().primary.current=true;t.tick();t.main().primary.current=false;t.tick();};
    const auto prepare=[&](Fixture& t,m::ReloadKind kind=m::ReloadKind::internal_rounds){
        // The user's current spas_db_sp variant holds eight shells, with six
        // loaded and 76 in reserve when the support hand stopped gripping.
        t.c.cod4_controls=true;t.main().primary.active=true;t.c.weapon=23;
        t.c.clip=6;t.c.capacity=8;t.c.reserve=76;t.c.mechanism={true,kind,1,true,false};t.tick();reload(t);
        check(t.out.stage==m::Stage::available&&t.out.block_attack,"partial SPAS shell reload waits for another deliberate interaction");
    };
    const auto front=[](Fixture& t){const auto right=t.main().grip.pose.position;
        t.off().grip.pose.position={right.x,right.y,right.z-.35f};t.off().squeeze.current=.8f;t.tick();};
    for(auto kind:{m::ReloadKind::internal_rounds,m::ReloadKind::break_action}){
        Fixture t;prepare(t,kind);front(t);
        check(t.out.stage==m::Stage::ready&&!t.out.block_attack&&!t.out.held_device&&!t.out.request_commit&&
            t.c.clip==6&&t.c.reserve==76,"fresh fore-end grip finishes only a loaded partial tube with unchanged ammunition");
        t.off().squeeze.current=0;t.tick();reload(t);t.grab();t.insert();
        check(t.out.request_commit&&t.out.commit_rounds==1,"another A request after front-grip finish can still load exactly one shell");
        t.ack();check(t.c.clip==7&&t.c.reserve==75&&t.out.stage==m::Stage::available,
            "native acknowledgement after resumed loading conserves shell ammunition");
        front(t);check(t.out.stage==m::Stage::ready&&!t.out.block_attack&&t.c.clip==7&&t.c.reserve==75,
            "support grip returns normally after an additional physical insertion");
    }
    {Fixture t;prepare(t);t.grab();check(t.out.stage==m::Stage::held&&t.out.held_device,
        "belt grip retains ordinary shell pickup instead of ending partial reload");
        front(t);check(t.out.stage==m::Stage::held&&t.out.held_device&&t.out.block_attack,
            "returning a held shell to the fore-end cannot discard it or finish reload");}
    {Fixture t;prepare(t);t.main().grip.pose.position.z=.5f;t.tick();
        t.off().grip.pose.position=t.out.supply;t.off().squeeze.current=.8f;
        check(bovr::input::two_hand_aim_basis(t.f.actions,t.f.head_center,xr::Hand::Right,true).paired,
            "belt exclusion fixture otherwise meets the exact existing support-pair policy");
        t.tick();check(t.out.stage==m::Stage::held&&t.out.held_device&&t.out.block_attack,
            "a belt pickup remains a shell pickup even when geometric support pairing also qualifies");}
    {Fixture t;prepare(t);t.off().grip.pose.position=t.out.receiver;t.off().squeeze.current=.8f;
        check(bovr::input::two_hand_aim_basis(t.f.actions,t.f.head_center,xr::Hand::Right,true).paired,
            "receiver exclusion fixture otherwise meets the exact support-pair policy");
        t.tick();
        check(t.out.stage==m::Stage::available&&!t.out.held_device&&t.out.block_attack,
            "receiver-zone grip neither invents belt pickup nor ends the reload");}
    {Fixture t;prepare(t);const auto right=t.main().grip.pose.position;
        // Within 13 cm of the non-pump handle, outside the receiver zone, and
        // far enough ahead to qualify as a pair absent the explicit exclusion.
        t.off().grip.pose.position={right.x,right.y+.12f,right.z-.11f};t.off().squeeze.current=.8f;
        check(bovr::input::two_hand_aim_basis(t.f.actions,t.f.head_center,xr::Hand::Right,true).paired,
            "charging-handle exclusion fixture otherwise meets the exact support-pair policy");
        t.tick();
        check(t.out.stage==m::Stage::available&&t.out.block_attack,
            "non-pump charging-handle grip cannot stand in for fore-end support");}
    {Fixture t;prepare(t);t.grab();t.off().grip.pose.position=t.out.receiver;t.tick();
        check(t.out.stage==m::Stage::insertion,"real held shell reaches receiver before commit");
        front(t);check(t.out.stage==m::Stage::held&&t.out.held_device&&t.out.block_attack,
            "moving an inserting shell to the fore-end returns held state without finishing");}
    {Fixture t;prepare(t);t.grab();t.insert();front(t);
        check(t.out.stage==m::Stage::committing&&t.out.request_commit&&t.out.block_attack&&t.c.clip==6&&t.c.reserve==76,
            "fore-end grip cannot cancel an unacknowledged native shell transaction");}
    {Fixture t;prepare(t);t.c.clip=0;t.tick();front(t);
        check(t.out.stage==m::Stage::available&&t.out.block_attack&&!t.out.request_commit,
            "gripping an empty tube cannot bypass loading or create a shell");}
    {Fixture t;t.off().squeeze.current=.8f;prepare(t);front(t);
        check(t.out.stage==m::Stage::available&&t.out.block_attack,
            "support grip already held before reload cannot finish it by moving forward");
        t.off().squeeze.current=0;t.tick();front(t);
        check(t.out.stage==m::Stage::ready&&!t.out.block_attack,
            "actual support release and fresh fore-end grip finish the partial tube");}
    {Fixture t;prepare(t);const auto right=t.main().grip.pose.position;
        t.off().grip.pose.position={right.x,right.y,right.z-.35f};t.off().squeeze.current=.8f;
        ++t.c.now;t.out=t.control.update(t.f,t.c);
        check(t.out.stage==m::Stage::available&&t.out.block_attack,"duplicate action sequence cannot supply a fore-end grip edge");
        t.tick();check(t.out.stage==m::Stage::available,"a consumed duplicate grip edge cannot be replayed on the next fresh sample");
        t.off().squeeze.current=0;t.tick();front(t);check(t.out.stage==m::Stage::ready,
            "a new release and press recover after duplicate-sample refusal");}
    {Fixture t;prepare(t);t.off().grip.position_tracked=false;front(t);
        check(!t.out.supported&&!t.out.request_commit&&t.c.clip==6&&t.c.reserve==76,
            "untracked support cannot finish or transfer tube ammunition");}
    {Fixture t;prepare(t);const auto right=t.main().grip.pose.position;
        t.off().grip.pose.position={right.x,right.y,right.z+.35f};t.off().squeeze.current=.8f;t.tick();
        check(t.out.stage==m::Stage::available&&t.out.block_attack,
            "grip behind the gun is not deliberate fore-end support");}
    for(auto kind:{m::ReloadKind::magazine,m::ReloadKind::single_round,m::ReloadKind::belt}){
        Fixture t;prepare(t,kind);front(t);check(t.out.stage==m::Stage::available&&t.out.block_attack,
            "fore-end return does not close magazine, belt or single-round transactions");
    }
    {Fixture t(false,true);prepare(t);front(t);check(t.out.stage==m::Stage::available&&t.out.block_attack,
        "left-handed reload keeps the existing explicit A finish policy");
        reload(t);check(t.out.stage==m::Stage::ready&&!t.out.block_attack,
            "left-handed A still finishes a partially loaded tube");}
    {Fixture t;prepare(t);t.c.cod4_controls=false;t.tick();t.begin();front(t);
        check(t.out.stage==m::Stage::available&&t.out.block_attack,
            "legacy button layout retains its explicit partial-tube finish policy");}
    for(bool empty_origin:{false,true}){
        Fixture t;prepare(t);t.c.mechanism={true,m::ReloadKind::internal_rounds,1,true,true};
        t.c.mechanism.grip_pump=true;t.c.mechanism.handle_offset={0,-.03f,-.19f};
        t.c.clip=empty_origin?0:6;t.c.confirmed_cycle_pending=!empty_origin;t.tick();reload(t);
        if(empty_origin){t.grab();t.insert();t.ack();check(t.c.clip==1,"empty tube receives only the physically inserted shell");}
        front(t);check(t.out.stage==m::Stage::charge&&t.out.cycle_pending&&t.out.block_attack&&!t.out.request_commit,
            "returning to support grip preserves pending pump and empty-origin charging requirements");
        const auto clip=t.c.clip,reserve=t.c.reserve;t.pump();
        check(!t.out.cycle_pending&&!t.out.block_attack&&t.c.clip==clip&&t.c.reserve==reserve,
            "a real pump stroke after support return clears the mechanical gate without extra ammunition");
    }
}
void independent_reload_rearming(){
    // A held charging trigger must not disable fresh reload-button presses
    // after a temporary loss. The old charge stroke still needs real neutral.
    for(bool left:{false,true})for(unsigned loss=0;loss<5;++loss){
        Fixture t(true,left);t.c.cod4_controls=true;t.main().primary.active=true;t.tick();
        t.off().trigger.current=1;t.tick();
        if(loss==0){t.off().grip.position_tracked=false;t.tick();t.off().grip.position_tracked=true;}
        if(loss==1){t.f.actions.focused=false;t.tick();t.f.actions.focused=true;}
        if(loss==2){t.c.valid=false;t.tick();t.c.valid=true;}
        if(loss==3){++t.c.epoch;t.tick();}
        if(loss==4){t.c.now+=101;t.out=t.control.update(t.f,t.c);}
        t.tick();
        check(t.out.stage==m::Stage::ready&&!t.out.held_device&&!t.out.request_commit,
            "valid recovery with held trigger cannot invent a magazine or reload");
        t.main().primary.current=true;t.tick();t.main().primary.current=false;t.tick();
        check(t.out.stage==m::Stage::available&&t.out.block_attack&&!t.out.request_commit,
            "fresh A reload recovers without releasing an unrelated held charging trigger");
        t.grab();t.insert();
        check(t.out.stage==m::Stage::charge&&!t.out.request_commit,
            "new empty magazine insertion still requires charging after held-trigger recovery");
        t.off().grip.pose.position=t.out.handle;t.tick();t.tick();
        t.off().grip.pose.position.z+=.08f;t.tick();t.tick();
        check(t.out.stage==m::Stage::charge&&t.out.charge_fraction==0&&!t.out.request_commit,
            "held trigger surviving a reset cannot grab or complete a charging stroke");
        t.off().trigger.current=0;
        ++t.c.now;t.out=t.control.update(t.f,t.c); // No fresh XR action occurrence.
        t.off().trigger.current=1;t.off().grip.pose.position=t.out.handle;t.tick();t.tick();
        check(t.out.stage==m::Stage::charge&&t.out.charge_fraction==0&&!t.out.request_commit,
            "duplicate-sample trigger release cannot rearm charging after reset");
        t.off().trigger.current=0;t.tick();
        check(t.out.stage==m::Stage::charge&&!t.out.request_commit,
            "real trigger neutral rearms without acknowledging an old charge or native transfer");
        t.charge();check(t.out.request_commit&&t.out.commit_rounds==30,
            "new full pull and release after genuine neutral requests one physical reload");
        t.ack();check(t.c.clip==30&&t.c.reserve==90&&t.out.stage==m::Stage::ready,
            "recovered reload completes only after native acknowledgement with conserved ammunition");
    }
    for(bool left:{false,true}){
        Fixture t(false,left);t.c.cod4_controls=true;t.main().primary.active=true;t.tick();
        t.off().trigger.current=1;t.main().primary.current=true;
        t.f.actions.focused=false;t.tick();t.f.actions.focused=true;t.tick();t.tick();
        check(t.out.stage==m::Stage::ready&&!t.out.held_device&&!t.out.request_commit,
            "held A across focus recovery cannot begin a physical reload");
        t.main().primary.current=false;t.tick();t.main().primary.current=true;t.tick();
        t.main().primary.current=false;t.tick();t.grab();t.insert();
        check(t.out.request_commit&&t.out.commit_rounds==10,
            "fresh A can complete a partial reload with trigger still held after focus recovery");
        t.ack();check(t.c.clip==30&&t.c.reserve==110,
            "partial recovery never invents an extra charging requirement");
    }
}
void ordinary_magazine_receiver_charge(){
    // Ordinary rifles use a bounded assist around their already-admitted
    // receiver-to-handle segment. This must still recognize a new, real
    // rearward pull/release, never a return to aiming or a delayed input copy.
    const auto add=[](xr::Vec3f a,xr::Vec3f b){return xr::Vec3f{a.x+b.x,a.y+b.y,a.z+b.z};};
    const auto prepare=[](Fixture& t,xr::Quaternionf orientation=xr::Quaternionf{}){
        t.c.weapon=30;t.c.clip=0;t.c.reserve=120;t.c.capacity=30;
        t.c.cod4_controls=true;t.main().primary.active=true;
        t.c.mechanism={true,m::ReloadKind::magazine,0,true,false};
        t.main().aim.pose.orientation=orientation;t.tick();
    };
    const auto begin=[](Fixture& t){t.main().primary.current=true;t.tick();t.main().primary.current=false;t.tick();};
    const auto insert=[&](Fixture& t){begin(t);t.grab();t.insert();
        check(t.out.stage==m::Stage::charge&&t.out.block_attack&&!t.out.request_commit&&t.c.clip==0,
            "ordinary empty rifle insertion retains charge gate without transferring ammunition");};
    const auto pull=[&](Fixture& t,float travel=.08f){
        t.off().grip.pose.position=add(t.off().grip.pose.position,xr::Rotate(t.main().aim.pose.orientation,{0,0,travel}));t.tick();
    };
    const auto receiver_grab=[](Fixture& t){t.off().grip.pose.position=t.out.receiver;t.off().trigger.current=1;t.tick();
        check(t.out.stage==m::Stage::charging&&m::uses_offhand(t.out),
            "receiver-origin charging publishes acquired support-hand ownership on that same fresh sample");t.tick();
        check(t.out.stage==m::Stage::charging&&t.out.charge_fraction==0&&!t.out.request_commit,
            "deliberate trigger at admitted receiver acquires ordinary rifle action without completing it");};
    constexpr float half=.70710678118f;
    for(bool left:{false,true})for(auto orientation:{xr::Quaternionf{},xr::Quaternionf{0,half,0,half},
        xr::Quaternionf{half,0,0,half},xr::Quaternionf{0,0,half,half}}){
        Fixture t(true,left);prepare(t,orientation);insert(t);receiver_grab(t);
        const auto translate=xr::Vec3f{.23f,-.17f,.19f};
        t.main().grip.pose.position=add(t.main().grip.pose.position,translate);
        t.off().grip.pose.position=add(t.off().grip.pose.position,translate);t.tick();
        check(t.out.charge_fraction<.001f&&t.out.block_attack&&!t.out.request_commit,
            "common weapon and controller translation cannot substitute for a receiver-origin charge stroke");
        pull(t,.02f);t.off().trigger.current=0;t.tick();
        check(t.out.block_attack&&!t.out.request_commit&&t.c.clip==0&&t.c.reserve==120,
            "short receiver-origin stroke and release cannot acknowledge native ammunition");
        receiver_grab(t);pull(t);
        check(t.out.charge_fraction>=.9f&&t.out.block_attack&&!t.out.request_commit,
            "rotated receiver-origin rearward stroke waits for explicit trigger release");
        t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==30,
            "full receiver-origin stroke requests only the missing bounded rifle rounds");
        t.ack();check(t.out.stage==m::Stage::ready&&t.c.clip==30&&t.c.reserve==90,
            "receiver-origin charge completes only after native acknowledgement with conserved ammunition");
        const auto first_transaction=t.out.transaction;
        t.c.clip=0;t.tick();insert(t);receiver_grab(t);pull(t);t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.transaction>first_transaction,
            "second consecutive empty rifle reload needs and recognizes a distinct real charging transaction");
        t.ack();check(t.c.clip==30&&t.c.reserve==60&&!t.out.block_attack,
            "consecutive receiver-origin reloads retire ownership without duplicate ammunition");
    }
    for(bool left:{false,true}){
        Fixture t(true,left);prepare(t);insert(t);
        t.off().grip.pose.position=t.out.receiver;t.off().trigger.current=1;
        ++t.c.now;t.out=t.control.update(t.f,t.c);
        check(t.out.stage==m::Stage::charge&&t.out.charge_fraction==0&&!t.out.request_commit,
            "duplicate input sample at receiver cannot acquire a newly reached charging action");
        t.tick();t.tick();check(t.out.stage==m::Stage::charging,"fresh receiver sample admits the deliberate charging grab");
        t.off().grip.pose.position=add(t.off().grip.pose.position,{0,0,.08f});
        ++t.c.now;t.out=t.control.update(t.f,t.c);
        check(t.out.charge_fraction==0&&!t.out.request_commit,
            "duplicate sample cannot supply unseen receiver-origin charging travel");
        t.tick();t.off().trigger.current=0;++t.c.now;t.out=t.control.update(t.f,t.c);
        check(t.out.charge_fraction>=.9f&&t.out.block_attack&&!t.out.request_commit,
            "duplicate sample cannot release a completed receiver-origin pull");
        t.tick();check(t.out.request_commit&&t.out.commit_rounds==30,
            "fresh release completes the receiver-origin action exactly once");t.ack();
        t.tick();check(!t.out.request_commit&&t.c.clip==30&&t.c.reserve==90,
            "completed receiver-origin action cannot replay an old native transfer");
    }
    {
        Fixture t(true);prepare(t);begin(t);t.grab();
        t.off().grip.pose.position=t.out.receiver;t.tick();
        check(t.out.stage==m::Stage::insertion&&t.out.held_device,
            "fast receiver charging fixture retains physical magazine until its explicit grip release");
        t.off().squeeze.current=0;t.off().trigger.current=1;t.tick();t.tick();
        check(t.out.stage==m::Stage::charging&&t.out.charge_fraction==0&&!t.out.request_commit,
            "same fresh sample may insert the magazine and deliberately acquire receiver charging without a delay");
        pull(t);t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==30,
            "immediate receiver-origin charge still requires the complete fresh pull and release");
    }
    for(bool at_hip:{false,true}){
        Fixture t(true);prepare(t);insert(t);
        t.off().grip.pose.position=at_hip?t.out.supply:add(t.main().grip.pose.position,{0,0,-.35f});
        t.off().squeeze.current=.8f;t.off().trigger.current=1;t.tick();pull(t);t.off().trigger.current=0;t.tick();
        check(t.out.block_attack&&!t.out.request_commit&&t.c.clip==0&&t.c.reserve==120,
            "hip or distant fore-end grip and trigger motion cannot complete an ordinary rifle reload");
    }
    {
        Fixture t(true);prepare(t);insert(t);receiver_grab(t);
        t.off().grip.pose.position.x+=.15f;t.tick();t.off().trigger.current=0;t.tick();
        check(t.out.block_attack&&!t.out.request_commit&&t.c.clip==0,
            "sideways receiver-origin motion without rearward action travel cannot charge the rifle");
    }
    {
        Fixture t(true);prepare(t);insert(t);
        t.off().grip.pose.position=add(t.main().grip.pose.position,{0,0,-.20f});
        t.off().squeeze.current=.8f;t.tick();pull(t);t.off().squeeze.current=0;t.tick();
        check(t.out.block_attack&&!t.out.request_commit&&t.c.clip==0&&t.c.reserve==120,
            "close support-grip motion without a charging trigger cannot silently complete the empty rifle reload");
    }
    for(bool left:{false,true}){
        Fixture t(true,left);prepare(t);insert(t);
        // This close fore-end is inside the receiver capsule but outside the
        // original handle sphere. Holding support squeeze must keep it solely
        // an aiming interaction, even when the trigger happens to be held.
        t.off().grip.pose.position=add(t.main().grip.pose.position,{0,0,-.20f});
        t.off().squeeze.current=.8f;t.off().trigger.current=1;t.tick();t.tick();
        check(t.out.stage==m::Stage::charge&&!m::uses_offhand(t.out)&&!t.out.request_commit&&
            t.out.charge_fraction==0&&t.c.clip==0&&t.c.reserve==120,
            "held close fore-end squeeze and trigger cannot acquire capsule-only ordinary rifle charging");
        t.off().trigger.current=0;t.tick();t.off().squeeze.current=0;t.off().trigger.current=1;t.tick();
        check(t.out.stage==m::Stage::charging&&m::uses_offhand(t.out)&&!t.out.request_commit&&t.out.charge_fraction==0,
            "released support squeeze and deliberate new trigger grab select the same bounded receiver area immediately");
        pull(t);check(t.out.block_attack&&!t.out.request_commit&&t.out.charge_fraction>=.9f,
            "a deliberate close receiver-area grab still needs a fresh complete stroke before release");
        t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==30,
            "close receiver-area charging cannot request ammunition until the complete stroke is deliberately released");
        t.ack();check(!m::uses_offhand(t.out)&&!t.out.block_attack&&t.c.clip==30&&t.c.reserve==90,
            "close receiver-area charging retires actual hand ownership only after conserved native acknowledgement");
    }
    for(float radius:{.129f,.131f}){
        Fixture t(true);prepare(t);insert(t);
        // Extend past the receiver endpoint, away from the handle. The nearest
        // point is the endpoint, rather than the infinite receiver/handle line.
        auto away=xr::Vec3f{t.out.receiver.x-t.out.handle.x,t.out.receiver.y-t.out.handle.y,t.out.receiver.z-t.out.handle.z};
        const float length=std::sqrt(away.x*away.x+away.y*away.y+away.z*away.z);
        t.off().grip.pose.position=add(t.out.receiver,{away.x*radius/length,away.y*radius/length,away.z*radius/length});
        t.off().trigger.current=1;t.tick();t.tick();
        check(t.out.stage==(radius<.13f?m::Stage::charging:m::Stage::charge)&&!t.out.request_commit,
            "receiver assist remains a bounded thirteen-centimeter capsule instead of an infinite corridor");
    }
    for(unsigned loss=0;loss<3;++loss){
        Fixture t(true);prepare(t);t.off().trigger.current=1;t.tick();
        if(loss==0){t.f.actions.focused=false;t.tick();t.f.actions.focused=true;}
        if(loss==1){t.off().grip.position_tracked=false;t.tick();t.off().grip.position_tracked=true;}
        if(loss==2){t.c.now+=101;t.out=t.control.update(t.f,t.c);}
        t.tick();insert(t);t.off().grip.pose.position=t.out.receiver;t.tick();t.tick();pull(t);
        check(t.out.stage==m::Stage::charge&&!t.out.request_commit&&t.out.charge_fraction==0,
            "receiver assist cannot revive a trigger held through tracking focus or stale-input recovery");
        t.off().trigger.current=0;t.tick();receiver_grab(t);pull(t);t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==30,
            "genuine trigger neutral and a new receiver-origin stroke recover without bypassing charge");
    }
    {
        Fixture t(true);prepare(t);insert(t);receiver_grab(t);pull(t);
        t.off().grip.position_tracked=false;t.tick();
        check(!t.out.held_device&&!t.out.request_commit&&t.c.clip==0&&t.c.reserve==120,
            "pose loss cancels an opened receiver-origin gesture without acknowledging ammunition");
        t.off().grip.position_tracked=true;t.off().trigger.current=0;t.tick();t.tick();
        check(t.out.stage==m::Stage::ready&&!t.out.request_commit&&t.c.clip==0,
            "pose recovery and release cannot replay the canceled receiver-origin transfer");
        insert(t);receiver_grab(t);pull(t);t.off().trigger.current=0;t.tick();
        check(t.out.request_commit&&t.out.commit_rounds==30,
            "fresh physical magazine and charge are required again after pose-loss cancellation");
    }
    for(bool pump:{false,true}){
        Fixture t(true);
        if(pump)t.pump_mechanism();else t.mechanism(m::ReloadKind::magazine,0,true,true);
        t.c.mechanism.receiver_offset={0,-.3f,-.3f};t.tick();
        if(pump){t.c.clip=1;t.c.capacity=5;t.tick();--t.c.clip;t.tick();}
        else {t.begin();t.grab();t.insert();}
        t.off().grip.pose.position=t.out.receiver;t.off().squeeze.current=.8f;t.off().trigger.current=1;t.tick();t.tick();
        check(t.out.stage==m::Stage::charge&&t.out.charge_fraction==0&&!t.out.request_commit,
            "ordinary rifle receiver assist never extends bolt or pump charging capture geometry");
    }
}
int main(){
    ordinary_magazine_receiver_charge();
    partial_tube_support_return();
    independent_reload_rearming();
    pump_controls();
    held_trigger_charge();
    {
        Fixture t;t.c.cod4_controls=true;t.main().primary.active=true;t.tick();
        t.begin();check(t.out.stage==m::Stage::ready,"COD4 right B cannot eject");
        t.main().primary.current=true;t.tick();check(t.out.stage==m::Stage::available,"COD4 right A ejects");
        t.main().primary.current=false;t.tick();t.grab();t.insert();
        check(t.out.request_commit,"COD4 A mapping retains physical insertion mechanics");
    }
    for(bool left:{false,true}){
        Fixture t(false,left);check(t.out.supported,"valid supported identity");
        t.begin();check(t.out.stage==m::Stage::available&&t.out.block_attack&&!t.out.request_commit,"eject blocks shots without transferring ammo");
        t.off().squeeze.current=.8f;t.tick();check(t.out.stage==m::Stage::available,"grip away from belt cannot spawn magazine");
        t.off().squeeze.current=0;t.tick();t.grab();check(t.out.held_device,"belt grip obtains physical magazine");
        t.off().squeeze.current=0;t.tick();check(t.out.stage==m::Stage::available&&!t.out.request_commit,"drop cannot reload");
        t.grab();t.insert();check(t.out.request_commit&&t.out.block_attack,"tactical insertion requests transfer");
        t.tick();check(t.out.request_commit,"request remains until native completion");
        t.c.clip=30;t.c.reserve=110;t.c.commit_completed=true;t.tick();check(t.out.stage==m::Stage::ready&&!t.out.block_attack,"authoritative completion unlocks weapon");
        t.c.commit_completed=false;t.begin();check(t.out.stage==m::Stage::ready,"full magazine does not arm reload");
        Fixture e(true,left);e.begin();e.grab();e.insert();check(e.out.stage==m::Stage::charge&&!e.out.request_commit,"empty insertion requires physical charging");
        e.charge(.02f);check(!e.out.request_commit,"partial charging pull cannot load");
        e.charge();check(e.out.request_commit,"full pull and trigger release requests native transfer");
    }
    Fixture tracking;tracking.begin();tracking.grab();tracking.off().grip.position_tracked=false;tracking.tick();
    check(!tracking.out.request_commit&&!tracking.out.held_device,"tracking loss discards held feed device");
    tracking.off().grip.position_tracked=true;tracking.tick();check(tracking.out.stage==m::Stage::ready,"tracking recovery cannot revive transaction");
    Fixture focus;focus.begin();focus.grab();focus.f.actions.focused=false;focus.tick();check(!focus.out.request_commit,"focus loss cancels transfer");
    Fixture changed;changed.begin();changed.grab();++changed.c.epoch;changed.tick();check(changed.out.stage==m::Stage::ready,"map epoch cancels transaction");
    Fixture weapon;weapon.begin();weapon.grab();weapon.c.weapon=42;weapon.tick();check(!weapon.out.supported,"unsupported tube/break action stays native");
    Fixture stale;stale.begin();stale.grab();stale.c.now+=101;stale.out=stale.control.update(stale.f,stale.c);check(!stale.out.held_device,"duplicate stale pose cannot retain feed device");
    Fixture bad;bad.off().grip.pose.position.x=std::numeric_limits<float>::quiet_NaN();bad.tick();check(!bad.out.supported,"nonfinite controller rejected");
    Fixture empty;empty.c.reserve=0;empty.begin();check(empty.out.stage==m::Stage::ready,"empty reserve cannot create ammunition");
    for(auto kind:{m::ReloadKind::internal_rounds,m::ReloadKind::break_action}){
        Fixture t(true);t.c.capacity=3;t.c.reserve=5;t.mechanism(kind,1);t.begin();
        std::uint64_t transaction=t.out.transaction;
        for(int round=0;round<3;++round){
            t.grab();t.insert();check(t.out.request_commit&&t.out.commit_rounds==1,"one physical shell requests one round");
            for(int held=0;held<5;++held){t.tick();check(t.out.request_commit&&t.out.transaction==transaction&&
                t.out.commit_rounds==1,"waiting for acknowledgment cannot generate a second transaction");}
            t.ack();
            if(round<2){check(t.out.stage==m::Stage::available&&t.out.block_attack&&!t.out.request_commit,"shell ack reopens supply with firing blocked");
                check(t.out.transaction>transaction,"next physical shell owns a distinct transaction");transaction=t.out.transaction;}
        }
        check(t.c.clip==3&&t.c.reserve==2&&t.out.stage==m::Stage::ready&&!t.out.block_attack,"full internal weapon finishes with conserved ammunition");
    }
    {
        Fixture t;t.c.clip=1;t.c.capacity=6;t.c.reserve=2;t.mechanism(m::ReloadKind::internal_rounds,1);
        t.begin();t.grab();t.insert();t.ack();t.begin();
        check(t.out.stage==m::Stage::ready&&!t.out.block_attack&&t.c.clip==2&&t.c.reserve==1,"A finishes a partial tube without loading free rounds");
        t.begin();t.grab();t.main().secondary.current=true;t.tick();
        check(t.out.held_device&&!t.out.request_commit,"A cannot close through a held round");
        t.main().secondary.current=false;t.tick();t.insert();t.ack();
        check(t.out.stage==m::Stage::ready&&t.c.clip==3&&t.c.reserve==0,"empty supply finishes a partial tube");
    }
    {
        Fixture t(true);t.c.capacity=5;t.c.reserve=2;t.mechanism(m::ReloadKind::internal_rounds,1,true);
        t.begin();t.grab();t.insert();check(t.out.request_commit,"empty tube inserts its first shell before pumping");t.ack();
        t.begin();check(t.out.stage==m::Stage::charge&&t.out.cycle_pending&&!t.out.request_commit,"finishing an initially empty tube requires the action");
        t.charge(.02f);check(t.out.block_attack,"partial pump stroke cannot enable firing");
        t.charge();check(t.out.stage==m::Stage::ready&&!t.out.block_attack&&!t.out.request_commit&&t.c.clip==1,"full pump enables loaded tube without extra ammunition");
    }
    {
        Fixture t(true);t.c.capacity=2;t.c.reserve=1;t.mechanism(m::ReloadKind::break_action,2);
        t.begin();t.grab();t.insert();check(t.out.commit_rounds==1,"insertion batch is bounded by reserve");t.ack();
        check(t.c.clip==1&&t.c.reserve==0&&!t.out.block_attack,"one available shell leaves a playable partial break action");
        Fixture cap;cap.c.clip=4;cap.c.capacity=5;cap.mechanism(m::ReloadKind::internal_rounds,2);
        cap.begin();cap.grab();cap.insert();check(cap.out.commit_rounds==1,"insertion batch is bounded by capacity");
    }
    {
        Fixture t(true);t.c.capacity=3;t.c.reserve=8;t.mechanism(m::ReloadKind::internal_rounds,1);
        t.begin();t.grab();t.insert();++t.c.clip;--t.c.reserve;t.c.commit_completed=true;t.tick();
        t.grab();t.insert();t.tick();
        check(t.out.request_commit&&t.out.stage==m::Stage::committing,"old held acknowledgement cannot complete the next shell");
        t.c.commit_completed=false;t.tick();++t.c.clip;--t.c.reserve;t.c.commit_completed=true;t.tick();
        check(t.out.stage==m::Stage::available&&!t.out.request_commit,"fresh acknowledgement completes the matching shell");
    }
    for(auto kind:{m::ReloadKind::magazine,m::ReloadKind::belt,m::ReloadKind::single_round}){
        Fixture t(true);t.c.capacity=kind==m::ReloadKind::single_round?1:30;
        t.mechanism(kind,kind==m::ReloadKind::single_round?1:0,false);
        t.begin();t.grab();t.insert();check(t.out.request_commit&&t.out.kind==kind,"native-admitted magazine belt or single round uses physical insertion");
        t.ack();check(t.out.stage==m::Stage::ready&&!t.out.block_attack,"non-repeated mechanism finishes after its one conserved commit");
    }
    {
        Fixture t;t.c.clip=5;t.c.capacity=5;t.mechanism(m::ReloadKind::magazine,0,true,true);
        check(!t.out.cycle_pending,"equipping a partly loaded bolt weapon never invents a shot");
        --t.c.clip;t.tick();check(t.out.cycle_pending&&t.out.block_attack&&t.out.stage==m::Stage::charge,"same-context shot requires a manual action stroke");
        t.charge(.01f);check(t.out.cycle_pending,"short stroke leaves the chamber gate closed");
        t.charge();check(!t.out.block_attack&&!t.out.request_commit&&t.c.clip==4,"full bolt stroke enables firing without changing clip");
        t.tick();check(!t.out.cycle_pending,"same clip cannot retrigger the action gate");
        --t.c.clip;t.tick();t.off().grip.position_tracked=false;t.tick();
        check(t.out.block_attack&&t.out.cycle_pending&&!t.out.held_device,"missing support tracking preserves firing gate without held geometry");
        t.off().grip.position_tracked=true;t.tick();check(t.out.cycle_pending&&t.out.block_attack,"tracking interruption cannot bypass a pending bolt cycle");
        t.charge();check(!t.out.cycle_pending,"fresh stroke after tracking recovery clears the gate");
        --t.c.clip;t.tick();++t.c.weapon;t.tick();check(!t.out.cycle_pending,"different weapon discards old cycle state");
    }
    {
        Fixture t;t.c.clip=5;t.c.capacity=5;t.c.shot_sequence=10;t.mechanism(m::ReloadKind::magazine,0,true,true);
        --t.c.clip;t.tick();check(!t.out.cycle_pending,"scripted clip loss without the supplied fire occurrence is not a shot");
        --t.c.clip;++t.c.shot_sequence;t.tick();check(t.out.cycle_pending,"validated fire occurrence and ammo decrement arm the cycle");
        t.charge();++t.c.epoch;t.c.clip=1;t.tick();check(!t.out.cycle_pending,"epoch baseline never compares ammo with the previous world");
    }
    {
        Fixture t;t.c.mechanism={true,m::ReloadKind::internal_rounds,0};t.tick();
        check(!t.out.supported,"internal mechanisms require a positive insertion count");
        t.c.mechanism={true,m::ReloadKind::magazine,0};t.c.mechanism.handle_offset.x=std::numeric_limits<float>::quiet_NaN();t.tick();
        check(!t.out.supported,"invalid mechanism coordinates cannot publish geometry");
        t.c.mechanism={true,m::ReloadKind::magazine,0};t.c.mechanism.receiver_offset={.05f,-.12f,-.2f};t.tick();
        check(t.out.supported&&std::fabs(t.out.receiver.x-(t.main().grip.pose.position.x+.05f))<.00001f,"admitted insertion point follows descriptor and current grip");
    }
    {
        Fixture t;t.c.clip=1;t.c.capacity=5;t.mechanism(m::ReloadKind::internal_rounds,1);
        t.begin();t.c.reserve=0;t.tick();check(t.out.stage==m::Stage::ready&&!t.out.block_attack,"supply exhaustion while waiting never strands a loaded weapon");
        Fixture large(true);large.c.capacity=999;large.c.reserve=999;large.mechanism(m::ReloadKind::belt,0,false);
        large.begin();large.grab();large.insert();check(large.out.commit_rounds==999,"native large belt capacity remains bounded and reloadable");
        large.c.capacity=static_cast<int>(m::max_reload_capacity)+1;large.tick();check(!large.out.supported,"unbounded native capacity is rejected");
    }
    {
        Fixture t;t.c.clip=5;t.c.capacity=5;t.c.shot_sequence=1;t.mechanism(m::ReloadKind::magazine,0,true,true);
        t.off().grip.position_tracked=false;--t.c.clip;++t.c.shot_sequence;t.c.confirmed_cycle_pending=true;t.tick();
        check(t.out.block_attack&&t.out.cycle_pending&&!t.out.held_device,"validated shot at tracking loss latches a cycle without waiting for a tracked clip sample");
        t.off().grip.position_tracked=true;t.tick();check(t.out.block_attack&&t.out.cycle_pending,"recovery cannot rebaseline away the native pending shot");
        t.charge();check(!t.out.cycle_pending&&!t.out.request_commit,"fresh physical stroke acknowledges native pending cycle");
        t.c.confirmed_cycle_pending=false;t.tick();check(!t.out.block_attack,"native owner retires cycle latch after physical acknowledgement");
        Fixture plain;plain.mechanism(m::ReloadKind::magazine,0,false,false);plain.c.confirmed_cycle_pending=true;plain.tick();
        check(!plain.out.cycle_pending,"non-cycling weapon never consumes a foreign pending bolt signal");
    }
    {
        Fixture t;t.begin();t.grab();const auto clip=t.c.clip,reserve=t.c.reserve;
        t.out=t.control.cancel();
        check(t.out.stage==m::Stage::ready&&!t.out.held_device&&!t.out.request_commit&&!t.out.block_attack,
            "tactical gesture cancellation releases only the physical reload gate");
        check(t.c.clip==clip&&t.c.reserve==reserve,"cancellation neither transfers nor invents ammunition");
        t.tick();check(t.out.stage==m::Stage::ready,"held grip cannot restart a canceled gesture");
        t.off().squeeze.current=0;t.tick();t.begin();t.grab();t.insert();
        check(t.out.request_commit,"fresh deliberate reload can follow cancellation");
        t.c.cancel_requested=true;t.main().secondary.current=true;t.tick();t.c.cancel_requested=false;t.tick();
        check(!t.out.request_commit&&t.out.stage==m::Stage::ready,"failure cancellation consumes held A and cannot replay a native request");
    }
    {
        Fixture t(true);t.begin();t.grab();t.insert();check(t.out.stage==m::Stage::charge,"empty magazine first requires physical charge");
        t.out=t.control.cancel();
        check(t.out.stage==m::Stage::charge&&t.out.cycle_pending&&t.out.block_attack&&!t.out.request_commit,
            "canceling empty reload cannot erase unresolved charging requirement");
        t.tick();check(t.out.block_attack&&!t.out.request_commit,"failed weapon switch still leaves current empty charging gate");
    }
    {
        Fixture t;t.c.clip=t.c.capacity=5;t.pump_mechanism();--t.c.clip;t.tick();
        t.out=t.control.cancel();check(t.out.cycle_pending&&t.out.block_attack,"selection cancellation cannot bypass pending shotgun pump");
        t.pump();check(!t.out.cycle_pending&&!t.out.request_commit,"fresh physical pump recovers canceled selection without ammunition transfer");
        Fixture empty_tube(true);empty_tube.pump_mechanism();empty_tube.begin();empty_tube.grab();empty_tube.insert();
        check(empty_tube.out.request_commit,"empty tube has a completed physical shell request");
        empty_tube.c.cancel_requested=true;empty_tube.tick();empty_tube.c.cancel_requested=false;
        check(empty_tube.out.cycle_pending&&empty_tube.out.block_attack&&!empty_tube.out.request_commit,
            "uncertain shell transfer cancellation preserves empty-tube pump requirement");
    }
    for(bool recovery:{false,true}){
        Fixture t;t.off().squeeze.current=1;
        if(recovery){t.off().grip.position_tracked=false;t.tick();t.off().grip.position_tracked=true;}
        else {t.c.weapon=17;t.c.mechanism={true,m::ReloadKind::magazine,0,true,false};}
        t.tick();t.off().grip.pose.position=t.out.supply;t.tick();
        check(t.out.stage==m::Stage::ready&&!t.out.held_device&&!t.out.request_commit,
            "pistol equip/recovery with support grip held records neutral without inventing reload or pickup");
        t.begin();check(t.out.stage==m::Stage::available&&!t.out.held_device,
            "fresh A starts pistol reload despite held support, without converting old grip into pickup");
        t.tick();check(!t.out.held_device,"remaining held at belt still cannot invent pickup");
        t.off().squeeze.current=0;t.tick();t.off().squeeze.current=1;t.tick();
        check(t.out.held_device,"new belt release/repress picks up magazine after held-support equip/recovery");
    }
    {Fixture t;t.c.weapon=17;t.c.mechanism={true};t.off().squeeze.current=1;t.main().secondary.current=true;t.tick();
        t.tick();check(t.out.stage==m::Stage::ready&&!t.out.held_device,"held A at weapon replacement cannot start a reload");
        t.main().secondary.current=false;t.tick();t.main().secondary.current=true;t.tick();
        check(t.out.stage==m::Stage::available&&!t.out.held_device,"fresh A after neutral works without forced support-grip release");}
    std::printf("%u physical reload interaction checks passed\n",checks);
}
