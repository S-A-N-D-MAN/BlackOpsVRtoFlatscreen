// SPDX-License-Identifier: GPL-3.0-only
#include "input/rig_snapshot_cache.hpp"
#include <cstdio>
int main() {
    unsigned checks{},failures{};
    const auto check=[&](bool good,const char* message){++checks;if(!good){++failures;std::printf("FAIL %s\n",message);}};
    bovr::input::RigSnapshotCache cache;
    bovr::input::CommandRigSample result{};
    wawvr::xr::FrameState frame{};frame.frame_id=32;frame.actions.sequence=42;
    frame.actions.focused=true;frame.head_center.position.x=1.25f;
    wawvr::xr::Posef anchor{};anchor.orientation.w=1;anchor.position.y=2.5f;
    check(!cache.read(100,frame.actions,3,anchor,3,&result),"empty cache");
    cache.publish(frame,3,100);
    check(cache.read(100,frame.actions,3,anchor,3,&result),"fresh exact actions and generation");
    check(result.frame.frame_id==32&&result.frame.head_center.position.x==1.25f&&result.published_ms==100&&
          result.xr_generation==3&&result.anchor_generation==3&&result.tracking_anchor.position.y==2.5f,"original frame time and anchor copied");
    frame.head_center.position.x=99;anchor.position.y=99;
    check(result.frame.head_center.position.x==1.25f&&result.tracking_anchor.position.y==2.5f,"no borrowed storage");
    check(cache.read(350,frame.actions,3,anchor,3,&result)&&result.published_ms==100,"boundary read does not freshen original time");
    check(!cache.read(351,frame.actions,3,anchor,3,&result)&&!result.published_ms&&!result.xr_generation,"stale clears output");
    check(!cache.read(99,frame.actions,3,anchor,3,&result),"clock regression");
    check(!cache.read(100,frame.actions,4,anchor,3,&result),"actions generation mismatch");
    check(!cache.read(100,frame.actions,3,anchor,4,&result),"anchor generation mismatch");
    check(!cache.read(100,frame.actions,3,anchor,0,&result),"missing anchor");
    ++frame.actions.sequence;
    check(!cache.read(100,frame.actions,3,anchor,3,&result),"newer action sequence cannot borrow old rig");
    --frame.actions.sequence;
    check(!cache.read(100,frame.actions,3,anchor,3,nullptr),"null output");
    cache.clear();check(!cache.read(100,frame.actions,3,anchor,3,&result),"clear invalidates");
    cache.publish(frame,0,100);check(!cache.read(100,frame.actions,0,anchor,0,&result),"zero generation");
    cache.publish(frame,3,0);check(!cache.read(100,frame.actions,3,anchor,3,&result),"zero original publication");
    frame.frame_id=0;cache.publish(frame,3,100);check(!cache.read(100,frame.actions,3,anchor,3,&result),"missing frame identity");
    frame.frame_id=32;frame.actions.sequence=0;cache.publish(frame,3,100);
    check(!cache.read(100,frame.actions,3,anchor,3,&result),"missing action identity");
    std::printf("rig_snapshot checks=%u failures=%u native_calls=0 xr_calls=0\n",checks,failures);return failures?1:0;
}
