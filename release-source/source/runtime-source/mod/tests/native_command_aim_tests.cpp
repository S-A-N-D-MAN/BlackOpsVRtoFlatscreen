// SPDX-License-Identifier: GPL-3.0-only
#include "input/native_command_aim.hpp"
#include "xr_math.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <xmmintrin.h>
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* text){++checks;if(!ok){++failures;printf("FAIL %s\n",text);}}
using namespace bovr::input;namespace xr=wawvr::xr;
xr::Quaternionf rotation(xr::Vec3f axis,float degrees){const float r=degrees*.00872664625997F,s=std::sin(r);return {axis.x*s,axis.y*s,axis.z*s,std::cos(r)};}
bool close(float a,float b){return std::fabs(a-b)<.006F;}
float decode(std::uint16_t word){std::int16_t value{};memcpy(&value,&word,2);return value*.0054931640625F;}
std::uint32_t native_pack_oracle(float pitch,float yaw){
 const float units=182.0444488525390625F;unsigned result{};
 __asm {
  movss xmm0,pitch
  mulss xmm0,units
  cvttss2si eax,xmm0
  and eax,0xffff
  movss xmm0,yaw
  mulss xmm0,units
  cvttss2si edx,xmm0
  shl edx,16
  or eax,edx
  mov result,eax
 }
 return result;
}
struct Fixture {
 CommandRigSample r{};xr::ActionSnapshot actions{};xr::Basis3f body{};
 Fixture(){r.xr_generation=r.anchor_generation=7;r.published_ms=990;r.frame.frame_id=11;r.frame.predicted_display_time=100000;r.frame.predicted_display_period=11111111;r.frame.should_render=r.frame.views_valid=true;
  actions.focused=true;actions.sequence=9;auto& aim=actions.hands[1].aim;aim.active=aim.orientation_valid=aim.orientation_tracked=true;r.frame.actions=actions;}
 CommandAimStatus run(CommandAimProposal* p){return propose_native_command_aim(r,actions,body,xr::Hand::Right,1000,p);}
 void pose(xr::Quaternionf q){actions.hands[1].aim.pose.orientation=q;r.frame.actions=actions;}
};
}
int main(){
 using S=CommandAimStatus;Fixture f;CommandAimProposal p{};
 check(f.run(&p)==S::accepted&&p.packed==0&&close(p.forward.x,1),"identity aim maps IW forward, four zero bytes");
 for(int angle=-179;angle<=179;++angle){f=Fixture{};f.pose(rotation({0,1,0},float(angle)));check(f.run(&p)==S::accepted&&close(decode(std::uint16_t(p.packed>>16)),float(angle)),"XR yaw native signed-short roundtrip");}
 for(int angle=-85;angle<=85;++angle){f=Fixture{};f.pose(rotation({1,0,0},float(angle)));check(f.run(&p)==S::accepted&&close(decode(std::uint16_t(p.packed)),float(-angle)),"XR pitch native forward sign and quantization");}
 std::uint32_t word{};check(pack_native_gun_angles(360,720,&word)&&word==0,"whole wraps pack zero");
 check(pack_native_gun_angles(180,-180,&word)&&word==0x80008000,"signed seam bits retained");
 const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
 word=123;check(!pack_native_gun_angles(nan,0,&word)&&word==123&&!pack_native_gun_angles(0,inf,&word),"nonfinite pack never writes destination");
 check(pack_native_gun_angles(0.004F,-0.004F,&word)&&word==0,"native truncation, no nearest rounding");
 const auto mxcsr=_mm_getcsr();
 for(unsigned rounding=0;rounding<4;++rounding){_mm_setcsr((mxcsr&~0x6000U)|(rounding<<13));
  for(int i=-32750;i<32750;i+=29){const float angle=float(i)*.0054931640625F;
   for(const auto value:{std::nextafter(angle,-inf),angle,std::nextafter(angle,inf)})
    check(pack_native_gun_angles(value,-value,&word)&&word==native_pack_oracle(value,-value),
          "both words match actual native SSE instruction oracle under every MXCSR rounding mode");
  }
 }
 _mm_setcsr(mxcsr);
 f=Fixture{};f.pose(rotation({1,0,0},90));check(f.run(&p)==S::pole,"exact pole refused without arbitrary yaw");
 f=Fixture{};f.pose(rotation({0,0,1},30));check(f.run(&p)==S::accepted&&close(p.pitch_degrees,0)&&close(p.yaw_degrees,0),"roll does not change forward; roll field native");
 f=Fixture{};f.body={{0,1,0},{-1,0,0},{0,0,1}};check(f.run(&p)==S::accepted&&close(p.yaw_degrees,90),"same native body yaw rotates aim");
 f=Fixture{};f.r.tracking_anchor.orientation=rotation({0,1,0},30);f.pose(rotation({0,1,0},30));check(f.run(&p)==S::accepted&&close(p.yaw_degrees,0),"original anchor orientation removed once");
 f=Fixture{};f.run(&p);const auto unchanged=p.packed;f.r.frame.head_center.orientation=rotation({0,1,0},80);f.r.frame.head_center.position={1,2,3};f.actions.hands[1].aim.pose.position={5,6,7};f.r.frame.actions=f.actions;
 check(f.run(&p)==S::accepted&&p.packed==unchanged,"head motion and aim translation never drag orientation");
 f=Fixture{};f.body={{0,0,-1},{0,1,0},{1,0,0}};check(f.run(&p)==S::accepted&&close(p.yaw_degrees,0),"gravity-level body shares renderer vertical fallback");
 for(unsigned which=0;which<15;++which){f=Fixture{};switch(which){
 case 0:f.r.xr_generation=0;break;case 1:f.r.anchor_generation=8;break;case 2:f.r.frame.frame_id=0;break;
 case 3:f.actions.sequence++;break;case 4:f.r.published_ms=899;break;case 5:f.r.published_ms=1001;break;
 case 6:f.r.frame.views_valid=false;break;case 7:f.r.frame.should_render=false;break;case 8:f.r.frame.head_center.orientation={0,0,0,0};break;
 case 9:f.actions.focused=false;f.r.frame.actions=f.actions;break;case 10:f.actions.hands[1].aim.orientation_tracked=false;f.r.frame.actions=f.actions;break;
 case 11:f.pose({nan,0,0,1});break;case 12:f.body.forward={2,0,0};break;case 13:f.body.left={0,-1,0};break;case 14:f.r.frame.predicted_display_period=0;break;}
 check(f.run(&p)!=S::accepted&&p.packed==0,"invalid identity/age/prediction/tracking/body has no proposal");}
 for(unsigned h=0;h<2;++h)for(unsigned axis=0;axis<2;++axis){f=Fixture{};f.actions.hands[h]=f.actions.hands[1];
  f.actions.hands[h].aim.pose.orientation=rotation(axis?xr::Vec3f{1,0,0}:xr::Vec3f{0,1,0},45);
  f.actions.hands[h^1].aim={};f.r.frame.actions=f.actions;
  check(propose_native_command_aim(f.r,f.actions,f.body,static_cast<xr::Hand>(h),1000,&p)==S::accepted&&
   close(p.yaw_degrees,axis?0.F:45.F)&&close(p.pitch_degrees,axis?-45.F:0.F),"both selected hands encode signed yaw/pitch independently of other hand");
 }
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
