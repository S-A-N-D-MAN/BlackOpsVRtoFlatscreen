// SPDX-License-Identifier: GPL-3.0-only
#include "action_router.hpp"
#include <algorithm>
#include <cmath>
namespace bovr::input {
namespace {
namespace xr=wawvr::xr;
constexpr unsigned count=static_cast<unsigned>(Action::count);
constexpr unsigned look_count=static_cast<unsigned>(LookAction::count);
constexpr float kMountedLookUp=0.65f, kMountedLookDown=-0.65f, kMountedLookX=0.20f;
constexpr float kWeaponAlternatePress=0.65f, kWeaponAlternateRelease=0.25f;
constexpr float kSupportStickPress=.75f,kSupportStickRelease=.35f,kSupportStickDominance=.12f;
struct Button { bool valid{},pressed{}; };
struct Stick { xr::Vec2f value{}; bool valid{}; };
Button button(const xr::BoolActionState& b) noexcept { return {b.active,b.active && b.current}; }
bool tracked(const xr::TrackedPose& p) noexcept {
    return p.active && p.orientation_valid && p.position_valid && p.orientation_tracked && p.position_tracked;
}
Button trigger(const xr::HandActionState& h) noexcept {
    if(h.trigger.active && !std::isfinite(h.trigger.current)) return {};
    return {h.trigger_click.active || h.trigger.active,
            (h.trigger_click.active && h.trigger_click.current) || (h.trigger.active && h.trigger.current>=0.55f)};
}
Stick stick(const xr::HandActionState& h) noexcept {
    if (!h.stick.active || !std::isfinite(h.stick.current.x) || !std::isfinite(h.stick.current.y)) return {};
    const float x=std::clamp(h.stick.current.x,-1.f,1.f),y=std::clamp(h.stick.current.y,-1.f,1.f);
    const float length=std::sqrt(x*x+y*y);
    if (length<=0.2f) return {{},true};
    const float magnitude=(std::min(length,1.f)-0.2f)/0.8f;
    return {{x/length*magnitude,y/length*magnitude},true};
}
int mounted_look(const xr::Vec2f& v) noexcept {
    if (v.y>=kMountedLookUp && std::fabs(v.x)<=kMountedLookX) return static_cast<int>(LookAction::up);
    if (v.y<=kMountedLookDown && std::fabs(v.x)<=kMountedLookX) return static_cast<int>(LookAction::down);
    return -1;
}
}
Output ActionRouter::release_all() noexcept {
    Output out{};
    for (unsigned i=0;i<count;++i) {
        if (held_[i]) out.edges[out.edge_count++]={static_cast<Action>(i),false};
        held_[i]=false; armed_[i]=false;
    }
    for (unsigned i=0;i<look_count;++i) {
        if (mounted_look_held_[i]) out.look_edges[out.look_edge_count++]={static_cast<LookAction>(i),false};
        mounted_look_held_[i]=false; mounted_look_armed_[i]=false;
    }
    menu_held_=menu_armed_=menu_accept_=menu_back_=menu_up_=menu_down_=false;
    move_armed_=turn_armed_=false;
    invalidate_weapon_cycle();
    lower_armed_=lower_claimed_=false;
    primary_fire_override_=primary_melee_override_=false;
    return out;
}
Output ActionRouter::expire(std::uint64_t now) noexcept {
    if (seen_ && (now<last_sample_ms_ || now-last_sample_ms_>250)) return release_all();
    return {};
}
Output ActionRouter::update(const xr::ActionSnapshot& s,Context context,std::uint64_t now,
                            bool weapon_cycle_allowed,bool shouldered,bool manual_busy,bool dual,bool button_grenade_busy,
                            bool primary_fire_override,bool primary_fire_pulse,bool reload_selection_escape,
                            bool support_selection_available,bool primary_melee_override,bool ordinary_aim_wait) noexcept {
    Output out{};
    // Re-evaluate validity even for duplicate sequences: focus/tracking/action
    // invalidation must never be hidden by cached axes. Held-state comparison
    // naturally suppresses duplicate edges; only a new sequence refreshes age.
    const bool duplicate=seen_ && s.sequence==sequence_;
    const bool expired=duplicate && (now<last_sample_ms_ || now-last_sample_ms_>250);
    const bool backwards=seen_ && (s.sequence<sequence_ || now<last_sample_ms_);
    const bool frozen_prompt=context==Context::scripted_use||context==Context::scripted_attack;
    ordinary_aim_wait=ordinary_aim_wait&&context==Context::gameplay&&!dual&&!primary_fire_override;
    primary_melee_override=primary_melee_override&&primary_fire_override&&context==Context::gameplay;
    const bool context_changed=context!=context_||dual!=dual_||primary_fire_override!=primary_fire_override_;
    const bool melee_mode_changed=primary_melee_override!=primary_melee_override_;
    const unsigned dominant=left_handed_?0:1, support=dominant^1;
    const auto& d=s.hands[dominant]; const auto& n=s.hands[support];
    const bool allowed=s.focused && context!=Context::suspended && !backwards && !expired &&
                       tracked(d.grip) && tracked(n.grip);
    if (!allowed || context_changed) out=release_all();
    else if(melee_mode_changed) {
        // A native swim permission read can briefly refuse during an otherwise
        // valid input context. Changing which key owns the knife gesture must
        // balance those keys without disarming a held movement/turn stick.
        for(const auto action:{Action::fire,Action::melee}) {
            const auto i=static_cast<unsigned>(action);
            if(held_[i])out.edges[out.edge_count++]={action,false};
            held_[i]=armed_[i]=false;
        }
    }
    if(support_available_!=support_selection_available) {
        cycle_armed_=cycle_claimed_=cycle_pending_=support_armed_=false;
        support_available_=support_selection_available;
    }
    if(!duplicate) last_sample_ms_=now;
    seen_=true; sequence_=s.sequence; context_=context;dual_=dual;primary_fire_override_=primary_fire_override;
    primary_melee_override_=primary_melee_override;
    if (!allowed) return out;
    std::array<Button,count> desired{trigger(d),trigger(n),button(d.primary),button(d.secondary),
        button(n.primary),button(n.secondary),button(n.stick_click),button(d.stick_click)};
    if(primary_fire_override)
        desired[unsigned(Action::fire)]={true,primary_fire_pulse&&!primary_melee_override};
    if(primary_melee_override) {
        // Some disabled-weapon scripts poll MeleeButtonPressed instead of the
        // primary knife's attack. Keep ownership in the melee action so pulse,
        // stick-click, pause and context exit all share a balanced native key.
        const auto click=button(d.stick_click);
        desired[unsigned(Action::melee)]={true,primary_fire_pulse||click.pressed};
    }
    const auto move_state=stick(n),turn_state=stick(d);
    const auto move=move_state.value,turn=turn_state.value;
    if(cod4_&&context!=Context::menu){
        const auto left_fire=trigger(n);
        desired[unsigned(Action::ads)]=dual?Button{left_fire.valid,left_fire.pressed&&!manual_busy}:Button{true,shouldered&&!manual_busy};
        desired[unsigned(Action::use)]=button(n.primary);
        desired[unsigned(Action::reload)]=button(d.primary);
        const auto jump=trigger(n);
        desired[unsigned(Action::jump)]={jump.valid||turn_state.valid,
            (!manual_busy&&!dual&&jump.pressed)||((!manual_busy||button_grenade_busy)&&
            turn_state.valid&&turn.y>=.65f&&std::fabs(turn.x)<=.20f)};
        desired[unsigned(Action::crouch)]=button(d.secondary);
        // Left-stick click selects tactical on the grenade trigger edge. Its
        // ordinary sprint hold ends while that trigger belongs to a grenade.
        if(button_grenade_busy)desired[unsigned(Action::sprint)]={n.stick_click.active,false};
    }
    if(cod4_&&context==Context::scripted_linked) {
        // Linked scripts poll held native mouse buttons without an aimed gun.
        // Keep this distinct from ordinary jump, shouldering and dual fire.
        desired[unsigned(Action::ads)]=trigger(n);
    }
    if(context==Context::aircraft_monitor) {
        // The native monitor polls both mouse buttons, use, jump and melee. A separate
        // command button keeps zoom independent of the ordinary trigger jump.
        desired={};
        desired[unsigned(Action::fire)]=trigger(d);
        desired[unsigned(Action::ads)]=trigger(n);
        desired[unsigned(Action::use)]=button(n.primary);
        desired[unsigned(Action::jump)]=button(d.primary);
        desired[unsigned(Action::melee)]=button(d.secondary);
    }
    if(!move_state.valid) move_armed_=false;
    else if(move.x==0 && move.y==0) move_armed_=true;
    if(!turn_state.valid) turn_armed_=false;
    else if(turn.x==0 && turn.y==0) turn_armed_=true;
    const auto menu=button(s.menu);
    if(!menu.valid) menu_armed_=false;
    else if(!menu.pressed) menu_armed_=true;
    out.menu_toggle=menu.valid && menu_armed_ && menu.pressed && !menu_held_;
    menu_held_=menu.pressed;
    if(context==Context::aircraft_monitor&&menu.pressed) {
        // Stop held monitor commands on the pause edge itself. Resuming with a
        // held trigger/button must pass neutral again before native delivery.
        const auto released=release_all();
        for(unsigned i=0;i<released.edge_count;++i)out.edges[out.edge_count++]=released.edges[i];
        return out;
    }
    if (context==Context::mounted || frozen_prompt ||
        context==Context::scripted_linked || context==Context::vehicle_weapon || context==Context::guided_missile ||
        context==Context::aircraft_monitor) {
        for (unsigned i=0;i<count;++i) {
            if(context==Context::guided_missile&&i!=static_cast<unsigned>(Action::fire)) {
                held_[i]=false;armed_[i]=false;continue;
            }
            const bool monitor_action=context==Context::aircraft_monitor&&
                (i==static_cast<unsigned>(Action::fire)||i==static_cast<unsigned>(Action::ads)||
                 i==static_cast<unsigned>(Action::use)||i==static_cast<unsigned>(Action::jump)||
                 i==static_cast<unsigned>(Action::melee));
            if (!monitor_action && i!=static_cast<unsigned>(Action::use) &&
                ((context!=Context::mounted&&context!=Context::scripted_linked&&context!=Context::vehicle_weapon&&context!=Context::guided_missile&&context!=Context::scripted_attack) ||
                 i!=static_cast<unsigned>(Action::fire)) &&
                (context!=Context::scripted_linked || (i!=static_cast<unsigned>(Action::ads)&&i!=static_cast<unsigned>(Action::melee))) &&
                (!frozen_prompt || (i!=static_cast<unsigned>(Action::jump)&&i!=static_cast<unsigned>(Action::melee)))) {
                // Frozen prompts retain use/jump/melee. The separate disabled-
                // weapon linked route also permits native attack for prompts
                // such as grabbing Dragovich, without aim or locomotion.
                held_[i]=false; armed_[i]=false; continue;
            }
            if(!desired[i].valid) armed_[i]=false;
            else if(!desired[i].pressed) armed_[i]=true;
            const bool next=desired[i].valid && armed_[i] && desired[i].pressed;
            if (next!=held_[i]) out.edges[out.edge_count++]={static_cast<Action>(i),next};
            held_[i]=next;
        }
    } else {
        for (unsigned i=0;i<count;++i) {
            if(ordinary_aim_wait&&i==static_cast<unsigned>(Action::fire)) {
                // Balance an already delivered attack while exact ordinary aim
                // waits for neutral. Keep the REAL trigger state: a held trigger
                // disarms, while an observed release may arm a later deliberate
                // press after aim is current again. Other actions are unchanged.
                const bool analog_neutral_valid=!d.trigger.active||
                    (std::isfinite(d.trigger.current)&&d.trigger.current>=0&&d.trigger.current<=1);
                armed_[i]=desired[i].valid&&!desired[i].pressed&&analog_neutral_valid;
                if(held_[i])out.edges[out.edge_count++]={Action::fire,false};
                held_[i]=false;continue;
            }
            if(!desired[i].valid) armed_[i]=false;
            else if(!desired[i].pressed) armed_[i]=true;
            const bool next=context==Context::gameplay && desired[i].valid && armed_[i] && desired[i].pressed;
            if (next!=held_[i]) out.edges[out.edge_count++]={static_cast<Action>(i),next};
            held_[i]=next;
        }
    }
    if (context==Context::gameplay || context==Context::scripted_linked || context==Context::aircraft_monitor) {
        if (move_armed_) {out.forward=move.y; out.right=move.x;}
        if (context==Context::gameplay&&turn_armed_) out.turn=turn.x;
        // The SR71 script reads GetNormalizedMovement for its feed camera.
        // Prefer the dominant stick while deflected; keep support navigation
        // as a fallback. Both sticks retain their independent neutral arming.
        if (context==Context::aircraft_monitor&&turn_armed_&&(turn.x!=0||turn.y!=0)) {
            out.forward=turn.y;out.right=turn.x;
        }
    } else if (context==Context::menu) {
        const bool accept=armed_[static_cast<unsigned>(Action::use)] && desired[static_cast<unsigned>(Action::use)].pressed;
        const bool back=armed_[static_cast<unsigned>(Action::reload)] && desired[static_cast<unsigned>(Action::reload)].pressed;
        const bool up=move_armed_ && move.y>0.65f,downward=move_armed_ && move.y<-0.65f;
        out.menu_accept=accept && !menu_accept_; out.menu_back=back && !menu_back_;
        out.menu_up=up && !menu_up_; out.menu_down=downward && !menu_down_;
        menu_accept_=accept; menu_back_=back; menu_up_=up; menu_down_=downward;
    } else if (context==Context::mounted && !cod4_) {
        // The tracked-controller layout gives the mounted gun one aim owner.
        // Keep these legacy keyboard-look aliases out of that route, including
        // vertical stick input; ordinary turning is already disabled above.
        const int held=mounted_look_held_[0]?0:(mounted_look_held_[1]?1:-1);
        const auto release_mounted=[&]{
            for(unsigned i=0;i<look_count;++i) {
                if (mounted_look_held_[i]) out.look_edges[out.look_edge_count++]={static_cast<LookAction>(i),false};
                mounted_look_held_[i]=false;
            }
            mounted_look_armed_.fill(false);
        };
        if (!turn_state.valid || button(d.stick_click).pressed || menu.pressed) {
            release_mounted();
        } else {
            const bool centered = turn.x==0.f && turn.y==0.f;
            const int look=mounted_look(turn);
            const bool has_look = look>=0;
            if (held!=-1 && (!has_look || look!=held)) {
                release_mounted();
                if (centered) mounted_look_armed_.fill(!duplicate);
            } else if (duplicate && held!=-1 && has_look && look==held) {
                // Duplicate unchanged directional hold must remain held without edges.
            } else if (centered) {
                mounted_look_armed_.fill(!duplicate);
            } else if (!has_look) {
                mounted_look_armed_.fill(false);
            } else if (!duplicate && mounted_look_armed_[look]) {
                mounted_look_armed_[look]=false;
                mounted_look_held_[look]=true;
                out.look_edges[out.look_edge_count++]={static_cast<LookAction>(look),true};
            } else if (duplicate) {
                mounted_look_armed_.fill(false);
            }
        }
    }
    if(cod4_){
        const auto cycle=button(n.secondary);
        const auto support_x=move_state.valid?std::clamp(n.stick.current.x,-1.f,1.f):0.f;
        const auto support_y=move_state.valid?std::clamp(n.stick.current.y,-1.f,1.f):0.f;
        const bool support_neutral=move_state.valid&&std::fabs(support_x)<=kSupportStickRelease&&
                                   std::fabs(support_y)<=kSupportStickRelease;
        const bool support_left=move_state.valid&&support_x<=-kSupportStickPress&&
                                -support_x>=std::fabs(support_y)+kSupportStickDominance;
        const bool support_wrong=move_state.valid&&!support_neutral&&
                                 (support_x>=0.f||-support_x<std::fabs(support_y)+kSupportStickDominance);
        // The support modifier owns this stick excursion. Releasing Y while
        // still deflected cannot turn the selection gesture into locomotion.
        if(support_available_&&context==Context::gameplay&&cycle.pressed) {
            out.forward=out.right=0.f;move_armed_=false;
        }
        const bool bad_grip=d.squeeze.active&&!std::isfinite(d.squeeze.current);
        const bool alternate_press = d.squeeze.active&&!bad_grip&&d.squeeze.current>=kWeaponAlternatePress;
        const bool alternate_neutral = d.squeeze.active&&!bad_grip&&d.squeeze.current<=kWeaponAlternateRelease;
        const bool admit=weapon_cycle_allowed&&context==Context::gameplay&&!menu.pressed&&!button_grenade_busy;
        if(!admit) {
            invalidate_weapon_cycle();
        } else {
            // Each input invalidates immediately, including duplicate XR
            // publications. Only a fresh neutral publication can rearm it.
            // Only Y can request an escape from an unfinished physical reload.
            // The caller must exclude grenade ownership and cancel the reload
            // against the current native publication before delivering weapnext.
            if(!cycle.valid||(manual_busy&&!reload_selection_escape)||
               (support_available_&&!move_state.valid)) {
                cycle_armed_=cycle_claimed_=cycle_pending_=support_armed_=false;
            }
            else if(!duplicate) {
                if(!cycle.pressed) {
                    // Delay only the authored support-weapon section's normal
                    // Y tap so a later left-stick chord cannot first weapnext.
                    out.weapon_next=support_available_&&cycle_pending_&&!manual_busy;
                    cycle_armed_=true;cycle_claimed_=cycle_pending_=false;
                    support_armed_=support_available_&&!manual_busy&&support_neutral;
                }
                else if(cycle_armed_&&!cycle_claimed_) {
                    cycle_armed_=false;cycle_claimed_=true;
                    if(!support_available_||manual_busy)out.weapon_next=true;
                    else cycle_pending_=true;
                }
            }
            if(support_available_&&cycle.pressed) {
                // Invalid or wrong-direction duplicate publications revoke
                // admission too; only fresh publications can select or rearm.
                if(manual_busy||!move_state.valid||support_wrong||
                   (!support_armed_&&!support_neutral)) {
                    cycle_pending_=support_armed_=false;
                } else if(!duplicate&&cycle_pending_&&support_armed_&&support_left) {
                    cycle_pending_=support_armed_=false;out.weapon_support=true;
                }
            }
            if(manual_busy||!d.squeeze.active||bad_grip)alternate_armed_=alternate_claimed_=false;
            else if(!duplicate) {
                if(alternate_neutral){alternate_armed_=true;alternate_claimed_=false;}
                // Y owns a concurrent selection and consumes the grip press.
                // Releasing Y alone cannot replay it as a later alternate.
                else if(cycle.pressed||out.weapon_next||out.weapon_support){alternate_armed_=false;alternate_claimed_=true;}
                else if(alternate_press&&alternate_armed_&&!alternate_claimed_) {
                    alternate_armed_=false;alternate_claimed_=true;out.weapon_alternate=true;
                }
            }
        }
        if(context!=Context::gameplay||!turn_state.valid||menu.pressed||manual_busy){lower_armed_=lower_claimed_=false;}
        else if(!duplicate){
            if(turn.x==0&&turn.y==0){lower_armed_=true;lower_claimed_=false;}
            else if(lower_armed_&&!lower_claimed_&&turn.y<=-.65f&&std::fabs(turn.x)<=.20f){lower_armed_=false;lower_claimed_=true;out.lower_stance=true;}
        }
    }else if(!weapon_cycle_allowed || context!=Context::gameplay || !turn_state.valid ||
       menu.pressed || button(d.stick_click).pressed) {
        invalidate_weapon_cycle();
    } else {
        // Duplicate publications may invalidate but never arm, press, or release
        // the excursion claim. No repeat and no synthetic release are emitted.
        if(!cycle_claimed_ && (std::fabs(turn.x)>0.20f || turn.y<0)) cycle_armed_=false;
        if(!duplicate) {
            if(turn.x==0 && turn.y==0) {cycle_armed_=true;cycle_claimed_=false;}
            else if(!cycle_claimed_) {
                if(cycle_armed_ && turn.y>=0.65f) {
                    cycle_armed_=false;cycle_claimed_=true;out.weapon_next=true;
                }
            }
        }
        if(cycle_claimed_) out.turn=0;
    }
    return out;
}
}
