// SPDX-License-Identifier: GPL-3.0-only
#include "input/room_scale_camera.hpp"
#include "t5/room_scale_native.hpp"
#include "t5/stereo_view_math.hpp"
#include "t5/manual_reload_pose.hpp"
#include "t5/manual_grenade_visual_pose.hpp"
#include <cstdio>
#include <limits>
namespace rs=bovr::input::room_scale;namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{},calls{};
float trace_fraction=.375f;
float expected_radius=6.f;
void check(bool v,const char* label){++checks;if(!v){++failures;printf("FAIL %s\n",label);}}
bool closef(float a,float b,float tolerance=.001f){return std::fabs(a-b)<=tolerance;}
struct Simulation {
    rs::Follow follow{};rs::Sample sample{};rs::Plan plan{};
    Simulation(){sample.eligible=true;sample.epoch=1;sample.player=0x10000;sample.command_number=1;sample.now=1000;
        auto& r=sample.rig;r.xr_generation=r.anchor_generation=1;r.published_ms=1000;
        r.frame.frame_id=r.frame.actions.sequence=1;r.frame.actions.focused=true;
        r.frame.views_valid=r.frame.should_render=true;step(0);}
    void step(float x,float z=0,bool wall=false){
        sample.now+=10;++sample.command_number;++sample.rig.frame.frame_id;++sample.rig.frame.actions.sequence;
        sample.rig.published_ms=sample.now;sample.rig.frame.head_center.position.x=x;
        sample.rig.frame.head_center.position.z=z;
        plan=follow.step(sample);
        if(wall){
            // Infinite wall X=20. Native body has15-unit clearance; the camera
            // trace keeps its independently pinned6-unit radius before it.
            const float end=sample.origin.x+plan.residual.x;
            const float f=end>14.f?(14.f-sample.origin.x)/std::max(.001f,plan.residual.x):1.f;
            follow.clip(sample,plan,std::clamp(f,0.f,1.f),false);
            check(sample.origin.x+plan.residual.x<=14.001f,"camera capsule stays before wall");
        }
        sample.rig.tracking_anchor=plan.anchor;
        // Controlled fixture of the engine's analog command + collision result;
        // this tests feedback ownership, not the retail acceleration tuning.
        const float magnitude=std::max(std::fabs(plan.forward),std::fabs(plan.right));
        const float norm=std::hypot(plan.forward,plan.right);
        if(norm>.00001f){const auto move=rs::world({plan.forward/norm,-plan.right/norm,0},sample.body_yaw);
            sample.origin.x+=move.x*magnitude*190.f*.01f;sample.origin.y+=move.y*magnitude*190.f*.01f;}
        if(wall)sample.origin.x=std::min(sample.origin.x,5.f);
    }
};
void __cdecl trace_fixture(void* result,const float* start,const float* mins,const float* maxs,
                         const float* end,int ignore,int mask,void* context){
    ++calls;const auto* c=static_cast<const int*>(context);
    check(ignore==0&&mask==0x280ea31&&mins[0]==-expected_radius&&mins[1]==-expected_radius&&mins[2]==-expected_radius&&
          maxs[0]==expected_radius&&maxs[1]==expected_radius&&maxs[2]==expected_radius,"exact native camera mask and geometric clearance bounds");
    for(unsigned i=0;i<10;++i)check(c[i]==(i==4||i==5?1023:0),"native default col_context stack layout");
    check(start[0]==1&&end[0]==9,"native trace arguments preserve endpoints");
    memcpy(static_cast<unsigned char*>(result)+0x10,&trace_fraction,4);
    static_cast<unsigned char*>(result)[0x2b]=1;
}
}
int main(){
    {Simulation s;for(unsigned i=0;i<20;++i)s.step(0,-.002f*float(i));
        check(closef(s.sample.origin.x,0)&&closef(s.plan.residual.x,.038f*rs::units_per_meter),"small physical lean remains six-degree tracking without body motion");}
    for(float yaw:{0.f,90.f,-90.f,180.f}) {
        Simulation s;s.sample.body_yaw=yaw;s.step(0);
        for(unsigned i=1;i<=100;++i)s.step(0,-.005f*float(i));
        for(unsigned i=0;i<50;++i)s.step(0,-.5f);
        const auto position=rs::relative(s.sample.origin,yaw);
        check(position.x>12&&position.x<20&&std::fabs(position.y)<.001f,"half-meter head walk follows native collision-resolved body at every cardinal yaw");
        check(!s.plan.moving&&closef(s.plan.forward,0)&&closef(s.plan.right,0),"stationary head stops demand after bounded settle");
        const float body=position.x;for(unsigned i=0;i<100;++i)s.step(0,-.5f);
        check(closef(rs::relative(s.sample.origin,yaw).x,body),"no retained movement debt or drift after physical stop");
        check(closef(s.sample.rig.tracking_anchor.position.y,0),"body follow never consumes vertical stance");
    }
    {Simulation s;for(unsigned i=1;i<=180;++i)s.step(0,-.005f*float(i),true);
        for(unsigned i=0;i<100;++i)s.step(0,-.9f,true);
        check(s.sample.origin.x<=5.001f&&!s.plan.moving,"native wall rejects body advance and blocked demand expires");
        const auto x=s.sample.origin.x;for(unsigned i=0;i<80;++i)s.step(0,-.9f);
        check(closef(x,s.sample.origin.x),"removing wall does not release a stored teleport or walking request");}
    {Simulation s;s.step(0,-.15f);s.sample.eligible=false;s.step(0,-.2f);
        check(!s.plan.valid&&!s.plan.moving,"menu/script/swim/vehicle ineligibility stops roomscale");
        s.sample.eligible=true;s.step(0,-.2f);s.step(0,-.2f);
        check(!s.plan.moving,"reacquisition does not replay motion made while inactive");
        s.sample.epoch++;s.sample.origin={500,600,70};s.step(0,-.2f);
        check(s.plan.rebased&&!s.plan.moving&&s.sample.origin.z==70,"spawn/checkpoint origin jump rebases without vertical writes");
        s.sample.rig.tracking_anchor.position.x+=.3f;s.step(0,-.2f);
        check(s.plan.rebased&&!s.plan.moving,"external recenter retires old physical demand");
        s.sample.body_yaw+=45;s.step(0,-.2f);check(s.plan.rebased&&!s.plan.moving,"snap turn cannot rotate stored motion into a new direction");
        s.sample.user_movement=true;s.step(.04f,-.23f);check(!s.plan.moving,"stick or keyboard motion has native priority");
        s.sample.rig.frame.views_valid=false;s.step(.04f,-.23f);check(!s.plan.valid,"tracking loss clears state immediately");}
    {Simulation s;const auto before=s.sample.origin;s.sample.rig.frame.head_center.position.y=-.4f;s.step(0);
        check(!s.plan.moving&&closef(s.plan.anchor.position.y,0)&&closef(s.sample.origin.z,before.z),"crouch changes only tracked vertical camera");}
    for(float yaw:{0.f,61.3971f,90.f,-90.f,180.f}) {
        Simulation s;s.sample.body_yaw=yaw;s.step(3.54304f,.20232f);
        const auto original=s.sample.rig.tracking_anchor;const auto body=s.sample.origin;
        s.step(0);
        check(s.plan.rebased&&!s.plan.moving&&rs::length(s.plan.observed_body)==0,
              "large tracking-origin jump is a reset with no native movement request");
        check(rs::length(s.plan.residual)<.0001f&&s.plan.anchor.position.x==0&&s.plan.anchor.position.z==0,
              "tracking jump consumes all horizontal reset debt instead of retaining30cm lean");
        check(std::hypot(s.plan.anchor.position.x-original.position.x,s.plan.anchor.position.z-original.position.z)>.5f,
              "live3.5metre reset regression exercises the former half-metre commit rejection");
        check(!rs::horizontal_anchor_transfer_allowed(original,s.sample.rig.frame.head_center,s.plan.anchor,false)&&
               rs::horizontal_anchor_transfer_allowed(original,s.sample.rig.frame.head_center,s.plan.anchor,true),
              "only explicit head-derived horizontal discontinuity can exceed ordinary commit limit");
        for(unsigned i=0;i<60;++i)s.step(0);
        check(closef(s.sample.origin.x,body.x)&&closef(s.sample.origin.y,body.y)&&closef(s.sample.origin.z,body.z)&&!s.plan.moving,
              "accepted reset does not teleport native body or replay accumulated movement debt");
        const auto reset_eye=rs::subtract(s.sample.origin,rs::scaled(s.plan.residual,-1));
        s.step(.025f);
        const auto first_eye=rs::subtract(s.sample.origin,rs::scaled(s.plan.residual,-1));
        check(closef(rs::length(rs::subtract(first_eye,reset_eye)),.025f*rs::units_per_meter),
              "first deliberate2.5cm movement after reset is not clipped by old maximum lean");
        for(unsigned i=2;i<=14;++i){s.step(.025f*float(i));for(unsigned j=0;j<9;++j)s.step(.025f*float(i));}
        for(unsigned i=0;i<70;++i)s.step(.35f);
        const auto final_eye=rs::subtract(s.sample.origin,rs::scaled(s.plan.residual,-1));
        const auto expected=rs::world({0,-.35f*rs::units_per_meter,0},yaw);
        const auto actual=rs::subtract(final_eye,reset_eye);
        check(closef(actual.x,expected.x,.002f)&&closef(actual.y,expected.y,.002f),
              "35cm post-reset movement yields35cm world-camera displacement without old debt");
        const auto body_distance=rs::length(rs::subtract(s.sample.origin,body));
        check(body_distance>.025f*rs::units_per_meter&&body_distance<=.3501f*rs::units_per_meter,
              "native follow resumes but cannot consume more than fresh post-reset displacement");
        const auto settled=s.sample.origin;for(unsigned i=0;i<150;++i)s.step(.35f);
        check(!s.plan.moving&&closef(s.sample.origin.x,settled.x)&&closef(s.sample.origin.y,settled.y),
              "post-reset follow settles with no delayed catch-up after the head stops");
    }
    {Simulation s;s.step(.08f);const auto anchor=s.sample.rig.tracking_anchor;
        s.sample.body_yaw+=45;s.step(.08f);
        check(s.plan.rebased&&!s.plan.moving&&closef(rs::length(s.plan.residual),.08f*rs::units_per_meter)&&
              !memcmp(&anchor,&s.plan.anchor,sizeof(anchor)),"ordinary body-yaw rebaseline preserves continuous physical lean");
        s.sample.epoch++;s.step(.08f);
        check(s.plan.rebased&&!s.plan.moving&&closef(rs::length(s.plan.residual),.08f*rs::units_per_meter),
              "owner lifecycle rebaseline alone does not act like a tracking-position jump");}
    {Simulation s;s.sample.rig.tracking_anchor.orientation={0,.258819f,0,.9659258f};
        s.sample.rig.tracking_anchor.position.y=1.2f;s.sample.rig.frame.head_center.position.y=1.6f;
        const auto orientation=s.sample.rig.tracking_anchor.orientation;s.step(3.5f,.4f);
        check(rs::length(s.plan.residual)<.0001f&&s.plan.anchor.position.y==1.2f&&
              !memcmp(&orientation,&s.plan.anchor.orientation,sizeof(orientation)),
              "zero-debt horizontal reset preserves yaw anchor and crouch/height even in rotated tracking space");}
    for(float fraction:{0.f,.375f}) {
        Simulation s;s.step(3.54304f,.20232f);
        const auto original=s.sample.rig.tracking_anchor;const auto body=s.sample.origin;
        s.step(0);auto reset=s.sample;reset.rig.tracking_anchor=original;
        s.follow.clip(reset,s.plan,fraction,fraction==0);s.sample.rig.tracking_anchor=s.plan.anchor;
        check(rs::horizontal_anchor_transfer_allowed(original,reset.rig.frame.head_center,s.plan.anchor,true),
              "native partial or fully blocked camera still authorizes the head-derived reset anchor");
        check(s.plan.blocked&&!s.plan.moving&&rs::length(s.plan.residual)<=.3f*rs::units_per_meter,
              "clipped reset consumes rejected displacement without native movement");
        s.step(0);check(!s.plan.moving&&closef(s.sample.origin.x,body.x)&&closef(s.sample.origin.y,body.y),
                       "stationary pose after clipped reset has no blocked walking debt");
    }
    {xr::Posef original{},head{},desired{};original.position={3.5f,2,0};head.position={0,3,0};
        desired=original;desired.position.x=.3f;
        check(rs::horizontal_anchor_transfer_allowed(original,head,desired,true),"bounded head residual accepts large reset");
        auto bad=desired;bad.position.x=-.1f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"reset cannot overshoot current head");
        bad=desired;bad.position.x=4.2f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"reset cannot move anchor away from current head");
        bad=desired;bad.position.z=.01f;bad.position.x=.2f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"reset cannot shift anchor off observed head segment");
        bad=desired;bad.position.x=.4f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"large reset cannot retain more than30cm head residual");
        bad=desired;bad.position.y+=.1f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"reset cannot change vertical stance anchor");
        bad=desired;bad.orientation.y=.1f;
        check(!rs::horizontal_anchor_transfer_allowed(original,head,bad,true),"reset cannot smuggle an anchor turn");
        head.position.x=std::numeric_limits<float>::quiet_NaN();
        check(!rs::horizontal_anchor_transfer_allowed(original,head,desired,true),"invalid head cannot authorize reset");}
    {using namespace bovr::t5;room_scale_native::Sweep hit{};
        check(room_scale_native::query(trace_fixture,{1,2,3},{9,2,3},&hit)&&closef(hit.fraction,.375f)&&hit.solid,
              "native trace decodes fraction16 and startsolid43 without aliasing normal");
        const auto before=calls;const float nan=std::numeric_limits<float>::quiet_NaN();
        check(!room_scale_native::query(trace_fixture,{nan,0,0},{},&hit)&&
              !room_scale_native::query(nullptr,{},{},&hit)&&!room_scale_native::sweep(0,{},{},&hit)&&
              !room_scale_native::query(trace_fixture,{0,0,0},{0,0,129},&hit)&&calls==before,
              "bad pointers base or coordinates never invoke native query");}
    {bovr::t5::room_scale_native::Sweep hit{};
        for(float invalid:{-1.f,1.01f,std::numeric_limits<float>::quiet_NaN()}){
            trace_fraction=invalid;
            check(!bovr::t5::room_scale_native::query(trace_fixture,{1,2,3},{9,2,3},&hit),
                  "malformed native collision fraction refuses publication");}
        trace_fraction=.375f;}
    {bovr::t5::SceneView stock{};stock.origin={120,230,50};stock.width=1280;stock.height=720;
        stock.tan_half_fov_x=stock.tan_half_fov_y=1;stock.near_clip=1;
        stock.axis={{.75f,.4330127f,-.5f},{-.21650635f,.875f,.4330127f},{.625f,-.21650635f,.75f}};
        xr::Posef anchor{};anchor.position={2,3,4};anchor.orientation={0,.258819f,0,.9659258f};
        xr::FrameState frame{};frame.frame_id=1;frame.should_render=frame.views_valid=true;
        frame.head_center=anchor;frame.eyes[0].pose.position={-.032f,0,0};frame.eyes[1].pose.position={.032f,0,0};
        for(auto& e:frame.eyes)e.fov={-.7f,.7f,.7f,-.7f};
        const xr::Vec3f approved{123,227,48};
        check(rs::apply_approved_eye(stock.origin,stock.axis,anchor,approved,true,&frame),"tilted stock camera accepts collision inverse");
        bovr::t5::StereoViews result{};
        check(bovr::t5::build_stereo_views(stock,frame,anchor,&result),"collision-approved tilted frame builds actual stereo cameras");
        const xr::Vec3f center{(result.eyes[0].origin.x+result.eyes[1].origin.x)*.5f,
            (result.eyes[0].origin.y+result.eyes[1].origin.y)*.5f,(result.eyes[0].origin.z+result.eyes[1].origin.z)*.5f};
        check(closef(center.x,approved.x,.002f)&&closef(center.y,approved.y,.002f)&&closef(center.z,approved.z,.002f),
              "same gravity-level inverse and forward keep both eyes at approved world center under pitch and roll");}
    {xr::FrameState frame{};frame.frame_id=1;frame.should_render=frame.views_valid=true;
        frame.eyes[0].pose.position={-.032f,0,0};frame.eyes[1].pose.position={.032f,0,0};
        frame.eyes[0].fov={-.942477763f,.69813168f,.767595768f,-.947190166f};
        frame.eyes[1].fov={-.69813168f,.942477763f,.767595768f,-.947190166f};
        float radius{};check(rs::camera_clearance(frame,4.f,&radius)&&radius>10.f&&radius<11.f,
            "captured simulator IPD and FOV require more than old six-unit camera clearance at near four");
        check(rs::camera_clearance_current(radius,frame,4.f)&&!rs::camera_clearance_current(6.f,frame,4.f),
            "render admission requires proof covering both full eye near planes");
        check(!rs::camera_clearance_current(radius,frame,5.f),"newer larger native near plane cannot reuse older clearance");
        auto wider=frame;wider.eyes[0].pose.position.x=-.05f;
        check(!rs::camera_clearance_current(radius,wider,4.f),"newer larger stereo separation falls back to stock eye");
        wider=frame;wider.eyes[1].fov.angle_right=1.1f;
        check(!rs::camera_clearance_current(radius,wider,4.f),"newer wider eye field cannot bypass approved sweep");
        check(!rs::camera_clearance_current(0,frame,4.f),"failed native query has no render displacement authority");
        float low{},default_near{},negative_default{};
        check(rs::camera_clearance(frame,0.f,&default_near)&&default_near==radius&&
              rs::camera_clearance(frame,-1.f,&negative_default)&&negative_default==radius,
            "native zero and negative near values use effective four and cannot retain old six-unit clearance");
        check(rs::camera_clearance(frame,.1f,&low)&&low==6.f&&rs::effective_camera_near(.1f)==1.f&&
              rs::effective_camera_near(1.f)==1.f&&rs::effective_camera_near(5.f)==5.f,
            "native positive near clamp retains minimum one and minimum six-unit clearance");
        check(rs::camera_clearance_current(default_near,frame,4.f)&&!rs::camera_clearance_current(6.f,frame,0.f),
            "render receipt validates effective default near rather than raw zero");
        const float nan=std::numeric_limits<float>::quiet_NaN();
        for(float near_value:{std::numeric_limits<float>::infinity(),nan,1000.f}) {float unchanged=123.f;
            check(!rs::camera_clearance(frame,near_value,&unchanged)&&unchanged==123.f,"invalid or unbounded native near plane refuses output");}
        for(unsigned mutation=0;mutation<7;++mutation) {auto bad=frame;
            if(mutation==0)bad.views_valid=false;if(mutation==1)bad.should_render=false;if(mutation==2)bad.frame_id=0;
            if(mutation==3)bad.eyes[0].fov.angle_left=0;if(mutation==4)bad.eyes[1].fov.angle_up=1.57f;
            if(mutation==5)bad.eyes[0].pose.position.x=nan;if(mutation==6)bad.eyes[0].pose.position.x=100.f;
            float unchanged=123.f;check(!rs::camera_clearance(bad,4,&unchanged)&&unchanged==123.f,
                "invalid tracking or malformed eye geometry never grants clearance");}
        bovr::t5::room_scale_native::Sweep hit{};expected_radius=radius;
        check(bovr::t5::room_scale_native::query(trace_fixture,{1,2,3},{9,2,3},&hit,radius),
            "production native trace receives computed stereo bounds without changing endpoint or mask");
        expected_radius=6;const auto prior=calls;
        for(float invalid:{0.f,5.99f,64.01f,nan}) {
            hit={.75f,true};check(!bovr::t5::room_scale_native::query(trace_fixture,{1,2,3},{9,2,3},&hit,invalid)&&
                hit.fraction==.75f&&hit.solid&&calls==prior,"invalid trace clearance neither calls native nor changes output");}
        // Build the actual render cameras. A center trace stopping radius units
        // before each of two perpendicular wall planes must keep every near-
        // plane corner on the accessible side, at any head/body orientation.
        unsigned old_radius_leaks{};
        for(float raw_near:{0.f,4.f})for(float yaw:{0.f,61.3971f,90.f,180.f,-135.f})for(float pitch:{-.7f,0.f,.7f}) {
            auto tilted=frame;tilted.head_center.orientation={std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
            bovr::t5::SceneView stock{};stock.origin={0,0,60};stock.width=2560;stock.height=1440;
            stock.tan_half_fov_x=stock.tan_half_fov_y=1;stock.near_clip=raw_near;
            const float angle=yaw*.0174532925199433f;
            stock.axis={{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}};
            for(float clearance:{6.f,radius}) {
                auto camera=tilted;xr::Posef anchor{};const xr::Vec3f approved{20-clearance,20-clearance,60};
                check(rs::apply_approved_eye(stock.origin,stock.axis,anchor,approved,true,&camera),"wall corner center inverse is valid");
                bovr::t5::StereoViews views{};
                check(bovr::t5::build_stereo_views(stock,camera,anchor,&views),"wall regression exercises actual stereo camera builder");
                for(const auto& eye:views.eyes)for(float sx:{-1.f,1.f})for(float sy:{-1.f,1.f}) {
                    const float near_plane=rs::effective_camera_near(eye.near_clip);
                    const auto corner=xr::Vec3f{
                        eye.origin.x+near_plane*(eye.axis.forward.x+sx*eye.tan_half_fov_x*eye.axis.left.x+sy*eye.tan_half_fov_y*eye.axis.up.x),
                        eye.origin.y+near_plane*(eye.axis.forward.y+sx*eye.tan_half_fov_x*eye.axis.left.y+sy*eye.tan_half_fov_y*eye.axis.up.y),
                        eye.origin.z+near_plane*(eye.axis.forward.z+sx*eye.tan_half_fov_x*eye.axis.left.z+sy*eye.tan_half_fov_y*eye.axis.up.z)};
                    if(clearance==6.f){if(corner.x>20.f||corner.y>20.f)++old_radius_leaks;}
                    else check(corner.x<=20-rs::camera_skin+.001f&&corner.y<=20-rs::camera_skin+.001f,
                        "new clearance keeps both eye near-plane rectangles outside rotated wall corner");
                }
            }
        }
        check(old_radius_leaks>0,"regression reproduces old six-unit near-plane wall penetration");
        bovr::t5::room_scale_native::Facts facts{},changed{};changed.near_clip=4;
        check(!bovr::t5::room_scale_native::same(facts,changed),"changed raw near plane invalidates repeated native facts");
        Simulation stopped;const auto body=stopped.sample.origin;
        for(unsigned i=1;i<30;++i){stopped.sample.now+=10;++stopped.sample.command_number;
            ++stopped.sample.rig.frame.frame_id;++stopped.sample.rig.frame.actions.sequence;
            stopped.sample.rig.published_ms=stopped.sample.now;stopped.sample.rig.frame.head_center.position.z=-float(i)*.01f;
            stopped.plan=stopped.follow.step(stopped.sample);stopped.follow.clip(stopped.sample,stopped.plan,0,true);
            stopped.sample.rig.tracking_anchor=stopped.plan.anchor;
            check(!stopped.plan.moving&&stopped.plan.forward==0&&stopped.plan.right==0&&rs::length(stopped.plan.residual)==0,
                "start-solid enlarged clearance consumes lean without native body request");}
        check(!std::memcmp(&body,&stopped.sample.origin,sizeof(body)),"clearance changes never teleport the native body");
    }
    // Reproduce command/render translation drift with fixed tracked controllers.
    // Exercise actual stereo, borrowed weapon, glove, grenade and dual math.
    {namespace input=bovr::input;namespace a=bovr::t5::animation_detail;
     input::ReservedWeaponFrame r{};r.identity={1,2,3,4};r.anchor_generation=1;r.published_ms=1000;
     r.exclusively_reserved_for_scene=true;r.tracking_anchor.position={0,1.6f,0};
     auto& f=r.prediction;f.frame_id=2;f.should_render=f.views_valid=f.actions.focused=true;
     f.predicted_display_time=200;f.predicted_display_period=10;f.actions.sequence=2;
     f.head_center.position={.1f,1.6f,-.2f};
     f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};
     for(auto& e:f.eyes)e.fov={-.7f,.7f,.7f,-.7f};
     for(auto& h:f.actions.hands){h.grip.active=h.grip.position_valid=h.grip.position_tracked=
         h.grip.orientation_valid=h.grip.orientation_tracked=true;
         h.aim.active=h.aim.orientation_valid=h.aim.orientation_tracked=true;}
     f.actions.hands[1].grip.pose.position={.2f,1.3f,-.4f};
     f.actions.hands[0].grip.pose.position={.05f,1.35f,-.75f};
     input::manual::Output reload{};reload.supported=true;reload.weapon=17;reload.epoch=7;
     reload.generation=1;reload.updated_ms=1000;
     a::LocalGeometry geometry{};geometry.gun_in_dobj.axis=geometry.flash_in_gun.axis={{1,0,0},{0,1,0},{0,0,1}};
     geometry.flash_in_gun.origin={8,0,0};
     unsigned old_drift{};
     for(float yaw:{0.f,44.f,90.f,-125.f})for(float height:{60.f,40.f})for(unsigned step=0;step<12;++step){
         const float rad=yaw*.0174532925199433f;
         bovr::t5::SceneView stock{};stock.origin={120+float(step)*.75f,230-float(step)*.3f,height};
         stock.width=3840;stock.height=2160;stock.near_clip=4;
         stock.axis={{std::cos(rad),std::sin(rad),0},{-std::sin(rad),std::cos(rad),0},{0,0,1}};
         stock.tan_half_fov_x=stock.tan_half_fov_y=1;
         const xr::Vec3f approved{120,230,height-2};auto camera=f;
         check(rs::apply_approved_eye(stock.origin,stock.axis,r.tracking_anchor,approved,true,&camera),
             "movement fixture uses unchanged collision-approved center under turn and crouch");
         xr::Vec3f H{},C{};r.camera_translation={};
         input::WeaponBodySample body{r.identity,{stock.origin,stock.axis},input::BodyCameraSource::completed_native_refdef};
         input::BorrowedPlacementValues old{},fixed{};
         check(input::propose_borrowed_placement(r,body,geometry,1050,&old)==input::BorrowedPlacementStatus::proposed,
             "baseline movement uses production tracked placement");
         check(rs::camera_translation(stock.origin,stock.axis,r.tracking_anchor,f.head_center,camera.head_center,
             &r.camera_translation,&H,&C),"fresh camera receipt yields coherent world translation");
         const auto raw_copy=f;const auto anchor_copy=r.tracking_anchor;const auto body_copy=body;
         check(input::propose_borrowed_placement(r,body,geometry,1050,&fixed)==input::BorrowedPlacementStatus::proposed,
             "corrected movement uses same production placement");
         bovr::t5::StereoViews views{};check(bovr::t5::build_stereo_views(stock,camera,r.tracking_anchor,&views),
             "corrected movement retains actual stereo collision cameras");
         const xr::Vec3f center{(views.eyes[0].origin.x+views.eyes[1].origin.x)*.5f,
             (views.eyes[0].origin.y+views.eyes[1].origin.y)*.5f,(views.eyes[0].origin.z+views.eyes[1].origin.z)*.5f};
         check(a::distance(center,approved)<.002f&&a::distance(center,C)<.002f,
             "both eyes stay at the authorized world center; no sweep bypass");
         check(a::distance(a::sub(fixed.gun.origin,C),a::sub(old.gun.origin,H))<.002f,
             "fixed controller-to-head relation is invariant during native walking");
         if(a::distance(a::sub(old.gun.origin,C),a::sub(old.gun.origin,H))>.5f)++old_drift;
         check(a::axis_error(old.gun.axis,fixed.gun.axis)==0&&a::axis_error(old.barrel.axis,fixed.barrel.axis)==0&&
             a::distance(a::sub(fixed.barrel.origin,old.barrel.origin),r.camera_translation)<.002f,
             "camera correction translates native gun and barrel equally without aim rotation");
         bovr::t5::reload_pose::Frame before{},after{};
         check(bovr::t5::reload_pose::make_frame(r,old,reload,17,7,1050,&before)&&
             bovr::t5::reload_pose::make_frame(r,fixed,reload,17,7,1050,&after)&&
             a::distance(a::sub(after.hand.origin,C),a::sub(before.hand.origin,H))<.002f,
             "automatic-ready and manual-ready free glove share the weapon camera correction");
         const auto old_grenade=bovr::t5::grenade_visual_pose::hand(f.actions.hands[0],r.tracking_anchor,old.body);
         const auto fixed_grenade=bovr::t5::grenade_visual_pose::hand(f.actions.hands[0],r.tracking_anchor,fixed.body);
         check(a::distance(a::sub(fixed_grenade.origin,C),a::sub(old_grenade.origin,H))<.002f,
             "shared grenade hand transform preserves head-relative placement");
         check(bovr::t5::reload_pose::make_frame(r,old,reload,17,7,1050,&before,true)&&
             bovr::t5::reload_pose::make_frame(r,fixed,reload,17,7,1050,&after,true)&&
             a::distance(a::sub(after.companion_aim.origin,C),a::sub(before.companion_aim.origin,H))<.002f,
             "independent companion pistol uses identical correction");
         check(!std::memcmp(&raw_copy,&f,sizeof(f))&&!std::memcmp(&anchor_copy,&r.tracking_anchor,sizeof(anchor_copy))&&
             !std::memcmp(&body_copy,&body,sizeof(body)),"correction cannot mutate XR actions, anchor or native camera sample");
         // Rejected/no approval keeps raw camera and exact zero correction.
         check(rs::camera_translation(stock.origin,stock.axis,r.tracking_anchor,f.head_center,f.head_center,
             &r.camera_translation)&&rs::length({r.camera_translation.x,r.camera_translation.y})==0&&r.camera_translation.z==0,
             "no approval leaves original tracked placement unchanged");
         // Stale or failed sweep falls back to stock; visuals must follow that same fallback.
         auto fallback=f;check(rs::apply_approved_eye(stock.origin,stock.axis,r.tracking_anchor,stock.origin,true,&fallback)&&
             rs::camera_translation(stock.origin,stock.axis,r.tracking_anchor,f.head_center,fallback.head_center,&r.camera_translation),
             "stale clearance uses stock-eye fallback rather than unapproved displacement");
     }
     check(old_drift>0,"fixture reproduces visible baseline drift despite perfectly steady tracking");
     auto bad=r;bad.camera_translation.x=std::numeric_limits<float>::quiet_NaN();
     input::BorrowedPlacementValues refused{};
     input::WeaponBodySample body{r.identity,{{120,230,60},{{1,0,0},{0,1,0},{0,0,1}}},input::BodyCameraSource::completed_native_refdef};
     check(input::propose_borrowed_placement(bad,body,geometry,1050,&refused)==input::BorrowedPlacementStatus::body&&!refused.valid,
         "invalid correction never reaches native pose writer");
     check(input::propose_borrowed_placement(r,body,geometry,1101,&refused)==input::BorrowedPlacementStatus::stale&&!refused.valid,
         "frozen correction cannot renew an expired XR reservation");
    }
    printf("room-scale: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
