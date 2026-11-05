// SPDX-License-Identifier: GPL-3.0-only
#include "input/paired_reload.hpp"
#include "t5/manual_reload_transfer.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace p=bovr::input::paired_reload;
namespace native=bovr::t5::reload_transfer;
unsigned checks{};
void check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
p::Snapshot pair(int right=0,int left=0,int reserve=30) {
    p::Snapshot s{};s.key={0x2000,1,2,3};
    s.hands[0]={17,11,9,0x1000,0x1100,right,reserve,7,0,0,0,false,false};
    s.hands[1]={18,12,9,0x1200,0x1300,left,reserve,7,0,0,0,false,false};
    return s;
}
native::AmmoValues native_values(const p::Slot& s) {
    const unsigned minimum=s.no_partial?
        static_cast<unsigned>(s.reload_add>0&&s.reload_add<s.capacity?s.reload_add:s.capacity):1U;
    return {s.clip,s.reserve,s.capacity,s.reload_add,s.state,minimum,false};
}
void verify_native_steps(const p::Plan& result) {
    for(const auto& step:result.steps) {
        const auto before=native_values(step.before.hands[step.hand]);
        const auto after=native_values(step.after.hands[step.hand]);
        const auto expected=native::plan(before,step.rounds);
        check(expected.allowed==(step.rounds>0),"paired amount agrees with guarded single-hand native policy");
        if(step.rounds)check(native::completed(expected,before,after),"single-hand native conservation holds");
        else check(step.before==step.after,"skipped hand changes no state");
    }
}
void planning_examples() {
    auto result=p::plan(pair());
    check(result.allowed&&result.status==p::PlanStatus::supported,"ordinary M1911 pair supported");
    check(result.shared_pool&&result.total_rounds==14&&result.reserve_total==30,"shared reserve counted once");
    check(result.virtual_clip==0&&result.virtual_capacity==14,"physical clip is summed pair");
    check(result.steps[0].hand==0&&result.steps[0].rounds==7,"primary is first");
    check(result.steps[0].after.hands[0].reserve==23&&result.steps[0].after.hands[1].reserve==23,
          "primary withdrawal reflected in both shared reserve copies");
    check(result.steps[1].before==result.steps[0].after,"partner begins after primary");
    check(result.steps[1].after.hands[0].clip==7&&result.steps[1].after.hands[1].clip==7&&
          result.steps[1].after.hands[0].reserve==16,"two native batches fill pair exactly");
    verify_native_steps(result);

    result=p::plan(pair(0,0,10));
    check(result.steps[0].rounds==7&&result.steps[1].rounds==3&&result.total_rounds==10,
          "limited shared reserve consumed in native hand order");
    check(result.steps[1].after.hands[0].reserve==0&&result.steps[1].after.hands[1].reserve==0,
          "limited shared pool never borrowed twice");
    verify_native_steps(result);
    result=p::plan(pair(0,0,4));
    check(result.steps[0].rounds==4&&result.steps[1].rounds==0,"primary can exhaust shared reserve");
    verify_native_steps(result);

    result=p::plan(pair(7,0,5));
    check(result.allowed&&result.steps[0].rounds==0&&result.steps[1].rounds==5,
          "full primary does not block partner reload");
    check(result.virtual_clip==7&&result.total_rounds==5,"partner-only physical inventory remains honest");
    verify_native_steps(result);
    result=p::plan(pair(0,7,20));
    check(result.allowed&&result.steps[0].rounds==7&&result.steps[1].rounds==0,
          "full partner requires only primary helper");
    verify_native_steps(result);

    auto separate=pair(3,2,1);
    separate.hands[1].pool_index=10;separate.hands[1].reserve=10;
    result=p::plan(separate);
    check(result.allowed&&!result.shared_pool&&result.reserve_total==11,"separate reserves counted separately");
    check(result.steps[0].rounds==1&&result.steps[1].rounds==5,"each hand uses its own reserve");
    check(result.steps[0].after.hands[1].reserve==10&&result.steps[1].after.hands[0].reserve==0,
          "distinct native pool is not mutated by other hand");
    verify_native_steps(result);

    auto constrained=pair(5,0,20);
    constrained.hands[0].no_partial=true;
    result=p::plan(constrained);
    check(result.allowed&&result.steps[0].rounds==0&&result.steps[1].rounds==7,
          "noPartial skips blocked hand while eligible partner proceeds");
    constrained.hands[0].reload_add=2;
    result=p::plan(constrained);
    check(result.steps[0].rounds==2,"noPartial permits native minimum increment");
    constrained.hands[0].reload_add=3;
    result=p::plan(constrained);
    check(result.steps[0].rounds==0&&result.steps[1].rounds==7,"noPartial native increment enforced");
    constrained.hands[0].clip=0;constrained.hands[0].reload_add=0;
    constrained.hands[1].no_partial=true;constrained.hands[1].clip=3;
    result=p::plan(constrained);
    check(result.steps[0].rounds==7&&result.steps[1].rounds==0,"blocked partner does not prevent primary");
    verify_native_steps(result);

    auto increments=pair(0,0,20);
    increments.hands[0].reload_add=2;increments.hands[1].reload_add=3;
    increments.hands[0].start_add=6;increments.hands[1].start_add=7;
    result=p::plan(increments);
    check(result.total_rounds==5&&result.steps[0].rounds==2&&result.steps[1].rounds==3,
          "normal native batches use reloadAdd rather than reloadStartAdd");
    verify_native_steps(result);
    increments.hands[0].reload_add=7;increments.hands[1].reload_add=4096;
    result=p::plan(increments);
    check(result.total_rounds==14,"reloadAdd at or beyond capacity fills capacity");
    verify_native_steps(result);
    check(p::plan(pair(7,7)).status==p::PlanStatus::no_transfer,"full pair unavailable");
    check(p::plan(pair(0,0,0)).status==p::PlanStatus::no_transfer,"empty reserve unavailable");
}
void invalid_plans() {
    for(unsigned field=0;field<4;++field) {
        auto s=pair();
        if(field==0)s.key.owner=0;
        if(field==1)s.key.epoch=0;
        if(field==2)s.key.generation=0;
        if(field==3)s.key.transaction=0;
        check(p::plan(s).status==p::PlanStatus::invalid_identity,"all identity components required");
    }
    for(unsigned field=0;field<3;++field) {
        auto s=pair();
        if(field==0)s.hands[1].weapon=s.hands[0].weapon;
        if(field==1)s.hands[1].variant=s.hands[0].variant;
        if(field==2)s.hands[1].definition=s.hands[0].definition;
        check(p::plan(s).status==p::PlanStatus::invalid_identity,"partner requires distinct native identities");
    }
    auto s=pair();s.hands[1].clip_index=s.hands[0].clip_index;
    check(p::plan(s).status==p::PlanStatus::clip_alias,"same native clip cannot be transferred twice");
    s=pair();s.hands[1].reserve--;
    check(p::plan(s).status==p::PlanStatus::shared_pool_mismatch,"shared reserve snapshots must agree");
    for(const int state:std::array<int,7>{-1,1,5,9,13,14,100}) {
        s=pair();s.hands[1].state=state;
        check(p::plan(s).status==p::PlanStatus::unsupported_state,"unsafe partner state rejects whole plan");
    }
    for(const int state:std::array<int,4>{0,6,7,8}) {
        s=pair();s.hands[0].state=state;s.hands[1].state=state;
        check(p::plan(s).allowed,"verified ordinary native states supported");
    }
    for(unsigned field=0;field<17;++field) {
        s=pair();auto& h=s.hands[1];
        switch(field) {
            case 0:h.weapon=0;break;
            case 1:h.weapon=256;break;
            case 2:h.variant=0;break;
            case 3:h.definition=0;break;
            case 4:h.clip=-1;break;
            case 5:h.clip=8;break;
            case 6:h.reserve=-1;break;
            case 7:h.reserve=100001;break;
            case 8:h.capacity=0;break;
            case 9:h.capacity=4097;break;
            case 10:h.reload_add=-1;break;
            case 11:h.reload_add=4097;break;
            case 12:h.start_add=-1;break;
            case 13:h.start_add=4097;break;
            case 14:h.clip_index=65536;break;
            case 15:h.pool_index=65536;break;
            case 16:h.fuel_tank=true;break;
        }
        check(!p::plan(s).allowed,"invalid slot metadata rejected before native work");
    }
    s=pair();s.hands[0].capacity=4090;
    check(p::plan(s).status==p::PlanStatus::invalid_slot,"aggregate physical capacity bounded");
    s=pair();s.hands[0].capacity=2048;s.hands[1].capacity=2048;
    s.hands[0].reserve=100000;s.hands[1].reserve=100000;
    check(p::plan(s).total_rounds==4096,"exact aggregate capacity limit accepted without overflow");
}
void complete_transaction() {
    const auto s=pair();const auto expected=p::plan(s);
    p::Transaction tx;
    check(tx.stage()==p::Stage::idle&&!tx.claim_next(s).allowed,"idle cannot claim native helper");
    check(!tx.begin(s,expected.total_rounds-1),"physical amount must match native paired batch");
    check(tx.begin(s,expected.total_rounds)&&tx.stage()==p::Stage::ready,"matching gesture begins");
    check(!tx.begin(s),"pending gesture cannot be replaced");
    auto right=tx.claim_next(s);
    check(right.allowed&&right.hand==0&&right.rounds==7&&tx.attempted_mask()==1,"primary helper claimed once");
    check(!tx.claim_next(s).allowed&&!tx.begin(s),"pending native call cannot be replayed");
    auto wrong=right;wrong.key.transaction++;
    check(!tx.acknowledge(wrong,expected.steps[0].after)&&tx.stage()==p::Stage::claimed,
          "stale acknowledgement cannot advance current claim");
    wrong=right;wrong.hand=1;
    check(!tx.acknowledge(wrong,expected.steps[0].after),"other hand cannot acknowledge primary");
    check(tx.acknowledge(right,expected.steps[0].after)&&tx.completed_mask()==1&&tx.completed_rounds()==7,
          "exact primary result acknowledged");
    check(!tx.acknowledge(right,expected.steps[0].after),"duplicate acknowledgement has no effect");
    auto left=tx.claim_next(expected.steps[0].after);
    check(left.allowed&&left.hand==1&&left.rounds==7&&tx.attempted_mask()==3,"partner claimed only after primary proof");
    check(tx.acknowledge(left,expected.steps[1].after),"exact partner result acknowledged");
    check(tx.stage()==p::Stage::complete&&tx.outcome()==p::Outcome::complete&&
          tx.completed_mask()==3&&tx.completed_rounds()==14,"both native transfers complete one gesture");
    check(!tx.claim_next(expected.steps[1].after).allowed&&!tx.begin(s),"completed transaction cannot replay");
    auto older=s;older.key.transaction--;
    check(!tx.begin(older),"old serial cannot reopen completed transaction");
    auto newer=s;newer.key.transaction++;
    check(tx.begin(newer),"new user gesture may begin after prior completion");
    tx.cancel();
    check(tx.stage()==p::Stage::stopped&&tx.outcome()==p::Outcome::failed&&tx.attempted_mask()==0,
          "cancel before native call has no side effects");
    check(!tx.begin(newer),"cancelled gesture cannot be reused");
    auto respawn=s;respawn.key.epoch++;
    check(tx.begin(respawn),"fresh native lifetime can start a new transaction scope");
}
void partner_only() {
    for(unsigned active=0;active<2;++active) {
        const auto s=active?pair(7,0):pair(0,7);const auto expected=p::plan(s);
        p::Transaction tx;check(tx.begin(s),"single eligible hand begins paired gesture");
        const auto request=tx.claim_next(s);
        check(request.allowed&&request.hand==active&&request.rounds==7,"zero transfer hand skipped without helper");
        check(tx.acknowledge(request,expected.steps[active].after),"eligible hand exact transfer acknowledged");
        check(tx.stage()==p::Stage::complete&&tx.attempted_mask()==(1U<<active)&&
              tx.completed_mask()==(1U<<active),"one helper completes one eligible hand only");
    }
}
void failed_transactions() {
    const auto s=pair();const auto expected=p::plan(s);
    p::Transaction tx;check(tx.begin(s),"failure test begins");
    auto right=tx.claim_next(s);
    check(!tx.acknowledge(right,s,false)&&tx.outcome()==p::Outcome::failed,
          "primary fault with unchanged snapshot stops without transfer claim");
    check(tx.completed_rounds()==0&&tx.attempted_mask()==1&&!tx.begin(s),"failed primary never replays same gesture");

    tx=p::Transaction{};check(tx.begin(s),"partial failure test begins");
    right=tx.claim_next(s);check(tx.acknowledge(right,expected.steps[0].after),"primary succeeds before partner failure");
    auto left=tx.claim_next(expected.steps[0].after);
    check(!tx.acknowledge(left,expected.steps[0].after,false)&&tx.outcome()==p::Outcome::partial,
          "unchanged failed partner preserves confirmed primary transfer");
    check(tx.completed_rounds()==7&&tx.completed_mask()==1&&tx.attempted_mask()==3,
          "partial accounting retains exactly one known transfer");
    check(!tx.claim_next(expected.steps[0].after).allowed&&!tx.begin(s),"partial failure cannot replay completed primary");
    auto retry=expected.steps[0].after;retry.key.transaction++;
    check(tx.begin(retry)&&tx.selected_plan().total_rounds==7,"new gesture replans actual remaining inventory");
    left=tx.claim_next(retry);
    check(left.allowed&&left.hand==1,"new gesture skips already full primary");
    check(tx.acknowledge(left,tx.selected_plan().steps[1].after)&&tx.completed_rounds()==7,
          "new gesture transfers only remaining partner ammunition");

    tx=p::Transaction{};check(tx.begin(s),"uncertain failure test begins");
    right=tx.claim_next(s);auto changed=s;changed.hands[0].clip=1;
    check(!tx.acknowledge(right,changed)&&tx.outcome()==p::Outcome::uncertain&&tx.completed_mask()==0,
          "unexpected partial mutation is uncertain and cannot be called complete");
    check(!tx.claim_next(s).allowed&&!tx.begin(s),"uncertain attempt cannot retry");

    tx=p::Transaction{};check(tx.begin(s),"fault after transfer test begins");
    right=tx.claim_next(s);
    check(!tx.acknowledge(right,expected.steps[0].after,false)&&tx.outcome()==p::Outcome::uncertain,
          "native fault after exact effect stops transaction as uncertain");
    check(tx.completed_rounds()==7&&tx.completed_mask()==1,"known effect retained despite native fault");
    check(!tx.claim_next(expected.steps[0].after).allowed,"native fault never proceeds into partner");

    tx=p::Transaction{};check(tx.begin(s),"pending cancellation begins");
    right=tx.claim_next(s);tx.cancel();
    check(tx.outcome()==p::Outcome::uncertain&&!tx.acknowledge(right,expected.steps[0].after),
          "cancellation during pending native call cannot imply rollback");
    check(!tx.begin(s),"pending cancellation cannot reopen gesture");

    tx=p::Transaction{};check(tx.begin(s),"ready partial cancellation begins");
    right=tx.claim_next(s);check(tx.acknowledge(right,expected.steps[0].after),"primary before ready cancellation");
    tx.cancel();
    check(tx.outcome()==p::Outcome::partial&&tx.completed_rounds()==7&&tx.attempted_mask()==1,
          "cancel between helpers retains known primary transfer");
}
void stale_snapshots() {
    const auto s=pair();const auto expected=p::plan(s);
    for(unsigned field=0;field<10;++field) {
        auto changed=s;
        switch(field) {
            case 0:changed.key.owner++;break;
            case 1:changed.key.epoch++;break;
            case 2:changed.key.generation++;break;
            case 3:changed.key.transaction++;break;
            case 4:changed.hands[1].variant++;break;
            case 5:changed.hands[1].definition++;break;
            case 6:changed.hands[1].clip++;break;
            case 7:changed.hands[1].reserve--;break;
            case 8:changed.hands[1].state=6;break;
            case 9:changed.hands[1].pool_index++;break;
        }
        p::Transaction tx;check(tx.begin(s),"stale snapshot test begins");
        check(!tx.claim_next(changed).allowed&&tx.stage()==p::Stage::stopped&&tx.attempted_mask()==0,
              "changed identity inventory or state prevents native call");
    }
    p::Transaction tx;check(tx.begin(s),"between helper mutation test begins");
    const auto right=tx.claim_next(s);check(tx.acknowledge(right,expected.steps[0].after),"primary proves before native drift");
    auto changed=expected.steps[0].after;changed.hands[1].clip++;
    check(!tx.claim_next(changed).allowed&&tx.outcome()==p::Outcome::partial&&tx.attempted_mask()==1,
          "partner drift aborts without repeating primary or claiming partner");
    tx=p::Transaction{};check(tx.begin(s),"stale acknowledgement snapshot begins");
    const auto request=tx.claim_next(s);changed=expected.steps[0].after;changed.key.generation++;
    check(!tx.acknowledge(request,changed)&&tx.outcome()==p::Outcome::uncertain&&tx.completed_mask()==0,
          "new native generation cannot prove transfer for former owner");
}
void bounded_inventory_grid() {
    // Small exhaustive inventories verify conservation and native agreement
    // across partial magazines, both reserve models, and native insert batches.
    for(bool shared:{false,true})for(bool no_partial:{false,true})
    for(int right=0;right<=7;++right)for(int left=0;left<=7;++left)
    for(int reserve=0;reserve<=16;++reserve)for(int add:std::array<int,4>{0,1,3,7}) {
        auto s=pair(right,left,reserve);
        s.hands[0].reload_add=add;s.hands[1].reload_add=add;
        s.hands[0].no_partial=no_partial;s.hands[1].no_partial=no_partial;
        if(!shared){s.hands[1].pool_index=10;s.hands[1].reserve=16-reserve;}
        const auto result=p::plan(s);
        check(result.status==p::PlanStatus::supported||result.status==p::PlanStatus::no_transfer,
              "valid inventory grid always yields supported or no-transfer plan");
        verify_native_steps(result);
        const auto& after=result.steps[1].after;
        const int initial_reserve=shared?reserve:s.hands[0].reserve+s.hands[1].reserve;
        const int remaining_reserve=shared?after.hands[0].reserve:after.hands[0].reserve+after.hands[1].reserve;
        check(after.hands[0].clip+after.hands[1].clip+remaining_reserve==right+left+initial_reserve,
              "full pair conserves ammunition across shared and separate pools");
        check(after.hands[0].clip<=7&&after.hands[1].clip<=7&&after.hands[0].reserve>=0&&after.hands[1].reserve>=0,
              "no slot overfill or reserve underflow");
        check(result.total_rounds==static_cast<unsigned>(initial_reserve-remaining_reserve)&&
              result.allowed==(result.total_rounds>0),"paired request equals exact reserve withdrawal");
    }
}
int main() {
    planning_examples();invalid_plans();complete_transaction();partner_only();
    failed_transactions();stale_snapshots();bounded_inventory_grid();
    std::printf("paired_reload_tests: %u checks passed\n",checks);return 0;
}
