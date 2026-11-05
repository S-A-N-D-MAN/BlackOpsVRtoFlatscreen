// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"

namespace bovr::input {
// Value-only companion to the action cache, protected by the adapter metadata
// lock. It never reads a clock or changes the renderer's tracking anchor.
class RigSnapshotCache final {
public:
    void publish(const wawvr::xr::FrameState& frame,std::uint64_t generation,
                 std::uint64_t original_publication_ms) noexcept {
        frame_=frame;generation_=generation;published_=original_publication_ms;
        valid_=generation&&original_publication_ms&&frame.frame_id&&frame.actions.sequence;
    }
    void clear() noexcept {frame_={};generation_=published_=0;valid_=false;}
    bool read(std::uint64_t now,const wawvr::xr::ActionSnapshot& same_actions,
              std::uint64_t same_generation,const wawvr::xr::Posef& anchor,
              std::uint64_t anchor_generation,CommandRigSample* output) const noexcept {
        if(output)*output={};
        if(!output||!valid_||now<published_||now-published_>250||
           same_generation!=generation_||anchor_generation!=generation_||
           same_actions.sequence!=frame_.actions.sequence) return false;
        *output={frame_,anchor,generation_,anchor_generation,published_};return true;
    }
private:
    wawvr::xr::FrameState frame_{};
    std::uint64_t generation_{},published_{};
    bool valid_{};
};
}
