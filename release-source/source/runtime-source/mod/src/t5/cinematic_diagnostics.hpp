// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/cinematic_frame.hpp"

namespace bovr::t5 {
enum CinematicDiagnosticReason : std::uint32_t {
    diagnostic_no_device=1U,diagnostic_no_epoch=2U,diagnostic_native_bytes=4U,
    diagnostic_world_views=8U,diagnostic_input_capture=16U,diagnostic_backend_epoch=32U,
    diagnostic_native_context=64U,diagnostic_decoder_copy=128U,diagnostic_canvas_copy=256U,
    diagnostic_viewport=512U,diagnostic_scissor=1024U,diagnostic_target=2048U,
    diagnostic_publication=4096U,diagnostic_age=8192U,diagnostic_pending=16384U,
    diagnostic_final_serial=32768U,diagnostic_generation_revision=65536U
};
struct CinematicDiagnosticStart {
    FinalizedMenuStamp production;
    NativeFinalUi native{};
    NativeCinematicSnapshot movie{};
    CinematicCanvas canvas{};
    std::uint64_t serial{},backend_serial{},started_ms{};
    std::uint32_t reasons{};
    bool sampled{},movie_copied{},canvas_copied{},owned_native_bytes{};
};
inline std::uint32_t cinematic_diagnostic_reasons(const CinematicDiagnosticStart& s,bool device,
                                                 std::uint64_t backend_serial) noexcept {
    std::uint32_t r{};const auto& p=s.production;
    if(!device)r|=diagnostic_no_device;
    if(!p.epoch||!p.native_epoch)r|=diagnostic_no_epoch;
    if(!s.owned_native_bytes)r|=diagnostic_native_bytes;
    if(p.end.native.view_count)r|=diagnostic_world_views;
    if(s.native.key_catchers)r|=diagnostic_input_capture;
    if(!s.backend_serial||s.backend_serial!=backend_serial)r|=diagnostic_backend_epoch;
    if(!s.native.valid)r|=diagnostic_native_context;
    if(!s.movie_copied)r|=diagnostic_decoder_copy;
    if(!s.canvas_copied)r|=diagnostic_canvas_copy;
    else {
        if(!s.canvas.full_viewport)r|=diagnostic_viewport;
        if(!s.canvas.full_scissor)r|=diagnostic_scissor;
        if(!s.canvas.backbuffer_target)r|=diagnostic_target;
    }
    if(!p.commands||p.commands->native!=p.end.native||!CinematicFrameGate::same_context(p.end.context,s.native)||
       p.end.native.buffer!=s.native.backend||p.end.native.frame!=s.native.native_frame||
       p.end.native.shared_commands!=s.native.commands)r|=diagnostic_publication;
    if(!p.begin.published_ms||s.started_ms<p.begin.published_ms||s.started_ms-p.begin.published_ms>250)r|=diagnostic_age;
    return r;
}
// Diagnostic classification admits captured-input/world-view contexts for
// observation only. No caller may use this result to stage or publish pixels.
inline std::uint32_t cinematic_diagnostic_finish_reasons(const CinematicDiagnosticStart& s,
        const NativeFinalUi& native,std::uint64_t serial,std::uint64_t backend,
        bool pending,std::uint64_t generation,std::uint64_t revision,bool native_bytes,
        bool decoder_copy,std::uint64_t now) noexcept {
    auto r=s.reasons;
    if(!s.serial||s.serial!=serial)r|=diagnostic_final_serial;
    if(!s.backend_serial||s.backend_serial!=backend)r|=diagnostic_backend_epoch;
    if(!pending)r|=diagnostic_pending;
    if(!generation||!revision||s.production.end.generation!=generation||s.production.end.revision!=revision)
        r|=diagnostic_generation_revision;
    if(!CinematicFrameGate::same_frame(s.native,native))r|=diagnostic_native_context;
    if(!native_bytes)r|=diagnostic_native_bytes;
    if(!decoder_copy)r|=diagnostic_decoder_copy;
    if(!s.production.begin.published_ms||now<s.production.begin.published_ms||now-s.production.begin.published_ms>250)
        r|=diagnostic_age;
    return r;
}
inline bool cinematic_diagnostic_can_classify(std::uint32_t reasons) noexcept {
    return !(reasons&~(diagnostic_input_capture|diagnostic_world_views));
}
struct CinematicDiagnosticKey {
    std::array<char,256> movie{};
    std::uintptr_t handle{},focused_menu{};
    std::uint32_t reasons{};
    int connection{},catchers{},stack{},type{};
    CinematicCommandStatus primitive{};
    bool initialized{},finished{},fullscreen{},movie_copied{},classified{};
    friend bool operator==(const CinematicDiagnosticKey&,const CinematicDiagnosticKey&)=default;
};
// At most ten observations/second and512 log records per process. Clock
// regression cannot reset either bound. Frame/texture ping-pong is not a key.
class CinematicDiagnosticBudget {
public:
    bool sample(std::uint64_t now) noexcept {
        if(samples_>=12000||reports_>=512||!now||(sampled_&&(now<sample_ms_||now-sample_ms_<100)))return false;
        sampled_=true;sample_ms_=now;++samples_;return true;
    }
    bool report(std::uint64_t now,const CinematicDiagnosticKey& key) noexcept {
        if(reports_>=512||!now||(reported_&&now<report_ms_))return false;
        const auto age=reported_?now-report_ms_:0;
        if(reports_>=6&&age<2000&&(key==key_||age<250))return false;
        reported_=true;report_ms_=now;key_=key;++reports_;return true;
    }
    std::uint64_t samples()const noexcept{return samples_;}
    std::uint64_t reports()const noexcept{return reports_;}
private:
    CinematicDiagnosticKey key_{};
    std::uint64_t sample_ms_{},report_ms_{},samples_{},reports_{};
    bool sampled_{},reported_{};
};
}
