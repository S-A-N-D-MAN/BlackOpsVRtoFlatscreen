// SPDX-License-Identifier: GPL-3.0-only
// Exercise the real native-first bridge and shared physical optic gate without
// installing a hook or touching a game/runtime.
#define infrared_scope_detail infrared_scope_test_detail
#define configure_infrared_scope_adapter configure_infrared_scope_adapter_test_copy
#define ensure_infrared_scope_adapter ensure_infrared_scope_adapter_test_copy
#define thermal_scene_vision_override thermal_scene_vision_override_test_copy
#include "../src/t5/infrared_scope_adapter.cpp"
#undef infrared_scope_detail
#undef configure_infrared_scope_adapter
#undef ensure_infrared_scope_adapter
#undef thermal_scene_vision_override
#include "t5/physical_infrared_scope.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace d=bovr::t5::infrared_scope_test_detail;
namespace xr=wawvr::xr;
namespace {
unsigned checks{},native_calls{},selection_calls{};
bool native_value{},selection_value{},nested{};
int native_client{};
void check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
bool __cdecl native(int local){++native_calls;native_client=local;SetLastError(317);return native_value;}
bool invoke(int local=0,std::uintptr_t caller=0x45ead5) {
    native_calls=selection_calls=0;const auto result=d::invoke(local,caller);
    check(native_calls==1,"native predicate runs exactly once");
    check(native_client==local,"native local-client argument unchanged");
    check(GetLastError()==317,"native last-error state preserved");return result;
}
void reset(){native_value=false;selection_value=true;nested=false;d::ready.store(true);d::selecting=false;d::original=native;}
}
namespace bovr::t5 {
bool thermal_scene_vision_override_test_copy() noexcept {
    ++selection_calls;
    if(nested){nested=false;check(!d::invoke(0,0x45ead5),"nested selector falls back to native");}
    SetLastError(99);return selection_value;
}
}
int main(){
    d::ready.store(false);d::armed.store(false);
    check(!bovr::t5::ensure_infrared_scope_adapter_test_copy(),"unarmed startup performs no hook installation");
    d::ready.store(true);
    check(bovr::t5::ensure_infrared_scope_adapter_test_copy(),"already installed hook needs no new patch transaction");
    for(const auto& site:d::sites){reset();const bool vision=site.address==0x45ead0;
        check(invoke(0,site.address+5)==vision,"only isolated vision conversion can override native false");
        check(selection_calls==(vision?1u:0u),"ordinary actors never ask for scope infrared");}
    reset();native_value=true;selection_value=false;check(invoke(),"native infrared remains available");check(!selection_calls,"native true needs no override");
    reset();selection_value=false;check(!invoke(),"lowered/ineligible optic preserves native false");
    reset();d::ready.store(false);check(!invoke(),"disabled hook preserves native");check(!selection_calls,"disabled never asks physical scope");
    reset();check(!invoke(1),"other local clients excluded");check(!selection_calls,"other client never asks physical scope");
    for(const auto returned:{std::uintptr_t(0x5c3bf1),std::uintptr_t(0x771612),std::uintptr_t(0x45ead4),std::uintptr_t(0)}){
        reset();check(!invoke(0,returned),"blur fullscreen-overlay and unknown consumers excluded");check(!selection_calls,"unknown caller never asks scope");}
    reset();d::selecting=true;check(!invoke(),"nested callback retains original");check(!selection_calls,"nested callback does not recurse");
    reset();nested=true;native_calls=selection_calls=0;check(d::invoke(0,0x45ead5),"outer selection survives nested native query");
    check(native_calls==2&&selection_calls==1&&!d::selecting,"native called once for each nested invocation and TLS restored");
    bovr::input::CommandRigSample rig{};rig.xr_generation=rig.anchor_generation=7;rig.published_ms=1000;
    auto& f=rig.frame;f.views_valid=f.should_render=f.actions.focused=true;f.frame_id=40;f.head_center.position={0,1.7f,0};
    bovr::t5::crossbow::LensFit lens{{-.11062f,.000084f,.11841f},.0042f,.045f,4};
    for(unsigned i=0;i<2;++i){auto& h=f.actions.hands[i];h.grip.active=h.grip.position_valid=h.grip.position_tracked=h.grip.orientation_valid=h.grip.orientation_tracked=true;
        h.aim.active=h.aim.position_valid=h.aim.position_tracked=h.aim.orientation_valid=h.aim.orientation_tracked=true;h.squeeze.active=true;
        h.grip.pose.position={.032084f,1.58159f,i?-.30f:-.60f};
        f.eyes[i].pose.position={i?.032f:-.032f,1.7f,0};f.eyes[i].fov={-.8f,.8f,.8f,-.8f};}
    f.actions.hands[0].squeeze.current=1;
    auto valid=[&](const auto& r,std::uint64_t now=1000){return bovr::t5::physical_infrared_scope(r,lens,7,now);};
    check(valid(rig),"raised observed Enfield IR optic requests infrared");
    check(valid(rig,1100)&&!valid(rig,1101)&&!valid(rig,999),"freshness includes boundary and rejects stale/future poses");
    for(unsigned field=0;field<16;++field){auto bad=rig;
        switch(field){case 0:bad.xr_generation=8;break;case 1:bad.anchor_generation=8;break;case 2:bad.published_ms=0;break;
        case 3:bad.frame.frame_id=0;break;case 4:bad.frame.views_valid=false;break;case 5:bad.frame.should_render=false;break;
        case 6:bad.frame.actions.focused=false;break;case 7:bad.frame.actions.menu.active=bad.frame.actions.menu.current=true;break;
        case 8:bad.frame.actions.hands[0].squeeze.current=0;break;case 9:bad.frame.actions.hands[0].grip.position_tracked=false;break;
        case 10:bad.frame.actions.hands[1].aim.orientation_tracked=false;break;case 11:bad.frame.actions.hands[1].grip.position_tracked=false;break;
        case 12:for(auto& h:bad.frame.actions.hands)h.grip.pose.position.y-=.6f;break;
        case 13:for(auto& h:bad.frame.actions.hands)h.grip.pose.position.z+=1.f;break;
        case 14:for(auto& h:bad.frame.actions.hands)h.grip.pose.position.z-=1.f;break;
        case 15:bad.frame.actions.hands[1].grip.pose.position.x=std::numeric_limits<float>::quiet_NaN();break;}
        check(!valid(bad),"generation focus menu support tracking and lowered/invalid geometry fail closed");}
    std::printf("%u infrared scope checks passed\n",checks);return 0;
}
