// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/hud_command_publication.hpp"
#include "t5/final_ui_probe.hpp"
#include <array>
#include <memory>

namespace bovr::t5 {
struct MenuFrameMarker {
    NativeHudFrame native{};
    NativeFinalUi context{};
    DWORD thread{};
    // serial is minted by one metadata owner for BOTH begin and completion;
    // the independent native hook counters are never compared with each other.
    std::uint64_t serial{},generation{},revision{},events{},published_ms{};
    bool valid{};
};
struct FinalizedMenuStamp {
    MenuFrameMarker begin{},end{};
    std::uint64_t epoch{},native_epoch{};
    std::shared_ptr<const HudCommandPublication> commands;
};
enum class MenuEpochStatus {accepted,invalid_marker,wrong_route,missing_begin,identity_changed,
    context_changed,stale,invalid_extent,duplicate_begin};

// Serialized by the adapter metadata mutex. No engine pointers are followed.
// Backend admission must additionally verify the exact owned native allocation.
class MenuFrameEpoch final {
public:
    explicit MenuFrameEpoch(std::uintptr_t image_base):base_(image_base){}
    MenuEpochStatus begin(const MenuFrameMarker& value,bool ordinary_route) noexcept {
        const auto slot=index(value.native.buffer);
        if(slot<0){invalidate_open();return MenuEpochStatus::invalid_marker;}
        // Every observed route invalidates this storage slot before admission.
        entries_[slot]={};
        if(!ordinary_route){invalidate_open();return MenuEpochStatus::wrong_route;}
        if(!valid(value)||value.native.used>value.native.capacity-4){invalidate_open();return MenuEpochStatus::invalid_marker;}
        if(value.generation!=generation_) {
            clear();generation_=value.generation;have_frame_=false;++native_epoch_;
        }
        if(value.serial<=last_serial_){invalidate_open();return MenuEpochStatus::identity_changed;}
        last_serial_=value.serial;
        if(have_frame_&&value.native.frame<last_frame_) {
            entries_={};++native_epoch_; // Native reset/wrap retires every prior buffer.
        } else if(have_frame_&&value.native.frame==last_frame_) {
            invalidate_open();return MenuEpochStatus::duplicate_begin;
        }
        last_frame_=value.native.frame;have_frame_=true;
        // One ordinary frontend interval at a time, even when SMP slots differ.
        invalidate_open();entries_[slot].begin=value;entries_[slot].native_epoch=native_epoch_;
        return MenuEpochStatus::accepted;
    }
    MenuEpochStatus complete(const MenuFrameMarker& end,std::uintptr_t enclosing_return,
                             std::shared_ptr<const HudCommandPublication> commands) noexcept {
        const auto slot=index(end.native.buffer);
        if(slot<0){invalidate_open();return MenuEpochStatus::invalid_marker;}
        auto& entry=entries_[slot];
        const auto begin=entry.begin;const auto source_epoch=entry.native_epoch;
        entry={};invalidate_open(); // Every completion consumes the sole open interval.
        if(enclosing_return!=base_+0x3a18af)return MenuEpochStatus::wrong_route;
        if(!valid(end))return MenuEpochStatus::invalid_marker;
        if(end.serial<=last_serial_)return MenuEpochStatus::identity_changed;
        last_serial_=end.serial;
        if(!valid(begin))return MenuEpochStatus::missing_begin;
        const auto& a=begin.native;const auto& b=end.native;
        if(begin.thread!=end.thread||begin.generation!=end.generation||end.generation!=generation_||
           begin.revision!=end.revision||begin.events!=end.events||end.serial<=begin.serial||
           source_epoch!=native_epoch_||a.buffer!=b.buffer||a.frame!=b.frame||
           a.array!=b.array||a.bytes!=b.bytes||a.capacity!=b.capacity||a.configured_views!=b.configured_views)
            return MenuEpochStatus::identity_changed;
        if(!same_context(begin.context,end.context))return MenuEpochStatus::context_changed;
        if(end.published_ms<begin.published_ms||end.published_ms-begin.published_ms>250)return MenuEpochStatus::stale;
        if(!commands||commands->native!=end.native||b.used<a.used+4||
           b.shared_commands<b.bytes+static_cast<unsigned>(a.used)||published_shared_list(*commands).empty())
            return MenuEpochStatus::invalid_extent;
        entry={begin,end,++epoch_,source_epoch,std::move(commands)};
        return MenuEpochStatus::accepted;
    }
    bool match(const NativeFinalUi& backend,std::uint64_t generation,std::uint64_t revision,
               std::uint64_t now,FinalizedMenuStamp* output) const noexcept {
        if(output)*output={};const auto slot=index(backend.backend);if(slot<0||!output)return false;
        const auto& entry=entries_[slot];const auto& end=entry.end;
        if(!entry.epoch||!entry.commands||entry.native_epoch!=native_epoch_||!backend.valid||
           generation!=generation_||end.generation!=generation||end.revision!=revision||
           now<entry.begin.published_ms||now-entry.begin.published_ms>250||
           backend.backend!=end.native.buffer||backend.native_frame!=end.native.frame||
           backend.commands!=end.native.shared_commands||!same_context(end.context,backend))return false;
        *output=entry;return true;
    }
    // Serials and epochs never rewind; generation adoption resets chronology.
    void clear() noexcept {entries_={};have_frame_=false;generation_=0;}
private:
    void invalidate_open() noexcept {
        for(auto& entry:entries_)if(!entry.epoch)entry={};
    }
    static bool same_context(const NativeFinalUi& a,const NativeFinalUi& b) noexcept {
        return a.valid&&b.valid&&a.connection_state==b.connection_state&&a.key_catchers==b.key_catchers&&
            a.menu_count==b.menu_count&&a.page==b.page;
    }
    bool valid(const MenuFrameMarker& v) const noexcept {
        const auto& n=v.native;const auto slot=index(n.buffer);
        return v.valid&&v.thread&&v.serial&&v.generation&&v.revision&&v.events&&v.published_ms&&
            slot>=0&&n.renderer_active==1&&n.array==base_+0x3737090+static_cast<unsigned>(slot)*24&&n.bytes&&!(n.bytes&3)&&
            n.configured_views>=1&&n.configured_views<=4&&n.capacity==n.configured_views*0x18000&&
            n.used>=0&&!(n.used&3)&&n.critical>=0&&!(n.critical&3)&&n.critical<=n.used&&n.used<=n.capacity&&
            static_cast<std::uint64_t>(n.bytes)+static_cast<unsigned>(n.capacity)<=UINT32_MAX&&v.context.valid;
    }
    int index(std::uintptr_t buffer) const noexcept {
        return buffer==base_+0x3740100?0:buffer==base_+0x38c6400?1:-1;
    }
    std::uintptr_t base_{};
    std::uint64_t epoch_{},native_epoch_{},last_serial_{},generation_{};
    std::uint32_t last_frame_{};
    bool have_frame_{};
    std::array<FinalizedMenuStamp,2> entries_{};
};
}
