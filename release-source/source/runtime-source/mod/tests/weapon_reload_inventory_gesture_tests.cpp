// SPDX-License-Identifier: GPL-3.0-only
#include "input/manual_reload.hpp"
#include "input/paired_reload.hpp"
#include "input/weapon_reload_mechanism.hpp"
#include "t5/manual_reload_transfer.hpp"
#include "xr_math.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace m=bovr::input::manual;
namespace paired=bovr::input::paired_reload;
namespace native=bovr::t5::reload_transfer;
namespace xr=wawvr::xr;
using S=m::ReloadClassificationStatus;
using K=m::ReloadKind;

namespace {
struct InventoryRow {m::NativeWeaponReloadValues values;S expected;};
constexpr std::array<InventoryRow,15> inventory{{
#include "fixtures/synthetic_weapon_reload_inventory.inc"
}};
unsigned checks{},variants{},direct_rows{},paired_rows{},fuel_rows{},scenarios{},commits{},cycles{},recoveries{},partial_refusals{};
std::array<unsigned,5> kinds{};
std::string_view current_name;
const char* current_scenario="";
void check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %.*s [%s]: %s\n",static_cast<int>(current_name.size()),current_name.data(),current_scenario,why);std::exit(1);}
}

struct Policy {
    m::ReloadMechanism mechanism{};
    unsigned minimum_missing{1};
    bool fuel{},dual{};
};
Policy adapter_policy(m::NativeWeaponReloadValues values) {
    // Model only the adapter's scalar routing. These copies cannot establish
    // native ownership, the real dual partner, or a writable player allocation.
    Policy result{};result.dual=values.dual;result.fuel=values.fuel_tank;
    const auto raw=m::classify_native_reload(values);
    if(result.dual){check(raw.status==S::dual_requires_separate_hands,"dual data takes explicit paired route");values.dual=false;}
    auto classified=m::classify_native_reload(values);
    if(result.fuel){
        check(!result.dual&&classified.status==S::fuel_tank_requires_native_exchange&&values.reload_add==0,
              "timed fuel must take an explicit supported exchange route");
        values.fuel_tank=false;classified=m::classify_native_reload(values);
        check(classified.status==S::supported,"fuel base scalar mechanism remains supported");
        classified.mechanism={true,K::magazine,0,false,false};classified.minimum_missing_rounds=1;
    }
    check(classified.status==S::supported,"selected inventory scalar data is supported after adapter routing");
    result.mechanism=classified.mechanism;result.minimum_missing=classified.minimum_missing_rounds;
    if(result.dual)result.mechanism={true,K::magazine,0,result.mechanism.charge_on_empty,false};
    return result;
}

struct Gesture {
    m::Controller controller;
    xr::FrameState frame{};
    m::Context context{};
    m::Output out{};
    Gesture(const m::ReloadMechanism& mechanism,int capacity,int clip,int reserve) {
        context.valid=context.cod4_controls=true;context.weapon=17;
        context.capacity=capacity;context.clip=clip;context.reserve=reserve;context.mechanism=mechanism;
        context.epoch=7;context.generation=11;context.now=1000;context.shot_sequence=1;
        frame.views_valid=frame.actions.focused=true;frame.head_center.position={0,1.7f,0};
        for(auto& hand:frame.actions.hands){
            hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;
            hand.grip.orientation_valid=hand.grip.orientation_tracked=true;
            hand.grip.pose.position={.2f,1.3f,-.35f};hand.aim=hand.grip;
            hand.primary.active=hand.secondary.active=hand.squeeze.active=hand.trigger.active=true;
        }
        tick();check(out.supported&&out.stage==m::Stage::ready,"fresh tracked input admits the supplied mechanism");
    }
    xr::HandActionState& main(){return frame.actions.hands[1];}
    xr::HandActionState& off(){return frame.actions.hands[0];}
    void tick(){++frame.actions.sequence;context.now+=16;out=controller.update(frame,context);}
    void begin(){main().primary.current=true;tick();main().primary.current=false;tick();}
    void grab(){off().grip.pose.position=out.supply;off().squeeze.current=.8f;tick();}
    void insert(){off().grip.pose.position=out.receiver;tick();off().squeeze.current=0;tick();}
    void trigger_charge(){
        off().squeeze.current=0;off().grip.pose.position=out.handle;off().trigger.current=1;tick();
        off().grip.pose.position.z+=.08f;tick();off().trigger.current=0;tick();
    }
    void grip_pump(){
        off().trigger.current=0;off().grip.pose.position=out.handle;off().squeeze.current=.8f;tick();
        off().grip.pose.position.z+=.08f;tick();off().grip.pose.position=out.handle;tick();
    }
    void action(){if(context.mechanism.grip_pump)grip_pump();else trigger_charge();}
    void prepare_insert(){
        check(out.stage==m::Stage::available&&!out.request_commit,"reload starts at supply without a native transfer");
        grab();check(out.held_device&&!out.request_commit,"hip grip obtains a physical device without ammo");
        insert();
        if(!out.request_commit&&(out.stage==m::Stage::charge||out.stage==m::Stage::charging))action();
        check(out.request_commit&&out.stage==m::Stage::committing&&out.block_attack,
              "receiver release and any required charge produce a blocked commit request");
    }
    void ack(){context.commit_completed=true;tick();context.commit_completed=false;tick();}
    void cancel_through_death(){
        begin();check(out.stage==m::Stage::available,"pre-death A arms a fresh transaction");
        grab();check(out.held_device,"pre-death gesture has a held device to invalidate");
        const int clip=context.clip,reserve=context.reserve;
        main().primary.current=true;context.valid=false;tick();
        check(!out.supported&&!out.held_device&&!out.request_commit&&!out.block_attack,"death cancels the held reload completely");
        ++context.epoch;context.valid=true;tick();tick();
        check(out.supported&&out.stage==m::Stage::ready&&!out.held_device&&!out.request_commit,
              "new epoch with A/grip held cannot inherit or restart the cancelled reload");
        main().primary.current=false;off().squeeze.current=off().trigger.current=0;tick();
        check(context.clip==clip&&context.reserve==reserve,"death and rearm cannot manufacture ammunition");
        ++recoveries;
    }
};

