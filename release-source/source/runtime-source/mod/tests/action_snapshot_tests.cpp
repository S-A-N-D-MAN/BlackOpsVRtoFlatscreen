// SPDX-License-Identifier: GPL-3.0-only
#include "input/action_snapshot_cache.hpp"
#include "t5/vr_scene_adapter.hpp"
#include <cstdio>
int main() {
    unsigned failures{};
    const auto check=[&](bool ok,const char* message) { if(!ok) { ++failures; printf("FAIL %s\n",message); } };
    bovr::input::ActionSnapshotCache cache;
    wawvr::xr::ActionSnapshot current{},read{};
    std::uint64_t generation{};
    current.focused=true; current.sequence=17;
    check(!cache.read(100,&read,&generation),"empty cache rejects");
    cache.publish(current,3,100);
    check(cache.read(100,&read,&generation) && read.focused && read.sequence==17 && generation==3,"fresh generation copied");
    check(cache.read(350,&read,&generation),"250 ms boundary accepted");
    check(!cache.read(351,&read,&generation) && !read.focused && read.sequence==0 && generation==0,"stale read clears prior pressed snapshot");
    check(!cache.read(99,&read,&generation),"clock regression cannot revive snapshot");
    current.focused=false; current.sequence=18;
    cache.publish(current,4,400);
    check(cache.read(401,&read,&generation) && !read.focused && read.sequence==18 && generation==4,"focus loss and new generation reach consumer");
    cache.clear(); check(!cache.read(401,&read,&generation) && generation==0,"reset invalidates recent actions");
    cache.publish(current,0,400); check(!cache.read(401,&read,&generation),"zero generation rejected");
    check(!cache.read(401,nullptr,&generation) && !cache.read(401,&read,nullptr),"missing output rejected");
    read=current; generation=42;
    check(!bovr::t5::read_vr_input_snapshot(&read,&generation) && read.sequence==0 && generation==0,"inactive retail adapter returns neutral outputs without XR");
    bovr::input::CommandRigSample rig{};rig.frame.frame_id=9;rig.xr_generation=42;rig.published_ms=100;
    read=current;generation=42;
    check(!bovr::t5::read_vr_command_input_snapshot(&read,&generation,&rig)&&read.sequence==0&&generation==0&&
          rig.frame.frame_id==0&&rig.xr_generation==0&&rig.published_ms==0,
          "inactive combined input cache clears actions generation and old rig without XR");
    read=current;generation=42;
    check(!bovr::t5::read_vr_command_input_snapshot(&read,&generation,nullptr)&&read.sequence==0&&generation==0,
          "missing rig output rejects and clears remaining copied outputs");
    printf("action_snapshot_failures=%u retail_processes=0\n",failures); return failures?1:0;
}
