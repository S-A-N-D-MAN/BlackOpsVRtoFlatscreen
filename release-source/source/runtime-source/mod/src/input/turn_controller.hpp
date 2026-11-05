// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace bovr::input {
enum class TurnMode { snap, smooth };

inline float apply_turn_delta(float native_yaw,float delta,TurnMode mode) noexcept {
    if(!std::isfinite(native_yaw)||!std::isfinite(delta)) return native_yaw;
    // Black Ops deliberately leaves client view yaw unbounded. Normalizing a
    // smooth step can therefore turn a one-degree request into a numerical
    // jump of one or more full revolutions, which invalidates visual history
    // and presents as a snap followed by a black blink. Preserve that native
    // continuum for smooth turning; snap turning retains its established
    // bounded write.
    return mode==TurnMode::smooth?native_yaw+delta:std::remainder(native_yaw+delta,360.f);
}

class TurnController final {
public:
    void reset() noexcept { armed_=false;seen_=false; }
    // Missing permission is not a physically centered stick. Preserve its
    // one-snap latch, but never accumulate smooth-turn time across the gap.
    void suspend() noexcept { seen_=false; }
    // Positive right-stick X turns right: engine yaw increases to the left.
    float update(float axis,std::uint64_t now,TurnMode mode=TurnMode::snap,
                 float snap_degrees=30.f,float smooth_degrees_per_second=120.f) noexcept {
        if (!std::isfinite(axis)) {reset();return 0;}
        const float dt=seen_ && now>=last_?std::min(float(now-last_)*0.001f,0.05f):0;
        if (seen_ && now<last_) armed_=false;
        seen_=true;last_=now;
        axis=std::clamp(axis,-1.f,1.f);
        if (std::fabs(axis)<0.25f) armed_=true;
        if (!armed_) return 0;
        if (mode==TurnMode::smooth) return -axis*std::clamp(smooth_degrees_per_second,15.f,360.f)*dt;
        if (std::fabs(axis)<0.65f) return 0;
        armed_=false;
        return (axis>0?-1.f:1.f)*std::clamp(snap_degrees,5.f,90.f);
    }
private:
    bool armed_{},seen_{};
    std::uint64_t last_{};
};
}
