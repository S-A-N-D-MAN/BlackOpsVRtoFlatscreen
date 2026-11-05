// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/cinematic_context.hpp"
#include "t5/final_ui_probe.hpp"
#include "t5/hud_command_publication.hpp"
#include <array>
#include <cstring>
#include <memory>

namespace bovr::t5 {
// Diagnostic production authority from the existing exact native begin/end
// observers. It has no menu event, menu epoch, input or render-submission role.
struct CinematicProductionMarker {
    NativeHudFrame native{};
    NativeFinalUi context{};
    NativeCinematicSnapshot movie{};
    DWORD thread{};
    std::uint64_t serial{}, generation{}, observed_ms{};
    bool valid{};
};
struct CinematicProductionStamp {
    CinematicProductionMarker begin{},end{};
    std::uint64_t epoch{},native_epoch{};
    std::shared_ptr<const HudCommandPublication> commands;
};
struct CinematicCensusOpen {
    CinematicProductionStamp production{};
    NativeFinalUi native{};
    NativeCinematicSnapshot movie{};
    std::uint64_t serial{},backend_serial{};
};
enum class CinematicProductionStatus {accepted,invalid_marker,wrong_route,missing_begin,identity_changed,
    context_changed,stale,invalid_extent,duplicate_begin,decoder_changed,world_views};
inline bool cinematic_production_current(const NativeCinematicSnapshot& m) noexcept {
    if(!m.valid||!m.image_base||!m.initialized||m.finished||!m.bink_handle||!m.cinematic_material||
       !m.current_name[0]||!std::memchr(m.current_name.data(),0,m.current_name.size())||
       m.active_image_frame<0||m.bound_texture_set<0||m.bound_texture_set>1||!m.cg_fullscreen_known||!m.cg_fullscreen)return false;
    const auto set=m.image_base+0x3a4cbcc+static_cast<unsigned>(m.bound_texture_set)*0x290;
    return m.renderer_images==std::array<std::uintptr_t,3>{set+0x1a0,set+0x1d4,set+0x208};
}
inline bool cinematic_production_same_movie(const NativeCinematicSnapshot& a,const NativeCinematicSnapshot& b) noexcept {
    // Native decoding may advance the image index while one frontend interval
    // is produced. This proves copied movie identity only, never frame pixels.
    return cinematic_production_current(a)&&cinematic_production_current(b)&&a.image_base==b.image_base&&
        a.bink_handle==b.bink_handle&&a.cinematic_material==b.cinematic_material&&a.current_name==b.current_name;
}
inline bool cinematic_production_same_context(const NativeFinalUi& a,const NativeFinalUi& b) noexcept {
    return a.valid&&b.valid&&a.connection_state==b.connection_state&&a.key_catchers==b.key_catchers&&
        a.menu_count==b.menu_count&&a.page==b.page;
}
class CinematicProductionEpoch final {
public:
    explicit CinematicProductionEpoch(std::uintptr_t base):base_(base){}
    CinematicProductionStatus begin(const CinematicProductionMarker& v,bool ordinary) noexcept {
        using S=CinematicProductionStatus;
        const auto slot=index(v.native.buffer);
        if(slot<0){clear();return S::invalid_marker;}
        entries_[slot]={};invalidate_open();
        if(!ordinary){clear();return S::wrong_route;}
        if(!valid(v)||v.native.used>v.native.capacity-4){clear();return S::invalid_marker;}
        if(v.generation!=generation_){clear();generation_=v.generation;++native_epoch_;}
        if(v.serial<=last_serial_){clear();return S::identity_changed;}
        last_serial_=v.serial;
        if(have_frame_&&v.native.frame<last_frame_){entries_={};++native_epoch_;}
        else if(have_frame_&&v.native.frame==last_frame_){clear();return S::duplicate_begin;}
        last_frame_=v.native.frame;have_frame_=true;
        entries_[slot].begin=v;entries_[slot].native_epoch=native_epoch_;
        return S::accepted;
    }
    CinematicProductionStatus complete(const CinematicProductionMarker& end,std::uintptr_t enclosing,
            std::shared_ptr<const HudCommandPublication> commands) noexcept {
        using S=CinematicProductionStatus;const auto slot=index(end.native.buffer);
        if(slot<0){clear();return S::invalid_marker;}
        const auto begin=entries_[slot].begin;const auto native_epoch=entries_[slot].native_epoch;
        entries_[slot]={};invalidate_open();
        if(enclosing!=base_+0x3a18af){clear();return S::wrong_route;}
        if(!valid(end)){clear();return S::invalid_marker;}
        if(end.serial<=last_serial_){clear();return S::identity_changed;}last_serial_=end.serial;
        if(!valid(begin))return S::missing_begin;
        const auto& a=begin.native;const auto& b=end.native;
        if(begin.thread!=end.thread||begin.generation!=end.generation||end.generation!=generation_||
           native_epoch!=native_epoch_||a.buffer!=b.buffer||a.frame!=b.frame||a.array!=b.array||a.bytes!=b.bytes||
           a.capacity!=b.capacity||a.configured_views!=b.configured_views)return S::identity_changed;
        if(b.view_count!=0)return S::world_views;
        if(!cinematic_production_same_context(begin.context,end.context))return S::context_changed;
        if(!cinematic_production_same_movie(begin.movie,end.movie))return S::decoder_changed;
        if(end.observed_ms<begin.observed_ms||end.observed_ms-begin.observed_ms>250)return S::stale;
        if(!commands||commands->native!=end.native||b.used<a.used+4||
           b.shared_commands<b.bytes+static_cast<unsigned>(a.used)||published_shared_list(*commands).empty())return S::invalid_extent;
        entries_[slot]={begin,end,++epoch_,native_epoch,std::move(commands)};return S::accepted;
    }
    bool match(const NativeFinalUi& backend,const NativeCinematicSnapshot& movie,std::uint64_t generation,
            std::uint64_t now,CinematicProductionStamp* out) const noexcept {
        if(out)*out={};const auto slot=index(backend.backend);if(!out||slot<0)return false;
        const auto& e=entries_[slot];const auto& end=e.end;
        if(!e.epoch||!e.commands||e.native_epoch!=native_epoch_||!backend.valid||generation!=generation_||
           end.generation!=generation||now<e.begin.observed_ms||now-e.begin.observed_ms>250||
           backend.backend!=end.native.buffer||backend.native_frame!=end.native.frame||backend.commands!=end.native.shared_commands||
           !cinematic_production_same_context(end.context,backend)||!cinematic_production_same_movie(end.movie,movie))return false;
        *out=e;return true;
    }
    void clear() noexcept {entries_={};generation_=0;have_frame_=false;}
private:
    int index(std::uintptr_t b) const noexcept {return b==base_+0x3740100?0:b==base_+0x38c6400?1:-1;}
    bool valid(const CinematicProductionMarker& v) const noexcept {
        const auto& n=v.native;const auto slot=index(n.buffer);
        return v.valid&&v.thread&&v.serial&&v.generation&&v.observed_ms&&v.context.valid&&
            v.movie.image_base==base_&&v.movie.thread_id==v.thread&&v.movie.observed_ms&&
            v.movie.observed_ms<=v.observed_ms&&v.observed_ms-v.movie.observed_ms<=250&&cinematic_production_current(v.movie)&&
            slot>=0&&n.renderer_active==1&&n.array==base_+0x3737090+static_cast<unsigned>(slot)*24&&n.bytes&&!(n.bytes&3)&&
            n.configured_views>=1&&n.configured_views<=4&&n.capacity==n.configured_views*0x18000&&n.view_count<=4&&
            n.used>=0&&!(n.used&3)&&n.critical>=0&&!(n.critical&3)&&n.critical<=n.used&&n.used<=n.capacity&&
            static_cast<std::uint64_t>(n.bytes)+static_cast<unsigned>(n.capacity)<=UINT32_MAX;
    }
    void invalidate_open() noexcept {for(auto& e:entries_)if(!e.epoch)e={};}
    std::uintptr_t base_{};
    std::uint64_t epoch_{},native_epoch_{},last_serial_{},generation_{};
    std::uint32_t last_frame_{};
    bool have_frame_{};
    std::array<CinematicProductionStamp,2> entries_{};
};
}
