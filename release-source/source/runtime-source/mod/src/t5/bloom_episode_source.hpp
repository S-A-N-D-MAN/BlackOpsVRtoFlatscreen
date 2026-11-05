// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_probe.hpp"
#include "t5/bloom_episode_native.hpp"
#include "loader/trace.hpp"
namespace bovr::t5 {
struct BloomEpisodeSourceContext {
    BloomShaderSource owner{};
    std::uintptr_t base{};
    const AuxiliaryViewLayout* layout{};
    unsigned rejection_logs{};
};
// Used by the actual adapter and integration fixtures. The legacy callback is
// deliberately removed; complete-LDR has no route back to schema1.
inline BloomShaderSource make_bloom_episode_source(BloomEpisodeSourceContext& c) noexcept {
    auto out=c.owner;out.context=&c;out.read=nullptr;out.native_draw_return=c.base+0x30b5d3;
    out.initial_eye=c.layout&&postfx_view_count(c.layout->count)?postfx_first_eye(c.layout->count):2u;
    out.current=[](void* p) noexcept {const auto& v=*static_cast<BloomEpisodeSourceContext*>(p);
        return v.owner.current&&v.owner.current(v.owner.context);};
    out.read_episode=[](void* p,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* draw,BloomEpisodePlan* plan,BloomReadStatus* why) noexcept {
        auto& v=*static_cast<BloomEpisodeSourceContext*>(p);const auto error=GetLastError();BloomEpisodeRejection reject{};
        const bool ok=v.owner.current&&v.layout&&v.owner.current(v.owner.context)&&
            read_bloom_episode_shader(v.base,*v.layout,cursor,draw,plan,why,&reject)&&v.owner.current(v.owner.context);
        if(!ok&&reject.reason!=BloomEpisodeReject::none&&v.rejection_logs==0){++v.rejection_logs;
            trace("t5_bloom_episode_rejected","reason=%s buffer=%08x frame=%u eye=%u role=%u material=%08x pass=%08x argument_index=%u argument_type=%u argument_destination=%u argument_value=%08x planned_draws=%u accepted=0",
                reject.reason==BloomEpisodeReject::planned_record_cap?"planned_record_cap":reject.reason==BloomEpisodeReject::argument_shape?"argument_shape":reject.reason==BloomEpisodeReject::argument_source?"unhandled_argument_source":reject.reason==BloomEpisodeReject::custom_samplers?"unhandled_custom_samplers":"unhandled_code_sampler",
                reject.buffer,reject.native_frame,reject.eye,reject.role,reject.material,reject.pass,reject.argument_index,unsigned(reject.argument.type),unsigned(reject.argument.destination),reject.argument.value,reject.planned_draws);
        }
        SetLastError(error);return ok;};
    return out;
}
inline void configure_bloom_episode_probe(BloomShaderProbe& p,bool enabled,const std::wstring& directory) noexcept {
    p.configure(enabled,directory,BloomCaptureContract::complete_ldr);
}
}
