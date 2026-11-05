// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_stage_timing.hpp"
namespace bovr::t5 {
enum class BloomSteadyStage : unsigned {before,initial_source,initial_quad,query,programs,replay,restore,post_validate,cleanup,after,count};
enum class BloomSteadyOutcome : unsigned {early,finished,unrelated,changed,quad_state,accepted,count};
struct BloomSteadyCounter {std::uint64_t entered{},returned{},ticks{},invalid_clock{};};
struct BloomSteadyRecord {
    std::array<BloomSteadyCounter,10> stages{};
    std::array<std::uint64_t,6> outcomes{};
    std::array<std::uint64_t,3> source_calls{},source_ticks{}; // accepted, unrelated, changed
    std::uint64_t producer{},generation{},frame{},source_reads{},quad_reads{},guards{},com_calls{};
    unsigned draws{},expected{},width{},height{},view_count{},pending{},active_mask{};
    bool complete{},abandoned{},overflow{},nested{};
    const char* reason{"none"};
};
struct BloomSteadyProfile {
    static constexpr unsigned warm_limit=30,record_limit=8;
    std::array<BloomSteadyRecord,record_limit> records{};
    unsigned warm{},used{};bool configured{},enabled{},active{},reported{};
    void configure(bool value) noexcept {if(!configured){configured=true;enabled=value;}}
    void begin(bool ordinary,bool detailed_exhausted,std::uint64_t producer,std::uint64_t generation,std::uint64_t frame) noexcept {
        // Never overwrite an interrupted sample or reset the process-State cap.
        if(active){records[used].abandoned=true;active=false;++used;}
        if(!enabled||reported||used>=record_limit||!ordinary||!detailed_exhausted||warm<warm_limit)return;
        active=true;auto& r=records[used];r={};r.producer=producer;r.generation=generation;r.frame=frame;
    }
    void finish(bool ordinary_complete) noexcept {
        if(active){active=false;++used;}
        else if(enabled&&ordinary_complete&&warm<warm_limit)++warm;
    }
    void outcome(BloomSteadyOutcome value) noexcept {
        if(!active)return;auto& r=records[used];auto& n=r.outcomes[static_cast<unsigned>(value)];
        if(n==UINT64_MAX)r.overflow=true;else ++n;
    }
};
struct BloomSteadyToken {std::uint64_t start{};unsigned kind{};bool entered{},clock_valid{};};
template<class Clock=BloomStageQpc>
__declspec(noinline) void bloom_steady_begin(BloomSteadyProfile& profile,BloomSteadyStage kind,BloomSteadyToken& token) noexcept {
    alignas(16) unsigned char fp[512];_fxsave(fp);const auto error=GetLastError();token={};
    if(profile.active&&profile.used<profile.record_limit&&static_cast<unsigned>(kind)<10){
        auto& r=profile.records[profile.used];token.kind=static_cast<unsigned>(kind);token.entered=true;
        const auto bit=1u<<token.kind;if(r.active_mask&bit)r.nested=true;r.active_mask|=bit;
        auto& c=r.stages[token.kind];if(c.entered==UINT64_MAX)r.overflow=true;else ++c.entered;
        LARGE_INTEGER now{};token.clock_valid=bloom_stage_detail::clock<Clock>(&now)&&now.QuadPart>0;
        if(token.clock_valid)token.start=static_cast<std::uint64_t>(now.QuadPart);
    }
    SetLastError(error);_fxrstor(fp);
}
template<class Clock=BloomStageQpc>
__declspec(noinline) void bloom_steady_end(BloomSteadyProfile& profile,const BloomSteadyToken& token) noexcept {
    alignas(16) unsigned char fp[512];_fxsave(fp);const auto error=GetLastError();
    if(token.entered&&profile.active&&profile.used<profile.record_limit&&token.kind<10){
        auto& r=profile.records[profile.used];auto& c=r.stages[token.kind];
        if(c.returned==UINT64_MAX)r.overflow=true;else ++c.returned;
        LARGE_INTEGER now{};const bool valid=bloom_stage_detail::clock<Clock>(&now)&&now.QuadPart>0&&token.clock_valid&&static_cast<std::uint64_t>(now.QuadPart)>=token.start;
        if(valid){const auto delta=static_cast<std::uint64_t>(now.QuadPart)-token.start;
            if(delta>UINT64_MAX-c.ticks){c.ticks=UINT64_MAX;r.overflow=true;}else c.ticks+=delta;
        }else if(c.invalid_clock==UINT64_MAX)r.overflow=true;else ++c.invalid_clock;
        r.active_mask&=~(1u<<token.kind);
    }
    SetLastError(error);_fxrstor(fp);
}
}
