// SPDX-License-Identifier: GPL-3.0-only
#include "input/manual_grenade.hpp"
#include "xr_math.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace i=bovr::input;namespace m=i::grenade;namespace g=wawvr::mod;namespace xr=wawvr::xr;
using Stage=g::ManualGrenadeStage;using Slot=g::ManualGrenadeSlot;using Event=g::ManualGrenadeEvent;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",why);}}
float length(xr::Vec3f value){return std::sqrt(value.x*value.x+value.y*value.y+value.z*value.z);}
bool close(xr::Vec3f a,xr::Vec3f b,float tolerance=.002f){return length({a.x-b.x,a.y-b.y,a.z-b.z})<=tolerance;}
struct Fixture {
    m::Controller controller;i::CommandRigSample rig{};m::Context context{};m::Output output{};
    explicit Fixture(float startup_trigger=0){
        context={1000,41,7,true,false,true,true};rig.xr_generation=rig.anchor_generation=7;rig.published_ms=1000;
        auto& frame=rig.frame;frame.frame_id=1;frame.actions.sequence=1;frame.actions.focused=frame.views_valid=true;
        frame.head_center.position={0,1.7f,0};rig.tracking_anchor=frame.head_center;
        auto& hand=left();hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;
        hand.grip.orientation_valid=hand.grip.orientation_tracked=true;hand.trigger.active=true;hand.trigger.current=startup_trigger;
        hand.squeeze.active=true;hip();output=controller.update(rig,context);
    }
    xr::HandActionState& left(){return rig.frame.actions.hands[0];}
    void tick(unsigned ms=16){context.now+=ms;rig.published_ms=context.now;++rig.frame.frame_id;++rig.frame.actions.sequence;output=controller.update(rig,context);}
    void hip(bool tactical=false){
        const auto offset=xr::Rotate(rig.frame.head_center.orientation,{tactical?.25f:-.25f,-.55f,-.1f});
        const auto head=rig.frame.head_center.position;
        left().grip.pose.position={head.x+offset.x,head.y+offset.y,head.z+offset.z};
    }
    void trigger(float value){left().trigger.current=value;tick();}
    void grab(bool tactical=false){hip(tactical);trigger(.9f);}
};
void basic_gestures(){
    for(bool tactical:{false,true}){
        Fixture f;f.grab(tactical);const auto transaction=f.output.transaction;
        check(f.output.valid&&f.output.stage==Stage::holding&&f.output.slot==(tactical?Slot::tactical:Slot::frag)&&
              f.output.event==(tactical?Event::grab_tactical:Event::grab_frag)&&transaction&&f.output.consume_trigger,
              "left trigger chooses frag at left hip and tactical at right hip");
        for(unsigned i=0;i<4;++i)f.tick();
        check(f.output.stage==Stage::holding&&f.output.event==Event::none&&f.output.transaction==transaction,"held trigger cannot emit another grab transaction");
        f.left().grip.pose.position={.15f,1.8f,-.65f};f.context.blocked=true;f.context.frag_available=f.context.tactical_available=false;f.tick();
        check(f.output.stage==Stage::holding&&f.output.slot==(tactical?Slot::tactical:Slot::frag),"held grenade retains slot when hand leaves belt or inventory availability changes");
        f.trigger(.35f);check(f.output.valid&&f.output.stage==Stage::released_pending&&f.output.event==Event::release&&
            f.output.transaction==transaction&&f.output.consume_trigger,"owned grenade releases outside belt despite a new-grab block");
        f.tick();check(f.output.event==Event::none&&f.output.stage==Stage::released_pending,"pending native delivery cannot replay release");
        f.context.blocked=false;f.context.frag_available=f.context.tactical_available=true;f.hip();f.trigger(.9f);
        check(f.output.stage==Stage::released_pending&&f.output.event==Event::none,"another trigger edge cannot replace unretired release");
        f.controller.retire();f.tick();check(f.output.stage==Stage::ready&&f.output.event==Event::none,"retirement retains held-input baseline");
        f.trigger(0);f.trigger(.9f);check(f.output.event==Event::grab_frag&&f.output.transaction>transaction,"fresh post-retirement press owns a new transaction");
    }
    {
        Fixture f(.9f);check(f.output.stage==Stage::ready&&f.output.event==Event::none,"held startup trigger cannot grab");
        f.tick();f.hip(true);f.tick();check(f.output.stage==Stage::ready,"moving held startup trigger between hips cannot grab");
        f.trigger(0);f.trigger(.69f);check(f.output.stage==Stage::ready,"trigger below engage threshold stays neutral");
        f.trigger(.70f);check(f.output.event==Event::grab_tactical,"fresh engage threshold grabs current hip slot");
        f.trigger(.36f);check(f.output.stage==Stage::holding&&f.output.event==Event::none,"hysteresis keeps grenade held above release threshold");
        f.trigger(.35f);check(f.output.event==Event::release,"release threshold creates exactly one release");
    }
    {
        Fixture f;f.left().squeeze.current=1;f.tick();f.hip(true);f.tick();f.left().squeeze.current=0;f.tick();
        check(f.output.stage==Stage::ready&&f.output.transaction==0&&!f.output.consume_trigger,"grip alone never activates grenades or consumes index trigger");
        f.rig.frame.actions.hands[1].trigger.active=true;f.rig.frame.actions.hands[1].trigger.current=1;f.tick();
        check(f.output.stage==Stage::ready,"dominant trigger does not substitute for left grenade trigger");
        f.left().grip.pose.position={-.25f,1.7f,-.1f};f.trigger(.9f);f.hip();f.tick();
        check(f.output.stage==Stage::ready,"trigger pressed outside belt does not grab merely by entering later");
    }
}
void arbitration_and_cancellation(){
    for(unsigned blocker=0;blocker<3;++blocker){
        Fixture f;const bool tactical=blocker==2;
        f.context.blocked=blocker==0;f.context.frag_available=blocker!=1;f.context.tactical_available=blocker!=2;
        f.grab(tactical);check(f.output.stage==Stage::ready&&f.output.transaction==0&&f.output.event==Event::none,
            "reload block or absent slot ammo rejects grab without transaction");
        f.context.blocked=false;f.context.frag_available=f.context.tactical_available=true;f.tick();
        check(f.output.stage==Stage::ready&&f.output.event==Event::none,"blocked press stays consumed after reload or ammo becomes available");
        f.trigger(0);f.trigger(.9f);check(f.output.stage==Stage::holding,"fresh press after blocker clears may grab");
    }
    for(unsigned invalid=0;invalid<12;++invalid){
        Fixture f;f.grab();const auto previous=f.output.transaction;
        switch(invalid){
        case 0:f.rig.frame.actions.focused=false;break;
        case 1:f.left().grip.position_tracked=false;break;
        case 2:f.context.now=f.rig.published_ms+101;break;
        case 3:f.rig.frame.views_valid=false;break;
        case 4:f.left().trigger.active=false;break;
        case 5:f.left().grip.pose.position.x=std::numeric_limits<float>::quiet_NaN();break;
        case 6:f.left().grip.pose.orientation={0,0,0,2};break;
        case 7:f.rig.anchor_generation=0;break;
        case 8:f.rig.published_ms=f.context.now+1;break;
        case 9:f.left().trigger.current=std::numeric_limits<float>::infinity();break;
        case 10:f.context.valid=false;break;
        case 11:f.rig.frame.actions.sequence=1;break;
        }
        f.output=f.controller.update(f.rig,f.context);
        check(!f.output.valid&&f.output.event==Event::reset&&f.output.stage==Stage::ready,"invalid focused tracked freshness context cancels holding without throw");
        // Restore copied input without replacing the controller, preserving the
        // transaction counter and testing the actual post-cancel baseline.
        f.rig.frame.actions.focused=f.rig.frame.views_valid=f.left().grip.position_tracked=f.left().trigger.active=f.context.valid=true;
        f.rig.anchor_generation=f.context.generation;f.left().grip.pose.orientation={};f.left().trigger.current=.9f;f.hip();f.tick();
        check(f.output.valid&&f.output.stage==Stage::ready&&f.output.event==Event::none,"recovery while trigger remains held cannot manufacture another grab or release");
        f.trigger(0);f.trigger(.9f);check(f.output.event==Event::grab_frag&&f.output.transaction>previous,"fresh recovered press advances transaction rather than replaying cancelled ownership");
    }
    {
        Fixture f;f.grab();++f.context.identity;f.tick();check(f.output.valid&&f.output.stage==Stage::ready&&f.output.event!=Event::release,
            "new native identity discards old grenade ownership without release");
        f.trigger(0);f.trigger(.9f);++f.context.generation;f.rig.xr_generation=f.rig.anchor_generation=f.context.generation;f.tick();
        check(f.output.stage==Stage::ready&&f.output.event!=Event::release,"new XR generation cannot throw a previous generation grenade");
    }
}
void velocity_and_coordinates(){
    constexpr float units=xr::kIwUnitsPerMeter;
    {
        Fixture f;f.grab();check(close(f.output.local_origin,{.1f*units,.25f*units,-.55f*units})&&close(f.output.local_forward,{1,0,0}),
            "left-hand release point and forward convert from XR to IW relative to anchor");
        f.left().grip.pose.position.z-=.032f;f.tick();f.trigger(0);
        check(f.output.event==Event::release&&close(f.output.local_velocity,{2*units,0,0},.02f),"recent forward hand motion is retained through stationary release sample");
    }
    for(const auto delta:std::array<xr::Vec3f,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}}){
        Fixture f;f.grab();auto& position=f.left().grip.pose.position;
        position={position.x+delta.x,position.y+delta.y,position.z+delta.z};f.tick();f.trigger(0);
        const xr::Vec3f expected{-delta.z*500,-delta.x*500,delta.y*500};
        check(f.output.event==Event::release&&close(f.output.local_velocity,expected,.05f)&&length(f.output.local_velocity)<=500.05f,
            "fast throw velocity preserves every signed XR-to-IW axis and stays capped");
    }
    {
        Fixture f;f.grab();auto& position=f.left().grip.pose.position;
        position.x+=.16f;position.y+=.04f;position.z-=.12f;f.tick();f.trigger(0);
        const float norm=std::sqrt(.12f*.12f+.16f*.16f+.04f*.04f);
        check(close(f.output.local_velocity,{500*.12f/norm,-500*.16f/norm,500*.04f/norm},.05f),"velocity cap scales diagonal direction uniformly");
    }
    {
        Fixture f;f.grab();f.left().grip.pose.position.z-=.1f;f.tick();
        for(unsigned i=0;i<10;++i)f.tick();f.trigger(0);
        check(f.output.event==Event::release&&length(f.output.local_velocity)<.001f,"old hand movement expires and stationary release stays a deliberate drop");
        f.controller.retire();f.hip();f.tick();f.trigger(.9f);
        for(unsigned i=0;i<10;++i)f.tick();f.trigger(0);check(length(f.output.local_velocity)<.001f,"retired interaction cannot lend old release velocity to new grenade");
    }
    {
        Fixture f;constexpr float half=.70710678118f;
        f.rig.frame.head_center.orientation=f.rig.tracking_anchor.orientation={0,half,0,half};f.hip();f.tick();f.grab();
        check(f.output.event==Event::grab_frag&&close(f.output.local_origin,{.1f*units,.25f*units,-.55f*units},.005f),
            "hip choice and local release point rotate with player and anchor");
        const auto forward=xr::Rotate(f.rig.tracking_anchor.orientation,{0,0,-.032f});auto& position=f.left().grip.pose.position;
        position={position.x+forward.x,position.y+forward.y,position.z+forward.z};f.tick();f.trigger(0);
        check(close(f.output.local_velocity,{2*units,0,0},.02f),"physical throw velocity follows rotated tracking anchor");
    }
    {
        Fixture f;f.grab();const auto current=f.output.local_origin;f.output=f.controller.update(f.rig,f.context);
        check(f.output.event==Event::none&&close(f.output.local_origin,current)&&length(f.output.local_velocity)<.001f,
            "identical repeated sample cannot create motion or duplicate grab");
    }
}
void button_gestures(){
    for(bool tactical:{false,true}){
        Fixture f;f.context.button_mode=true;f.left().stick_click.active=true;
        f.left().grip.pose.position={.4f,1.9f,-.6f};f.tick(); // Deliberately outside both hips.
        f.left().stick_click.current=tactical;f.trigger(.55f);
        check(f.output.consume_trigger&&f.output.event==Event::none,"button analog approach reserves trigger before ordinary jump threshold");
        f.trigger(.9f);const auto id=f.output.transaction;
        check(f.output.event==(tactical?Event::grab_tactical:Event::grab_frag)&&f.output.stage==Stage::holding,
            "fresh button press anywhere chooses modifier slot");
        f.left().stick_click.current=!tactical;f.context.blocked=true;f.tick();
        check(f.output.slot==(tactical?Slot::tactical:Slot::frag)&&f.output.transaction==id,
            "button modifier and later blockers cannot switch an owned grenade");
        f.left().trigger.current=0;f.output=f.controller.update(f.rig,f.context);
        check(f.output.stage==Stage::holding&&f.output.event==Event::none,"changed duplicate cannot release held button grenade");
        f.tick();check(f.output.event==Event::release&&f.output.stage==Stage::released_pending,
            "fresh button release completes despite new blocker without hand motion");
        check(length(f.output.local_velocity)==0,"button mode does not accumulate physical throw velocity");
    }
    {
        Fixture f;f.context.button_mode=true;f.left().stick_click.active=true;f.context.blocked=true;f.tick();
        f.trigger(.55f);check(!f.output.consume_trigger,"blocked support/dual trigger remains available to its original control");
        f.context.blocked=false;f.trigger(.9f);
        check(f.output.event==Event::none&&!f.output.consume_trigger,"clearing blocker during same analog excursion never grabs or steals trigger");
        f.trigger(0);f.trigger(.9f);check(f.output.event==Event::grab_frag,"fresh release and repress after blocker admits grenade");
    }
    {
        Fixture f;f.context.button_mode=true;f.left().stick_click.active=true;f.tick();
        f.context.frag_available=false;f.trigger(.9f);
        check(f.output.event==Event::none&&f.output.consume_trigger,"empty selected slot does not turn grenade press into jump");
        f.context.frag_available=true;f.tick();check(f.output.event==Event::none,"ammo arrival cannot replay blocked button press");
        f.trigger(0);f.left().trigger.current=.9f;f.output=f.controller.update(f.rig,f.context);
        check(f.output.event==Event::none,"changed duplicate cannot manufacture button press");
        f.tick();check(f.output.event==Event::grab_frag,"genuine new trigger sample can press after fresh neutral");
        f.controller.retire(); // Cannot retire holding; cancellation below must still report reset.
        f.left().grip.position_tracked=false;f.tick();
        check(!f.output.valid&&f.output.event==Event::reset,"button tracking loss cancels ownership without release throw");
        f.left().grip.position_tracked=true;f.tick();check(f.output.stage==Stage::ready,"held button recovery cannot grab again");
        f.left().trigger.current=0;f.output=f.controller.update(f.rig,f.context);f.left().trigger.current=.9f;f.tick();
        check(f.output.event==Event::none,"duplicate neutral cannot rearm cancelled button input");
        f.trigger(0);f.trigger(.9f);check(f.output.event==Event::grab_frag,"fresh neutral recovers cancelled button input");
    }
    {
        Fixture f;f.context.button_mode=true;f.tick();f.trigger(.9f);
        check(f.output.event==Event::none&&f.output.consume_trigger,"missing tactical modifier cannot choose unknown slot or jump");
    }
    {
        Fixture f;f.context.button_mode=true;f.left().stick_click.active=true;f.tick();
        constexpr float half=.70710678118f;
        f.rig.frame.head_center.orientation={0,half,0,half};f.tick();f.trigger(.9f);
        check(close(f.output.local_forward,{0,1,0},.002f),"button direction follows head yaw rather than hand orientation");
        f.rig.tracking_anchor.orientation={0,half,0,half};f.tick();
        check(close(f.output.local_forward,{1,0,0},.002f),"button head direction uses same tracking-anchor transform as renderer");
    }
    {
        xr::Vec3f result{};const g::ManualGrenadeLaunchCalibration tuning{700,100,200};
        check(m::button_launch_velocity({0,1,0},tuning,&result)&&close(result,{0,800,200}),"button launch retains native speed and world-up arc");
        check(m::button_launch_velocity({.6f,0,.8f},tuning,&result)&&close(result,{480,0,840}),"button head pitch contributes full forward velocity plus native world-up");
        check(m::button_launch_velocity({0,0,-1},tuning,&result)&&close(result,{0,0,-600}),"button vertical ray remains vertical without fallback horizontal throw");
        check(!m::button_launch_velocity({},tuning,&result)&&length(result)==0,"degenerate button head ray refuses");
        check(!m::button_launch_velocity({1,0,0},{700,-1,200},&result)&&length(result)==0,"invalid native calibration refuses");
        check(!m::button_launch_velocity({1,0,0},{},&result)&&length(result)==0,"zero native launch speed cannot invent force");
        check(!m::button_launch_velocity({1,0,0},tuning,nullptr),"null button launch output refuses");
    }
}
}
int main(){basic_gestures();arbitration_and_cancellation();velocity_and_coordinates();button_gestures();
    std::printf("manual_grenade_checks=%u failures=%u native_calls=0 game_writes=0\n",checks,failures);return failures?1:0;}