native::AmmoValues ammo_values(const m::NativeWeaponReloadValues& v,const Policy& p,int clip,int reserve) {
    return {clip,reserve,v.capacity,v.reload_add,0,p.minimum_missing,p.fuel};
}
int interaction_reserve(const native::AmmoValues& ammo) {
    return !ammo.fuel_exchange&&ammo.capacity-ammo.clip<static_cast<int>(ammo.minimum_missing)?0:ammo.reserve;
}
bool repeated(K kind){return kind==K::internal_rounds||kind==K::break_action;}

void check_post_reload_cycle(Gesture& gesture) {
    if(!gesture.context.mechanism.cycle_after_shot||gesture.context.clip<1)return;
    gesture.off().squeeze.current=gesture.off().trigger.current=0;gesture.tick();
    --gesture.context.clip;++gesture.context.shot_sequence;gesture.tick();
    check(gesture.out.cycle_pending&&gesture.out.block_attack,"one validated post-reload shot requires its authored cycle");
    const int clip=gesture.context.clip,reserve=gesture.context.reserve;
    if(gesture.context.mechanism.grip_pump){
        gesture.trigger_charge();check(gesture.out.cycle_pending,"shotgun cannot cycle with the support trigger");
        gesture.grip_pump();check(gesture.out.pump_gripped,"complete grip pump retains the visual support attachment");
    }else{
        gesture.grip_pump();check(gesture.out.cycle_pending,"non-shotgun bolt/action cannot cycle with grip");
        gesture.trigger_charge();
    }
    check(!gesture.out.cycle_pending&&!gesture.out.block_attack&&!gesture.out.request_commit&&
          gesture.context.clip==clip&&gesture.context.reserve==reserve,"action enables the next shot without an extra ammo transfer");
    ++cycles;
}

