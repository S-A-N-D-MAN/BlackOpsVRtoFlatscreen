// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace bovr::t5 {
enum class MovieLogStage : unsigned {begin,complete,staged,present};
// Owner-thread diagnostics only. No timestamps, native pointers, frame gating
// or routing authority. Sixteen immutable movie keys prevent repeated restarts or
// arbitrary decoder names from replenishing the process-wide 512-record cap.
class CinematicMovieLogBudget {
public:
    bool admit(const std::array<char,256>& movie,MovieLogStage stage,unsigned reason=0) noexcept {
        const auto index=static_cast<unsigned>(stage);
        if(index>=4||reason>=16||!movie[0]||emitted_>=512)return false;
        unsigned length=0;while(length<movie.size()&&movie[length])++length;
        if(length==movie.size())return false;
        std::array<char,256> key{};
        for(unsigned i=0;i<length;++i)key[i]=movie[i];
        unsigned slot=0;while(slot<keys_.size()&&keys_[slot][0]&&keys_[slot]!=key)++slot;
        if(slot==keys_.size())return false;
        if(!keys_[slot][0])keys_[slot]=key;
        auto& count=counts_[slot][index][reason];
        if(count!=std::numeric_limits<std::uint64_t>::max())++count;
        if(count<=6||count%120==0){++emitted_;return true;}return false;
    }
    unsigned emitted() const noexcept {return emitted_;}
private:
    std::array<std::array<char,256>,16> keys_{};
    std::array<std::array<std::array<std::uint64_t,16>,4>,16> counts_{};
    unsigned emitted_{};
};
}
