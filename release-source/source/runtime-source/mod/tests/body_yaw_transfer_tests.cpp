// SPDX-License-Identifier: GPL-3.0-only
#include "input/body_yaw_transfer.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace xr = wawvr::xr;
namespace input = bovr::input;
namespace {
int failures{};
void check(bool value,const char* label){if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool close(float a,float b,float e=.001f){return std::fabs(a-b)<=e;}
xr::Quaternionf yaw(float degrees){constexpr float pi=3.14159265358979323846f;const float h=degrees*pi/360.f;return {0,std::sin(h),0,std::cos(h)};}
xr::FrameState frame(float degrees){xr::FrameState f{};f.frame_id=7;f.should_render=f.views_valid=true;f.head_center.orientation=yaw(degrees);f.head_center.position={3,4,5};return f;}
}
int main(){
    xr::Posef anchor{};anchor.position={1,2,3};input::BodyYawTransferPlan plan{};
    check(!input::plan_hmd_body_yaw_transfer(frame(.2f),anchor,.5f,&plan),"sub-threshold yaw stays in the HMD");
    check(!input::plan_hmd_body_yaw_transfer(frame(8.824f),anchor,input::kHmdBodyYawTransferDeadbandDegrees,&plan),
          "ordinary tracked head motion stays below the body-transfer deadband");
    check(input::plan_hmd_body_yaw_transfer(frame(20),anchor,input::kHmdBodyYawTransferDeadbandDegrees,&plan)&&
          close(plan.delta_degrees,20),"deliberate physical turn still transfers to body yaw");
    check(input::plan_hmd_body_yaw_transfer(frame(90),anchor,.5f,&plan)&&close(plan.delta_degrees,90)&&
          close(plan.desired_anchor.orientation.y,.7071068f)&&close(plan.desired_anchor.orientation.w,.7071068f),
          "physical left turn plans positive native body yaw and matching anchor yaw");
    check(close(plan.desired_anchor.position.x,1)&&close(plan.desired_anchor.position.y,2)&&close(plan.desired_anchor.position.z,3),
          "body yaw transfer preserves the room-scale anchor position");
    const auto rebased=plan.desired_anchor;
    check(!input::plan_hmd_body_yaw_transfer(frame(90),rebased,.5f,&plan),"matching anchor consumes physical yaw once");
    auto pitched=frame(45);pitched.head_center.orientation={.2705981f,.6532815f,-.2705981f,.6532815f};
    check(input::plan_hmd_body_yaw_transfer(pitched,anchor,.5f,&plan)&&close(plan.desired_anchor.orientation.x,0)&&close(plan.desired_anchor.orientation.z,0),
          "new anchor remains level when the HMD has pitch");
    auto invalid=frame(30);invalid.views_valid=false;
    check(!input::plan_hmd_body_yaw_transfer(invalid,anchor,.5f,&plan),"invalid stereo tracking fails closed");
    invalid=frame(30);invalid.head_center.orientation.x=std::numeric_limits<float>::quiet_NaN();
    check(!input::plan_hmd_body_yaw_transfer(invalid,anchor,.5f,&plan),"non-finite HMD orientation fails closed");
    invalid=frame(30);invalid.head_center.orientation={.7071068f,0,0,.7071068f};
    check(!input::plan_hmd_body_yaw_transfer(invalid,anchor,.5f,&plan),"near-vertical heading fails closed");
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
