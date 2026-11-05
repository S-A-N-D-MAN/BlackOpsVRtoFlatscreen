// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/cinematic_context.hpp"
#include "t5/menu_frame_epoch.hpp"

namespace bovr::t5 {
struct CinematicCanvas {
    std::uint32_t width{},height{};
    bool full_viewport{},full_scissor{},backbuffer_target{};
};
struct CinematicFrameStamp {
    FinalizedMenuStamp production;
    NativeFinalUi native{};
    NativeCinematicSnapshot before{},after{};
    CinematicCanvas canvas{};
    CinematicCommandEvidence primitive{};
    std::uint64_t final_serial{},backend_serial{},completed_ms{};
};
// XR-owner value state; holds only immutable copied commands/scalars. Native
// allocation equality is supplied by the existing guarded copy/recheck seam.
// Every final-loop entry retires the preceding result, including rejected work.
class CinematicFrameGate final {
public:
    void invalidate() noexcept {open_={};completed_={};}
    std::shared_ptr<const HudCommandPublication> open_commands() const noexcept {return open_.production.commands;}
    bool begin(std::uint64_t serial,std::uint64_t backend_serial,const FinalizedMenuStamp& production,
               const NativeFinalUi& native,const NativeCinematicSnapshot& movie,
               CinematicCanvas canvas,bool owned_native_bytes,std::uint64_t now) noexcept {
        invalidate();
        if(!serial||serial<=last_serial_||!backend_serial)return false;last_serial_=serial;
        if(!production.epoch||!production.native_epoch||!production.commands||production.commands->native!=production.end.native||!production.begin.published_ms||
           !production.end.generation||!production.end.revision||native.key_catchers!=0||!same_context(production.end.context,native)||
           production.end.native.buffer!=native.backend||production.end.native.frame!=native.native_frame||
           production.end.native.shared_commands!=native.commands||production.end.native.view_count!=0||
           !owned_native_bytes||!canvas.width||!canvas.height||!canvas.full_viewport||!canvas.full_scissor||!canvas.backbuffer_target||
           now<production.begin.published_ms||now-production.begin.published_ms>250||
           movie.thread_id==0||movie.observed_ms>now||now-movie.observed_ms>250)return false;
        open_={production,native,movie,{},canvas,{},serial,backend_serial,0};return true;
    }
    CinematicCommandEvidence complete(std::uint64_t serial,std::uint64_t backend_serial,const NativeFinalUi& native,
                    const NativeCinematicSnapshot& movie,bool owned_native_bytes,std::uint64_t now) noexcept {
        auto value=open_;open_={};completed_={};
        CinematicCommandEvidence rejected{};rejected.status=CinematicCommandStatus::changed_snapshot;
        if(!value.final_serial||serial!=last_serial_||serial!=value.final_serial||backend_serial!=value.backend_serial||!owned_native_bytes||
           !same_frame(value.native,native)||now<value.production.begin.published_ms||now-value.production.begin.published_ms>250)return rejected;
        value.after=movie;value.completed_ms=now;
        value.primitive=classify_cinematic_commands(published_shared_list(*value.production.commands),value.before,value.after,value.canvas.width,value.canvas.height);
        if(value.primitive.status==CinematicCommandStatus::full_canvas_movie_primitive)completed_=value;
        return value.primitive;
    }
    bool take_at_present(std::uint64_t serial,std::uint64_t backend_serial,std::uint64_t generation,std::uint64_t revision,
                    const NativeFinalUi& current,const NativeCinematicSnapshot& movie,std::uint64_t now,
                    CinematicFrameStamp* output) noexcept {
        if(output)*output={};auto value=completed_;completed_={};
        if(!output||!value.final_serial||serial!=last_serial_||serial!=value.final_serial||backend_serial!=value.backend_serial||
           generation!=value.production.end.generation||revision!=value.production.end.revision||
           !same_context(value.native,current)||now<value.completed_ms||
           now<value.production.begin.published_ms||now-value.production.begin.published_ms>250)return false;
        const auto check=classify_cinematic_commands(published_shared_list(*value.production.commands),value.after,movie,value.canvas.width,value.canvas.height);
        if(check.status!=CinematicCommandStatus::full_canvas_movie_primitive)return false;
        *output=std::move(value);return true;
    }
    static bool same_context(const NativeFinalUi& a,const NativeFinalUi& b) noexcept {
        return a.valid&&b.valid&&a.connection_state==b.connection_state&&a.key_catchers==b.key_catchers&&a.menu_count==b.menu_count&&a.page==b.page;
    }
    static bool same_frame(const NativeFinalUi& a,const NativeFinalUi& b) noexcept {
        return same_context(a,b)&&a.backend==b.backend&&a.commands==b.commands&&a.native_frame==b.native_frame;
    }
private:
    CinematicFrameStamp open_,completed_;
    std::uint64_t last_serial_{};
};
}
