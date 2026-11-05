// SPDX-License-Identifier: GPL-3.0-only
#include "input/movement_orientation.hpp"
#include "input/swimming_movement.hpp"
#include <cmath>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <xmmintrin.h>

namespace xr = wawvr::xr;
namespace input = bovr::input;

namespace {
int failures{};

void check(bool condition, const char* label) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

bool close(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

xr::Quaternionf yaw(float degrees) {
    constexpr float pi = 3.14159265358979323846f;
    const float half = degrees * pi / 360.0f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

xr::TrackedPose tracked(xr::Quaternionf orientation) {
    xr::TrackedPose result{};
    result.active = result.orientation_valid = result.orientation_tracked = true;
    result.pose.orientation = orientation;
    return result;
}

xr::FrameState frame() {
    xr::FrameState result{};
    result.views_valid = true;
    result.head_center.orientation = {};
    for (auto& hand : result.actions.hands) {
        hand.grip = tracked({});
        hand.aim = tracked({});
    }
    return result;
}

void swimming_vectors() {
    constexpr float radians=.0174532925199433f;
    for(float stock_yaw:{0.f,37.f,-140.f})for(float native_yaw:{0.f,91.f,-170.f})
    for(float heading:{0.f,45.f,90.f,-90.f,180.f})for(float up:{-89.f,-45.f,0.f,45.f,89.f})
    for(auto stick:{std::array<float,2>{1,0},{-1,0},{0,1},{0,-1},{.6f,.8f},{-.8f,.6f},{0,0}}) {
        auto f=frame();f.should_render=true;
        xr::Posef anchor{};anchor.position={5,3,-2};anchor.orientation=yaw(23.f);
        const float half=up*radians*.5f;
        f.head_center.orientation=xr::Multiply(anchor.orientation,
            xr::Multiply(yaw(heading),xr::Quaternionf{std::sin(half),0,0,std::cos(half)}));
        f.head_center.position={7,8,9};
        const float c=std::cos(stock_yaw*radians),s=std::sin(stock_yaw*radians);
        const xr::Basis3f body{{c,s,0},{-s,c,0},{0,0,1}};
        input::SwimmingMovement move{};
        check(input::orient_swimming(f,anchor,body,native_yaw,0,85,85,stick[0],stick[1],&move),
              "swimming accepts tracked HMD with independent native/body/anchor yaw");
        const float p=move.pitch_degrees*radians,y=native_yaw*radians;
        const xr::Vec3f actual{move.forward*std::cos(p)*std::cos(y)+move.right*std::sin(y),
            move.forward*std::cos(p)*std::sin(y)-move.right*std::cos(y),-move.forward*std::sin(p)};
        const auto& desired=move.desired_world;
        const float desired_len=std::sqrt(desired.x*desired.x+desired.y*desired.y+desired.z*desired.z);
        const float actual_len=std::sqrt(actual.x*actual.x+actual.y*actual.y+actual.z*actual.z);
        if(desired_len>.001f) {
            const float agreement=(actual.x*desired.x+actual.y*desired.y+actual.z*desired.z)/(desired_len*actual_len);
            check(agreement>(move.pitch_limited?std::cos(5.11f*radians):.99999f),
                  "native swim formula reconstructs intended 3D direction, bounded only at native pitch limit");
            const float cmd_len=std::hypot(move.forward,move.right);
            const float native_speed=actual_len*std::max(std::fabs(move.forward),std::fabs(move.right))/cmd_len;
            check(close(native_speed,std::max(std::fabs(stick[0]),std::fabs(stick[1])),.00001f),
                  "native PM_CmdScale preserves original analog speed across yaw, pitch, diagonal and reverse input");
        } else check(move.forward==0&&move.right==0,"neutral swim axes remain neutral");
        check(move.pitch_degrees>=-84.901f&&move.pitch_degrees<=84.901f,
              "swimming stays inside native clamp without engine delta mutation");
    }
    auto f=frame();f.should_render=true;xr::Posef anchor{};input::SwimmingMovement move{};
    for(unsigned fault=0;fault<8;++fault) {
        auto bad=f;auto bad_anchor=anchor;float roll=0,up=85,forward=1;
        if(fault==0)bad.views_valid=false;
        if(fault==1)bad.should_render=false;
        if(fault==2)bad.head_center.orientation.x=std::numeric_limits<float>::quiet_NaN();
        if(fault==3)bad_anchor.orientation={0,0,0,0};
        if(fault==4)roll=.03f;
        if(fault==5)up=0;
        if(fault==6)forward=std::numeric_limits<float>::infinity();
        if(fault==7)up=.15f;
        check(!input::orient_swimming(bad,bad_anchor,{},0,roll,up,85,forward,0,&move)&&
              move.forward==0&&move.right==0,"invalid swim geometry fails neutral");
    }
}
float retail_pitch_decode(std::int32_t packed,float delta) {
    // Float32 SSE sequence in retail PM_UpdateViewAngles_Clamp, including its
    // divide/subtract/multiply normalization rather than double fmod/remainder.
    const auto angle=_mm_add_ss(_mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(),packed),
        _mm_set_ss(.0054931640625f)),_mm_set_ss(delta));
    const auto turns=_mm_mul_ss(angle,_mm_set_ss(.0027777778450399637f));
    const float whole=std::floor(_mm_cvtss_f32(_mm_add_ss(turns,_mm_set_ss(.5f))));
    return _mm_cvtss_f32(_mm_mul_ss(_mm_sub_ss(turns,_mm_set_ss(whole)),_mm_set_ss(360.f)));
}
void swimming_extreme_delta_bounds() {
    check(retail_pitch_decode(15474,360000.f)>85.f,
          "regression fixture reproduces old margin crossing native default pitch limit at large delta");
    for(float limit:{.2f,1.f,84.975f,85.f,90.f})for(float direction:{-1.f,1.f}) {
        auto f=frame();f.should_render=true;
        constexpr float half=.7766715171374766f; // 89-degree HMD pitch.
        f.head_center.orientation={direction*std::sin(half),0,0,std::cos(half)};
        input::SwimmingMovement move{};
        check(input::orient_swimming(f,{}, {},0,0,limit,limit,1,0,&move),
              "legal pitch limits retain a non-inverted safety interval");
        for(float delta:{-360000.f,-359999.96875f,-180000.f,-47.966f,0.f,47.966f,180000.f,359999.96875f,360000.f}) {
            const auto packed=static_cast<std::int32_t>(std::lround(
                std::remainder(move.pitch_degrees-delta,360.f)*(65536.f/360.f)))&0xffff;
            const float decoded=retail_pitch_decode(packed,delta);
            check(decoded>=-limit&&decoded<=limit,
                  "retail float32 decode stays inside native pitch limits through both admitted delta extremes");
        }
    }
}
}

