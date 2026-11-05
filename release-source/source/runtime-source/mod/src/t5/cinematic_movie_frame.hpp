// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/cinematic_frame.hpp"
#include "t5/cinematic_production.hpp"

namespace bovr::t5 {
// Separate from native-menu revision/event authority. Final2D only records
// completion; the actual native backbuffer is captured after the native tail.
struct CinematicMovieFrameStamp {
    CinematicProductionStamp production;
    NativeFinalUi native{};
    NativeCinematicSnapshot before{},after{};
    CinematicCanvas canvas{};
    std::uint64_t final_serial{},backend_serial{},completed_ms{};
};
class CinematicMovieFrameGate final {
public:
    void invalidate() noexcept {open_={};completed_={};}
    std::shared_ptr<const HudCommandPublication> open_commands() const noexcept {return open_.production.commands;}
    bool begin(std::uint64_t serial,std::uint64_t backend_serial,const CinematicProductionStamp& p,
               const NativeFinalUi& native,const NativeCinematicSnapshot& movie,CinematicCanvas canvas,
               bool owned,std::uint64_t now) noexcept {
        invalidate();if(!serial||serial<=last_serial_||!backend_serial)return false;last_serial_=serial;
        if(!p.epoch||!p.native_epoch||!p.commands||p.commands->native!=p.end.native||!p.begin.observed_ms||
           !p.end.generation||p.begin.generation!=p.end.generation||p.begin.thread!=p.end.thread||
           !p.begin.thread||!p.begin.serial||p.begin.serial>=p.end.serial||p.begin.native.buffer!=p.end.native.buffer||
           p.begin.native.frame!=p.end.native.frame||!owned||native.key_catchers!=0||
           !cinematic_production_same_context(p.end.context,native)||p.end.native.view_count!=0||
           p.end.native.buffer!=native.backend||p.end.native.frame!=native.native_frame||
           p.end.native.shared_commands!=native.commands||!cinematic_production_same_movie(p.end.movie,movie)||
           now<p.begin.observed_ms||now-p.begin.observed_ms>250||movie.observed_ms>now||
           !movie.observed_ms||now-movie.observed_ms>250||!movie.thread_id||
           !canvas.width||!canvas.height||!canvas.full_viewport||!canvas.full_scissor||!canvas.backbuffer_target)return false;
        open_={p,native,movie,{},canvas,serial,backend_serial,0};return true;
    }
    CinematicCommandEvidence complete(std::uint64_t serial,std::uint64_t backend_serial,const NativeFinalUi& native,
             const NativeCinematicSnapshot& movie,bool owned,std::uint64_t now) noexcept {
        auto v=std::move(open_);open_={};completed_={};CinematicCommandEvidence rejected{};
        rejected.status=CinematicCommandStatus::changed_snapshot;
        if(!v.final_serial||serial!=v.final_serial||serial!=last_serial_||backend_serial!=v.backend_serial||!owned||
           !CinematicFrameGate::same_frame(v.native,native)||now<v.production.begin.observed_ms||
           now-v.production.begin.observed_ms>250)return rejected;
        v.after=movie;v.completed_ms=now;
        const auto proof=classify_cinematic_movie_commands(published_shared_list(*v.production.commands),v.before,movie,v.canvas.width,v.canvas.height);
        if(proof.status==CinematicCommandStatus::full_canvas_movie_primitive)completed_=std::move(v);
        return proof;
    }
    bool take_at_present(std::uint64_t serial,std::uint64_t backend_serial,std::uint64_t generation,
             const NativeFinalUi& native,const NativeCinematicSnapshot& movie,std::uint64_t now,
             CinematicMovieFrameStamp* out) noexcept {
        if(out)*out={};auto v=std::move(completed_);completed_={};
        if(!out||!v.final_serial||serial!=v.final_serial||serial!=last_serial_||backend_serial!=v.backend_serial||
           generation!=v.production.end.generation||native.key_catchers!=0||
           !CinematicFrameGate::same_context(v.native,native)||now<v.completed_ms||
           now<v.production.begin.observed_ms||now-v.production.begin.observed_ms>250)return false;
        if(classify_cinematic_movie_commands(published_shared_list(*v.production.commands),v.after,movie,v.canvas.width,v.canvas.height).status!=
           CinematicCommandStatus::full_canvas_movie_primitive)return false;
        *out=std::move(v);return true;
    }
private:
    CinematicMovieFrameStamp open_,completed_;
    std::uint64_t last_serial_{};
};
}