void single_scenario(const m::NativeWeaponReloadValues& v,const Policy& policy,int clip,int reserve,bool recover) {
    ++scenarios;
    auto native_ammo=ammo_values(v,policy,clip,reserve);
    Gesture gesture(policy.mechanism,v.capacity,policy.fuel?0:clip,interaction_reserve(native_ammo));
    if(!gesture.context.reserve||(!policy.fuel&&clip==v.capacity)){
        gesture.begin();gesture.grab();gesture.insert();
        check(gesture.out.stage==m::Stage::ready&&!gesture.out.request_commit&&!gesture.out.block_attack,
              "full, no-reserve or native no-partial denial cannot start a physical reload");
        check(!native::plan(native_ammo,1).allowed,"native transfer independently refuses the denied reload");
        if(clip>0&&clip<v.capacity&&reserve>0)++partial_refusals;
        return;
    }
    if(recover)gesture.cancel_through_death();
    gesture.begin();
    unsigned inserted{};
    while(true){
        check(++inserted<=static_cast<unsigned>(v.capacity)+1,"bounded insert sequence must finish");
        gesture.prepare_insert();
        const auto transaction=gesture.out.transaction;
        const unsigned request=gesture.out.commit_rounds;
        const auto transfer=native::plan(native_ammo,request);
        check(transfer.allowed&&transfer.rounds==request,"controller commit amount matches independent native transfer plan");
        check(request==native::maximum_capacity||!native::plan(native_ammo,request+1).allowed,
              "the native amount is exact, not merely within capacity");
        gesture.tick();
        check(gesture.out.request_commit&&gesture.out.transaction==transaction&&gesture.out.commit_rounds==request,
              "waiting for acknowledgement cannot create another insertion or transaction");
        auto after=native_ammo;
        after.clip=policy.fuel?static_cast<int>(request):after.clip+static_cast<int>(request);
        after.reserve-=static_cast<int>(request);
        check(native::completed(transfer,native_ammo,after),"modelled native acknowledgement completes the exact selected transfer");
        if(policy.fuel)check(transfer.discarded==static_cast<unsigned>(native_ammo.clip)&&
            after.clip+after.reserve==native_ammo.clip+native_ammo.reserve-static_cast<int>(transfer.discarded),
            "fuel exchange accounts for discarded old fuel instead of claiming conservation");
        else check(after.clip+after.reserve==native_ammo.clip+native_ammo.reserve,"ordinary reload conserves clip plus reserve");
        native_ammo=after;++commits;
        gesture.context.clip=after.clip;gesture.context.reserve=interaction_reserve(after);gesture.ack();
        if(gesture.out.stage==m::Stage::available){
            check(repeated(policy.mechanism.kind)&&!gesture.out.request_commit,
                  "only repeated mechanisms reopen supply after an acknowledged native increment");
            continue;
        }
        if(gesture.out.stage==m::Stage::charge||gesture.out.stage==m::Stage::charging){
            check(!gesture.out.request_commit,"empty internal weapon charges after its inserts, without another transfer");
            gesture.action();
        }
        check(gesture.out.stage==m::Stage::ready&&!gesture.out.block_attack&&!gesture.out.request_commit,
              "acknowledged reload and any final cycle restore ready firing state");
        break;
    }
    check(native_ammo.clip>0,"completed reload leaves actual loaded ammunition");
    check_post_reload_cycle(gesture);
}

native::AmmoValues pair_ammo(const paired::Slot& slot) {
    const unsigned minimum=slot.no_partial?static_cast<unsigned>(
        slot.reload_add>0&&slot.reload_add<slot.capacity?slot.reload_add:slot.capacity):1U;
    return {slot.clip,slot.reserve,slot.capacity,slot.reload_add,slot.state,minimum,false};
}
paired::Snapshot pair_snapshot(const m::NativeWeaponReloadValues& v,int primary,int partner,int reserve) {
    // Synthetic distinct identities/shared pool exercise routing for every
    // recorded dual row. This does not certify the real partner or ownership.
    paired::Snapshot snapshot{};snapshot.key={0x2000,7,11,1};
    snapshot.hands[0]={17,21,19,0x1000,0x1100,primary,reserve,v.capacity,v.reload_add,v.start_add,0,v.no_partial,false};
    snapshot.hands[1]={18,22,19,0x1200,0x1300,partner,reserve,v.capacity,v.reload_add,v.start_add,0,v.no_partial,false};
    return snapshot;
}
void paired_scenario(const m::NativeWeaponReloadValues& v,const Policy& policy,int primary,int partner,int reserve,bool recover) {
    ++scenarios;
    auto snapshot=pair_snapshot(v,primary,partner,reserve);
    auto plan=paired::plan(snapshot);
    check(plan.status==paired::PlanStatus::supported||plan.status==paired::PlanStatus::no_transfer,
          "dual fixture produces a legitimate distinct-clip native plan");
    Gesture gesture(policy.mechanism,2*v.capacity,(!primary||!partner)?0:primary+partner,static_cast<int>(plan.total_rounds));
    if(!plan.allowed){
        gesture.begin();check(gesture.out.stage==m::Stage::ready&&!gesture.out.request_commit,"full/no-ammo pair cannot reload");return;
    }
    if(recover)gesture.cancel_through_death();
    gesture.begin();gesture.prepare_insert();
    check(gesture.out.commit_rounds==plan.total_rounds,"one physical paired gesture requests the ordered native total");
    snapshot.key.epoch=gesture.context.epoch;snapshot.key.transaction=gesture.out.transaction;
    plan=paired::plan(snapshot);paired::Transaction transaction;
    check(transaction.begin(snapshot,gesture.out.commit_rounds),"paired commit claims the exact planned total once");
    for(unsigned step=0;step<2&&transaction.stage()==paired::Stage::ready;++step){
        const auto request=transaction.claim_next(snapshot);
        check(request.allowed&&request.hand<2,"each paired native step owns a single hand");
        const auto& selected=plan.steps[request.hand];
        const auto before=pair_ammo(snapshot.hands[request.hand]);
        const auto after=pair_ammo(selected.after.hands[request.hand]);
        const auto transfer=native::plan(before,request.rounds);
        check(transfer.allowed&&native::completed(transfer,before,after),
              "each paired hand independently satisfies native amount and conservation checks");
        check(transaction.acknowledge(request,selected.after),"exact ordered paired acknowledgement is accepted");
        snapshot=selected.after;++commits;
    }
    check(transaction.stage()==paired::Stage::complete&&transaction.completed_rounds()==plan.total_rounds,
          "both eligible paired clips finish without double-spending shared reserve");
    const auto& a=snapshot.hands[0];const auto& b=snapshot.hands[1];
    check(a.clip+b.clip+a.reserve==primary+partner+reserve&&a.reserve==b.reserve,
          "paired gesture preserves the one shared reserve and both clip totals");
    gesture.context.clip=(!a.clip||!b.clip)?0:a.clip+b.clip;
    gesture.context.reserve=static_cast<int>(paired::plan(snapshot).total_rounds);gesture.ack();
    check(gesture.out.stage==m::Stage::ready&&!gesture.out.block_attack&&!gesture.out.request_commit,
          "paired native acknowledgement restores ready even when limited reserve fills only one hand");
    check(!transaction.begin(snapshot,plan.total_rounds),"completed paired transaction cannot be replayed");
}

