// SPDX-License-Identifier: GPL-3.0-only
#include "input/helicopter_head_ray.hpp"
#include "t5/stereo_view_math.hpp"
#include "xr_math.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace xr=wawvr::xr;
using bovr::input::CommandRigSample;
using bovr::input::helicopter_head_ray;
unsigned checks{},failures{};
void check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
bool close(float a,float b,float tolerance=.0002F){return std::fabs(a-b)<tolerance;}
bool close(xr::Vec3f a,xr::Vec3f b){return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z);}
bool close(const xr::Basis3f& a,const xr::Basis3f& b){return close(a.forward,b.forward)&&close(a.left,b.left)&&close(a.up,b.up);}
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
    xr::EnginePose stock{{150,250,80},{}};
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
            auto& e=f.eyes[i];e.pose=f.head_center;
            const auto offset=xr::Rotate(e.pose.orientation,{i?.032F:-.032F,0,0});
            e.pose.position.x+=offset.x;e.pose.position.y+=offset.y;e.pose.position.z+=offset.z;
            e.fov={-.72F,.78F,.76F,-.70F};
        }
    }
    bool run(xr::EnginePose* out) const {return helicopter_head_ray(rig,stock,now,out);}
    bool same_as_renderer() const {
        bovr::t5::SceneView view{};view.width=1920;view.height=1080;
        view.tan_half_fov_x=view.tan_half_fov_y=1;view.origin=stock.position;view.axis=stock.axis;view.near_clip=.1F;
        bovr::t5::StereoViews stereo{};xr::EnginePose head{};
        if(!run(&head)||!bovr::t5::build_stereo_views(view,rig.frame,rig.tracking_anchor,&stereo))return false;
        const auto& a=stereo.eyes[0];const auto& b=stereo.eyes[1];
        const xr::Vec3f middle{(a.origin.x+b.origin.x)*.5F,(a.origin.y+b.origin.y)*.5F,(a.origin.z+b.origin.z)*.5F};
        return close(head.position,middle)&&close(head.axis,a.axis)&&close(head.axis,b.axis);
    }
};
}

