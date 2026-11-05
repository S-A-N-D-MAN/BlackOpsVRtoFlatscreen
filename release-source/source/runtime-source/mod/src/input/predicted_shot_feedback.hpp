// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

namespace bovr::input {
// Copied native predicted-player events, never trigger edges or server hit
// confirmation. The retail producer stores its sequence modulo 256 in a word.
struct PredictedFireSample {
    std::uint64_t generation{},epoch{},player{},occurrence{},observed_ms{},published_ms{};
    unsigned weapon{},sequence{};
    int native_time{};
    std::array<int,4> events{};
    bool valid{},dual{};
};
struct PredictedFireResult {
    unsigned shots{};
    bool reset{};
    std::array<unsigned,2> hand_shots{};
};
class PredictedShotFeedback final {
public:
    static constexpr std::uint64_t maximum_age_ms=100,kick_duration_ms=120;
    static constexpr float maximum_kick_meters=0.018F;

    PredictedFireResult observe(const PredictedFireSample& s,std::uint64_t now) noexcept {
        if(!s.valid||!s.generation||!s.epoch||!s.player||!s.weapon||!s.occurrence||s.sequence>255||
           !fresh(s.observed_ms,now)||!fresh(s.published_ms,now))return invalidate();
        if(!baseline_||!same_identity(s)||now<previous_.observed_ms||
           now-previous_.observed_ms>maximum_age_ms) {
            seed(s);return {0,true};
        }
        // Repeated native callbacks cannot refresh time or replay an event.
        if(s.occurrence<=previous_.occurrence||s.observed_ms<previous_.observed_ms)return {};
        const unsigned delta=(s.sequence-cursor_)&255u;
        if(s.native_time<previous_.native_time) {
            // Normally the command observer changes gameplay epoch on native
            // time rollback. Retain both high-water marks even if it does not:
            // recovery must not replay events from the superseded prediction.
            const int high_time=previous_.native_time;
            previous_=s;previous_.native_time=high_time;
            if(delta<128)cursor_=s.sequence;
            kick_started_=0;return {0,true};
        }
        previous_=s;
        if(delta>=128) {
            // A prediction rollback retains the high-water cursor. Reseeding
            // here would replay already-observed shots when prediction catches up.
            kick_started_=0;return {0,true};
        }
        if(delta>4) {
            // More events than the native four-entry ring can prove: forget the
            // gap without inventing how many rounds were fired.
            cursor_=s.sequence;kick_started_=0;return {0,true};
        }
        PredictedFireResult result{};
        for(unsigned offset=0;offset<delta;++offset) {
            const auto event=s.events[(cursor_+offset)&3u];
            if(event==29||event==30){++result.shots;++result.hand_shots[1];}
            else if(s.dual&&(event==31||event==32)){++result.shots;++result.hand_shots[0];}
        }
        cursor_=s.sequence;
        if(result.shots)kick_started_=now;
        return result;
    }
    PredictedFireResult invalidate() noexcept {
        const bool was=baseline_||kick_started_!=0;
        previous_={};cursor_=0;kick_started_=0;baseline_=false;return {0,was};
    }
    bool current(std::uint64_t generation,std::uint64_t epoch,std::uint64_t now) const noexcept {
        return baseline_&&previous_.generation==generation&&previous_.epoch==epoch&&
            fresh(previous_.observed_ms,now)&&fresh(previous_.published_ms,now);
    }
    float kickback(std::uint64_t generation,std::uint64_t epoch,unsigned weapon,std::uint64_t now) const noexcept {
        if(!current(generation,epoch,now)||previous_.weapon!=weapon||!kick_started_||now<kick_started_||
           now-kick_started_>=kick_duration_ms)return 0;
        const float remaining=1.F-static_cast<float>(now-kick_started_)/static_cast<float>(kick_duration_ms);
        return maximum_kick_meters*remaining*remaining;
    }
private:
    static bool fresh(std::uint64_t time,std::uint64_t now) noexcept {
        return time&&now>=time&&now-time<=maximum_age_ms;
    }
    bool same_identity(const PredictedFireSample& s) const noexcept {
        return s.generation==previous_.generation&&s.epoch==previous_.epoch&&
            s.player==previous_.player&&s.weapon==previous_.weapon&&s.dual==previous_.dual;
    }
    void seed(const PredictedFireSample& s) noexcept {
        previous_=s;cursor_=s.sequence;kick_started_=0;baseline_=true;
    }
    PredictedFireSample previous_{};
    unsigned cursor_{};
    std::uint64_t kick_started_{};
    bool baseline_{};
};
}
