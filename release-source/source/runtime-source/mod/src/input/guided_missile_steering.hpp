// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bovr::input {
struct GuidedMissileAxes { std::int8_t pitch{},yaw{}; };
// The retail missile treats values <= its stick deadzone as raw mouse degrees.
// Avoid that discontinuous fast-turn path: analog output is zero or 16..126.
inline std::int8_t guided_axis(float v,float deadzone) noexcept {
    if(!std::isfinite(v)||std::fabs(v)<=deadzone)return 0;
    const float amount=std::clamp((std::fabs(v)-deadzone)/(1.f-deadzone),0.f,1.f);
    const int magnitude=16+static_cast<int>(std::lround(amount*110.f));
    return static_cast<std::int8_t>(v<0?-magnitude:magnitude);
}
class GuidedMissileSteering final {
public:
    void reset() noexcept {ready_=false;stick_armed_=false;}
    GuidedMissileAxes update(bool current,std::uint64_t sequence,float pitch,float yaw,
                            bool stick_valid,float stick_x,float stick_y) noexcept {
        if(!current||!sequence||!std::isfinite(pitch)||!std::isfinite(yaw)) {reset();return {};}
        if(!ready_||sequence<sequence_) {
            pitch_=pitch;yaw_=yaw;ready_=true;stick_armed_=false;
        }
        sequence_=sequence;
        float p=std::remainder(pitch-pitch_,360.f)/25.f;
        float y=std::remainder(yaw-yaw_,360.f)/25.f;
        auto result=GuidedMissileAxes{guided_axis(p,.08f),guided_axis(y,.08f)};
        if(!stick_valid||!std::isfinite(stick_x)||!std::isfinite(stick_y))stick_armed_=false;
        else {
            if(std::fabs(stick_x)<=.2f&&std::fabs(stick_y)<=.2f)stick_armed_=true;
            if(stick_armed_&&(std::fabs(stick_x)>.2f||std::fabs(stick_y)>.2f))
                result={guided_axis(-stick_y,.2f),guided_axis(-stick_x,.2f)};
        }
        return result;
    }
private:
    bool ready_{},stick_armed_{};
    std::uint64_t sequence_{};
    float pitch_{},yaw_{};
};
}
