// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include "gameplay/manual_reload_logic.hpp"
#include <cstdint>

namespace bovr::input::manual {
namespace xr=wawvr::xr;
inline constexpr unsigned max_reload_capacity=4096;
inline constexpr float charging_travel_meters=.075f;
enum class Stage : unsigned { ready, available, held, insertion, charge, charging, committing };
enum class ReloadKind : unsigned { magazine, internal_rounds, break_action, single_round, belt };
struct ReloadMechanism {
    bool valid{};
    ReloadKind kind{ReloadKind::magazine};
    // Zero requests the native magazine fill; other values bound one insertion.
    unsigned rounds_per_insert{};
    bool charge_on_empty{true},cycle_after_shot{};
    xr::Vec3f receiver_offset{0,-.08f,-.10f},handle_offset{0,.045f,-.035f};
    // Native-classified pump shotguns use the support grip and a complete
    // rearward/return stroke. Other charging handles retain trigger pull/release.
    bool grip_pump{};
};
struct Context {
    bool valid{}, left_handed{}, commit_completed{},cod4_controls{},cancel_requested{};
    unsigned weapon{};
    int clip{}, reserve{}, capacity{};
    std::uint64_t epoch{}, generation{}, now{};
    ReloadMechanism mechanism{};
    // Optional validated fire occurrence. Zero uses a fresh clip decrement.
    std::uint64_t shot_sequence{};
    // Native event owner retains this across pose loss until a physical stroke
    // clears Output.cycle_pending; it must not come from trigger/clip guesses.
    bool confirmed_cycle_pending{};
};
struct Output {
    Stage stage{};
    bool supported{}, block_attack{}, request_commit{}, held_device{};
    unsigned weapon{}, hand{1};
    std::uint64_t transaction{}, epoch{}, generation{}, updated_ms{};
    xr::Vec3f supply{}, receiver{}, handle{};
    float charge_fraction{};
    ReloadKind kind{ReloadKind::magazine};
    unsigned rounds_per_insert{},commit_rounds{};
    bool cycle_pending{};
    // Visual attachment only: keep the authored support glove on a pump while
    // held, including at rest and after returning. This never enables aiming.
    bool pump_gripped{};
    // Observations only, for bounded diagnostics of a missed charging gesture.
    bool charge_trigger_armed{},charge_sample_fresh{},charge_capture_near{},charge_acquired{},charge_opened{};
    float charge_handle_distance{},charge_coordinate{};
};
// A chambering requirement blocks firing, but waiting for a charging grab
// does not own the support hand. It may grip the fore-end until an actual
// feed device, handle/pump capture or native commit needs that hand again.
// This copied-value policy never completes a stroke or acknowledges ammo.
inline bool uses_offhand(const Output& value) noexcept {
    return value.supported&&value.block_attack&&
        !(value.stage==Stage::charge&&!value.held_device&&
          !value.request_commit&&!value.pump_gripped);
}
// Interaction coordinates are meters in the current XR reference space.
// Ammo and native animation are owned by the retail adapter, never this core.
class Controller {
public:
    Output update(const xr::FrameState&,const Context&) noexcept;
    // Cancel only the physical transaction. Preserve any uncompleted pump or
    // empty-weapon charging requirement; this never acknowledges native ammo.
    Output cancel() noexcept;
    void reset() noexcept;
private:
    wawvr::gameplay::ReloadState reload_{};
    Output output_{};
    std::uint64_t sequence_{},sample_ms_{},epoch_{},generation_{},transaction_{};
    unsigned weapon_{};
    bool baseline_{},reload_held_{},grip_held_{},trigger_held_{},empty_{};
    // Reload A and the charging trigger recover independently. A held trigger
    // surviving a reset needs genuine fresh neutral before it may grab again.
    bool charge_trigger_armed_{};
    bool inserted_{},charge_grabbed_{},charge_opened_{};
    float charge_start_{};
    ReloadMechanism mechanism_{};
    int previous_clip_{};
    unsigned commit_rounds_{};
    std::uint64_t shot_sequence_{};
    bool cycle_pending_{},left_handed_{},cod4_controls_{},commit_ack_held_{};
};
bool supported_weapon(unsigned) noexcept;
}
