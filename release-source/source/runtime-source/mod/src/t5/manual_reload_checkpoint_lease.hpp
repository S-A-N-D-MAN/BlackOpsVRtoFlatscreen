// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace bovr::t5::checkpoint_lease {
struct Identity {
    std::uintptr_t cg{},server{},world{}; // world may be zero if not mapped.
    friend bool operator==(const Identity&,const Identity&)=default;
};
struct Key {
    std::uintptr_t cg{},predicted{},server{},world{};
    unsigned weapon{};
    std::uintptr_t variant{},definition{};
    unsigned partner{};
    std::uintptr_t pairvariant{},pairdef{};
    friend bool operator==(const Key&,const Key&)=default;
};
struct Times {
    std::int64_t predicted{},server{},native_game{};
    friend bool operator==(const Times&,const Times&)=default;
};
inline Identity identity(const Key& k) noexcept {return {k.cg,k.server,k.world};}
inline bool valid(const Identity& i) noexcept {return i.cg&&i.server&&i.cg!=i.server;}
inline bool valid(const Key& k) noexcept {
    if(!valid(identity(k))||!k.predicted||k.predicted==k.server||!k.weapon||k.weapon>=256||
       !k.variant||!k.definition)return false;
    if(!k.partner)return !k.pairvariant&&!k.pairdef;
    return k.partner<256&&k.partner!=k.weapon&&k.pairvariant&&k.pairdef;
}
inline bool valid_time(std::int64_t time) noexcept {
    return time>=0&&time<=(std::numeric_limits<std::int32_t>::max)();
}
inline bool valid(const Times& t) noexcept {
    return valid_time(t.predicted)&&valid_time(t.server)&&valid_time(t.native_game);
}

// Records actual mod-owned healthy leases, not an inference from a set bit.
// This does not distinguish a script OR of the same bit while already owned.
// It provides no disk-save provenance and never reads/writes native memory.
// The caller serializes all access on the existing main input owner.
class History {
public:
    static constexpr std::size_t capacity=64;
    static constexpr std::uint64_t sample_window_ms=100,death_window_ms=15000;
    // The recorded AK rewind reached its first ordinary pre-input after three
    // loading observations (16 ms wall / 63 ms native), not one render frame.
    static constexpr std::uint64_t recovery_frames=3;

    void reset() noexcept {*this={};}
    void retire() noexcept {
        active_=false;
        // Preserve the preceding owned observation until the next lifecycle
        // input: death may be reported immediately before or after retirement.
    }

    // Call once for every pre-input observation, including death/loading and
    // rejected gameplay. commandclock is the monotonic native input clock,
    // not player commandTime. Its origin is unrelated to role/game times;
    // those times may be greater than it and need not equal each other.
    void observe_lifecycle(Identity id,std::int64_t game_time,std::uint64_t commandclock,
                           bool connected,bool dead,std::uint64_t now) noexcept {
        if(!connected||!valid(id)||!valid_time(game_time)||!commandclock||!now){reset();return;}
        if(!have_lifecycle_||id!=identity_||commandclock<clock_||now<now_){
            reset();bind(id,game_time,commandclock,dead,now);return;
        }
        if(sequence_==UINT64_MAX){reset();return;}
        const auto next_sequence=sequence_+1;
        const bool was_armed=phase_==Phase::death;
        const bool backwards=game_time<game_time_;
        const bool recent_owned=last_owned_sequence_&&last_owned_sequence_+1==next_sequence&&
            now>=last_owned_wall_&&now-last_owned_wall_<=sample_window_ms;

        if(backwards){
            // The death must have been observed BEFORE the rewind, not merely
            // claimed in the same sample as an arbitrary load/time reset.
            if(!was_armed||now<death_wall_||now-death_wall_>death_window_ms){
                reset();bind(id,game_time,commandclock,dead,now);return;
            }
            phase_=Phase::restore;restore_time_=game_time;restore_wall_=now;
            restore_sequence_=next_sequence;active_=false;
        }else if(phase_==Phase::death&&(now<death_wall_||now-death_wall_>death_window_ms)){
            clear_arm();
        }else if(phase_==Phase::restore&&
                 (now<restore_wall_||now-restore_wall_>sample_window_ms||
                  next_sequence-restore_sequence_>recovery_frames||
                  game_time<restore_time_||game_time-restore_time_>static_cast<std::int64_t>(sample_window_ms))){
            clear_arm();
        }
        if(dead&&!dead_&&!backwards&&phase_==Phase::none&&recent_owned){
            phase_=Phase::death;death_wall_=now;active_=false;
        }
        if(dead||last_owned_sequence_+1<next_sequence)active_=false;
        identity_=id;game_time_=game_time;clock_=commandclock;now_=now;
        dead_=dead;sequence_=next_sequence;
    }

