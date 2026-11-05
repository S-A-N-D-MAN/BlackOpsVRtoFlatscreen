// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include "t5/borrowed_profile_shape.hpp"

namespace bovr::input::profile_admission {
inline constexpr unsigned policy_revision = 1, max_bones = 160, max_models = 16;
inline bool advance_revision(std::uint64_t& revision) noexcept {
    if(revision==std::numeric_limits<std::uint64_t>::max())return false;
    ++revision;return revision!=std::numeric_limits<std::uint64_t>::max();
}

struct Token {
    std::uint64_t serial{}, revision{};
    friend bool operator==(const Token&,const Token&)=default;
};

struct Descriptor {
    unsigned row{}, revision{}, weapon{}, gun{}, flash{}, bones{};
    unsigned policy{};
    friend bool operator==(const Descriptor&, const Descriptor&) = default;
};
inline bool valid(const Descriptor& d) noexcept {
    if(d.policy!=policy_revision) return false;
    switch(d.row) {
    case 0: case 1: return d.revision==1&&d.weapon==4&&d.gun==72&&d.flash==78&&d.bones==82;
    case 2: return d.revision==1&&d.weapon==4&&d.gun==60&&d.flash==66&&d.bones==70;
    case 3: return d.revision==2&&d.weapon==42&&d.gun==72&&d.flash==75&&d.bones==83;
    case 4: return d.revision==3&&d.weapon==4&&d.gun==60&&d.flash==66&&d.bones==70;
    case 5: return d.revision==3&&d.weapon==42&&d.gun==60&&d.flash==63&&d.bones==71;
    case 6: return d.revision==4&&d.weapon==32&&d.gun==72&&d.flash==82&&d.bones==100;
    case 7: return d.revision==4&&d.weapon==14&&d.gun==72&&d.flash==81&&d.bones==99;
    default: return bovr::t5::borrowed_profile::binding(d.row,d.weapon,d.revision)&&
        bovr::t5::borrowed_profile::matches(d.row,d.gun,d.flash,d.bones);
    }
}
// Integer observations only. A saved value is never permission to dereference
// an address or proof that a native allocation has not been replaced.
struct ObservedIdentity {
    std::uint32_t cg{}, owner{}, object{}, variant{}, definition{}, server{}, table{}, arena{};
    std::uint32_t server_link{}, tracking_mode{};
    std::uint16_t pool{}, tag{}, duplicates{};
    std::uint8_t duplicate_size{};
    std::array<std::array<std::uint32_t,5>,max_models> model_storage{}; // model/name/IDs/parents/bind
    std::array<std::uint16_t,max_bones> ids{};
    friend bool operator==(const ObservedIdentity&, const ObservedIdentity&) = default;
};
struct RearmKey {
    Descriptor profile{};
    ObservedIdentity identity{};
    std::uint64_t gameplay_epoch{}, xr_generation{}, invalidation{};
    std::uint32_t owner_thread{};
    std::array<std::uint32_t,7> anchor_bits{}; // position XYZ, orientation XYZW
    friend bool operator==(const RearmKey&, const RearmKey&) = default;
};
// The neutral latch belongs to the live player/control owner, not one gun
// allocation. Exact profile/model equality remains mandatory for each new
// render token and every command write in the adapter.
inline bool same_control_owner(const RearmKey& a,const RearmKey& b) noexcept {
    return a.gameplay_epoch==b.gameplay_epoch&&a.xr_generation==b.xr_generation&&
        a.invalidation==b.invalidation&&a.owner_thread==b.owner_thread&&a.anchor_bits==b.anchor_bits&&
        a.identity.cg==b.identity.cg&&a.identity.owner==b.identity.owner&&
        a.identity.server==b.identity.server&&a.identity.arena==b.identity.arena&&
        a.identity.server_link==b.identity.server_link&&a.identity.tracking_mode==b.identity.tracking_mode;
}
inline bool valid(const RearmKey& k) noexcept {
    // Structural precondition only. The reader and actual current XR gate,
    // not this copied-value state machine, establish the supplied observations.
    return valid(k.profile)&&k.gameplay_epoch&&k.xr_generation&&k.owner_thread&&
        k.invalidation!=std::numeric_limits<std::uint64_t>::max()&&
        k.identity.cg&&k.identity.owner&&k.identity.object&&k.identity.variant&&
        k.identity.definition&&k.identity.server&&k.identity.table&&k.identity.arena&&
        k.identity.pool&&k.identity.pool<0xc00&&k.identity.tag&&k.identity.duplicates&&
        k.identity.duplicate_size>=21&&(k.identity.duplicate_size&1);
}
struct Trigger {
    bool click_active{}, click_pressed{}, analog_active{};
    float analog{};
};
struct NeutralSample {
    bool focused{}, dominant_tracking{}, menu_active{}, menu_pressed{};
    std::array<bool,2> squeeze_active{};
    std::array<float,2> squeeze{};
    std::array<Trigger,2> trigger{};
};
inline bool neutral(const NeutralSample& s) noexcept {
    if(!s.focused||!s.dominant_tracking||!s.menu_active||s.menu_pressed) return false;
    for(unsigned i=0;i<2;++i) {
        if(!s.squeeze_active[i]||!std::isfinite(s.squeeze[i])||s.squeeze[i]<0||s.squeeze[i]>=.65F) return false;
        const auto& t=s.trigger[i];
        if((!t.click_active&&!t.analog_active)||(t.click_active&&t.click_pressed)) return false;
        if(t.analog_active&&(!std::isfinite(t.analog)||t.analog<0||t.analog>=.55F)) return false;
    }
    return true;
}
enum class RearmResult { refused, wait_neutral, neutral_observed, ready, exhausted };
// Owner-thread copied-value state only; no engine pointer, clock or callback.
// The adapter must atomically revoke on foreign/Reset paths, then supply that
// new invalidation revision here at the owner's next entry. This is not a lock.
class RearmState {
public:
    void revoke(std::uint64_t action_sequence) noexcept {
        armed_=false;have_key_=false;
        if(action_sequence>sequence_floor_)sequence_floor_=action_sequence;
        if(!advance_revision(revision_))exhausted_=true;
    }
    RearmResult observe(const RearmKey& key,std::uint64_t action_sequence,const NeutralSample& sample,bool occurrence_valid) noexcept {
        if(exhausted_)return RearmResult::exhausted;
        // A verified different XR generation may begin a new action sequence.
        // Profile/context invalidation in the same stream never resets floors.
        if(occurrence_valid&&valid(key)&&key_.xr_generation&&key.xr_generation!=key_.xr_generation) {
            revoke(0);last_sequence_=0;sequence_floor_=0;
            if(exhausted_)return RearmResult::exhausted;
        }
        if(!occurrence_valid||!valid(key)||!action_sequence||action_sequence<last_sequence_||
           !sample.focused||!sample.dominant_tracking) {
            revoke(action_sequence);return exhausted_?RearmResult::exhausted:RearmResult::refused;
        }
        last_sequence_=action_sequence;
        if(!have_key_||key_!=key) {
            if(have_key_&&armed_&&same_control_owner(key_,key)) {
                key_=key;return RearmResult::ready; // fresh reader proof, same held input intent
            }
            revoke(action_sequence);
            if(exhausted_)return RearmResult::exhausted;
            key_=key;have_key_=true;
            return RearmResult::wait_neutral; // transition sample cannot rearm
        }
        if(armed_)return RearmResult::ready;
        if(action_sequence<=sequence_floor_||!neutral(sample))return RearmResult::wait_neutral;
        armed_=true;sequence_floor_=action_sequence;
        return RearmResult::neutral_observed; // this command still stays stock
    }
    // A fresh valid weapon/model replacement may keep this latch. This never
    // retains an admission token; unreadable/unsafe context still calls revoke.
    bool armed()const noexcept{return armed_&&!exhausted_;}
    // The caller must prove an actual owned body/anchor commit. This carries
    // existing input intent only; it grants no command or rendering admission.
    // An arbitrary changed anchor passed to observe still requires neutral.
    bool rebase_anchor(const RearmKey& expected,const RearmKey& rebased) noexcept {
        if(!armed()||!have_key_||!valid(expected)||!valid(rebased)||
           !same_control_owner(key_,expected))return false;
        auto unchanged=rebased;unchanged.anchor_bits=expected.anchor_bits;
        if(unchanged!=expected)return false;
        for(unsigned i=0;i<3;++i)if(rebased.anchor_bits[i]!=expected.anchor_bits[i])return false;
        key_=rebased;return true;
    }
    std::uint64_t revision()const noexcept{return revision_;}
    // Collision/body-follow owner already committed this exact horizontal
    // anchor transfer. Orientation, height and every native identity stay exact.
    bool rebase_horizontal_anchor(const RearmKey& expected,const RearmKey& rebased) noexcept {
        if(!armed()||!have_key_||!valid(expected)||!valid(rebased)||
           !same_control_owner(key_,expected))return false;
        auto unchanged=rebased;
        unchanged.anchor_bits[0]=expected.anchor_bits[0];
        unchanged.anchor_bits[2]=expected.anchor_bits[2];
        if(unchanged!=expected)return false;
        key_=rebased;return true;
    }
private:
    RearmKey key_{};
    std::uint64_t revision_{},sequence_floor_{},last_sequence_{};
    bool have_key_{},armed_{},exhausted_{};
};
} // namespace bovr::input::profile_admission
