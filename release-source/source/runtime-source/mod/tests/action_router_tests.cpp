// SPDX-License-Identifier: GPL-3.0-only
#include "input/action_router.hpp"
#include "input/turn_controller.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace bovr::input;
namespace xr=wawvr::xr;
int main() {
    unsigned checks{},failures{};
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}};
    xr::ActionSnapshot s{};s.focused=true;s.sequence=1;
    for(auto& h:s.hands) { h.grip.active=h.grip.position_valid=h.grip.orientation_valid=h.grip.position_tracked=h.grip.orientation_tracked=true;h.trigger.active=true;h.stick.active=true; }
    ActionRouter router;
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter guarded(handed,true);std::uint64_t tick=50;
        const unsigned d=handed?0u:1u,n=d^1;
        for(auto& h:c.hands)h.primary.active=h.secondary.active=h.stick_click.active=h.squeeze.active=true;
        const auto edge=[](const Output& o,Action action,bool pressed){for(unsigned i=0;i<o.edge_count;++i)
            if(o.edges[i].action==action&&o.edges[i].pressed==pressed)return true;return false;};
        const auto update=[&](bool wait){++c.sequence;return guarded.update(c,Context::gameplay,++tick,
            true,false,false,false,false,false,false,false,false,false,wait);};
        update(false);c.hands[d].trigger.current=1;
        check(edge(update(false),Action::fire,true),"ordinary aim gate fixture starts with a real native attack press");
        c.hands[d].squeeze.current=1;c.hands[d].stick_click.current=true;
        c.hands[n].stick.current={.8f,.7f};c.hands[n].primary.current=true;
        const auto gated=update(true);
        check(edge(gated,Action::fire,false)&&!edge(gated,Action::fire,true),"proved ordinary aim wait balances attack once without a new press");
        check(edge(gated,Action::melee,true)&&edge(gated,Action::use,true)&&gated.forward!=0&&gated.right!=0,
            "attack-only wait retains actual melee use and movement inputs");
        for(unsigned i=0;i<12;++i){const auto held=update(true);
            check(!edge(held,Action::fire,true)&&!edge(held,Action::fire,false),"held trigger during aim wait generates no extra fire edges");}
        c.hands[d].stick_click.current=false;
        check(!edge(update(false),Action::fire,true),"reader gap or ended gate cannot manufacture a trigger release");
        c.hands[d].trigger.current=0;
        check(!edge(update(true),Action::fire,true),"one genuine release during neutral-observed wait is retained");
        c.hands[d].trigger.current=1;
        check(edge(update(false),Action::fire,true),"first fresh press after ready aim fires after exactly one real release");
        check(!edge(update(false),Action::fire,true),"ordinary held fire remains a single balanced press after recovery");
        for(float invalid:{std::numeric_limits<float>::quiet_NaN(),-.1f,1.1f,std::numeric_limits<float>::infinity()}){
            update(true);c.hands[d].trigger.current=invalid;update(true);c.hands[d].trigger.current=1;
            check(!edge(update(false),Action::fire,true),"invalid trigger during wait does not count as neutral");}
    }
    for(const auto context:{Context::mounted,Context::scripted_attack,Context::scripted_linked,
                            Context::vehicle_weapon,Context::guided_missile,Context::aircraft_monitor}) {
        auto c=s;ActionRouter scoped(false,true);std::uint64_t tick=90;
        const auto update=[&]{++c.sequence;return scoped.update(c,context,++tick,
            false,false,false,false,false,false,false,false,false,false,true);};
        update();c.hands[1].trigger.current=1;const auto out=update();bool pressed=false;
        for(unsigned i=0;i<out.edge_count;++i)pressed|=out.edges[i].action==Action::fire&&out.edges[i].pressed;
        check(pressed,"ordinary aim wait flag cannot inhibit mounted scripted vehicle guided or monitor fire");
    }
    for(bool dual:{false,true}) {
        auto c=s;ActionRouter scoped(false,true);std::uint64_t tick=95;
        const auto update=[&](bool pulse){++c.sequence;return scoped.update(c,Context::gameplay,++tick,
            false,false,false,dual,false,!dual,pulse,false,false,false,true);};
        update(false);c.hands[1].trigger.current=1;const auto out=update(true);bool pressed=false;
        for(unsigned i=0;i<out.edge_count;++i)pressed|=out.edges[i].action==Action::fire&&out.edges[i].pressed;
        check(pressed,"ordinary wait flag preserves dual fire and primary knife gesture override");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter frozen(handed,true);std::uint64_t tick=100;
        const unsigned d=handed?0u:1u,n=d^1;
        for(auto& h:c.hands)h.primary.active=h.secondary.active=h.stick_click.active=h.squeeze.active=true;
        const auto update=[&](Context context=Context::scripted_attack){++c.sequence;return frozen.update(c,context,++tick,true,true);};
        const auto edge=[](const Output& o,Action action,bool pressed){for(unsigned i=0;i<o.edge_count;++i)
            if(o.edges[i].action==action&&o.edges[i].pressed==pressed)return true;return false;};
        c.hands[d].trigger.current=1;
        check(!update().edge_count,"frozen attack requires neutral rather than firing on entry in either handed layout");
        c.hands[d].trigger.current=0;update();
        c.hands[d].trigger.current=1;c.hands[n].stick.current={1,1};c.hands[d].stick.current={1,1};
        c.hands[d].primary.current=c.hands[d].secondary.current=true;
        auto out=update();
        check(out.edge_count==1&&edge(out,Action::fire,true),"frozen disabled-weapon trigger owns one native attack press");
        check(!out.forward&&!out.right&&!out.turn&&!out.look_edge_count&&!out.weapon_next&&!out.weapon_alternate&&!out.lower_stance,
              "frozen attack cannot grant ADS, reload, stance, movement, turning or weapon cycling");
        check(!frozen.update(c,Context::scripted_attack,++tick).edge_count&&!update().edge_count,
              "duplicate and fresh held trigger samples do not repeat frozen attack");
        c.hands[d].trigger.current=0;out=update();
        check(out.edge_count==1&&edge(out,Action::fire,false),"frozen trigger release balances native attack");
        c.hands[d].trigger.current=1;update();c.focused=false;out=update();
        check(out.edge_count==1&&edge(out,Action::fire,false),"focus loss releases frozen attack");
        c.focused=true;check(!update().edge_count,"focus recovery cannot reacquire an already held trigger");
        c.hands[d].trigger.current=0;update();c.hands[d].trigger.current=1;update();
        tick+=251;out=frozen.expire(tick);
        check(out.edge_count==1&&edge(out,Action::fire,false),"stale XR input balances frozen attack");
        check(!update().edge_count,"fresh held input after expiration still needs neutral");
        c.hands[d].trigger.current=0;update();c.hands[d].trigger.current=1;update();
        out=update(Context::scripted_use);
        check(out.edge_count==1&&edge(out,Action::fire,false),"withdrawing linked disabled-weapon admission releases frozen attack");
        check(!update(Context::scripted_use).edge_count,"ordinary frozen prompts still cannot gain attack");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter frozen(handed,true);std::uint64_t tick=300;
        const unsigned d=handed?0u:1u,n=d^1;
        for(auto& h:c.hands)h.primary.active=h.secondary.active=h.stick_click.active=h.squeeze.active=true;
        const auto update=[&]{++c.sequence;return frozen.update(c,Context::scripted_use,++tick,true,true);};
        const auto edge=[](const Output& o,Action action,bool pressed){for(unsigned i=0;i<o.edge_count;++i)
            if(o.edges[i].action==action&&o.edges[i].pressed==pressed)return true;return false;};
        update();
        for(auto& h:c.hands){h.trigger.current=h.squeeze.current=1;h.primary.current=h.secondary.current=h.stick_click.current=true;h.stick.current={1,1};}
        auto out=update();
        check(out.edge_count==3&&edge(out,Action::use,true)&&edge(out,Action::jump,true)&&edge(out,Action::melee,true),
              "frozen scripts admit only use, jump and dominant-stick native melee in both handed layouts");
        check(!out.forward&&!out.right&&!out.turn&&!out.look_edge_count&&!out.weapon_next&&!out.weapon_alternate&&!out.lower_stance,
              "frozen melee permission cannot grant locomotion, turning, weapon switching or stance");
        tick+=251;out=frozen.expire(tick);
        check(out.edge_count==3&&edge(out,Action::melee,false),"expired frozen input balances its native melee lease");
        out=update();check(!out.edge_count,"held frozen buttons cannot reacquire after input expiration");
        for(auto& h:c.hands){h.trigger.current=h.squeeze.current=0;h.primary.current=h.secondary.current=h.stick_click.current=false;h.stick.current={};}
        update();c.hands[n].stick_click.current=true;
        check(!update().edge_count,"support-stick click cannot become frozen native melee");
        c.hands[d].stick_click.current=true;out=update();
        check(out.edge_count==1&&edge(out,Action::melee,true),"fresh dominant-stick click recovers frozen melee after expiration");
        c.hands[d].grip.position_tracked=false;out=update();
        check(out.edge_count==1&&edge(out,Action::melee,false),"losing tracking releases frozen melee instead of leaving V held");
    }
    {
        ActionRouter mounted(false,true);auto v=s;
        mounted.update(v,Context::mounted,v.sequence,true);
        for(const auto stick : {xr::Vec2f{1,0},xr::Vec2f{-1,0},xr::Vec2f{0,1},xr::Vec2f{0,-1}}) {
            v.hands[1].stick.current=stick;++v.sequence;
            const auto out=mounted.update(v,Context::mounted,v.sequence,true);
            check(!out.look_edge_count&&out.turn==0&&out.forward==0&&out.right==0,
                  "controller-mounted gun ignores stick aiming, body turning and movement");
        }
        v.hands[1].stick.current={};v.hands[1].trigger.current=1;++v.sequence;
        const auto firing=mounted.update(v,Context::mounted,v.sequence,true);
        bool fire=false;for(unsigned i=0;i<firing.edge_count;++i)
            fire|=firing.edges[i].action==Action::fire&&firing.edges[i].pressed;
        check(fire,"controller-mounted gun retains right-trigger firing without a grip press");
        ++v.sequence;const auto paused=mounted.update(v,Context::suspended,v.sequence,true);
        bool released=false;for(unsigned i=0;i<paused.edge_count;++i)
            released|=paused.edges[i].action==Action::fire&&!paused.edges[i].pressed;
        check(released,"pausing controller-mounted gun releases its firing action");
    }
    {
        auto c=s;ActionRouter r(false,true);std::uint64_t tick=500;
        const auto update=[&](bool busy,bool grenade){++c.sequence;return r.update(c,Context::gameplay,++tick,true,false,busy,false,grenade);};
        const auto edge=[](const Output& o,Action a){for(unsigned j=0;j<o.edge_count;++j)if(o.edges[j].action==a&&o.edges[j].pressed)return true;return false;};
        update(false,false);c.hands[0].trigger.current=1;
        check(!edge(update(true,true),Action::jump),"button grenade owns left trigger instead of jump");
        c.hands[1].stick.current.y=1;
        check(edge(update(true,true),Action::jump),"right stick up remains available while button grenade owns trigger");
        c.hands[1].stick.current={};update(true,true);c.hands[1].stick.current.y=1;
        check(!edge(update(true,false),Action::jump),"physical reload busy retains original stick jump prohibition");
        c.hands[0].trigger.current=0;c.hands[1].stick.current={};update(false,false);
        c.hands[0].trigger.current=1;check(edge(update(false,false),Action::jump),"support-held non-grenade route keeps left trigger jump");
        c.hands[0].trigger.current=0;c.hands[0].stick_click.active=true;update(false,false);
        c.hands[0].stick_click.current=true;check(edge(update(false,false),Action::sprint),"tactical modifier retains ordinary sprint before grenade trigger");
        c.hands[0].trigger.current=1;const auto suppressed=update(true,true);bool released_sprint=false;
        for(unsigned j=0;j<suppressed.edge_count;++j)if(suppressed.edges[j].action==Action::sprint&&!suppressed.edges[j].pressed)released_sprint=true;
        check(released_sprint&&!edge(suppressed,Action::sprint),"button grenade reservation balances existing sprint hold");
    }
    {
        auto dual_sample=s;ActionRouter pair(false,true);std::uint64_t tick=600;
        const auto update=[&](bool dual=true,bool busy=false){++dual_sample.sequence;return pair.update(dual_sample,Context::gameplay,++tick,true,true,busy,dual);};
        const auto press=[](const Output& o,Action a){for(unsigned i=0;i<o.edge_count;++i)if(o.edges[i].action==a&&o.edges[i].pressed)return true;return false;};
        update();dual_sample.hands[0].trigger.current=1;
        auto out=update();check(press(out,Action::ads)&&!press(out,Action::jump),"dual left trigger fires partner without jumping");
        check(!press(update(),Action::ads),"dual held trigger does not repeat edge");
        out=update(true,true);check(!press(out,Action::ads),"dual manual gesture cannot fire partner");
        dual_sample.hands[0].trigger.current=0;update();
        dual_sample.hands[1].trigger.current=1;check(press(update(),Action::fire),"dual right trigger fires primary");
        dual_sample.hands[1].trigger.current=0;update();
        dual_sample.hands[0].trigger.current=1;out=update(false);
        check(!press(out,Action::jump),"weapon mode transition requires neutral before remapped jump");
        dual_sample.hands[0].trigger.current=0;update(false);dual_sample.hands[0].trigger.current=1;
        check(press(update(false),Action::jump),"ordinary left trigger jump recovers after neutral");
    }
    {
        xr::ActionSnapshot c=s;for(auto& h:c.hands)h.primary.active=h.secondary.active=true;
        ActionRouter r(false,true);std::uint64_t tick=1000;
        const auto update=[&](bool busy=false){++c.sequence;return r.update(c,Context::gameplay,++tick,true,false,busy);};
        const auto edge=[&](const Output& value,Action action){for(unsigned j=0;j<value.edge_count;++j)if(value.edges[j].action==action&&value.edges[j].pressed)return true;return false;};
        update();c.hands[1].primary.current=true;check(edge(update(),Action::reload),"COD4 right A reload");c.hands[1].primary.current=false;update();
        c.hands[0].primary.current=true;check(edge(update(),Action::use),"COD4 left X use");c.hands[0].primary.current=false;update();
        c.hands[0].secondary.current=true;check(update().weapon_next,"COD4 left Y switches weapon");check(!update().weapon_next,"COD4 weapon hold cannot repeat");c.hands[0].secondary.current=false;update();
        c.hands[1].stick.current.y=1;auto v=update();check(edge(v,Action::jump)&&!v.weapon_next,"COD4 stick up jumps without switching weapons");c.hands[1].stick.current={};update();
        c.hands[0].trigger.current=1;v=update();check(edge(v,Action::jump)&&!edge(v,Action::ads),"COD4 left trigger jumps without ADS");c.hands[0].trigger.current=0;update();
        c.hands[0].trigger.current=1;check(!edge(update(true),Action::jump),"manual charging cannot jump");c.hands[0].trigger.current=0;update();
        c.hands[1].secondary.current=true;check(edge(update(),Action::crouch),"COD4 right B stance");c.hands[1].secondary.current=false;update();
        c.hands[1].stick.current.y=-1;check(update().lower_stance&&!update().lower_stance,"COD4 stick down lowers stance once");c.hands[1].stick.current={};update();
        c.focused=false;update();c.focused=true;c.hands[0].secondary.current=true;check(!update().weapon_next,"COD4 focus recovery cannot cycle from a held Y");
        c.hands[0].secondary.current=false;update();
        ++c.sequence;r.update(c,Context::menu,++tick);++c.sequence;r.update(c,Context::menu,++tick);
        c.hands[1].primary.current=true;++c.sequence;check(r.update(c,Context::menu,++tick).menu_accept,"COD4 menus still accept on right A");
    }
    {
        auto c=s;ActionRouter knife(false,true);std::uint64_t tick=1500;
        const auto edge=[](const Output& o,Action action,bool pressed){for(unsigned i=0;i<o.edge_count;++i)if(o.edges[i].action==action&&o.edges[i].pressed==pressed)return true;return false;};
        ++c.sequence;knife.update(c,Context::gameplay,++tick,false,false,false,false,false,true,false);
        c.hands[1].trigger.current=1;++c.sequence;
        check(!edge(knife.update(c,Context::gameplay,++tick,false,false,false,false,false,true,false),Action::fire,true),
              "knife mode suppresses a held right trigger");
        ++c.sequence;
        check(edge(knife.update(c,Context::gameplay,++tick,false,false,false,false,false,true,true),Action::fire,true),
              "knife gesture pulse owns primary attack");
        check(!edge(knife.update(c,Context::gameplay,tick,false,false,false,false,false,true,true),Action::fire,true),
              "duplicate knife pulse cannot repeat attack");
        ++c.sequence;
        check(edge(knife.update(c,Context::gameplay,++tick,false,false,false,false,false,true,false),Action::fire,false),
              "knife gesture releases primary attack on the next sample");
        ++c.sequence;
        check(!edge(knife.update(c,Context::gameplay,++tick,false,false,false,false,false,false,false),Action::fire,true),
              "leaving knife mode with trigger held requires neutral");
        c.hands[1].trigger.current=0;++c.sequence;knife.update(c,Context::gameplay,++tick);
        c.hands[1].trigger.current=1;++c.sequence;
        check(edge(knife.update(c,Context::gameplay,++tick),Action::fire,true),"ordinary gun trigger recovers after neutral");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter knife(handed,true);std::uint64_t tick=1700;
        const unsigned d=handed?0u:1u;c.hands[d].stick_click.active=true;
        const auto edge=[](const Output& o,Action action,bool pressed){for(unsigned i=0;i<o.edge_count;++i)
            if(o.edges[i].action==action&&o.edges[i].pressed==pressed)return true;return false;};
        const auto update=[&](bool pulse=false,bool scripted=true,Context context=Context::gameplay){
            ++c.sequence;return knife.update(c,context,++tick,false,false,false,false,false,true,pulse,false,false,scripted);};
        update();c.hands[d].trigger.current=1;
        auto out=update(true);
        check(edge(out,Action::melee,true)&&!edge(out,Action::fire,true),"scripted swim knife gesture presses native melee instead of fire in both handed layouts");
        out=knife.update(c,Context::gameplay,tick,false,false,false,false,false,true,true,false,false,true);
        check(!out.edge_count,"duplicate scripted melee publication cannot repeat the pulse");
        out=update();check(edge(out,Action::melee,false),"scripted melee gesture releases on the next sample");
        c.hands[d].stick_click.current=true;out=update();
        check(edge(out,Action::melee,true)&&!edge(out,Action::fire,true),"ordinary stick-click still owns native melee in scripted swim mode");
        out=update(true);check(!out.edge_count,"gesture overlapping held melee click cannot duplicate or release its key");
        c.hands[d].stick_click.current=false;out=update();
        check(edge(out,Action::melee,false),"combined scripted gesture and stick-click release once when both are neutral");
        out=update(true);check(edge(out,Action::melee,true),"fresh scripted gesture rearms after neutral");
        out=update(true,false);
        check(edge(out,Action::melee,false)&&!edge(out,Action::fire,true),"leaving scripted swim balances melee and refuses a held pulse as ordinary fire");
        update(false,false);out=update(true,false);
        check(edge(out,Action::fire,true)&&!edge(out,Action::melee,true),"ordinary primary knife gesture retains its existing native attack mapping");
        out=update(false,true);check(edge(out,Action::fire,false),"entering scripted swim releases an existing primary attack lease");
        out=update(true);check(edge(out,Action::melee,true),"scripted melee works after entry neutral");
        out=update(false,true,Context::suspended);
        check(edge(out,Action::melee,false)&&!edge(out,Action::fire,true),"pause balances the scripted melee lease without primary fire");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter knife(handed,true);std::uint64_t tick=1800;
        const unsigned d=handed?0u:1u,n=d^1;
        const auto update=[&](bool scripted,Context context=Context::gameplay){++c.sequence;
            return knife.update(c,context,++tick,false,false,false,false,false,true,false,false,false,scripted);};
        update(true);c.hands[n].stick.current={.3f,.8f};c.hands[d].stick.current={.8f,0};
        const auto before=update(true),refusal=update(false),recovered=update(true);
        check(before.forward>0&&before.turn>0&&refusal.forward==before.forward&&refusal.right==before.right&&
              refusal.turn==before.turn&&recovered.forward==before.forward&&recovered.right==before.right&&recovered.turn==before.turn,
              "one scripted melee selector refusal and recovery preserve both held locomotion sticks");
        check(!refusal.edge_count&&!recovered.edge_count,"melee selector changes cannot manufacture button presses");
        update(true,Context::suspended);const auto resumed=update(true);
        check(!resumed.forward&&!resumed.right&&!resumed.turn,
              "actual pause still disarms held locomotion despite the narrower melee transition");
        c.hands[n].stick.current={};c.hands[d].stick.current={};update(true);
        c.hands[n].stick.current={0,1};c.hands[d].stick.current={1,0};
        const auto rearmed=update(true);check(rearmed.forward==1&&rearmed.turn==1,"paused locomotion recovers only after real neutral");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter r(handed,true);std::uint64_t tick=2000;
        const unsigned d=handed?0u:1u,n=d^1;
        c.hands[n].secondary.active=true;c.hands[d].squeeze.active=true;
        const auto update=[&](bool admit=true,bool busy=false,Context context=Context::gameplay){
            ++c.sequence;return r.update(c,context,++tick,admit,false,busy);
        };
        const auto duplicate=[&]{return r.update(c,Context::gameplay,tick,true);};
        const auto quiet=[](const Output& value){return !value.weapon_next&&!value.weapon_alternate;};
        c.hands[d].squeeze.current=1;
        check(quiet(update()),"held grip at startup requires neutral in both hand roles");
        c.hands[d].squeeze.current=0;update();c.hands[d].squeeze.current=1;
        auto v=update();check(v.weapon_alternate&&!v.weapon_next,"right dominant grip alone toggles alternate");
        check(quiet(duplicate())&&quiet(update()),"duplicate and fresh held grip never repeat");
        c.hands[d].squeeze.current=.4f;update();c.hands[d].squeeze.current=.7f;
        check(quiet(update()),"grip jitter above release threshold cannot retoggle");
        c.hands[d].squeeze.current=0;check(quiet(duplicate()),"duplicate neutral cannot rearm grip");
        c.hands[d].squeeze.current=1;check(quiet(update()),"held grip after duplicate neutral remains consumed");
        c.hands[d].squeeze.current=.25f;update();c.hands[d].squeeze.current=.64f;
        check(quiet(update()),"below press threshold does not toggle");
        c.hands[d].squeeze.current=.65f;check(update().weapon_alternate,"release and press thresholds allow one new toggle");
        c.hands[n].secondary.current=true;v=update();
        check(v.weapon_next&&!v.weapon_alternate,"Y cycles while right grip is held");
        c.hands[n].secondary.current=false;check(quiet(update()),"Y release cannot replay a held grip");
        c.hands[d].squeeze.current=0;update();c.hands[d].squeeze.current=1;c.hands[n].secondary.current=true;
        v=update();check(v.weapon_next&&!v.weapon_alternate,"simultaneous Y and grip select Y only");
        c.hands[n].secondary.current=false;check(quiet(update()),"simultaneous Y consumes grip until released");
        c.hands[d].squeeze.current=0;update();c.hands[n].secondary.current=true;update();
        c.hands[d].squeeze.current=1;check(quiet(update()),"new grip while Y held is consumed");
        c.hands[n].secondary.current=false;check(quiet(update()),"held Y arbitration cannot queue an alternate");
        for(unsigned fault=0;fault<14;++fault) {
            c.hands[d].squeeze.current=0;update();c.hands[d].squeeze.current=1;
            bool admit=true,busy=false;auto context=Context::gameplay;
            switch(fault) {
            case 0:c.focused=false;break;
            case 1:c.hands[d].grip.position_tracked=false;break;
            case 2:c.hands[n].grip.position_tracked=false;break;
            case 3:c.hands[d].squeeze.current=std::numeric_limits<float>::quiet_NaN();break;
            case 4:c.hands[d].squeeze.active=false;break;
            case 5:admit=false;break;
            case 6:busy=true;break;
            case 7:context=Context::menu;break;
            case 8:context=Context::mounted;break;
            case 9:context=Context::scripted_use;break;
            case 10:context=Context::scripted_linked;break;
            case 11:context=Context::suspended;break;
            case 12:c.menu={true,true,true};break;
            case 13:context=Context::vehicle_weapon;break;
            }
            check(quiet(update(admit,busy,context)),"invalid context cannot select either weapon action");
            c.focused=true;c.hands[d].grip.position_tracked=c.hands[n].grip.position_tracked=true;
            c.hands[d].squeeze.active=true;c.hands[d].squeeze.current=1;c.menu={true,false,true};
            check(quiet(update()),"held grip cannot revive after invalidation");
            c.hands[d].squeeze.current=0;update();c.hands[d].squeeze.current=1;
            check(update().weapon_alternate,"fresh release and press recovers alternate");
        }
        for(bool inactive:{false,true}) {
            c.hands[d].squeeze.current=0;update();
            c.hands[d].squeeze.active=!inactive;
            if(!inactive)c.hands[d].squeeze.current=std::numeric_limits<float>::quiet_NaN();
            check(quiet(duplicate()),"invalid duplicate grip clears its prior neutral admission");
            c.hands[n].secondary.current=true;v=update();
            check(v.weapon_next&&!v.weapon_alternate,"Y remains independent of inactive or NaN grip");
            c.hands[n].secondary.current=false;c.hands[d].squeeze.active=true;c.hands[d].squeeze.current=1;
            check(quiet(update()),"duplicate grip invalidation requires fresh neutral after recovery");
        }
        c.hands[n].secondary.active=false;check(quiet(duplicate()),"inactive duplicate Y invalidates its prior neutral");
        c.hands[n].secondary.active=true;c.hands[n].secondary.current=true;
        check(quiet(update()),"recovered held Y cannot use an invalidated neutral");
        c.hands[n].secondary.current=false;c.hands[n].secondary.active=false;c.hands[d].squeeze.current=0;update();
        c.hands[d].squeeze.current=1;check(update().weapon_alternate,"right grip does not require an active Y binding");
        r.expire(tick+251);check(quiet(update()),"expired grip remains inhibited while held");
    }
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter r(handed,true);std::uint64_t tick=4000;
        const unsigned d=handed?0u:1u,n=d^1;
        c.hands[n].secondary.active=true;c.hands[d].squeeze.active=true;
        const auto update=[&](bool reload=true,bool grenade=false,bool allowed=true,Context context=Context::gameplay){
            ++c.sequence;return r.update(c,context,++tick,allowed,false,reload||grenade,false,grenade,false,false,reload&&!grenade);
        };
        const auto quiet=[](const Output& value){return !value.weapon_next&&!value.weapon_alternate;};
        c.hands[n].secondary.current=true;c.hands[d].squeeze.current=1;
        check(quiet(update()),"reload escape cannot use held Y at startup");
        c.hands[n].secondary.current=false;c.hands[d].squeeze.current=0;update();
        c.hands[n].secondary.current=true;c.hands[d].squeeze.current=1;
        const auto next=update();
        check(next.weapon_next&&!next.weapon_alternate,"fresh Y escapes reload while right grip remains restricted");
        check(quiet(r.update(c,Context::gameplay,tick,true,false,true,false,false,false,false,true))&&quiet(update()),
              "duplicate and fresh held Y do not repeat reload escape");
        c.hands[n].secondary.current=false;c.hands[d].squeeze.current=0;update();
        c.hands[d].squeeze.current=1;
        check(quiet(update()),"reload escape never enables alternate fire selection");
        check(quiet(update(false)),"ending reload cannot replay held alternate grip");
        c.hands[d].squeeze.current=0;update(false);c.hands[d].squeeze.current=1;
        check(update(false).weapon_alternate,"alternate still recovers after fresh neutral once reload ends");
        for(unsigned blocked=0;blocked<4;++blocked) {
            c.hands[n].secondary.current=false;c.hands[d].squeeze.current=0;update();
            c.hands[n].secondary.current=true;
            check(quiet(update(true,blocked==0,blocked!=1,blocked==2?Context::menu:blocked==3?Context::scripted_linked:Context::gameplay)),
                  "grenade ownership or invalid context blocks reload escape");
            check(quiet(update()),"recovered reload context cannot replay held Y");
            c.hands[n].secondary.current=false;update();c.hands[n].secondary.current=true;
            check(update().weapon_next,"fresh Y recovers after rejected reload escape");
        }
        c.hands[n].secondary.current=false;update();
        c.hands[n].secondary.active=false;
        check(quiet(r.update(c,Context::gameplay,tick,true,false,true,false,false,false,false,true)),
              "inactive duplicate Y revokes reload escape neutral");
        c.hands[n].secondary.active=true;c.hands[n].secondary.current=true;
        check(quiet(update()),"restored held Y cannot use stale neutral during reload");
    }
    const auto support_start=checks;
    for(bool handed:{false,true}) {
        auto c=s;ActionRouter r(handed,true);std::uint64_t tick=5000;
        const unsigned d=handed?0u:1u,n=d^1;
        c.hands[n].secondary.active=true;c.hands[d].squeeze.active=true;
        const auto update=[&](bool support=true,bool busy=false,bool grenade=false,bool admit=true,
                              Context context=Context::gameplay,bool fresh=true,bool escape=false) {
            if(fresh)++c.sequence;
            return r.update(c,context,++tick,admit,false,busy,false,grenade,false,false,escape,support);
        };
        const auto quiet=[](const Output& v){return !v.weapon_next&&!v.weapon_alternate&&!v.weapon_support;};
        update();c.hands[n].secondary.current=true;
        check(quiet(update()),"available support defers normal Y selection until release");
        check(quiet(update()),"pending Y hold does not cycle or select support");
        c.hands[n].secondary.current=false;
        check(update().weapon_next,"clean Y tap still cycles with authored support available");
        check(quiet(update()),"Y release produces only one normal selection");
        c.hands[n].secondary.current=true;c.hands[n].stick.current={-.5f,0.f};
        auto v=update();check(quiet(v)&&v.forward==0.f&&v.right==0.f,"support modifier consumes a gradual stick ramp");
        c.hands[n].stick.current={-.74f,0.f};check(quiet(update()),"support gesture uses the WaW raw-stick engagement threshold");
        c.hands[n].stick.current={-.75f,0.f};v=update();
        check(v.weapon_support&&!v.weapon_next&&!v.weapon_alternate&&v.right==0.f&&v.forward==0.f,
              "Y and support stick left select support exactly once in either handed mode");
        check(quiet(update(true,false,false,true,Context::gameplay,false))&&quiet(update()),
              "duplicate and fresh held support chord cannot repeat");
        c.hands[n].stick.current={};update();c.hands[n].stick.current={-1.f,0.f};
        check(quiet(update()),"returning stick to neutral while Y held cannot select again");
        c.hands[n].secondary.current=false;v=update();
        check(quiet(v)&&v.right==0.f&&v.forward==0.f,"chord release cannot cycle or start strafing");
        v=update();check(v.right==0.f,"held stick stays movement-neutral after chord release");
        c.hands[n].stick.current={};update();c.hands[n].stick.current={-1.f,0.f};v=update();
        check(v.right==-1.f&&!v.weapon_support,"fresh stick neutral restores ordinary locomotion");
        c.hands[n].secondary.current=true;check(quiet(update()),"already-deflected stick cannot claim a new support chord");
        c.hands[n].secondary.current=false;check(quiet(update()),"rejected support gesture cannot fall through to weapon cycle");
        c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;c.hands[n].stick.current={1.f,0.f};
        check(quiet(update()),"wrong stick direction cannot select support");
        c.hands[n].stick.current={-1.f,0.f};check(quiet(update()),"wrong-direction excursion cannot slide into support selection");
        c.hands[n].secondary.current=false;check(quiet(update()),"wrong-direction gesture consumes the normal Y tap");
        c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;update();
        c.hands[d].squeeze.current=1.f;c.hands[n].secondary.current=false;v=update();
        check(v.weapon_next&&!v.weapon_alternate&&!v.weapon_support,"released Y tap owns a simultaneous alternate-grip press");
        c.hands[d].squeeze.current=0.f;update();
        c.hands[n].secondary.current=true;update();
        check(quiet(update(false)),"support disappearance revokes a pending Y tap");
        c.hands[n].secondary.current=false;check(quiet(update(false)),"support disappearance cannot dispatch on release");
        c.hands[n].secondary.current=true;
        check(update(false).weapon_next,"sections without support retain immediate Y selection");
        check(quiet(update(true)),"support appearance cannot replay a held Y");
        c.hands[n].stick.current={-1.f,0.f};check(quiet(update()),"newly available support requires fresh combined neutral");
        c.hands[n].secondary.current=false;c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;v=update(true,true,false,true,Context::gameplay,true,true);
        check(v.weapon_next&&!v.weapon_support,"manual reload escape remains immediate with support available");
        c.hands[n].stick.current={-1.f,0.f};check(quiet(update()),"reload escape cannot become a later support chord");
        c.hands[n].secondary.current=false;c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;update();
        check(quiet(update(true,true)),"manual ownership cancels a pending support modifier");
        c.hands[n].secondary.current=false;check(quiet(update()),"ending manual ownership cannot replay pending Y");
        for(unsigned fault=0;fault<13;++fault) {
            c.hands[n].secondary.current=false;c.hands[n].stick.current={};update();
            c.hands[n].secondary.current=true;update();
            bool busy=false,grenade=false,admit=true;auto context=Context::gameplay;
            switch(fault) {
            case 0:c.focused=false;break;
            case 1:c.hands[d].grip.position_tracked=false;break;
            case 2:c.hands[n].grip.orientation_tracked=false;break;
            case 3:c.hands[n].stick.active=false;break;
            case 4:c.hands[n].stick.current.x=std::numeric_limits<float>::quiet_NaN();break;
            case 5:c.hands[n].secondary.active=false;break;
            case 6:busy=true;break;
            case 7:grenade=true;break;
            case 8:admit=false;break;
            case 9:c.menu={true,true,true};break;
            case 10:context=Context::menu;break;
            case 11:context=Context::scripted_linked;break;
            case 12:context=Context::guided_missile;break;
            }
            check(quiet(update(true,busy,grenade,admit,context,false)),"invalid duplicate cancels pending support selection");
            c.focused=true;c.hands[d].grip.position_tracked=c.hands[n].grip.orientation_tracked=true;
            c.hands[n].stick.active=c.hands[n].secondary.active=true;c.menu={true,false,true};
            c.hands[n].stick.current={-1.f,0.f};
            check(quiet(update()),"held modifier cannot revive after support invalidation");
            c.hands[n].secondary.current=false;c.hands[n].stick.current={};
            check(quiet(update()),"invalidated support request has no deferred cycle on release");
            c.hands[n].secondary.current=true;c.hands[n].stick.current={-1.f,0.f};
            check(update().weapon_support,"fresh combined neutral recovers support after invalidation");
        }
        c.hands[n].secondary.current=false;c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;update();r.expire(tick+251);
        c.hands[n].stick.current={-1.f,0.f};check(quiet(update()),"expired support modifier cannot resume held selection");
        c.hands[n].secondary.current=false;c.hands[n].stick.current={};update();
        c.hands[n].secondary.current=true;update();c.hands[n].stick.current={1.f,0.f};
        check(quiet(update(true,false,false,true,Context::gameplay,false)),"wrong-direction duplicate revokes support neutral");
        c.hands[n].stick.current={-1.f,0.f};check(quiet(update()),"duplicate conflict cannot revive into a later fresh support chord");
        c.hands[n].secondary.current=false;c.hands[n].stick.current={};
        check(quiet(update(true,false,false,true,Context::gameplay,false)),"duplicate combined neutral cannot rearm support");
        c.hands[n].secondary.current=true;c.hands[n].stick.current={-1.f,0.f};
        check(quiet(update()),"held chord after duplicate neutral stays consumed");
        c.hands[n].secondary.current=false;c.hands[n].stick.current={.35f,.35f};update();
        c.hands[n].secondary.current=true;c.hands[n].stick.current={-.9f,.6f};
        check(update().weapon_support,"WaW neutral hysteresis and dominant left diagonal select support");
    }
    std::printf("support_weapon_router_checks=%u\n",checks-support_start);
    check(router.update(s,Context::gameplay,1).edge_count==0,"neutral initial sample arms controls");
    s.hands[1].trigger.current=1;s.sequence++;
    auto o=router.update(s,Context::gameplay,2);
    check(o.edge_count==1 && o.edges[0].action==Action::fire && o.edges[0].pressed,"one trigger press");
    check(router.update(s,Context::gameplay,3).edge_count==0,"same sample never duplicates");
    s.sequence++;s.focused=false;o=router.update(s,Context::gameplay,4);
    check(o.edge_count==1 && !o.edges[0].pressed,"focus loss releases held fire");
    s.sequence++;s.focused=true;
    check(router.update(s,Context::gameplay,5).edge_count==0,"recovered held trigger cannot fire");
    s.sequence++;s.hands[1].trigger.current=0;router.update(s,Context::gameplay,6);
    s.sequence++;s.hands[1].trigger.current=1;
    check(router.update(s,Context::gameplay,7).edge_count==1,"neutral then press rearms");
    o=router.expire(258);check(o.edge_count==1 && !o.edges[0].pressed,"missing samples release after250ms");
    check(router.expire(300).edge_count==0,"watchdog release is idempotent");
    s.sequence++;s.hands[1].trigger.current=0;s.hands[0].stick.current={1,1};
    o=router.update(s,Context::gameplay,299);
    check(o.forward==0 && o.right==0,"held recovery stick cannot move until centered");
    s.sequence++;s.hands[0].stick.current={};router.update(s,Context::gameplay,300);
    s.sequence++;s.hands[0].stick.current={1,1};o=router.update(s,Context::gameplay,301);
    check(std::fabs(o.forward*o.forward+o.right*o.right-1.f)<0.0001f,"diagonal movement unit bounded");
    auto repeat=router.update(s,Context::gameplay,301);
    check(repeat.edge_count==0 && repeat.forward==o.forward && repeat.right==o.right,"native60Hz holds latestXR axes without duplicated edges");
    s.sequence++;s.hands[0].stick.current.x=std::numeric_limits<float>::quiet_NaN();
    o=router.update(s,Context::gameplay,302);check(o.forward==0 && o.right==0,"invalid axis neutral");
    s.sequence++;s.hands[0].stick.current={};s.hands[1].trigger.current=1;router.update(s,Context::gameplay,303);
    s.sequence++;s.hands[0].grip.position_tracked=false;
    o=router.update(s,Context::gameplay,304);check(o.edge_count==1 && !o.edges[0].pressed,"tracking loss releases");
    s.sequence++;s.hands[0].grip.position_tracked=true;s.hands[1].trigger.current=0;router.update(s,Context::gameplay,305);
    s.sequence++;s.hands[1].trigger.current=1;router.update(s,Context::gameplay,306);
    s.sequence++;o=router.update(s,Context::menu,307);
    check(o.edge_count==1 && !o.edges[0].pressed && o.forward==0,"menu captures game controls");
    s.sequence++;s.hands[1].primary={true,false,false};router.update(s,Context::menu,308);
    s.sequence++;s.hands[1].primary.current=true;o=router.update(s,Context::menu,309);
    check(o.menu_accept && o.edge_count==0,"menu accept distinct from use");
    s.sequence++;check(!router.update(s,Context::menu,310).menu_accept,"held menu button one edge");
    ActionRouter left(true);s={};s.sequence=1;s.focused=true;
    for(auto& h:s.hands)h.grip.active=h.grip.position_valid=h.grip.orientation_valid=h.grip.position_tracked=h.grip.orientation_tracked=true;
    s.hands[0].trigger_click={true,false,false};
    left.update(s,Context::gameplay,1);s.sequence++;s.hands[0].trigger_click={true,true,true};
    o=left.update(s,Context::gameplay,2);check(o.edge_count==1 && o.edges[0].action==Action::fire,"left handed trigger follows dominant hand");
    TurnController turning;
    check(turning.update(1,0)==0,"held stick after reset cannot turn");
    turning.update(0,10);check(turning.update(1,20)==-30,"right snap follows native yaw convention");
    check(turning.update(1,30)==0 && turning.update(-1,40)==0,"held or crossed stick cannot repeat snap");
    turning.update(0,50);check(turning.update(-1,60)==30,"center rearms snap");
    turning.suspend();check(turning.update(-1,160)==0,"temporary turn refusal cannot rearm a held snap stick");
    turning.update(0,170);check(turning.update(-1,180)==30,"physical neutral still rearms after turn suspension");
    turning.reset();turning.suspend();check(turning.update(1,190)==0,"suspension cannot undo a real context reset's neutral requirement");
    turning.reset();turning.update(0,0,TurnMode::smooth);
    check(std::fabs(turning.update(1,10,TurnMode::smooth)+1.2f)<0.0001f,"smooth turn scales with elapsed time");
    check(turning.update(1,1000,TurnMode::smooth)==-6.f,"long hitch cannot create large smooth turn");
    turning.suspend();check(turning.update(1,2000,TurnMode::smooth)==0,"resumed smooth turn cannot accumulate elapsed time while permission was absent");
    check(std::fabs(turning.update(1,2010,TurnMode::smooth)+1.2f)<.0001f,"valid smooth turn continues on the next timed sample");
    const auto cycle_start=checks;
    const auto fresh=[] {
        xr::ActionSnapshot v{};v.sequence=1;v.focused=true;
        for(auto& h:v.hands) {
            h.grip.active=h.grip.position_valid=h.grip.orientation_valid=true;
            h.grip.position_tracked=h.grip.orientation_tracked=true;
            h.trigger.active=h.stick.active=true;
        }
        return v;
    };
    for(bool handed:{false,true}) {
        ActionRouter r(handed);auto v=fresh();const unsigned hand=handed?0:1;
        auto tick=[&](float x,float y,bool admit=true) {
            ++v.sequence;v.hands[hand].stick.current={x,y};
            return r.update(v,Context::gameplay,v.sequence,admit);
        };
        check(!tick(0,1).weapon_next,"held first up cannot cycle");
        check(!tick(0,0).weapon_next,"fresh neutral arms without command");
        check(!tick(0,.5f).weapon_next,"gradual upward ramp below threshold");
        auto press=tick(0,1);
        check(press.weapon_next && press.edge_count==0 && press.turn==0,"one independent up press both handed modes");
        check(!r.update(v,Context::gameplay,v.sequence,true).weapon_next,"duplicate press never repeats");
        check(!tick(0,1).weapon_next,"fresh held press never repeats");
        auto crossed=tick(1,0);check(!crossed.weapon_next && crossed.turn==0,"cycle excursion cannot become lateral turn");
        v.hands[hand].stick.current={};
        check(r.update(v,Context::gameplay,v.sequence,true).turn==0,"mutated duplicate neutral cannot release claim");
        v.hands[hand].stick.current={1,0};
        check(r.update(v,Context::gameplay,v.sequence,true).turn==0,"same-sequence return keeps excursion blocked");
        tick(0,0);crossed=tick(1,0);
        check(crossed.turn==1 && !crossed.weapon_next,"neutral restores original lateral turn");
        check(!tick(0,1).weapon_next,"lateral excursion cannot slide into cycle");
        tick(0,0);tick(0,-1);check(!tick(0,1).weapon_next,"down-to-up requires neutral");
        tick(0,0);v.hands[hand].stick_click={true,true,true};
        check(!tick(0,1).weapon_next,"melee click excludes cycle");
        v.hands[hand].stick_click={true,false,true};
        check(!tick(0,1).weapon_next,"released click while still up cannot revive");
        tick(0,0);v.menu={true,true,true};check(!tick(0,1).weapon_next,"menu press excludes cycle");
        v.menu={true,false,true};check(!tick(0,1).weapon_next,"released menu while up cannot revive");
        tick(0,0);check(tick(0,1).weapon_next,"neutral recovers clean new cycle");
        r.release_all();check(!tick(0,1).weapon_next,"release_all resets cycle");
        tick(0,0);tick(0,1,false);check(!tick(0,1).weapon_next,"disabled admission requires new neutral");
    }
    for(unsigned invalid=0;invalid<12;++invalid) {
        ActionRouter r;auto v=fresh();r.update(v,Context::gameplay,1,true);
        ++v.sequence;v.hands[1].stick.current={0,1};
        check(r.update(v,Context::gameplay,2,true).weapon_next,"invalidation setup cycle");
        ++v.sequence;
        switch(invalid) {
        case 0:v.focused=false;break;
        case 1:v.hands[0].grip.position_tracked=false;break;
        case 2:v.hands[1].grip.orientation_valid=false;break;
        case 3:v.hands[1].stick.active=false;break;
        case 4:v.hands[1].stick.current.x=std::numeric_limits<float>::quiet_NaN();break;
        case 5:v.hands[1].stick.current.y=std::numeric_limits<float>::infinity();break;
        case 6:v.sequence=1;break;
        case 7:break; // menu context
        case 8:break; // suspended
        case 9:break; // backwards clock
        case 10:r.expire(300);break;
        case 11:--v.sequence;v.focused=false;break; // duplicate invalidation
        }
        const auto context=invalid==7?Context::menu:invalid==8?Context::suspended:Context::gameplay;
        check(!r.update(v,context,invalid==9?0:3,true).weapon_next,"invalid sample never cycles");
        v=fresh();v.sequence=100;v.hands[1].stick.current={0,1};
        check(!r.update(v,Context::gameplay,400,true).weapon_next,"restored held input cannot revive cycle");
        ++v.sequence;v.hands[1].stick.current={};r.update(v,Context::gameplay,401,true);
        ++v.sequence;v.hands[1].stick.current={0,1};
        check(r.update(v,Context::gameplay,402,true).weapon_next,"new neutral restores eligibility");
    }
    for(bool handed:{false,true})for(bool downward:{false,true}) {
        ActionRouter r(handed);auto v=fresh();const unsigned hand=handed?0:1;
        r.update(v,Context::gameplay,1,true); // Fresh neutral initially arms.
        v.hands[hand].stick.current=downward?xr::Vec2f{0,-1}:xr::Vec2f{1,0};
        check(!r.update(v,Context::gameplay,2,true).weapon_next,"duplicate conflicting excursion cannot press");
        ++v.sequence;v.hands[hand].stick.current={0,1};
        check(!r.update(v,Context::gameplay,3,true).weapon_next,"duplicate lateral/down cancels until fresh neutral");
        ++v.sequence;v.hands[hand].stick.current={};r.update(v,Context::gameplay,4,true);
        ++v.sequence;v.hands[hand].stick.current={0,1};
        check(r.update(v,Context::gameplay,5,true).weapon_next,"fresh neutral recovers after duplicate conflict");
    }
    {
        ActionRouter r;auto v=fresh();r.update(v,Context::gameplay,1,true);
        // Changing a value without a new sequence cannot consume an armed edge.
        v.hands[1].stick.current={0,1};check(!r.update(v,Context::gameplay,2,true).weapon_next,"mutated duplicate never presses");
        ++v.sequence;check(r.update(v,Context::gameplay,3,true).weapon_next,"fresh publication can consume prior neutral");
    }
    for(bool admitted:{false,true}) {
        ActionRouter r;auto v=fresh();
        for(auto& h:v.hands) {h.primary.active=h.secondary.active=h.stick_click.active=true;}
        r.update(v,Context::gameplay,1,admitted);++v.sequence;
        for(auto& h:v.hands) {h.trigger.current=1;h.primary.current=h.secondary.current=h.stick_click.current=true;}
        const auto all=r.update(v,Context::gameplay,2,admitted);
        check(all.edge_count==8 && !all.weapon_next,"all eight paired actions retained");
        for(unsigned i=0;i<all.edge_count;++i)check(static_cast<unsigned>(all.edges[i].action)==i && all.edges[i].pressed,"paired ordering retained");
        const auto releases=r.release_all();check(releases.edge_count==8 && !releases.weapon_next,"no invented cycle release");
        for(unsigned i=0;i<releases.edge_count;++i)check(!releases.edges[i].pressed,"ordinary release retained");
    }
    for(bool left_handed:{false,true}) {
        ActionRouter mounted(left_handed);
        auto v=fresh();
        const unsigned hand=left_handed?0u:1u;
        check(!mounted.update(v,Context::mounted,1,true).look_edge_count,"mounted starts arming from neutral");
        v.hands[hand].stick.current={0.f,1.f}; ++v.sequence;
        const auto up=mounted.update(v,Context::mounted,v.sequence,true);
        check(up.look_edge_count==1 && up.look_edges[0].action==LookAction::up && up.look_edges[0].pressed,"mounted first dominant up press");
        ++v.sequence; v.hands[hand].stick.current={0.f,1.f};
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"mounted ignores held up repeat");
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        const auto reverse=mounted.update(v,Context::mounted,v.sequence,true);
        check(reverse.look_edge_count==1 && !reverse.look_edges[0].pressed && reverse.look_edges[0].action==LookAction::up,"mounted opposite direction releases without re-trigger");
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"direct reverse disarms until fresh neutral");
        ++v.sequence; v.hands[hand].stick.current={};
        mounted.update(v,Context::mounted,v.sequence,true);
        ++v.sequence; v.hands[hand].stick.current={1.f,1.f};
        const auto lateral=mounted.update(v,Context::mounted,v.sequence,true);
        check(lateral.look_edge_count==0,"lateral is blocked while not held");
        ++v.sequence; v.hands[hand].stick.current={};
        mounted.update(v,Context::mounted,v.sequence,true);
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        const auto down=mounted.update(v,Context::mounted,v.sequence,true);
        check(down.look_edge_count==1 && down.look_edges[0].action==LookAction::down && down.look_edges[0].pressed,"mounted accepts opposite direction after neutral");
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"mounted holds down without repeats");
        auto duplicate_down=mounted.update(v,Context::mounted,v.sequence,true);
        check(duplicate_down.look_edge_count==0,"duplicate sample keeps held down and emits no edge");
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"duplicate does not re-trigger");
        v.hands[hand].stick.current={}; auto duplicate_neutral=mounted.update(v,Context::mounted,v.sequence,true);
        check(duplicate_neutral.look_edge_count==1 && !duplicate_neutral.look_edges[0].pressed && duplicate_neutral.look_edges[0].action==LookAction::down,"duplicate neutral releases held");
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"duplicate neutral disarms");
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        check(!mounted.update(v,Context::mounted,v.sequence,true).look_edge_count,"duplicate-neutral leaves re-arm blocked");
        ++v.sequence; v.hands[hand].stick.current={}; mounted.update(v,Context::mounted,v.sequence,true);
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        check(mounted.update(v,Context::mounted,v.sequence,true).look_edge_count==1,"non-duplicate neutral recovers after duplicate neutral");
        ++v.sequence; v.hands[hand].stick.current={};
        mounted.update(v,Context::mounted,v.sequence,true);
        ++v.sequence; v.hands[hand].stick.current={0.f,-1.f};
        check(mounted.update(v,Context::mounted,v.sequence,true).look_edge_count==1,"fresh neutral re-arms after duplicate release");
    }
    {
        for(bool left_handed:{false,true}) {
            ActionRouter r(left_handed);
            auto v=fresh();
            const unsigned hand=left_handed?0u:1u;
            r.update(v,Context::mounted,1,true);
            v.hands[hand].stick.current={0.f,1.f}; ++v.sequence;
            check(r.update(v,Context::mounted,v.sequence,true).look_edge_count==1,"focus loss after mount holds a look");
            v.focused=false; ++v.sequence;
            check(r.update(v,Context::mounted,v.sequence,true).look_edge_count==1,"focused out releases held look");
            v.focused=true;
            ++v.sequence; v.hands[hand].stick.current={0.f,1.f};
            check(!r.update(v,Context::mounted,v.sequence,true).look_edge_count,"focus return remains disarmed");
            v.hands[hand].stick.current={}; ++v.sequence; r.update(v,Context::mounted,v.sequence,true);
            ++v.sequence; v.hands[hand].stick.current={0.f,1.f};
            check(r.update(v,Context::mounted,v.sequence,true).look_edge_count==1,"focus return re-arms with neutral");

            {
                ActionRouter t(left_handed);
                auto vv=fresh();
                t.update(vv,Context::mounted,1,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(t.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"tracking loss releases held look");
                vv.hands[hand].grip.position_tracked=false; ++vv.sequence;
                check(t.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"dominant tracking loss disarms");
                vv.hands[hand].grip.position_tracked=true; ++vv.sequence; vv.hands[hand].stick.current={0.f,1.f};
                check(!t.update(vv,Context::mounted,vv.sequence,true).look_edge_count,"tracking recovery needs re-arm");
            }

            {
                ActionRouter m(left_handed);
                auto vv=fresh();
                m.update(vv,Context::mounted,1,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(m.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"menu/button press invalidates look");
                vv.menu={true,true,true}; ++vv.sequence;
                check(m.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"menu press releases held look");
                vv.menu={true,false,true};
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(!m.update(vv,Context::mounted,vv.sequence,true).look_edge_count,"menu release alone does not re-arm");
                vv.hands[hand].stick.current={}; ++vv.sequence;
                m.update(vv,Context::mounted,vv.sequence,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(m.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"menu recovery requires re-arm");
            }
            {
                ActionRouter click_router(left_handed);
                auto vv=fresh();
                click_router.update(vv,Context::mounted,1,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(click_router.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"stick click invalidates look");
                vv.hands[hand].stick_click={true,true,true}; ++vv.sequence;
                check(click_router.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"stick click releases held look");
                vv.hands[hand].stick_click={true,false,true}; ++vv.sequence; vv.hands[hand].stick.current={0.f,1.f};
                check(!click_router.update(vv,Context::mounted,vv.sequence,true).look_edge_count,"stick click release needs re-arm");
            }
            {
                ActionRouter c(left_handed);
                auto vv=fresh();
                c.update(vv,Context::mounted,1,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(c.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"context move releases held look");
                ++vv.sequence; vv.menu={true,false,true};
                check(c.update(vv,Context::menu,vv.sequence,true).look_edge_count==1,"context transition releases hold");
                vv.menu={true,false,true}; ++vv.sequence;
                check(!c.update(vv,Context::mounted,vv.sequence,true).look_edge_count,"context transition stays disarmed");
            }
            {
                ActionRouter b(left_handed);
                auto vv=fresh();
                b.update(vv,Context::mounted,1,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(b.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"backward/duplicate are invalidating");
                vv.sequence=1; vv.hands[hand].stick.current={0.f,1.f}; vv.focused=true;
                check(b.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"older sequence cannot recover held press");
                b.expire(500);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(!b.update(vv,Context::mounted,vv.sequence,true).look_edge_count,"expiry disarms look");
                vv.hands[hand].stick.current={}; ++vv.sequence;
                b.update(vv,Context::mounted,vv.sequence,true);
                vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
                check(b.update(vv,Context::mounted,vv.sequence,true).look_edge_count==1,"expiry recovery requires neutral");
            }
        }
    }
    {
        ActionRouter mounted_expire;
        auto vv=fresh();
        const unsigned hand=1u;
        mounted_expire.update(vv,Context::mounted,1000,true);
        vv.hands[hand].stick.current={0.f,1.f}; ++vv.sequence;
        check(mounted_expire.update(vv,Context::mounted,1250,true).look_edge_count==1,"mounted look press before age boundary");
        check(!mounted_expire.expire(1500).look_edge_count,"mounted age at250ms does not release");
        const auto aged_release=mounted_expire.expire(1501);
        check(aged_release.look_edge_count==1 && !aged_release.look_edges[0].pressed && aged_release.look_edges[0].action==LookAction::up,"mounted age at250+1ms releases held");
    }
    {
        ActionRouter mount_gate;
        auto v=fresh();
        for(auto& h:v.hands) {h.primary.active=h.secondary.active=h.stick_click.active=true;}
        mount_gate.update(v,Context::mounted,1,true);
        v.hands[0].trigger.current=1; v.hands[1].trigger.current=1;
        v.hands[0].primary.current=v.hands[1].primary.current=true;
        v.hands[0].secondary.current=v.hands[1].secondary.current=true;
        v.hands[0].stick_click.current=v.hands[1].stick_click.current=true;
        ++v.sequence;
        const auto all_actions=mount_gate.update(v,Context::mounted,v.sequence,true);
        check(all_actions.edge_count==2 && all_actions.edges[0].action==Action::fire && all_actions.edges[1].action==Action::use,"mounted emits only fire/use");
        check(!all_actions.weapon_next && all_actions.forward==0.f && all_actions.right==0.f && all_actions.turn==0.f,"mounted suppresses movement and weapon_next");
        check(!all_actions.menu_accept && !all_actions.menu_back && !all_actions.menu_up && !all_actions.menu_down,"mounted suppresses gameplay/menu actions");
    }
    std::printf("weapon_cycle_router_checks=%u\n",checks-cycle_start);
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