void exercise(const m::NativeWeaponReloadValues& values) {
    current_name=values.internal_name;
    const auto policy=adapter_policy(values);++variants;++kinds[static_cast<unsigned>(policy.mechanism.kind)];
    if(policy.dual){
        ++paired_rows;
        current_scenario="paired empty/death recovery";paired_scenario(values,policy,0,0,2*values.capacity+3,true);
        current_scenario="paired partial";paired_scenario(values,policy,values.capacity/2,values.capacity/2,2*values.capacity,false);
        current_scenario="paired primary full/limited partner";paired_scenario(values,policy,values.capacity,0,std::max(1,values.capacity/2),false);
        current_scenario="paired full denial";paired_scenario(values,policy,values.capacity,values.capacity,2*values.capacity,false);
    }else{
        if(policy.fuel)++fuel_rows;else ++direct_rows;
        current_scenario="empty/death recovery";single_scenario(values,policy,0,values.capacity+3,true);
        current_scenario="partial or full timed tank";single_scenario(values,policy,policy.fuel?values.capacity:values.capacity/2,values.capacity+3,false);
        current_scenario="empty/limited reserve";single_scenario(values,policy,0,std::max(1,values.capacity/3),false);
        current_scenario="no reserve denial";single_scenario(values,policy,0,0,false);
        if(!policy.fuel){current_scenario="full denial";single_scenario(values,policy,values.capacity,values.capacity,false);}
    }
}
} // namespace

int main() {
    static_assert(inventory.size()==15,"authored fixture covers each feed and special routing class");
    for(const auto& row:inventory){
        current_name=row.values.internal_name;current_scenario="inventory admission";
        check(m::classify_native_reload(row.values).status==row.expected,"authored row retains its independent expected classifier status");
        exercise(row.values);
    }
    current_name="authored inventory";current_scenario="coverage";
    check(variants==15,"all authored boundary candidates replay gestures");
    check(direct_rows==13&&paired_rows==1&&fuel_rows==1,"coverage retains explicit direct, paired and timed-fuel routes");
    check(recoveries==variants,"every candidate survives cancellation through death and fresh epoch rearm");
    for(const unsigned count:kinds)check(count>0,"every authored reload kind has gesture coverage");
    check(cycles>0&&partial_refusals>0,"cycle controls and native no-partial refusal were exercised");
    std::printf("weapon reload inventory gestures: %u checks, %u variants, %u scenarios, %u modelled native commits, "
        "%u cycles, %u death recoveries; direct=%u paired=%u fuel=%u noPartial refusals=%u\n",
        checks,variants,scenarios,commits,cycles,recoveries,direct_rows,paired_rows,fuel_rows,partial_refusals);
    // No native calls occur here: acknowledgements are value-modelled transfer
    // results, not evidence of live asset admission, game writes or VR imagery.
    return 0;
}
