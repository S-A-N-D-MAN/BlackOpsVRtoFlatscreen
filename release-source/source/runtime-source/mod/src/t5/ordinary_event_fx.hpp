// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
namespace bovr::t5::ordinary_event_fx {
enum class Mode { disabled,witness,observe,correct };
inline Mode mode() noexcept {
    wchar_t s[16]{};const auto n=GetEnvironmentVariableW(L"BOVR_ORDINARY_EVENT_FX",s,16);
    if(n==7&&!wcscmp(s,L"witness"))return Mode::witness;
    if(n==7&&!wcscmp(s,L"observe"))return Mode::observe;
    if(n==7&&!wcscmp(s,L"correct"))return Mode::correct;
    return Mode::disabled;
}
struct Angles {float pitch{},yaw{};friend bool operator==(const Angles&,const Angles&)=default;};
inline Angles decode(std::uint32_t word) noexcept {
    return {static_cast<std::int16_t>(word&65535u)*(360.f/65536.f),static_cast<std::int16_t>(word>>16)*(360.f/65536.f)};
}
struct Witness {
    std::array<std::uint8_t,52> command{};
    std::uintptr_t cg{},client{};std::uint64_t publication{},epoch{},revision{},observed{};
    unsigned use{},network_use{},sequence{},event{},parm{},weapon{};int birth{};
    friend bool operator==(const Witness&,const Witness&)=default;
};
inline unsigned appended(unsigned before,unsigned after) noexcept {
    if(before>255||after>255)return 0;
    const auto count=(after-before)&255u;return count<=4?count:0;
}
struct Ledger {
    std::array<Witness,64> rows{};unsigned next{};
    void add(Witness w) noexcept {
        // Prediction may replay the identical command. This only coalesces its
        // immutable witness; no native event is removed or called again.
        for(const auto& row:rows){auto prior=row;prior.observed=w.observed;if(prior==w)return;}
        rows[next++%rows.size()]=w;
    }
    bool find(std::uintptr_t cg,unsigned seq,unsigned event,unsigned parm,unsigned weapon,
              std::uint64_t now,Witness* out)const noexcept {
        if(!out||seq>255)return false;unsigned matches{};Witness found{};
        for(const auto& row:rows)if(row.publication&&row.cg==cg&&row.sequence==seq&&row.event==event&&
            row.parm==parm&&row.weapon==weapon&&row.observed&&now>=row.observed&&now-row.observed<=100){++matches;found=row;}
        if(matches!=1)return false;*out=found;return true;
    }
};
inline bool restore_owned(bool same_incarnation,bool written,Angles selected,Angles current) noexcept {
    return same_incarnation&&written&&!std::memcmp(&selected,&current,sizeof selected);
}
inline unsigned restore_mask(bool same_incarnation,unsigned written,Angles selected,Angles current) noexcept {
    if(!same_incarnation)return 0;unsigned a[2]{},b[2]{};std::memcpy(a,&selected,8);std::memcpy(b,&current,8);
    return (written&1&&a[0]==b[0]?1u:0u)|(written&2&&a[1]==b[1]?2u:0u);
}
// The pinned ordinary predicted call is below CG_CheckPlayerstateEvents.
// A batch cursor, not final ps.commandTime, selects the authored witness.
struct Cursor {
    std::uintptr_t fire_return{},entity_return{},check_return{},transition_return{};
    std::uintptr_t ps{},expected_ps{},ops{},expected_ops{};
    unsigned event{},ring_event{},parm{},ring_parm{},sequence{};int cursor{},previous{};
};
inline bool predicted_cursor(const Cursor& c) noexcept {
    return c.fire_return==0x64b86b&&c.entity_return==0x78206f&&c.check_return==0x56e892&&
        c.transition_return==0x5c3901&&c.ps==c.expected_ps&&c.ops==c.expected_ops&&c.sequence<=255&&
        c.cursor>=c.previous&&c.cursor>=int(c.sequence)-4&&c.cursor<int(c.sequence)&&
        (c.event==29||c.event==30)&&c.event==c.ring_event&&c.parm==c.ring_parm;
}
}
namespace bovr::t5 {
bool ordinary_event_fx_enabled() noexcept;
bool prepare_ordinary_event_fx(HMODULE) noexcept;
bool commit_ordinary_event_fx() noexcept;
}
