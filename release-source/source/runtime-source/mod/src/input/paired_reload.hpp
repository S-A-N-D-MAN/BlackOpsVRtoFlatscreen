// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace bovr::input::paired_reload {
// Opaque copied identity only. This policy never dereferences a native address,
// selects a native hand, calls the engine, or establishes a native allocation.
struct Key {
    std::uint64_t owner{},epoch{},generation{},transaction{};
    friend bool operator==(const Key&,const Key&)=default;
};
struct Slot {
    unsigned weapon{},clip_index{},pool_index{};
    std::uint64_t variant{},definition{};
    int clip{},reserve{},capacity{},reload_add{},start_add{},state{};
    bool no_partial{},fuel_tank{};
    friend bool operator==(const Slot&,const Slot&)=default;
};
struct Snapshot {
    Key key{};
    std::array<Slot,2> hands{}; // primary, then its current native partner
    friend bool operator==(const Snapshot&,const Snapshot&)=default;
};
enum class PlanStatus : unsigned {
    supported,invalid_identity,invalid_slot,clip_alias,shared_pool_mismatch,
    unsupported_state,no_transfer
};
struct Step {
    Snapshot before{},after{};
    unsigned hand{},rounds{}; // native bRunLeftGun value, not XR hand enum
    friend bool operator==(const Step&,const Step&)=default;
};
struct Plan {
    std::array<Step,2> steps{};
    unsigned total_rounds{},virtual_clip{},virtual_capacity{},reserve_total{};
    bool shared_pool{},allowed{};
    PlanStatus status{PlanStatus::invalid_identity};
};
inline bool valid(const Key& k) noexcept {return k.owner&&k.epoch&&k.generation&&k.transaction;}
inline bool same_scope(const Key& a,const Key& b) noexcept {
    return a.owner==b.owner&&a.epoch==b.epoch&&a.generation==b.generation;
}
inline bool valid(const Slot& s) noexcept {
    return s.weapon>0&&s.weapon<256&&s.variant&&s.definition&&s.clip_index<65536&&s.pool_index<65536&&
        s.capacity>0&&s.capacity<=4096&&s.clip>=0&&s.clip<=s.capacity&&s.reserve>=0&&s.reserve<=100000&&
        s.reload_add>=0&&s.reload_add<=4096&&s.start_add>=0&&s.start_add<=4096&&!s.fuel_tank;
}
inline bool ordinary_state(int state) noexcept {return state==0||state==6||state==7||state==8;}
inline unsigned native_rounds(const Slot& s) noexcept {
    if(!valid(s)||!ordinary_state(s.state))return 0;
    const int missing=s.capacity-s.clip;
    const int increment=s.reload_add>0&&s.reload_add<s.capacity?s.reload_add:s.capacity;
    if(s.no_partial&&missing<increment)return 0;
    return static_cast<unsigned>(std::min({missing,s.reserve,increment}));
}
inline Plan plan(const Snapshot& before) noexcept {
    Plan p{};
    if(!valid(before.key)||before.hands[0].weapon==before.hands[1].weapon||
       before.hands[0].variant==before.hands[1].variant||before.hands[0].definition==before.hands[1].definition)return p;
    for(const auto& h:before.hands)if(!valid(h)){p.status=PlanStatus::invalid_slot;return p;}
    if(before.hands[0].capacity+before.hands[1].capacity>4096){p.status=PlanStatus::invalid_slot;return p;}
    if(before.hands[0].clip_index==before.hands[1].clip_index){p.status=PlanStatus::clip_alias;return p;}
    p.shared_pool=before.hands[0].pool_index==before.hands[1].pool_index;
    if(p.shared_pool&&before.hands[0].reserve!=before.hands[1].reserve){p.status=PlanStatus::shared_pool_mismatch;return p;}
    for(const auto& h:before.hands)if(!ordinary_state(h.state)){p.status=PlanStatus::unsupported_state;return p;}
    p.virtual_clip=static_cast<unsigned>(before.hands[0].clip+before.hands[1].clip);
    p.virtual_capacity=static_cast<unsigned>(before.hands[0].capacity+before.hands[1].capacity);
    p.reserve_total=static_cast<unsigned>(before.hands[0].reserve+(p.shared_pool?0:before.hands[1].reserve));
    auto current=before;
    for(unsigned hand=0;hand<2;++hand){
        auto& step=p.steps[hand];step.before=current;step.hand=hand;
        step.rounds=native_rounds(current.hands[hand]);
        current.hands[hand].clip+=static_cast<int>(step.rounds);
        current.hands[hand].reserve-=static_cast<int>(step.rounds);
        if(p.shared_pool)current.hands[hand^1].reserve=current.hands[hand].reserve;
        step.after=current;p.total_rounds+=step.rounds;
    }
    p.allowed=p.total_rounds>0;p.status=p.allowed?PlanStatus::supported:PlanStatus::no_transfer;
    // For one paired gesture, the physical Controller can use these summed
    // clip/capacity values and total_rounds as its available reserve. Native
    // inventory remains separately validated by the exact ordered snapshots.
    return p;
}
enum class Stage : unsigned {idle,ready,claimed,complete,stopped};
enum class Outcome : unsigned {none,complete,partial,failed,uncertain};
struct Request {
    Key key{};unsigned hand{},rounds{};bool allowed{};
    friend bool operator==(const Request&,const Request&)=default;
};
// Owner-thread value state. Claim before the native call; acknowledge only
// after restoring bRunLeftGun and rechecking native identity under the server
// barrier. There is intentionally no reset that reopens this transaction.
class Transaction {
public:
    bool begin(const Snapshot& before,unsigned requested_rounds=0) noexcept {
        if(stage_==Stage::ready||stage_==Stage::claimed)return false;
        if(have_key_&&same_scope(last_key_,before.key)&&before.key.transaction<=last_key_.transaction)return false;
        const auto next=plan(before);
        if(!next.allowed||(requested_rounds&&requested_rounds!=next.total_rounds))return false;
        plan_=next;last_key_=before.key;have_key_=true;next_=0;completed_=attempted_=rounds_=0;
        request_={};outcome_=Outcome::none;stage_=Stage::ready;skip_empty();return true;
    }
    Request claim_next(const Snapshot& current) noexcept {
        if(stage_!=Stage::ready)return {};
        if(next_>=2||current!=plan_.steps[next_].before){stop(false);return {};}
        const auto& step=plan_.steps[next_];
        request_={last_key_,step.hand,step.rounds,true};attempted_|=1U<<next_;
        stage_=Stage::claimed;return request_;
    }
    bool acknowledge(const Request& request,const Snapshot& after,bool native_returned=true) noexcept {
        if(stage_!=Stage::claimed||request!=request_||!request.allowed)return false;
        const auto& step=plan_.steps[next_];
        const bool exact=after==step.after;
        if(exact){completed_|=1U<<next_;rounds_+=step.rounds;}
        // A fault or changed post-state ends this gesture. Never infer rollback
        // or replay an attempted helper, even if its effects are uncertain.
        if(!native_returned||!exact){stop(after!=step.before);return false;}
        request_={};++next_;stage_=Stage::ready;skip_empty();return true;
    }
    void cancel() noexcept {
        if(stage_==Stage::ready||stage_==Stage::claimed)stop(stage_==Stage::claimed);
    }
    Stage stage()const noexcept{return stage_;}
    Outcome outcome()const noexcept{return outcome_;}
    unsigned completed_mask()const noexcept{return completed_;}
    unsigned attempted_mask()const noexcept{return attempted_;}
    unsigned completed_rounds()const noexcept{return rounds_;}
    const Plan& selected_plan()const noexcept{return plan_;}
private:
    void skip_empty() noexcept {
        while(next_<2&&!plan_.steps[next_].rounds)++next_;
        if(next_==2){stage_=Stage::complete;outcome_=Outcome::complete;}
    }
    void stop(bool uncertain) noexcept {
        stage_=Stage::stopped;request_={};outcome_=uncertain?Outcome::uncertain:(rounds_?Outcome::partial:Outcome::failed);
    }
    Plan plan_{};Key last_key_{};Request request_{};
    Stage stage_{Stage::idle};Outcome outcome_{Outcome::none};
    unsigned next_{},completed_{},attempted_{},rounds_{};bool have_key_{};
};
} // namespace bovr::input::paired_reload
