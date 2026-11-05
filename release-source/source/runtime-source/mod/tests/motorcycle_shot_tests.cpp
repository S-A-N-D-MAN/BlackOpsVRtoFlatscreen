// SPDX-License-Identifier: GPL-3.0-only
#include "t5/motorcycle_shot.hpp"
#include <cstdio>
#include <limits>
namespace {
namespace xr=wawvr::xr;
using bovr::t5::NativeBulletGeometry;
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}}
bool close(float a,float b){return std::fabs(a-b)<.001f;}
xr::Quaternionf rotation(xr::Vec3f axis,float degrees){float a=degrees*.00872664626f,s=std::sin(a);return {axis.x*s,axis.y*s,axis.z*s,std::cos(a)};}
struct Fixture {
 bovr::input::CommandRigSample rig{};xr::ActionSnapshot actions{};
 NativeBulletGeometry original{{1,0,0},{0,-1,0},{0,0,1},{123,456,789}};
 Fixture(){rig.xr_generation=rig.anchor_generation=7;rig.published_ms=990;rig.frame.frame_id=11;
 rig.frame.predicted_display_time=100000;rig.frame.predicted_display_period=11111111;rig.frame.should_render=rig.frame.views_valid=true;
 actions.focused=true;actions.sequence=9;auto& aim=actions.hands[1].aim;aim.active=aim.orientation_valid=aim.orientation_tracked=true;rig.frame.actions=actions;}
 bool run(float yaw,NativeBulletGeometry* out,xr::Hand hand=xr::Hand::Right){return bovr::t5::motorcycle_shot::geometry(rig,actions,yaw,hand,1000,original,out);}
 void pose(xr::Quaternionf q){actions.hands[1].aim.pose.orientation=q;rig.frame.actions=actions;}
};
}
int main(){
 Fixture f;NativeBulletGeometry out{};
 check(f.run(0,&out)&&close(out.forward[0],1)&&close(out.forward[1],0)&&close(out.forward[2],0)&&
       close(out.right[0],0)&&close(out.right[1],-1)&&close(out.right[2],0)&&close(out.up[0],0)&&close(out.up[1],0)&&close(out.up[2],1)&&
       !memcmp(out.muzzle,f.original.muzzle,12),"neutral pose preserves native muzzle and proper basis including signed zero");
 for(int bike=-180;bike<=180;bike+=30)for(int hand=-80;hand<=80;hand+=20){
  f=Fixture{};f.pose(rotation({0,1,0},float(hand)));const float a=(bike+hand)*.01745329252f;
  check(f.run(float(bike),&out)&&close(out.forward[0],std::cos(a))&&close(out.forward[1],std::sin(a)),"shot direction combines bike heading and controller yaw");
  check(!memcmp(out.muzzle,f.original.muzzle,12),"native scripted muzzle remains unchanged");
  float dot{},length{};for(int i=0;i<3;++i){dot+=out.forward[i]*out.right[i];length+=out.forward[i]*out.forward[i];}
  check(close(dot,0)&&close(length,1)&&close(out.up[2],1),"bullet spread basis remains orthonormal and correctly handed");
 }
 f=Fixture{};f.pose(rotation({1,0,0},30));check(f.run(90,&out)&&close(out.forward[0],0)&&close(out.forward[1],.8660254f)&&close(out.forward[2],.5f),"controller elevation steers above bike plane");
 f=Fixture{};f.rig.tracking_anchor.orientation=rotation({0,1,0},35);f.pose(rotation({0,1,0},35));check(f.run(60,&out)&&close(out.forward[0],.5f)&&close(out.forward[1],.8660254f),"tracking anchor is removed exactly once");
 f=Fixture{};f.actions.hands[0]=f.actions.hands[1];f.actions.hands[0].aim.pose.orientation=rotation({0,1,0},30);f.actions.hands[1]={};f.rig.frame.actions=f.actions;
 check(f.run(0,&out,xr::Hand::Left)&&close(out.forward[1],.5f),"selected left hand works independently of inactive right hand");
 for(unsigned mode=0;mode<9;++mode){f=Fixture{};float yaw=0;switch(mode){
 case 0:f.rig.published_ms=899;break;case 1:f.rig.anchor_generation=8;break;case 2:f.actions.focused=false;f.rig.frame.actions=f.actions;break;
 case 3:f.actions.hands[1].aim.orientation_tracked=false;f.rig.frame.actions=f.actions;break;case 4:f.actions.sequence++;break;
 case 5:yaw=std::numeric_limits<float>::infinity();break;case 6:f.original.muzzle[0]=std::numeric_limits<float>::quiet_NaN();break;
 case 7:f.rig.frame.should_render=false;break;case 8:f.pose(rotation({1,0,0},90));break;}
 NativeBulletGeometry before{};memset(&before,0x42,sizeof(before));out=before;
 check(!f.run(yaw,&out)&&!memcmp(&before,&out,sizeof(out)),"stale invalid or unavailable aim leaves destination untouched");
 }
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
