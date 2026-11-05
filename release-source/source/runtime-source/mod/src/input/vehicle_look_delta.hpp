// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>

namespace bovr::input {
struct VehicleLookDelta { float pitch{}, yaw{}; };

// Apply physical rotation once, like native mouse look. The caller supplies
// controller angles in a fixed tracking basis, not the changing camera basis.
// Native vehicle clamps and scripted heading changes remain authoritative.
class VehicleLook final {
public:
    void reset() noexcept { ready_=false; }
    bool update(std::uint64_t sequence,float pitch,float yaw,VehicleLookDelta* out) noexcept {
        if(!out)return false;
        *out={};
        if(!sequence||!std::isfinite(pitch)||!std::isfinite(yaw)){reset();return false;}
        if(ready_&&sequence==sequence_)return false;
        const bool advance=ready_&&sequence>sequence_;
        if(advance)*out={std::remainder(pitch-pitch_,360.f),std::remainder(yaw-yaw_,360.f)};
        pitch_=pitch;yaw_=yaw;sequence_=sequence;ready_=true;
        return advance;
    }
private:
    bool ready_{};
    std::uint64_t sequence_{};
    float pitch_{},yaw_{};
};
}