int main(){
    Fixture f;xr::EnginePose head{};
    check(f.run(&head)&&close(head.position,f.stock.position)&&close(head.axis.forward,{1,0,0}),
        "neutral HMD ray starts at common camera and points along native forward");
    check(f.same_as_renderer(),"head ray equals actual two-eye camera mean and common axis");
    f.rig.frame.head_center.position={.2F,1.75F,-.4F};f.eyes();
    constexpr float units=xr::kIwUnitsPerMeter;
    check(f.run(&head)&&close(head.position,{150+.4F*units,250-.2F*units,80+.15F*units}),
        "head translation uses XR -Z forward, -X left and +Y up in IW units");
    check(f.same_as_renderer(),"translated head matches actual stereo origins");
    f=Fixture{};f.rig.frame.head_center.orientation=rotation({0,1,0},90);f.eyes();
    check(f.run(&head)&&close(head.axis.forward,{0,1,0}),"positive XR yaw turns the ray toward native left");
    f.rig.frame.head_center.orientation=rotation({1,0,0},90);f.eyes();
    check(f.run(&head)&&close(head.axis.forward,{0,0,1})&&f.same_as_renderer(),
        "vertical HMD aim remains a valid ray without an Euler yaw singularity");
    f=Fixture{};f.stock.axis=native_axis(90,35,24);
    check(f.run(&head)&&close(head.axis.forward,{0,1,0})&&close(head.axis.up,{0,0,1}),
        "native body yaw remains while stock flight pitch and roll are leveled like stereo");
    check(f.same_as_renderer(),"animated stock camera agrees with actual stereo comfort transform");
    f.stock.axis={{0,0,-1},{0,1,0},{1,0,0}};
    check(f.run(&head)&&close(head.axis.forward,{1,0,0})&&f.same_as_renderer(),
        "vertical stock camera uses the renderer's left-row yaw fallback");
    f=Fixture{};f.rig.tracking_anchor={rotation({0,1,0},47),{3,1.4F,-2}};
    f.rig.frame.head_center=f.rig.tracking_anchor;f.stock.axis=native_axis(-65,10,15);f.eyes();
    check(f.run(&head)&&close(head.position,f.stock.position)&&close(head.axis,native_axis(-65,0,0)),
        "recentered anchor position and orientation are removed exactly once");
    check(f.same_as_renderer(),"nonidentity anchor matches actual stereo");
    for(int body=-150;body<=150;body+=50)for(int yaw=-90;yaw<=90;yaw+=45)for(int pitch=-60;pitch<=60;pitch+=30){
        f=Fixture{};f.stock.axis=native_axis(float(body),27,-18);
        f.rig.tracking_anchor={rotation({0,1,0},31),{2,1.7F,-1}};
        const auto local=xr::Multiply(rotation({0,1,0},float(yaw)),rotation({1,0,0},float(pitch)));
        f.rig.frame.head_center.orientation=xr::Multiply(f.rig.tracking_anchor.orientation,local);
        const auto offset=xr::Rotate(f.rig.tracking_anchor.orientation,{.17F,.08F,-.23F});
        const auto& anchor=f.rig.tracking_anchor.position;
        f.rig.frame.head_center.position={anchor.x+offset.x,anchor.y+offset.y,anchor.z+offset.z};f.eyes();
        check(f.same_as_renderer(),"combined body yaw, head yaw/pitch/translation and recenter share actual stereo geometry");
    }
    f=Fixture{};f.rig.frame.head_center.orientation=rotation({0,0,1},35);f.eyes();
    check(f.run(&head)&&close(head.axis.forward,{1,0,0})&&f.same_as_renderer(),
        "head roll rotates the camera basis while forward stays on its ray");
    f.rig.frame.eyes[0].pose.orientation=rotation({0,1,0},5);
    f.rig.frame.eyes[1].pose.orientation=rotation({0,1,0},-6);
    check(f.same_as_renderer(),"per-eye orientation noise does not replace the common HMD orientation");
    f=Fixture{};f.run(&head);const auto expected=head;
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    f.rig.frame.actions.hands[1].aim.pose={{nan,0,0,0},{nan,nan,nan}};
    check(f.run(&head)&&close(head.position,expected.position)&&close(head.axis,expected.axis),
        "head geometry never borrows controller aim; router separately owns controller eligibility");
    const auto before_rig=f.rig;const auto before_stock=f.stock;
    check(f.run(&head)&&!std::memcmp(&before_rig,&f.rig,sizeof(f.rig))&&!std::memcmp(&before_stock,&f.stock,sizeof(f.stock)),
        "ray computation leaves original prediction and native flight/look basis unchanged");
    f=Fixture{};f.rig.published_ms=900;check(f.run(&head),"exact 100ms age is accepted without republishing");
    f.now=1001;check(!f.run(&head),"101ms age expires the same immutable prediction");
    for(unsigned which=0;which<27;++which){
        f=Fixture{};
        switch(which){
        case 0:f.rig.xr_generation=0;break;case 1:f.rig.anchor_generation=8;break;
        case 2:f.rig.frame.frame_id=0;break;case 3:f.rig.frame.actions.sequence=0;break;
        case 4:f.rig.frame.actions.focused=false;break;case 5:f.rig.published_ms=0;break;
        case 6:f.rig.published_ms=1001;break;case 7:f.rig.published_ms=899;break;
        case 8:f.rig.frame.views_valid=false;break;case 9:f.rig.frame.should_render=false;break;
        case 10:f.rig.frame.predicted_display_time=0;break;case 11:f.rig.frame.predicted_display_period=-1;break;
        case 12:f.rig.frame.head_center.orientation={0,0,0,0};break;
        case 13:f.rig.frame.head_center.orientation={nan,0,0,1};break;
        case 14:f.rig.frame.head_center.position.x=inf;break;
        case 15:f.rig.tracking_anchor.position.z=nan;break;case 16:f.rig.tracking_anchor.orientation={0,0,0,2};break;
        case 17:f.stock.position.y=inf;break;case 18:f.stock.axis.forward={2,0,0};break;
        case 19:f.stock.axis.left={0,-1,0};break;case 20:f.stock.axis.up=f.stock.axis.left;break;
        case 21:f.stock.axis.forward.x=nan;break;case 22:f.rig.frame.eyes[0].pose.position.x=nan;break;
        case 23:f.rig.frame.eyes[1].pose.orientation={0,0,0,0};break;
        case 24:f.rig.frame.head_center.orientation={0,0,0,1.02F};break;
        case 25:f.rig.tracking_anchor.orientation={0,0,0,.98F};break;
        case 26:f.rig.frame.head_center.position.x=std::numeric_limits<float>::max();break;
        }
        head.position={91,92,93};head.axis=native_axis(20,30,40);const xr::EnginePose cleared{};
        check(!f.run(&head)&&close(head.position,cleared.position)&&close(head.axis,cleared.axis),
            "invalid identity, freshness, prediction, pose or composition clears the output");
    }
    f=Fixture{};check(!f.run(nullptr),"null destination rejected");
    std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
