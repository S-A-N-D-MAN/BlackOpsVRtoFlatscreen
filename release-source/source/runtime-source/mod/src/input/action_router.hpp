// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <array>
#include <cstdint>

namespace bovr::input {
enum class Context : std::uint8_t { suspended, menu, gameplay, mounted, scripted_use, scripted_linked, vehicle_weapon, guided_missile, aircraft_monitor, scripted_attack };
enum class Action : std::uint8_t { fire, ads, use, reload, jump, crouch, sprint, melee, count };
enum class LookAction : std::uint8_t { up, down, count };
struct Edge { Action action{}; bool pressed{}; };
struct LookEdge { LookAction action{}; bool pressed{}; };
struct Output {
    std::array<Edge,16> edges{};
    unsigned edge_count{};
    float forward{}, right{}, turn{};
    std::array<LookEdge,2> look_edges{};
    unsigned look_edge_count{};
    bool menu_toggle{}, menu_accept{}, menu_back{}, menu_up{}, menu_down{};
    bool weapon_next{},weapon_alternate{},weapon_support{},lower_stance{}; // One-shot requests; never held Actions.
};
// Value-only bridge: no guessed engine layout, native call, or OS input.
// Native delivery must consume edges once on the game's command path.
// Each control requires an active finite neutral sample after invalidation.
class ActionRouter final {
public:
    explicit ActionRouter(bool left_handed=false,bool cod4=false):left_handed_(left_handed),cod4_(cod4){}
    Output update(const wawvr::xr::ActionSnapshot&, Context, std::uint64_t now_ms,
                  bool weapon_cycle_allowed=false,bool shouldered=false,bool manual_busy=false,bool dual=false,
                  bool button_grenade_busy=false,bool primary_fire_override=false,
                  bool primary_fire_pulse=false,bool reload_selection_escape=false,
                  bool support_selection_available=false,bool primary_melee_override=false,
                  bool ordinary_aim_wait=false) noexcept;
    Output expire(std::uint64_t now_ms) noexcept;
    Output release_all() noexcept;
    void invalidate_weapon_cycle() noexcept {
        cycle_armed_=cycle_claimed_=false;
        cycle_pending_=support_armed_=false;
        alternate_armed_=alternate_claimed_=false;
    }
private:
    std::array<bool,static_cast<unsigned>(Action::count)> held_{}, armed_{};
    bool left_handed_{}, seen_{}, menu_held_{}, menu_armed_{};
    bool menu_accept_{}, menu_back_{}, menu_up_{}, menu_down_{};
    bool move_armed_{},turn_armed_{};
    std::array<bool,static_cast<unsigned>(LookAction::count)> mounted_look_held_{}, mounted_look_armed_{};
    bool cycle_armed_{},cycle_claimed_{};
    bool cycle_pending_{},support_armed_{},support_available_{};
    bool alternate_armed_{},alternate_claimed_{};
    bool cod4_{},lower_armed_{},lower_claimed_{},dual_{},primary_fire_override_{},primary_melee_override_{};
    std::uint64_t sequence_{}, last_sample_ms_{};
    Context context_{Context::suspended};
};
}
