// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bovr::input {
enum class MotorcycleAction : std::uint8_t {
    gas, reverse, attack, attack_second, handbrake, reload, use, script_attack, count
};

struct MotorcycleOutput {
    float drive{}, steer{};
    std::array<bool, static_cast<unsigned>(MotorcycleAction::count)> held{};
    bool valid{};
};

// Value-only policy for the Vorkuta motorcycle. The adapter owns native command
// edges and the copied campaign/vehicle identity. Fresh analog movement resumes
// after a transition; discrete buttons must be released before reacquiring them.
class MotorcycleRouter final {
public:
    void reset() noexcept {
        held_.fill(false);
        armed_.fill(false);
        published_.fill(false);
        stick_ = {};
        active_ = false;
        // Keep sequence_ as a watermark. Resetting a vehicle context must not
        // make an already-published controller state fresh.
    }

    MotorcycleOutput update(const wawvr::xr::ActionSnapshot& s, bool context_active,
                            std::uint64_t now_ms) noexcept {
        const bool duplicate = seen_ && s.sequence == sequence_;
        const bool backwards = seen_ && (s.sequence < sequence_ || now_ms < last_now_ms_);
        const bool expired = seen_ && now_ms >= sample_ms_ && now_ms - sample_ms_ > 250;
        const bool fresh = s.sequence > 0 && (!seen_ || s.sequence > sequence_);
        const bool allowed = context_active && s.focused && s.sequence > 0 && !backwards &&
            !(duplicate && expired) && tracked(s.hands[0].grip) && tracked(s.hands[1].grip);
        if (!allowed || !active_ || expired) reset();
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
        const auto move = update_stick(left.stick, fresh);
        const bool gas = stick_.armed && move.y >= 0.30f;
        const bool reverse = stick_.armed && move.y <= -0.30f;
        const std::array<Button, count> desired{
            Button{stick_.copied, gas, stick_.copied && !gas},
            Button{stick_.copied, reverse, stick_.copied && !reverse},
            trigger(right), trigger(left), button(right.primary),
            grip(right.squeeze, held_[index(MotorcycleAction::reload)]), button(left.primary), trigger(right)};
        for (unsigned i = 0; i < count; ++i) {
            const auto b = desired[i];
            if (!b.valid) {
                held_[i] = armed_[i] = false;
            } else if (fresh) {
                if (i == index(MotorcycleAction::gas) || i == index(MotorcycleAction::reverse)) {
                    // Throttle is a current analog intention, not a button edge.
                    // A transient snapshot gap must not strand a held stick.
                    armed_[i] = true;
                    held_[i] = b.pressed;
                }
                else if (b.neutral) { held_[i] = false; armed_[i] = true; }
                else held_[i] = armed_[i] && b.pressed;
            } else if (b.pressed != held_[i]) {
                // A changed duplicate may release, but it may not acquire or
                // rearm a native key lease.
                held_[i] = armed_[i] = false;
            }
        }
        if (held_[index(MotorcycleAction::gas)] && held_[index(MotorcycleAction::reverse)]) {
            held_[index(MotorcycleAction::gas)] = false;
            held_[index(MotorcycleAction::reverse)] = false;
        }
        MotorcycleOutput out{};
        out.valid = true;
        out.drive = stick_.armed ? move.y : 0.0f;
        out.steer = stick_.armed ? move.x : 0.0f;
        out.held = held_;
        if (!fresh) {
            for (unsigned i = 0; i < count; ++i) out.held[i] = out.held[i] && published_[i];
        }
        published_ = out.held;
        return out;
    }

private:
    static constexpr unsigned count = static_cast<unsigned>(MotorcycleAction::count);
    static constexpr unsigned index(MotorcycleAction action) noexcept { return static_cast<unsigned>(action); }
    struct Button { bool valid{}, pressed{}, neutral{}; };
    struct Stick { wawvr::xr::Vec2f raw{}; bool armed{}, copied{}; };

    static bool tracked(const wawvr::xr::TrackedPose& p) noexcept {
        return p.active && p.orientation_valid && p.position_valid &&
            p.orientation_tracked && p.position_tracked;
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
    wawvr::xr::Vec2f update_stick(const wawvr::xr::Vec2ActionState& input, bool fresh) noexcept {
        if (!input.active || !std::isfinite(input.current.x) || !std::isfinite(input.current.y)) {
            stick_ = {};
            return {};
        }
        wawvr::xr::Vec2f value{std::clamp(input.current.x, -1.0f, 1.0f),
                              std::clamp(input.current.y, -1.0f, 1.0f)};
        const float length = std::sqrt(value.x * value.x + value.y * value.y);
        if (length <= 0.20f) value = {};
        else {
            const float magnitude = (std::min(length, 1.0f) - 0.20f) / 0.80f;
            value = {value.x / length * magnitude, value.y / length * magnitude};
        }
        if (fresh) {
            stick_.raw = value;
            stick_.copied = true;
            stick_.armed = true;
        } else if (!stick_.copied || value.x != stick_.raw.x || value.y != stick_.raw.y) {
            stick_ = {};
            return {};
        }
        return stick_.armed ? stick_.raw : wawvr::xr::Vec2f{};
    }

    std::array<bool, count> held_{}, armed_{}, published_{};
    Stick stick_{};
    bool seen_{}, active_{};
    std::uint64_t sequence_{}, sample_ms_{}, last_now_ms_{};
};
} // namespace bovr::input
