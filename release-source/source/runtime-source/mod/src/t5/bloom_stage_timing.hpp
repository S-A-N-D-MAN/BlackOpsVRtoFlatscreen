// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <intrin.h>
#include <array>
#include <climits>
#include <cstdint>

namespace bovr::t5 {
enum class BloomStageKind : unsigned {query,programs,replay,restore,count};
enum class BloomStageReturn : unsigned {normal_true,normal_false,normal_void};
struct BloomStageCounter {
    std::uint64_t entered{},normal_true{},normal_false{},normal_void{},valid_pairs{},invalid_clock{},ticks{};
};
// Ordinary State-owned diagnostic storage. Its callers retain the existing
// owner/Operation rules; foreign/reset/unwind paths never touch this record.
struct BloomStageTiming {
    std::array<BloomStageCounter,4> stages{};
    unsigned depth{},overflow_mask{};bool overlap{};
};
// No constructor/destructor or default initializer: every token write happens
// inside the entry helper's independently preserved environment.
struct BloomStageToken {std::uint64_t start;unsigned stage,depth;bool entered,clock_valid;};
struct BloomStageQpc {
    static bool read(LARGE_INTEGER* out) {return QueryPerformanceCounter(out)!=FALSE;}
};
namespace bloom_stage_detail {
template<class Clock>bool clock_seh(LARGE_INTEGER* out) {
    __try {return Clock::read(out);}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class Clock>bool clock(LARGE_INTEGER* out) noexcept {
    try {return clock_seh<Clock>(out);}catch(...){return false;}
}
inline void increment(std::uint64_t& value,BloomStageTiming& record,unsigned stage) noexcept {
    if(value==UINT64_MAX)record.overflow_mask|=1u<<stage;else ++value;
}
}

// Both helpers preserve only their OWN footprint. They never preserve across
// the measured method, never invoke an owner/native/COM callback, and never
// report a telemetry failure to correction admission. The Clock policy is
// compile-time only; production always uses BloomStageQpc.
template<class Clock=BloomStageQpc>
__declspec(noinline) void bloom_stage_begin(BloomStageTiming& record,BloomStageKind kind,BloomStageToken& token) noexcept {
    alignas(16) unsigned char fp[512];_fxsave(fp);const DWORD error=GetLastError();
    token.start=0;token.stage=static_cast<unsigned>(kind);token.depth=0;token.entered=false;token.clock_valid=false;
    if(token.stage<record.stages.size()){
        auto& counter=record.stages[token.stage];bloom_stage_detail::increment(counter.entered,record,token.stage);token.entered=true;
        if(record.depth)record.overlap=true;
        if(record.depth==UINT_MAX){record.overflow_mask|=1u<<token.stage;record.overlap=true;}
        else ++record.depth;
        token.depth=record.depth;
        LARGE_INTEGER value{};
        token.clock_valid=bloom_stage_detail::clock<Clock>(&value)&&value.QuadPart>0;
        if(token.clock_valid)token.start=static_cast<std::uint64_t>(value.QuadPart);
    }else record.overlap=true;
    SetLastError(error);_fxrstor(fp);
}

template<class Clock=BloomStageQpc>
__declspec(noinline) void bloom_stage_end(BloomStageTiming& record,const BloomStageToken& token,BloomStageReturn result) noexcept {
    alignas(16) unsigned char fp[512];_fxsave(fp);const DWORD error=GetLastError();
    if(token.entered&&token.stage<record.stages.size()){
        auto& counter=record.stages[token.stage];
        switch(result){
        case BloomStageReturn::normal_true:bloom_stage_detail::increment(counter.normal_true,record,token.stage);break;
        case BloomStageReturn::normal_false:bloom_stage_detail::increment(counter.normal_false,record,token.stage);break;
        case BloomStageReturn::normal_void:bloom_stage_detail::increment(counter.normal_void,record,token.stage);break;
        default:record.overlap=true;break;
        }
        LARGE_INTEGER value{};const bool valid=bloom_stage_detail::clock<Clock>(&value)&&value.QuadPart>0&&
            token.clock_valid&&static_cast<std::uint64_t>(value.QuadPart)>=token.start;
        if(valid){
            bloom_stage_detail::increment(counter.valid_pairs,record,token.stage);
            const auto delta=static_cast<std::uint64_t>(value.QuadPart)-token.start;
            if(delta>UINT64_MAX-counter.ticks){counter.ticks=UINT64_MAX;record.overflow_mask|=1u<<token.stage;}
            else counter.ticks+=delta;
        }else bloom_stage_detail::increment(counter.invalid_clock,record,token.stage);
        if(!record.depth||record.depth!=token.depth)record.overlap=true;
        if(record.depth)--record.depth;
    }else record.overlap=true;
    SetLastError(error);_fxrstor(fp);
}
}
