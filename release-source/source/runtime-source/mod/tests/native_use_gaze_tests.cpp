// SPDX-License-Identifier: GPL-3.0-only
#include "input/native_use_gaze.hpp"
#include "t5/stereo_view_math.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace xr=wawvr::xr;
using bovr::input::CommandRigSample;
unsigned checks{},failures{};
void check(bool ok,const char* description){++checks;if(!ok){++failures;std::printf("FAIL %s\n",description);}}
bool close(float a,float b){return std::fabs(a-b)<.0002F;}
bool close(xr::Vec3f a,xr::Vec3f b){return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z);}
float dot(xr::Vec3f a,xr::Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
xr::Quaternionf rotation(xr::Vec3f axis,float degrees){
    const float r=degrees*.00872664625997F,s=std::sin(r);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(r)};
}
xr::Basis3f native_axis(float yaw,float pitch,float roll){
    const auto q=xr::Multiply(rotation({0,0,1},yaw),
        xr::Multiply(rotation({0,1,0},pitch),rotation({1,0,0},roll)));
    return {xr::Rotate(q,{1,0,0}),xr::Rotate(q,{0,1,0}),xr::Rotate(q,{0,0,1})};
}
struct Fixture {
    CommandRigSample rig{};
    xr::EnginePose stock{{14594.2363F,10752.5166F,-268.875F},native_axis(8.2615F,58.6245F,0)};
    std::uint64_t now=1000;
    Fixture(){
        rig.xr_generation=rig.anchor_generation=7;rig.published_ms=990;
        auto& f=rig.frame;f.frame_id=23;f.predicted_display_time=100000;f.predicted_display_period=11111111;
        f.should_render=f.views_valid=true;f.actions.focused=true;f.actions.sequence=29;
        rig.tracking_anchor.position={0,1.6F,0};f.head_center=rig.tracking_anchor;eyes();
    }
    void eyes(){
        auto& f=rig.frame;
        for(unsigned i=0;i<2;++i){
            auto& eye=f.eyes[i];eye.pose=f.head_center;
            const auto offset=xr::Rotate(eye.pose.orientation,{i?.032F:-.032F,0,0});
            eye.pose.position.x+=offset.x;eye.pose.position.y+=offset.y;eye.pose.position.z+=offset.z;
            eye.fov={-.72F,.78F,.76F,-.70F};
        }
    }
    bool run(xr::Vec3f* out) const {return bovr::input::native_use_gaze(rig,stock,now,out);}
    bool matches_renderer() const {
        bovr::t5::SceneView view{};view.width=1920;view.height=1080;
        view.tan_half_fov_x=view.tan_half_fov_y=1;view.origin=stock.position;view.axis=stock.axis;view.near_clip=.1F;
        bovr::t5::StereoViews stereo{};xr::Vec3f selected{};
        return run(&selected)&&bovr::t5::build_stereo_views(view,rig.frame,rig.tracking_anchor,&stereo)&&
            close(selected,stereo.eyes[0].axis.forward)&&close(selected,stereo.eyes[1].axis.forward);
    }
};
}