    // Only after the caller has acquired/rechecked the actual ordinary lease.
    // False only means no recovery evidence was recorded; it is not a request
    // to change native flags. Gaps/key changes start distinct bounded spans.
    bool record_owned(const Key& key,const Times& times,std::uint64_t commandclock,
                      std::uint64_t now) noexcept {
        if(!fresh(key,times,commandclock,now)||dead_||phase_!=Phase::none)return false;
        if(last_owned_sequence_==sequence_)return false;
        if(active_){
            auto& span=spans_[active_index_];
            if(span.key==key&&last_owned_sequence_+1==sequence_&&
               now>=last_owned_wall_&&now-last_owned_wall_<=sample_window_ms){
                if(times.predicted<span.end.predicted||times.server<span.end.server||
                   times.native_game<span.end.native_game){reset();return false;}
                span.end=times;
            }else {active_=false;}
        }
        if(!active_){
            // Evict only the oldest complete span. Never bridge its gaps or
            // turn an eviction into a wider minimum/maximum ownership range.
            active_index_=next_;spans_[next_]={key,times,times};
            next_=(next_+1)%capacity;if(count_<capacity)++count_;active_=true;
        }
        last_owned_sequence_=sequence_;last_owned_wall_=now;
        return true;
    }

    // Invoke once at the FIRST normal restored candidate, after synchronizing
    // and rechecking native storage. Every invocation consumes the arm, even
    // on wrong key/time/bit. Loading samples should only observe_lifecycle().
    // Success authorizes reacquiring metadata for the already-set known bit;
    // it does not authorize clearing a script bit or claiming startup data.
    bool claim(const Key& key,const Times& times,std::uint64_t commandclock,
               std::uint64_t now,bool normal,bool reload_bit_set) noexcept {
        const auto phase=phase_;const auto restored=restore_time_;
        const auto restored_wall=restore_wall_,restored_sequence=restore_sequence_;
        clear_arm();
        if(phase!=Phase::restore||!normal||!reload_bit_set||dead_||
           !fresh(key,times,commandclock,now)||now<restored_wall||now-restored_wall>sample_window_ms||
           sequence_<restored_sequence||sequence_-restored_sequence>recovery_frames||
           times.native_game<restored||times.native_game-restored>static_cast<std::int64_t>(sample_window_ms))return false;
        for(std::size_t i=0;i<count_;++i){const auto& span=spans_[i];
            if(span.key==key&&inside(restored,span.begin.native_game,span.end.native_game)&&
               inside(times.native_game,span.begin.native_game,span.end.native_game)&&
               inside(times.predicted,span.begin.predicted,span.end.predicted)&&
               inside(times.server,span.begin.server,span.end.server))return true;
        }
        return false;
    }

    std::size_t spans()const noexcept{return count_;}
    bool awaiting_restore()const noexcept{return phase_==Phase::restore;}

private:
    struct Span {Key key{};Times begin{},end{};};
    enum class Phase {none,death,restore};
    static bool inside(std::int64_t t,std::int64_t lo,std::int64_t hi) noexcept {return t>=lo&&t<=hi;}
    bool fresh(const Key& key,const Times& times,std::uint64_t commandclock,std::uint64_t now)const noexcept {
        return have_lifecycle_&&valid(key)&&identity(key)==identity_&&valid(times)&&
            commandclock==clock_&&times.native_game==game_time_&&now>=now_&&now-now_<=sample_window_ms;
    }
    void clear_arm() noexcept {phase_=Phase::none;death_wall_=restore_wall_=restore_sequence_=0;restore_time_=0;}
    void bind(Identity id,std::int64_t game_time,std::uint64_t commandclock,bool dead,std::uint64_t now) noexcept {
        identity_=id;game_time_=game_time;clock_=commandclock;now_=now;
        dead_=dead;have_lifecycle_=true;sequence_=1;
    }
    std::array<Span,capacity> spans_{};
    std::size_t count_{},next_{},active_index_{};
    Identity identity_{};
    std::int64_t game_time_{},restore_time_{};
    std::uint64_t sequence_{},clock_{},now_{},last_owned_sequence_{},last_owned_wall_{};
    std::uint64_t death_wall_{},restore_wall_{},restore_sequence_{};
    Phase phase_{Phase::none};
    bool have_lifecycle_{},dead_{},active_{};
};
}
