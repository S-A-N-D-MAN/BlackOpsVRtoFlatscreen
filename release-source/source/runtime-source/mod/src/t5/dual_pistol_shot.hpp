// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/native_command_aim.hpp"
#include "t5/bullet_probe.hpp"
#include <cmath>
#include <cstring>
namespace bovr::t5::dual_pistol_shot {
inline int hand(unsigned event) noexcept {
    return event==29||event==30?1:event==31||event==32?0:-1;
}
inline bool event_current(int old_sequence,int cursor,int sequence,unsigned event,unsigned ring_event,
                          unsigned parameter,unsigned ring_parameter) noexcept {
    if(hand(event)<0||old_sequence<0||old_sequence>255||sequence<0||sequence>255||cursor< -4||cursor>255)return false;
    const unsigned count=(unsigned(sequence)-unsigned(old_sequence))&255u;
    const unsigned offset=(unsigned(cursor)-unsigned(old_sequence))&255u;
    return count>0&&count<=4&&offset<count&&ring_event==event&&parameter==ring_parameter;
}
inline bool geometry(const input::CommandRigSample& rig,const wawvr::xr::ActionSnapshot& actions,
                     const wawvr::xr::Basis3f& body,unsigned event,std::uint64_t now,
                     const NativeBulletGeometry& original,NativeBulletGeometry* out) noexcept {
    const auto which=hand(event);if(!out||which<0)return false;
    for(float value:original.muzzle)if(!std::isfinite(value))return false;
    input::CommandAimProposal aim{};
    if(input::propose_native_command_aim(rig,actions,body,static_cast<wawvr::xr::Hand>(which),now,&aim,false)!=
       input::CommandAimStatus::accepted)return false;
    const float horizontal=std::hypot(aim.forward.x,aim.forward.y);if(horizontal<.001f)return false;
    auto value=original;value.forward[0]=aim.forward.x;value.forward[1]=aim.forward.y;value.forward[2]=aim.forward.z;
    value.right[0]=aim.forward.y/horizontal;value.right[1]=-aim.forward.x/horizontal;value.right[2]=0;
    for(unsigned i=0;i<3;++i){const unsigned j=(i+1)%3,k=(i+2)%3;
        value.up[i]=value.right[j]*value.forward[k]-value.right[k]*value.forward[j];}
    *out=value;return true;
}
}
namespace bovr::t5 {
// Guard validation occurs before patching the ordinary Bullet_Fire CALL.
bool dual_pistol_shot_guards(std::uintptr_t) noexcept;
// Exact ordinary caller, actual ClientEvents event, and retained consumed-command
// match only. Scratch is private to this invocation; native WP stays untouched.
bool select_dual_pistol_shot(std::uintptr_t,const std::uint32_t* original_stack,unsigned event,
                            unsigned char* scratch) noexcept;
bool select_dual_pistol_projectile_shot(std::uintptr_t,const std::uint32_t* original_stack,unsigned event,
                                       unsigned char* scratch) noexcept;
}