int main(){
    Fixture f;xr::Vec3f gaze{};
    const auto level=native_axis(8.2615F,0,0).forward;
    check(f.run(&gaze)&&close(gaze,level)&&f.matches_renderer(),
        "Rebirth stale 58.6245 degree native pitch does not tilt the HMD use query");
    check(dot(f.stock.axis.forward,level)<.76F&&dot(gaze,level)>.999F,
        "level visible door passes the unchanged native look cone while the stale PC pitch fails");
    f.stock.axis=native_axis(0,58.6245F,0);
    f.rig.frame.head_center.orientation=rotation({0,1,0},90);f.eyes();
    check(f.run(&gaze)&&close(gaze,{0,1,0})&&f.matches_renderer(),"head yaw selects the visible side door");
    check(dot(gaze,{1,0,0})<.76F,"looking away does not bypass the native door look cone");
    f.rig.frame.head_center.orientation=rotation({1,0,0},30);f.eyes();
    check(f.run(&gaze)&&close(gaze,{.8660254F,0,.5F})&&f.matches_renderer(),
        "physical head pitch remains active after removing stale native pitch");
    f.rig.frame.head_center.orientation=rotation({1,0,0},90);f.eyes();
    check(f.run(&gaze)&&close(gaze,{0,0,1})&&f.matches_renderer(),"vertical HMD gaze needs no Euler yaw approximation");
    for(int body=-150;body<=150;body+=75)for(int yaw=-90;yaw<=90;yaw+=45)for(int pitch=-60;pitch<=60;pitch+=30){
        f=Fixture{};f.stock.axis=native_axis(float(body),58.6245F,-18);
        f.rig.tracking_anchor={rotation({0,1,0},31),{2,1.7F,-1}};
        const auto local=xr::Multiply(rotation({0,1,0},float(yaw)),rotation({1,0,0},float(pitch)));
        f.rig.frame.head_center.orientation=xr::Multiply(f.rig.tracking_anchor.orientation,local);
        f.rig.frame.head_center.position={2.3F,1.8F,-1.4F};f.eyes();
        check(f.matches_renderer(),"combined recenter, body yaw, head yaw and pitch select the actual stereo axis");
    }
    f=Fixture{};f.stock.axis={{0,0,-1},{0,1,0},{1,0,0}};
    check(f.run(&gaze)&&close(gaze,{1,0,0})&&f.matches_renderer(),"vertical native camera shares renderer yaw fallback");
    f=Fixture{};f.run(&gaze);const auto original=gaze;
    f.rig.frame.head_center.position={.4F,1.9F,-.3F};f.eyes();
    check(f.run(&gaze)&&close(gaze,original),"head translation does not change use direction or manufacture a new native origin");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    f.rig.frame.actions.hands[1].aim.pose={{nan,0,0,0},{nan,nan,nan}};
    check(f.run(&gaze)&&close(gaze,original),"native use gaze never borrows firearm or controller aim");
    const auto before_rig=f.rig;const auto before_stock=f.stock;
    check(f.run(&gaze)&&!std::memcmp(&before_rig,&f.rig,sizeof(f.rig))&&!std::memcmp(&before_stock,&f.stock,sizeof(f.stock)),
        "selection preserves the copied prediction and native firearm or camera inputs");
    f=Fixture{};f.rig.published_ms=900;
    check(f.run(&gaze),"original prediction accepted at 100 milliseconds");
    f.now=1001;check(!f.run(&gaze)&&close(gaze,{}),"same prediction expires at 101 milliseconds without republishing");
    for(unsigned which=0;which<22;++which){
        f=Fixture{};
        switch(which){
        case 0:f.rig.xr_generation=0;break;case 1:f.rig.anchor_generation=8;break;
        case 2:f.rig.frame.frame_id=0;break;case 3:f.rig.frame.actions.sequence=0;break;
        case 4:f.rig.frame.actions.focused=false;break;case 5:f.rig.published_ms=0;break;
        case 6:f.rig.published_ms=1001;break;case 7:f.rig.published_ms=899;break;
        case 8:f.rig.frame.views_valid=false;break;case 9:f.rig.frame.should_render=false;break;
        case 10:f.rig.frame.predicted_display_time=0;break;case 11:f.rig.frame.predicted_display_period=0;break;
        case 12:f.rig.frame.head_center.orientation={0,0,0,0};break;
        case 13:f.rig.frame.head_center.orientation={nan,0,0,1};break;
        case 14:f.rig.frame.head_center.position.x=nan;break;
        case 15:f.rig.tracking_anchor.position.z=nan;break;
        case 16:f.rig.tracking_anchor.orientation={0,0,0,2};break;
        case 17:f.stock.axis.forward={2,0,0};break;case 18:f.stock.axis.left={0,-1,0};break;
        case 19:f.rig.frame.eyes[0].pose.position.x=nan;break;
        case 20:f.rig.frame.eyes[1].pose.orientation={0,0,0,0};break;
        case 21:f.rig.frame.actions.menu.active=f.rig.frame.actions.menu.current=true;break;
        }
        gaze={91,92,93};check(!f.run(&gaze)&&close(gaze,{}),"invalid or interrupted source fails without retaining old gaze");
    }
    f=Fixture{};check(!f.run(nullptr),"null output rejected");
    std::printf("native_use_gaze: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
