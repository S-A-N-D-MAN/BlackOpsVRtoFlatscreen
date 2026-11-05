// SPDX-License-Identifier: GPL-3.0-only
#include "t5/primary_support_rearm.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace pa=bovr::input::profile_admission;
namespace sr=bovr::t5::primary_support_rearm;
namespace tracking=bovr::t5::primary_weapon_tracking;
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}

bovr::t5::command_profile::Copy profile(bool linked=false){
    bovr::t5::command_profile::Copy p{};
    p.profile={0,1,4,72,78,82,pa::policy_revision};
    p.identity.cg=1;p.identity.owner=2;p.identity.object=3;p.identity.variant=4;p.identity.definition=5;
    p.identity.server=6;p.identity.table=7;p.identity.arena=8;p.identity.pool=1;p.identity.tag=1;
    p.identity.duplicates=1;p.identity.duplicate_size=21;
    p.identity.tracking_mode=static_cast<unsigned>(linked?tracking::Mode::linked:tracking::Mode::ordinary);
    p.identity.server_link=linked?0x306ed00:0;
    p.native.weapon_type=0;p.native.weapon_class=4;p.native.fire_type=1;
    p.pre_sequence=1;p.gameplay_epoch=1;p.thread=1;
    return p;
}
pa::NeutralSample supported(){
    pa::NeutralSample s{};s.focused=s.dominant_tracking=s.menu_active=true;
    for(unsigned i=0;i<2;++i){s.squeeze_active[i]=true;s.trigger[i].analog_active=true;s.trigger[i].click_active=true;}
    s.squeeze[0]=1;return s;
}
pa::RearmKey key(const bovr::t5::command_profile::Copy& p){
    pa::RearmKey k{};k.profile=p.profile;k.identity=p.identity;k.gameplay_epoch=1;k.xr_generation=1;
    k.owner_thread=1;k.anchor_bits[6]=0x3f800000;return k;
}
int main(){
    const auto p=profile();const auto held=supported();
    check(!pa::neutral(held),"baseline held support sample is not neutral");
    check(pa::neutral(sr::sample(held,p)),"ordinary single primary permits held left support during aim rearm");
    check(pa::neutral(sr::sample(held,profile(true))),"paired linked single primary permits held support after reader admission");
    check(held.squeeze[0]==1,"copy-only policy leaves original input unchanged");
    for(int weapon_class=0;weapon_class<9;++weapon_class){auto q=p;q.native.weapon_class=weapon_class;
        check(pa::neutral(sr::sample(held,q)),"all admitted single bullet classes retain support");}
    for(int weapon_type:{1,2,3,4,5,6}){auto q=p;q.native.weapon_type=weapon_type;
        check(!pa::neutral(sr::sample(held,q)),"projectile mode melee and other types are not broadened");}
    for(int weapon_class:{-1,9,10}){auto q=p;q.native.weapon_class=weapon_class;
        check(!pa::neutral(sr::sample(held,q)),"turret and invalid classes remain excluded");}
    {auto q=p;q.native.dual_wield=true;check(!pa::neutral(sr::sample(held,q)),"dual weapons keep existing policy");}
    {auto q=p;++q.profile.revision;check(!pa::neutral(sr::sample(held,q)),"invalid exact descriptor cannot grant support exemption");}
    for(unsigned mode:{0u,3u,0xffffffffu}){auto q=p;q.identity.tracking_mode=mode;
        check(!pa::neutral(sr::sample(held,q)),"rejected unknown or vehicle mode cannot grant support exemption");}
    {auto q=p;q.identity.server_link=0x306ed00;check(!pa::neutral(sr::sample(held,q)),"ordinary mode with link mismatch stays blocked");}
    for(unsigned link:{0u,0x100u,0x306ed01u,0xffffffffu}){auto q=profile(true);q.identity.server_link=link;
        check(!pa::neutral(sr::sample(held,q)),"linked mode still needs structurally valid link identity");}
    for(float grip:{0.f,.649f,.65f,1.f}){auto s=held;s.squeeze[0]=grip;
        check(pa::neutral(sr::sample(s,p)),"finite active in-range support values allow aim-neutral observation");}
    for(float bad:{-.1f,1.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        auto s=held;s.squeeze[0]=bad;const auto result=sr::sample(s,p);
        check(!pa::neutral(result),"invalid support values stay non-neutral");}
    {auto s=held;s.squeeze_active[0]=false;check(!pa::neutral(sr::sample(s,p)),"inactive support input stays non-neutral");}
    for(unsigned hand=0;hand<2;++hand){
        auto s=held;s.trigger[hand].analog=.55f;
        check(!pa::neutral(sr::sample(s,p)),"held analog firing or grenade trigger cannot rearm");
        s=held;s.trigger[hand].click_pressed=true;
        check(!pa::neutral(sr::sample(s,p)),"held digital firing or grenade trigger cannot rearm");
        s=held;s.trigger[hand].analog=std::numeric_limits<float>::quiet_NaN();
        check(!pa::neutral(sr::sample(s,p)),"invalid trigger remains non-neutral");
        s=held;s.trigger[hand].analog_active=s.trigger[hand].click_active=false;
        check(!pa::neutral(sr::sample(s,p)),"inactive trigger remains non-neutral");
    }
    {auto s=held;s.squeeze[1]=.65f;check(!pa::neutral(sr::sample(s,p)),"dominant alternate-mode grip still requires neutral");}
    {auto s=held;s.menu_pressed=true;check(!pa::neutral(sr::sample(s,p)),"menu remains blocked");}
    {auto s=held;s.focused=false;check(!pa::neutral(sr::sample(s,p)),"focus loss remains blocked");}
    {auto s=held;s.dominant_tracking=false;check(!pa::neutral(sr::sample(s,p)),"tracking loss remains blocked");}
    // A real owner/context transition still consumes a transition command and a
    // fresh neutral observation before admission. Only the harmless grip differs.
    pa::RearmState state;auto k=key(p);
    check(state.observe(k,1,sr::sample(held,p),true)==pa::RearmResult::wait_neutral,"initial transition stays stock");
    check(state.observe(k,1,sr::sample(held,p),true)==pa::RearmResult::wait_neutral,"duplicate transition cannot rearm");
    check(state.observe(k,2,sr::sample(held,p),true)==pa::RearmResult::neutral_observed,"fresh held-support neutral command stays stock");
    check(state.observe(k,2,sr::sample(held,p),true)==pa::RearmResult::ready,"next occurrence admits aim with support still held");
    state.revoke(3);
    check(state.observe(k,4,sr::sample(held,p),true)==pa::RearmResult::wait_neutral,"menu or unsafe reader revocation still requires transition");
    check(state.observe(k,5,sr::sample(held,p),true)==pa::RearmResult::neutral_observed,"valid menu return can observe neutral with support held");
    check(state.observe(k,5,sr::sample(held,p),true)==pa::RearmResult::ready,"valid menu return recovers without dropping support");
    const auto linked=profile(true);k=key(linked);
    check(state.observe(k,6,sr::sample(held,linked),true)==pa::RearmResult::wait_neutral,"scripted link change cannot inherit old ownership");
    auto firing=held;firing.trigger[1].analog=1;
    check(state.observe(k,7,sr::sample(firing,linked),true)==pa::RearmResult::wait_neutral,"held attack remains blocked across scripted handoff");
    check(state.observe(k,8,sr::sample(held,linked),true)==pa::RearmResult::neutral_observed,"released trigger rearms linked aim with support retained");
    check(state.observe(k,8,sr::sample(held,linked),true)==pa::RearmResult::ready,"linked aim only resumes after real neutral observation");
    ++k.anchor_bits[0];
    check(state.observe(k,9,sr::sample(held,linked),true)==pa::RearmResult::wait_neutral,"unreceipted recenter remains a real ownership transition");
    check(state.observe(k,10,sr::sample(held,linked),false)==pa::RearmResult::refused,"invalid current occurrence is never admitted by grip policy");
    // The opt-in dominant exemption is narrower than the established foregrip
    // policy. It only permits aim rearming after an ordinary gun is idle.
    auto dominant=held;dominant.squeeze[1]=1;
    check(!pa::neutral(sr::sample(dominant,p)),"dominant grip retains released-build behavior by default");
    check(pa::neutral(sr::sample(dominant,p,true)),"idle ordinary gun can observe aim neutral with both grips held");
    check(dominant.squeeze[0]==1&&dominant.squeeze[1]==1,"dominant exemption never changes original grip input");
    for(int kind=0;kind<9;++kind){auto q=p;q.native.weapon_class=kind;
        check(pa::neutral(sr::sample(dominant,q,true))==(kind<=4),"dominant recovery is limited to rifle MG SMG spread and pistol classes");}
    for(int native_state=1;native_state<=40;++native_state)for(bool other:{false,true}){
        auto q=p;(other?q.native.other_state:q.native.weapon_state)=native_state;
        check(!pa::neutral(sr::sample(dominant,q,true)),"raising switching firing reloading melee and offhand states cannot grant dominant exemption");}
    {auto q=p;q.native.dual_wield=true;check(!pa::neutral(sr::sample(dominant,q,true)),"dual controls require their original real grip neutral");}
    check(!pa::neutral(sr::sample(dominant,linked,true)),"linked scene cannot use ordinary dominant exemption");
    for(float bad:{-.1f,1.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        auto s=dominant;s.squeeze[1]=bad;check(!pa::neutral(sr::sample(s,p,true)),"invalid dominant grip never becomes neutral");}
    {auto s=dominant;s.squeeze_active[1]=false;check(!pa::neutral(sr::sample(s,p,true)),"inactive dominant grip is refused");}
    for(unsigned hand=0;hand<2;++hand){
        auto s=dominant;s.trigger[hand].analog=.55f;check(!pa::neutral(sr::sample(s,p,true)),"either analog trigger held blocks dominant recovery");
        s=dominant;s.trigger[hand].click_pressed=true;check(!pa::neutral(sr::sample(s,p,true)),"either digital trigger held blocks dominant recovery");
        s=dominant;s.trigger[hand].analog=std::numeric_limits<float>::quiet_NaN();check(!pa::neutral(sr::sample(s,p,true)),"nonfinite trigger blocks dominant recovery");
        s=dominant;s.trigger[hand].analog_active=s.trigger[hand].click_active=false;check(!pa::neutral(sr::sample(s,p,true)),"unknown trigger blocks dominant recovery");
    }
    for(unsigned fault=0;fault<4;++fault){auto s=dominant;
        if(fault==0)s.focused=false;if(fault==1)s.dominant_tracking=false;if(fault==2)s.menu_active=false;if(fault==3)s.menu_pressed=true;
        check(!pa::neutral(sr::sample(s,p,true)),"focus tracking and menu checks remain mandatory for dominant recovery");}
    pa::RearmState dominant_state;const auto dominant_key=key(p);
    check(dominant_state.observe(dominant_key,1,sr::sample(dominant,p,true),true)==pa::RearmResult::wait_neutral,"dominant exemption cannot skip owner transition");
    auto held_attack=dominant;held_attack.trigger[1].analog=1;
    check(dominant_state.observe(dominant_key,2,sr::sample(held_attack,p,true),true)==pa::RearmResult::wait_neutral,"dominant recovery cannot arm while firing trigger is held");
    check(dominant_state.observe(dominant_key,3,sr::sample(dominant,p,true),true)==pa::RearmResult::neutral_observed,"released triggers provide a fresh aim neutral without releasing grips");
    check(dominant_state.observe(dominant_key,4,sr::sample(dominant,p,true),true)==pa::RearmResult::ready,"only following owned occurrence permits command aim");
    std::printf("primary_support_rearm_checks=%u failures=0\n",checks);return 0;
}
