// SPDX-License-Identifier: GPL-3.0-only
#include "t5/slingshot_context.hpp"
#include <cmath>
#include <cstdio>

namespace {
namespace s=bovr::t5::slingshot;
unsigned checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
bool closef(float a,float b){return std::fabs(a-b)<.001f;}
s::Facts sample(){
    s::Facts f{};f.pre.sequence=7;f.pre.gameplay_epoch=3;f.pre.native_gameplay_fields_valid=true;
    f.pre.native_cg_identity=f.cg=0x12340000;f.refdef=f.cg+0x8c100;
    f.pre.native_game_time=f.native_time=f.refdef_time=421763;
    f.pre.native_pm_type=1;f.pre.native_pm_flags=0x00200000;f.pre.native_weapon_flags=0x02000080;
    f.pre.native_entity_flags=0x000a0002;f.primary=0;f.offhand=13;
    f.delta_angles[0]=2.252197f;f.delta_angles[1]=-52.025757f;f.delta_angles[2]=-22.236328f;
    f.clamp_base[0]=355.f;f.clamp_base[1]=186.000015f;
    f.clamp_range[0]=15.f;f.clamp_range[1]=25.f;
    f.origin={-2654.4563f,4569.2900f,1482.0892f};return f;
}
}
int main(){
    auto f=sample();check(s::eligible(f),"captured Vorkuta slingshot tuple is admitted");
    f.pre.native_pm_flags|=0x20;check(s::eligible(f),"pause-transition bit does not change the native slingshot identity");
    f=sample();f.pre.native_pm_type=0;check(!s::eligible(f),"ordinary player is rejected");
    f=sample();f.pre.native_weapon_flags=0x80;check(!s::eligible(f),"generic linked campaign scene is rejected");
    f=sample();f.pre.native_entity_flags=2;check(!s::eligible(f),"linked scene without slingshot entity flags is rejected");
    f=sample();f.primary=3;check(!s::eligible(f),"ordinary primary weapon is rejected");
    f=sample();f.offhand=12;check(!s::eligible(f),"different scripted inventory is rejected");
    f=sample();f.origin.x=-2500;check(!s::eligible(f),"matching flags outside the emplacement are rejected");
    f=sample();f.refdef_time++;check(!s::eligible(f),"mixed native and render times are rejected");
    f=sample();f.primary=3;check(s::retained(f,f.cg,f.pre.gameplay_epoch),"admitted rooftop occurrence survives the post-menu predicted weapon transition");
    f.pre.native_entity_flags=0x00080002;check(s::retained(f,f.cg,f.pre.gameplay_epoch),"restored save survives the observed teleport-bit clear after admission");
    f.pre.native_entity_flags=0x00000002;check(!s::retained(f,f.cg,f.pre.gameplay_epoch),"the rooftop latch rejects unrelated ordinary entity flags");
    f.pre.gameplay_epoch++;check(!s::retained(f,f.cg,f.pre.gameplay_epoch-1),"a different gameplay epoch cannot inherit the rooftop latch");
    f=sample();f.pre.native_pm_type=0;check(!s::retained(f,f.cg,f.pre.gameplay_epoch),"ordinary gameplay cannot inherit the rooftop latch");
    f=sample();f.offhand=0;check(!s::retained(f,f.cg,f.pre.gameplay_epoch),"the latch ends when the scripted slingshot offhand leaves");
    {
        const auto native=sample();s::RelativeAim aim{};
        check(s::relative_aim(native,-22.164581f,-146.476379f,-22.164581f,-146.476379f,&aim),"first tracked controller sample calibrates relative aim");
        check(closef(aim.desired_pitch,-5.f)&&closef(aim.desired_yaw,-173.999985f),"calibration begins at the native clamp centre");
        check(closef(aim.command_pitch,-7.252197f)&&closef(aim.command_yaw,-121.974228f),"current native delta angles are compensated before command packing");
        check(s::relative_aim(native,-22.164581f,-146.476379f,-32.164581f,-131.476379f,&aim),"controller motion produces a second relative solution");
        check(closef(aim.pitch_offset,-10.f)&&closef(aim.yaw_offset,15.f)&&closef(aim.desired_pitch,-15.f)&&closef(aim.desired_yaw,-158.999985f),"controller degrees map one-for-one inside the native range");
        check(s::relative_aim(native,170.f,170.f,-170.f,-150.f,&aim),"controller wraparound remains finite");
        check(closef(aim.pitch_offset,15.f)&&closef(aim.yaw_offset,25.f),"relative aim clamps wrapped motion to the script limits");
    }
    const auto a=sample();auto b=a;check(s::same_current(a,b),"two exact protected copies join the same occurrence");
    ++b.pre.context.command_number;check(!s::same_current(a,b),"command change revokes the occurrence");
    b=a;b.origin.z+=.01f;check(!s::same_current(a,b),"camera movement between copies revokes the occurrence");
    std::printf("slingshot_context_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