int main() {
    swimming_vectors();
    swimming_extreme_delta_bounds();
    xr::Posef anchor{};
    float forward{}, right{};

    auto value = frame();
    check(input::orient_movement(value, anchor, false, input::MovementOrientation::hmd,
                                 1.0f, 0.0f, &forward, &right) &&
          close(forward, 1.0f) && close(right, 0.0f),
          "identity HMD keeps forward movement");

    value.head_center.orientation = yaw(90.0f);
    check(input::orient_movement(value, anchor, false, input::MovementOrientation::hmd,
                                 1.0f, 0.0f, &forward, &right) &&
          close(forward, 0.0f) && close(right, -1.0f),
          "HMD yaw rotates forward stick into head direction");

    value = frame();
    value.actions.hands[0].aim = tracked(yaw(-90.0f));
    check(input::orient_movement(value, anchor, false, input::MovementOrientation::controller,
                                 1.0f, 0.0f, &forward, &right) &&
          close(forward, 0.0f) && close(right, 1.0f),
          "right-handed controls use left support controller");

    value = frame();
    value.actions.hands[1].aim = tracked(yaw(90.0f));
    check(input::orient_movement(value, anchor, true, input::MovementOrientation::controller,
                                 1.0f, 0.0f, &forward, &right) &&
          close(forward, 0.0f) && close(right, -1.0f),
          "left-handed controls use right support controller");

    value = frame();
    value.actions.hands[0].aim.orientation_valid = false;
    value.actions.hands[0].grip = tracked(yaw(-90.0f));
    check(input::orient_movement(value, anchor, false, input::MovementOrientation::controller,
                                 0.6f, 0.8f, &forward, &right) &&
          close(std::sqrt(forward * forward + right * right), 1.0f),
          "controller grip fallback preserves stick magnitude");

    value = frame();
    value.views_valid = false;
    forward = right = 7.0f;
    check(!input::orient_movement(value, anchor, false, input::MovementOrientation::hmd,
                                  1.0f, 0.0f, &forward, &right) &&
          forward == 0.0f && right == 0.0f,
          "invalid HMD tracking fails neutral");

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
