// SPDX-License-Identifier: GPL-3.0-only
#include "t5/dual_pistol_shot.hpp"
#include <cstdio>
#include <limits>
namespace {
namespace xr=wawvr::xr;
namespace policy=bovr::t5::dual_pistol_shot;
unsigned checks{},failures{};
void check(bool ok,const char* what){++checks;if(!ok){++failures;printf("FAIL %s\n",what);}}
bool close(float a,float b){return std::fabs(a-b)<.001f;}
xr::Quaternionf yaw(float degrees){const float a=degrees*.00872664626f;return {0,std::sin(a),0,std::cos(a)};}
struct Fixture {
    bovr::input::CommandRigSample rig{};
    xr::Basis3f body{{1,0,0},{0,1,0},{0,0,1}};
    bovr::t5::NativeBulletGeometry original{{1,0,0},{0,-1,0},{0,0,1},{123,456,789}};
    Fixture(){rig.xr_generation=rig.anchor_generation=7;rig.published_ms=990;rig.frame.frame_id=11;
        rig.frame.predicted_display_time=100000;rig.frame.predicted_display_period=11111111;
        rig.frame.should_render=rig.frame.views_valid=true;rig.frame.actions.focused=true;rig.frame.actions.sequence=9;
        for(auto& h:rig.frame.actions.hands){h.aim.active=h.aim.orientation_valid=h.aim.orientation_tracked=true;}}
    bool run(unsigned event,bovr::t5::NativeBulletGeometry* out){return policy::geometry(rig,rig.frame.actions,body,event,1000,original,out);}
};
}
int main(){
    for(unsigned event=0;event<200;++event)check(policy::hand(event)==(event==29||event==30?1:event==31||event==32?0:-1),"only native dual firing events select a hand");
    Fixture f;f.rig.frame.actions.hands[0].aim.pose.orientation=yaw(-45);f.rig.frame.actions.hands[1].aim.pose.orientation=yaw(60);
    bovr::t5::NativeBulletGeometry left{},right{};
    check(f.run(31,&left)&&f.run(29,&right)&&close(left.forward[1],-.7071068f)&&close(right.forward[1],.8660254f),"same command fires each hand along its own independent direction");
    check(!memcmp(left.muzzle,f.original.muzzle,12)&&!memcmp(right.muzzle,f.original.muzzle,12),"native muzzle origin and native spread ownership are preserved");
    auto old_right=right;f.rig.frame.actions.hands[0].aim.pose.orientation=yaw(15);
    check(f.run(29,&right)&&!memcmp(&right,&old_right,sizeof(right)),"moving left hand does not change right shot");
    f.rig.frame.actions.hands[1].aim.active=false;
    check(f.run(32,&left)&&!f.run(30,&right),"missing right tracking does not reject valid left shot");
    for(unsigned fault=0;fault<6;++fault){f=Fixture{};switch(fault){
        case 0:f.rig.published_ms=899;break;case 1:f.rig.anchor_generation=9;break;
        case 2:f.rig.frame.actions.focused=false;break;case 3:f.rig.frame.actions.hands[0].aim.orientation_tracked=false;break;
        case 4:f.rig.frame.should_render=false;break;case 5:f.original.muzzle[1]=std::numeric_limits<float>::infinity();break;}
        memset(&left,0x42,sizeof(left));const auto before=left;check(!f.run(31,&left)&&!memcmp(&before,&left,sizeof(left)),"failed pose admission keeps original output untouched");}
    check(policy::event_current(10,10,12,29,29,1,1)&&policy::event_current(10,11,12,31,31,1,1),"simultaneous native events are independently current in the same command");
    check(!policy::event_current(10,12,12,31,31,1,1)&&!policy::event_current(10,9,12,31,31,1,1)&&
        !policy::event_current(10,10,15,31,31,1,1)&&!policy::event_current(10,10,12,31,29,1,1)&&
        !policy::event_current(10,10,12,31,31,1,2)&&!policy::event_current(250,0,1,31,31,1,1),"stale future mismatched and overwritten events fail closed");
    check(policy::event_current(255,255,1,29,29,1,1)&&policy::event_current(255,-1,1,29,29,1,1)&&
        policy::event_current(255,0,1,31,31,1,1)&&!policy::event_current(255,1,1,31,31,1,1),
        "bounded modulo256 batch accepts each actual native cursor across wrap including rebased negative cursor");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
