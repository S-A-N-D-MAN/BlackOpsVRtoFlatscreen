// SPDX-License-Identifier: GPL-3.0-only
#include "input/guided_missile_steering.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bovr::input;
unsigned checks{};
void check(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
int main(){
    GuidedMissileSteering steer;
    auto a=steer.update(true,1,-20,179,true,0,0);
    check(!a.pitch&&!a.yaw,"entry anchors the held controller without steering");
    a=steer.update(true,2,-21,-179,true,0,0);
    check(!a.pitch&&!a.yaw,"small motion and yaw wrap remain in deadzone");
    a=steer.update(true,3,-45,154,true,0,0);
    check(a.pitch==-126&&a.yaw==-126,"up and right physical deflection steer up and right");
    a=steer.update(true,4,5,-156,true,0,0);
    check(a.pitch==126&&a.yaw==126,"opposite physical deflection steers down and left");
    a=steer.update(true,5,5,-156,true,1,1);
    check(a.pitch==-126&&a.yaw==-126,"thumbstick takes priority over controller deflection");
    a=steer.update(false,6,0,0,true,1,1);
    check(!a.pitch&&!a.yaw,"lost context immediately clears both axes");
    a=steer.update(true,7,0,0,true,1,1);
    check(!a.pitch&&!a.yaw,"held stick cannot steer on context entry");
    steer.update(true,8,0,0,true,0,0);
    a=steer.update(true,9,0,0,true,-1,-1);
    check(a.pitch==126&&a.yaw==126,"neutral rearms both stick directions");
    a=steer.update(true,10,0,0,false,-1,-1);
    check(!a.pitch&&!a.yaw,"invalid stick cannot retain steering");
    a=steer.update(true,11,std::numeric_limits<float>::quiet_NaN(),0,true,0,0);
    check(!a.pitch&&!a.yaw,"invalid pose clears steering");
    steer.update(true,12,0,0,true,0,0);
    a=steer.update(true,2,25,25,true,1,1);
    check(!a.pitch&&!a.yaw,"sequence regression rebases without a turn");
    for(int i=-200;i<=200;++i){
        const int axis=guided_axis(i*.01f,.2f);
        check(axis>=-126&&axis<=126&&(axis==0||std::abs(axis)>15),"every analog output avoids retail raw-mouse fallback");
    }
    std::printf("%u guided missile steering checks passed\n",checks);
}
