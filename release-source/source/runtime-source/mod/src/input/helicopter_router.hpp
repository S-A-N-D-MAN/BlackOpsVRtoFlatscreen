// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include "helicopter_axes.hpp"
#include <array>
#include <cstdint>

namespace bovr::input {
enum class HelicopterAction : std::uint8_t { cannon, rockets, ascend, descend, reload, use, count };
struct HelicopterOutput {
    HelicopterAxes axes{};
    std::array<bool, static_cast<unsigned>(HelicopterAction::count)> held{};
    bool valid{};
};

// Native edge slots: attack, throw, up, down, reload, use, vehicleattack,
// gas, reverse, vehicleattacksecond. The PBR's driver-controlled gunner seat
// fires on button35; the Hind turret fires on button34. The river script reads ThrowButtonPressed for Woods/Bowman's
// rockets, just like the Hind; helicopter-only ascent/descent stay released.
inline std::array<bool,10> driver_keys(const HelicopterOutput& output,int type) noexcept {
    if(!output.valid||(type!=4&&type!=6))return {};
    const auto& h=output.held;
    const bool boat=type==4;
    return {h[0],h[1],!boat&&h[2],!boat&&h[3],h[4],h[5],!boat&&h[0],
            boat&&output.axes.forward>0.f,boat&&output.axes.forward<0.f,boat&&h[0]};
}

// Value-only controller policy. The adapter owns native edges and vehicle identity.
// Invalid controls release immediately; only fresh neutral publications rearm.
class HelicopterRouter final {
public:
    void reset() noexcept {
        held_.fill(false);
        armed_.fill(false);
        published_.fill(false);
        sticks_ = {};
        active_ = false;
        // Keep the watermark: reset must not make the same XR publication fresh.
        // A restarted sequence is rejected once, then rebased by update().
    }

    HelicopterOutput update(const wawvr::xr::ActionSnapshot& s, bool context_active,
                            std::uint64_t now_ms) noexcept {
        const bool duplicate = seen_ && s.sequence == sequence_;
        const bool backwards = seen_ && (s.sequence < sequence_ || now_ms < last_now_ms_);
        const bool expired = seen_ && now_ms >= sample_ms_ && now_ms - sample_ms_ > 250;
        const bool fresh = s.sequence > 0 && (!seen_ || s.sequence > sequence_);
        const bool allowed = context_active && s.focused && s.sequence > 0 && !backwards &&
            !(duplicate && expired) && tracked(s.hands[0].grip) && tracked(s.hands[1].grip);
        if (!allowed || !active_ || expired) reset();
        // An invalid publication still consumes its identity. Returning focus or
        // tracking on a duplicate cannot turn changed neutral into fresh input.
        if (s.sequence > 0) {
            if (!duplicate) sample_ms_ = now_ms;
            sequence_ = s.sequence;
            seen_ = true;
        }
        last_now_ms_ = now_ms;
        if (!allowed) return {};
        active_ = true;

        const auto& left = s.hands[0];
        const auto& right = s.hands[1];
        const std::array<Button, count> desired{trigger(right), trigger(left),
            button(right.primary), button(right.secondary),
            grip(right.squeeze, held_[index(HelicopterAction::reload)]), button(left.primary)};
        for (unsigned i = 0; i < count; ++i) {
            const auto b = desired[i];
            if (!b.valid) {
                held_[i] = armed_[i] = false;
            } else if (fresh) {
                if (b.neutral) { held_[i] = false; armed_[i] = true; }
                else held_[i] = armed_[i] && b.pressed;
            } else if (b.pressed != held_[i]) {
                // Changed duplicates may release, never acquire or rearm.
                held_[i] = armed_[i] = false;
            }
        }

        const auto move = update_stick(left.stick, sticks_[0], fresh);
        const auto look = update_stick(right.stick, sticks_[1], fresh);
        HelicopterOutput out{};
        out.valid = true;
        // Each group has already been sanitized independently. Valid zero axes
        // deliberately release native movement when an individual group is lost.
        out.axes = helicopter_axes(move.x, move.y, look.x, look.y, true);
        out.held = held_;
        const auto up = index(HelicopterAction::ascend), down = index(HelicopterAction::descend);
        if (out.held[up] && out.held[down]) out.held[up] = out.held[down] = false;
        if (!fresh) {
            // A duplicate release of one opposed button cannot synthesize a new
            // press of the other, previously suppressed, button.
            for (unsigned i = 0; i < count; ++i) out.held[i] = out.held[i] && published_[i];
        }
        published_ = out.held;
        return out;
    }

private:
    static constexpr unsigned count = static_cast<unsigned>(HelicopterAction::count);
    static constexpr unsigned index(HelicopterAction action) noexcept { return static_cast<unsigned>(action); }
    struct Button { bool valid{}, pressed{}, neutral{}; };
    struct Stick { wawvr::xr::Vec2f raw{}; bool armed{}, copied{}; };

    static bool tracked(const wawvr::xr::TrackedPose& p) noexcept {
        return p.active && p.orientation_valid && p.position_valid && p.orientation_tracked && p.position_tracked;
    }
    static Button button(const wawvr::xr::BoolActionState& b) noexcept {
        return {b.active, b.active && b.current, b.active && !b.current};
    }
    static Button trigger(const wawvr::xr::HandActionState& h) noexcept {
        if (h.trigger.active && !std::isfinite(h.trigger.current)) return {};
        const bool valid = h.trigger.active || h.trigger_click.active;
        const bool pressed = (h.trigger.active && h.trigger.current >= 0.55f) ||
                             (h.trigger_click.active && h.trigger_click.current);
        return {valid, valid && pressed, valid && !pressed};
    }
    static Button grip(const wawvr::xr::FloatActionState& b, bool held) noexcept {
        if (!b.active || !std::isfinite(b.current)) return {};
        return {true, b.current >= 0.65f || (held && b.current > 0.25f), b.current <= 0.25f};
    }
    static wawvr::xr::Vec2f update_stick(const wawvr::xr::Vec2ActionState& input, Stick& state,
                                        bool fresh) noexcept {
        if (!input.active || !std::isfinite(input.current.x) || !std::isfinite(input.current.y)) {
            state = {};
            return {};
        }
        if (fresh) {
            state.raw = input.current;
            state.copied = true;
            if (std::fabs(input.current.x) <= 0.20f && std::fabs(input.current.y) <= 0.20f)
                state.armed = true;
        } else if (!state.copied || input.current.x != state.raw.x || input.current.y != state.raw.y) {
            state = {};
        }
        return state.armed ? state.raw : wawvr::xr::Vec2f{};
    }

    std::array<bool, count> held_{}, armed_{}, published_{};
    std::array<Stick, 2> sticks_{};
    bool seen_{}, active_{};
    std::uint64_t sequence_{}, sample_ms_{}, last_now_ms_{};
};
}
