// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_flag_policy.hpp"
#include <array>
#include <cstdint>
#include <cstdio>

namespace policy=bovr::t5::manual_reload_flag_policy;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* label) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}
}
constexpr std::uint32_t disable_reload=0x02000000;
struct Role {std::uint32_t flags{};bool owned{};unsigned writes{};};

// This pure fixture models publication only after successful native write and
// readback. It does not claim to exercise the adapter/native transfer itself.
bool acquire(Role& role,bool write_succeeds=true) {
    const auto decision=policy::decide((role.flags&disable_reload)!=0,role.owned);
    if(decision.write_disable){
        if(!write_succeeds)return false;
        role.flags|=disable_reload;++role.writes;
    }
    role.owned=decision.own_after;
    return true;
}
void retire(Role& role) {
    if(role.owned)role.flags&=~disable_reload;
    role.owned=false;
}

void decision_table() {
    constexpr std::array<policy::FlagDecision,4> expected{{{true,true},{false,false},{true,true},{false,true}}};
    for(unsigned input=0;input<4;++input){
        const bool set=(input&1)!=0,owned=(input&2)!=0;
        const auto result=policy::decide(set,owned);
        check(result.write_disable==expected[input].write_disable&&result.own_after==expected[input].own_after,
              "all four per-role bit and proven-owner decisions are explicit");
    }
}

void mixed_roles() {
    // Every predicted/server mixed bit pattern, independently crossed with
    // every proven owner pattern; distinct other flags catch collateral edits.
    for(unsigned bits=0;bits<4;++bits)for(unsigned owners=0;owners<4;++owners){
        constexpr std::array<std::uint32_t,2> other{0x01800800,0x41000004};
        std::array<Role,2> roles{};
        for(unsigned role=0;role<2;++role){
            const auto mask=1U<<role;
            roles[role]={other[role]|((bits&mask)?disable_reload:0),(owners&mask)!=0,0};
            check(acquire(roles[role]),"preexisting unknown bit does not reject this flag-policy step");
            check(roles[role].owned==((owners&mask)!=0||(bits&mask)==0),
                  "only existing matching owner or role actually written becomes owned");
            check(roles[role].writes==((bits&mask)?0U:1U),"set roles are never redundantly rewritten");
            check(roles[role].flags==(other[role]|disable_reload),"acquisition preserves every unrelated flag");
        }
        // An explicit reload neither needs to adopt the unknown flag nor
        // remove it: repeating the real decision with the same role must keep
        // its ownership provenance unchanged throughout that operation.
        for(unsigned role=0;role<2;++role){
            const auto mask=1U<<role;
            const auto flags=roles[role].flags;const auto owned=roles[role].owned;
            const auto writes=roles[role].writes;
            check(acquire(roles[role])&&roles[role].flags==flags&&roles[role].owned==owned&&roles[role].writes==writes,
                  "explicit reload admission does not adopt or clear an unknown set bit");
            retire(roles[role]);
            const bool unknown_original=(bits&mask)!=0&&(owners&mask)==0;
            check(roles[role].flags==(other[role]|(unknown_original?disable_reload:0))&&!roles[role].owned,
                  "retirement removes owned-only flag and leaves unknown native bit and other flags intact");
            const auto retired=roles[role].flags;retire(roles[role]);
            check(roles[role].flags==retired,"repeated retirement cannot consume retained native ownership");
        }
    }
}

void write_and_identity_boundaries() {
    {Role role{0x1800000,false,0};
        check(!acquire(role,false)&&role.flags==0x1800000&&!role.owned&&!role.writes,
              "failed native write cannot publish a new lease from a proposed decision");
        retire(role);check(role.flags==0x1800000,"failed acquisition leaves no flag for retirement");}
    {Role role{disable_reload|0x80,false,0};
        check(acquire(role,false)&&!role.owned&&!role.writes&&role.flags==(disable_reload|0x80),
              "unknown set bit requests no flag write and no ownership adoption at this policy boundary");}
    {Role role{disable_reload,true,0};
        // After identity loss, the adapter must pass current_owned=false;
        // remembered ownership from a different role is never sufficient.
        role.owned=false;
        check(acquire(role)&&!role.owned&&!role.writes,"foreign or retired role identity cannot reclaim a preexisting bit");
        retire(role);check(role.flags==disable_reload,"identity replacement leaves the unknown native bit untouched");}
    {Role role{0,true,0};
        check(acquire(role)&&role.owned&&role.writes==1&&role.flags==disable_reload,
              "still-proven owner may reapply a currently cleared flag through a verified write");
        retire(role);check(role.flags==0,"reapplied known ownership retires normally");}
}
}
int main() {
    decision_table();mixed_roles();write_and_identity_boundaries();
    std::printf("manual_reload_flag_policy_checks=%u failures=%u native_calls=0 native_writes=0\n",checks,failures);
    return failures?1:0;
}
