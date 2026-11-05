// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>
namespace bovr::t5::producer_pacing {
// Copied metadata only. These values are never frame/reservation authority.
struct Context {
    std::uintptr_t adapter{},base{},caller{},frontend{},views{},cg{};
    std::uint32_t thread{},owner{},native_frame{},view_count{},parms_count{};
    std::uint64_t epoch{},command_sequence{},menu_revision{},generation{};
    int command{},native_clock{},game_time{};
    bool live{},ordinary{},known_caller{},recursive{};
    friend bool operator==(const Context&,const Context&)=default;
};
struct Pixels {
    std::uint64_t generation{},frame{},producer{};
    std::uintptr_t buffer{};
    std::uint32_t native_frame{};
    bool valid{},completed{},hud{},menu{},auxiliary{},cinematic{},thermal{};
    friend bool operator==(const Pixels&,const Pixels&)=default;
};
struct Pending {
    std::uint64_t generation{},frame{},published_ms{},reservation{},build{},producer{};
    std::uintptr_t reserved_buffer{},backend_buffer{};
    std::uint32_t reserved_frame{},eye_mask{};
    bool initialized{},busy{},faulted{},reset{},pending{},invalid{},claimed{},staged{};
    bool menu{},menu_complete{},render{},views{},awaiting{};
    friend bool operator==(const Pending&,const Pending&)=default;
};
struct Flight {
    std::uint64_t serial{};Pending completed{};
    friend bool operator==(const Flight&,const Flight&)=default;
};
struct Sample {
    Context context{};Pixels pixels{};Pending pending{};Flight flight{};
    std::uint64_t now_ms{};
    unsigned copy_stage{}; // Diagnostic only: no decision is authorized by it.
    bool copied{};
};
enum class Reason : unsigned { admitted,copy,context,pixels,successor,coordinator,prior,changed,ready,timeout,clock,iterations,diagnostic };
inline const char* name(Reason r) noexcept {
    switch(r){
    case Reason::admitted:return "admitted";case Reason::copy:return "copy";
    case Reason::context:return "context";case Reason::pixels:return "pixels";
    case Reason::successor:return "successor";case Reason::coordinator:return "coordinator";
    case Reason::prior:return "prior";case Reason::changed:return "changed";
    case Reason::ready:return "ready";case Reason::timeout:return "timeout";
    case Reason::clock:return "clock";case Reason::iterations:return "iterations";
    case Reason::diagnostic:return "diagnostic";
    }return "unknown";
}
inline bool fresh(const Sample& s) noexcept {
    return s.pending.published_ms&&s.now_ms>=s.pending.published_ms&&
        s.now_ms-s.pending.published_ms<=100;
}
inline bool healthy(const Pending& p) noexcept {
    return p.initialized&&!p.faulted&&!p.reset;
}
inline bool old_complete(const Sample& s) noexcept {
    const auto& p=s.pending;const auto& x=s.pixels;
    return healthy(p)&&!p.busy&&p.pending&&!p.invalid&&p.claimed&&p.staged&&
        !p.menu&&!p.menu_complete&&p.render&&p.views&&!p.awaiting&&p.eye_mask==3&&
        p.generation==x.generation&&p.frame==x.frame&&p.producer==x.producer&&
        p.backend_buffer==x.buffer&&p.reservation&&p.build&&
        p.reserved_buffer==x.buffer&&p.reserved_frame==x.native_frame&&fresh(s);
}
inline bool submitting_complete(const Sample& s) noexcept {
    if(!healthy(s.pending)||!s.pending.busy||s.pending.pending||!s.flight.serial)return false;
    // Match the exact archived reservation and completed pixels at original age.
    auto archived=s;archived.pending=s.flight.completed;
    return old_complete(archived);
}
inline Reason admit(const Sample& s) noexcept {
    if(!s.copied)return Reason::copy;
    const auto& c=s.context;const auto& x=s.pixels;
    if(!c.live||!c.ordinary||!c.known_caller||c.recursive||!c.adapter||!c.base||
       c.base>UINTPTR_MAX-0x3a17de||c.caller!=c.base+0x3a17de||!c.thread||
       !c.owner||c.thread==c.owner||!c.cg||!c.epoch||!c.command_sequence||
       !c.menu_revision||!c.generation)return Reason::context;
    if(!x.valid||!x.completed||!x.hud||x.menu||x.auxiliary||x.cinematic||x.thermal||
       !x.generation||!x.frame||!x.producer||!x.buffer||x.generation!=c.generation)
        return Reason::pixels;
    // The reader proves both native ring buffers/view arrays and the empty
    // current slot. The completed pixels are copied staging metadata, not
    // authority to read or reuse the old native buffer contents.
    const bool alternate=x.native_frame<UINT32_MAX&&c.native_frame==x.native_frame+1&&
        c.frontend!=x.buffer;
    const bool reused=x.native_frame<=UINT32_MAX-2&&c.native_frame==x.native_frame+2&&
        c.frontend==x.buffer;
    if(!c.frontend||!c.views||c.view_count||c.parms_count||(!alternate&&!reused))
        return Reason::successor;
    if(!healthy(s.pending))return Reason::coordinator;
    if(submitting_complete(s))return Reason::admitted;
    if(s.pending.busy)return Reason::coordinator;
    return !s.flight.serial&&old_complete(s)?Reason::admitted:Reason::prior;
}
inline Reason step(const Sample& initial,const Sample& current) noexcept {
    if(!current.copied)return Reason::copy;
    if(current.context!=initial.context||current.pixels!=initial.pixels)return Reason::changed;
    const auto& p=current.pending;
    const auto& old=initial.flight.serial?initial.flight.completed:initial.pending;
    if(!healthy(p))return Reason::coordinator;
    // Only the exact finish-issued proof can extend an empty/busy interval.
    // Its removal ends the wait; no idle/begin operation inherits this proof.
    if(!p.pending) {
        if(!submitting_complete(current))return Reason::changed;
        const bool same=initial.flight.serial?current.flight==initial.flight:
            current.flight.completed==old;
        return same?Reason::admitted:Reason::changed;
    }
    if(current.flight.serial)return Reason::changed;
    if(p.generation==old.generation&&p.frame==old.frame)
        return !initial.flight.serial&&p==old&&old_complete(current)?Reason::admitted:Reason::changed;
    const bool next=old.frame!=UINT64_MAX&&p.generation==old.generation&&p.frame==old.frame+1;
    if(next&&!p.busy&&!p.invalid&&!p.claimed&&!p.staged&&!p.reservation&&!p.build&&
       !p.reserved_buffer&&!p.reserved_frame&&!p.backend_buffer&&!p.producer&&!p.eye_mask&&
       !p.menu&&!p.menu_complete&&p.render&&p.views&&!p.awaiting&&fresh(current))
        return Reason::ready;
    return Reason::changed;
}
struct Result {Reason reason{Reason::copy};bool admitted{};unsigned yields{};std::int64_t elapsed_ticks{};};
inline constexpr unsigned maximum_yields=2048;
// Deadline is two milliseconds of QPC, plus a 2048-yield hard bound. OS scheduling
// may overshoot a deadline; this is not a guarantee of <=2ms wall time.
// sample() must release every metadata lock before returning. No lease is held.
template<class Clock,class Read,class Yield>
Result run(const Sample& initial,bool wait,std::int64_t frequency,Clock&& clock,Read&& read,Yield&& yield) {
    Result out{};out.reason=admit(initial);out.admitted=out.reason==Reason::admitted;
    if(!out.admitted)return out;
    if(!wait){out.reason=Reason::diagnostic;return out;}
    if(frequency<500||frequency>1000000000000LL){out.reason=Reason::clock;return out;}
    const auto budget=(frequency+499)/500;const auto start=clock();auto previous=start;
    if(start<=0){out.reason=Reason::clock;return out;}
    for(unsigned n=0;n<maximum_yields;++n){
        auto now=clock();
        if(now<previous){out.reason=Reason::clock;return out;}
        previous=now;out.elapsed_ticks=now-start;
        if(out.elapsed_ticks>=budget){out.reason=Reason::timeout;return out;}
        Sample current{};
        if(!read(&current)){out.reason=Reason::copy;return out;}
        out.reason=step(initial,current);
        if(out.reason!=Reason::admitted)return out;
        // Metadata copies themselves count against the budget.
        now=clock();if(now<previous){out.reason=Reason::clock;return out;}
        previous=now;out.elapsed_ticks=now-start;
        if(out.elapsed_ticks>=budget){out.reason=Reason::timeout;return out;}
        yield();++out.yields;
    }
    const auto end=clock();out.elapsed_ticks=end>=start?end-start:0;
    out.reason=end<previous?Reason::clock:Reason::iterations;return out;
}
}
