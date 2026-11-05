// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/native_command_aim.hpp"
#include "t5/bullet_probe.hpp"
#include <cmath>
#include <cstring>
namespace bovr::t5::motorcycle_shot {
inline bool geometry(const input::CommandRigSample& rig,const wawvr::xr::ActionSnapshot& actions,
                     float vehicle_yaw,wawvr::xr::Hand hand,std::uint64_t now,
                     const NativeBulletGeometry& original,NativeBulletGeometry* out) noexcept {
    if(!out||!std::isfinite(vehicle_yaw))return false;
    for(float v:original.muzzle)if(!std::isfinite(v))return false;
    const float yaw=vehicle_yaw*.0174532925199433f,c=std::cos(yaw),s=std::sin(yaw);
    const wawvr::xr::Basis3f body{{c,s,0},{-s,c,0},{0,0,1}};
    input::CommandAimProposal p{};
    if(input::propose_native_command_aim(rig,actions,body,hand,now,&p,false)!=input::CommandAimStatus::accepted)return false;
    const float horizontal=std::hypot(p.forward.x,p.forward.y);
    if(horizontal<.001f)return false;
    auto value=original;
    value.forward[0]=p.forward.x;value.forward[1]=p.forward.y;value.forward[2]=p.forward.z;
    value.right[0]=p.forward.y/horizontal;value.right[1]=-p.forward.x/horizontal;value.right[2]=0;
    for(unsigned i=0;i<3;++i){const unsigned j=(i+1)%3,k=(i+2)%3;
        value.up[i]=value.right[j]*value.forward[k]-value.right[k]*value.forward[j];}
    *out=value;return true;
}
}
namespace bovr::t5 {
bool select_motorcycle_script_shot(std::uintptr_t,const std::uint32_t*,unsigned char*) noexcept;
}
