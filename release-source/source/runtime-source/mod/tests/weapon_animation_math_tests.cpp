// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_animation_math.hpp"
#include <cstdio>
#include <limits>
using namespace bovr::t5::animation_detail;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;printf("FAIL %s\n",label);}}
std::array<float,4> quaternion(Vec e) {
    constexpr float h=0.008726646259971647884F;
    const auto sp=std::sin(e.x*h),cp=std::cos(e.x*h),sy=std::sin(e.y*h),cy=std::cos(e.y*h),sr=std::sin(e.z*h),cr=std::cos(e.z*h);
    return {sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy,cr*cp*cy+sr*sp*sy};
}
std::array<float,4> multiply(const std::array<float,4>& a,const std::array<float,4>& b){
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
NativeMat32 make(const std::array<float,4>& q,Vec origin,float scale=1){
    auto v=q;for(auto& f:v)f*=scale;return {v,origin,2/(scale*scale)};
}
}
int main(){
    for(int i=0;i<200;++i){
        PoseCopy p{{-100.F+i*0.5F,-1256.F+i*2.F,143.F-i*0.2F},{i*1.31F-95.F,i*5.19F-400.F,i*0.87F-50.F}};
        Transform pt{};check(pose_transform(p,&pt),"finite all-angle persistent pose");
        Transform from_quat{};check(convert_native(make(quaternion(p.angles),p.origin),&from_quat),"native quat/weight accepts pose");
        check(axis_error(pt.axis,from_quat.axis)<0.00001F,"all pitch yaw roll agree with native quaternion convention");
        const auto gq=quaternion({i*0.33F,i*0.71F,-i*0.19F}),fq=quaternion({3.F,-5.F,2.F});
        Transform gl{},fl{};check(convert_native(make(gq,{5.F,-2.F,1.F}),&gl)&&convert_native(make(fq,{19.3F,-2.18F,-1.9F}),&fl),"authored copied local transforms");
        const auto gw=compose(pt,gl),fw=compose(gw,fl);
        const auto world_gq=multiply(quaternion(p.angles),gq),world_fq=multiply(world_gq,fq);
        const auto raw_gun=make(world_gq,gw.origin,3.F),raw_flash=make(world_fq,fw.origin,0.25F);
        const auto saved_gun=raw_gun,saved_flash=raw_flash;
        LocalGeometry geometry{};check(derive_local(raw_gun,raw_flash,p,&geometry),"weighted native matrices derive copied geometry");
        check(distance(geometry.gun_in_dobj.origin,gl.origin)<0.0002F&&axis_error(geometry.gun_in_dobj.axis,gl.axis)<0.00002F,"strip actual persistent cpose to recover animated gun");
        check(distance(geometry.flash_in_gun.origin,fl.origin)<0.0002F&&axis_error(geometry.flash_in_gun.axis,fl.axis)<0.00002F,"gun-local barrel recovered through attached gun transform");
        check(raw_gun.quaternion==saved_gun.quaternion&&raw_flash.quaternion==saved_flash.quaternion,"native quaternion representations remain untouched");
        PoseCopy moved{{p.origin.x+30,p.origin.y-70,p.origin.z+11},{p.angles.x+21,p.angles.y-53,p.angles.z+34}};
        Transform moved_pose{};pose_transform(moved,&moved_pose);
        const auto next_g=compose(moved_pose,gl),next_f=compose(next_g,fl);
        const auto next_gq=multiply(quaternion(moved.angles),gq),next_fq=multiply(next_gq,fq);
        LocalGeometry post{};Difference d{};
        check(derive_local(make(next_gq,next_g.origin),make(next_fq,next_f.origin),moved,&post)&&difference(geometry,post,&d),"pre/post after native placement produces valid comparison");
        check(d.gun_position<0.0003F&&d.flash_position<0.0003F&&d.gun_angle_radians<0.003F&&d.flash_angle_radians<0.003F,"placement change is removed without suppressing local animation");
    }
    NativeMat32 good{{0,0,0,1},{1,2,3},2};Transform output{};
    check(convert_native(good,&output),"identity native matrix");
    auto bad=good;bad.quaternion={0,0,0,0};check(!convert_native(bad,&output),"zero quaternion rejected without repair");
    bad=good;bad.trans_weight=1;check(!convert_native(bad,&output),"inconsistent quaternion weight rejected");
    bad=good;bad.trans_weight=-2;check(!convert_native(bad,&output),"negative weight rejected");
    bad=good;bad.quaternion[1]=std::numeric_limits<float>::infinity();check(!convert_native(bad,&output),"infinite quaternion rejected");
    bad=good;bad.origin.z=std::numeric_limits<float>::quiet_NaN();check(!convert_native(bad,&output),"nonfinite translation rejected");
    bad=good;bad.trans_weight=std::numeric_limits<float>::quiet_NaN();check(!convert_native(bad,&output),"nonfinite native weight rejected");
    check(!pose_transform({{}, {0,std::numeric_limits<float>::infinity(),0}},&output),"nonfinite persistent angle rejected");
    LocalGeometry before{},after{};PoseCopy identity{};derive_local(good,good,identity,&before);
    auto rotated=make(quaternion({0,15,0}),{3,2,3});derive_local(rotated,rotated,identity,&after);Difference d{};
    check(difference(before,after,&d)&&std::fabs(d.gun_position-2)<0.00001F&&std::fabs(d.gun_angle_radians-0.261799F)<0.00001F,"actual animated gun translation and rotation remain measurable");
    check(d.flash_position<0.00001F&&d.flash_angle_radians<0.001F,"common gun/barrel change preserves gun-local attachment");
    after.gun_in_dobj.axis.left=after.gun_in_dobj.axis.forward;check(!difference(before,after,&d),"malformed comparison basis rejected");
    check(!convert_native(good,nullptr)&&!derive_local(good,good,identity,nullptr),"null result rejected");
    printf("checks=%u failures=%u native_calls=0 native_writes=0\n",checks,failures);return failures?1:0;
}
