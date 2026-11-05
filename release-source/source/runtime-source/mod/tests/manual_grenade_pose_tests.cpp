// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_grenade_pose.hpp"
#include "t5/manual_grenade_visual_pose.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace a=bovr::t5::animation_detail;namespace b=bovr::t5::borrowed_profile;
namespace r=bovr::t5::reload_pose;namespace g=bovr::t5::grenade_pose;
unsigned checks{};
constexpr a::Axis identity_axis{{1,0,0},{0,1,0},{0,0,1}};
void check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}

void visual_hand_continuity(unsigned row,std::span<const a::NativeMat32> native){
    namespace input=bovr::input;namespace xr=wawvr::xr;
    const auto& profile=*b::get(row);const auto& shape=*b::shape(row);
    const std::array<xr::Quaternionf,3> rotations{{{},
        {0,std::sin(.37F),0,std::cos(.37F)},
        xr::Multiply({std::sin(-.29F),0,0,std::cos(-.29F)},
            xr::Multiply({0,std::sin(.43F),0,std::cos(.43F)},{0,0,std::sin(.51F),std::cos(.51F)}))}};
    for(const auto& rotation:rotations)for(unsigned fault=0;fault<9;++fault){
        input::ReservedWeaponFrame reserved{};input::BorrowedPlacementValues placement{};input::manual::Output reload{};
        reserved.identity={2,9,3,4};reserved.anchor_generation=2;reserved.published_ms=1000;reserved.exclusively_reserved_for_scene=true;
        reserved.tracking_anchor={{0,std::sin(-.23F),0,std::cos(-.23F)},{.4F,1.7F,-.2F}};
        auto& prediction=reserved.prediction;prediction.frame_id=9;
        prediction.should_render=prediction.views_valid=prediction.actions.focused=true;
        auto& support=prediction.actions.hands[0];auto& grip=support.grip;
        grip.active=grip.position_valid=grip.position_tracked=grip.orientation_valid=grip.orientation_tracked=true;
        grip.pose={rotation,{-.2F,1.3F,-.4F}};
        support.palm=grip;support.palm.pose.position={-.18F,1.34F,-.47F};
        support.palm.pose.orientation=xr::Multiply(rotation,{std::sin(.18F),0,0,std::cos(.18F)});
        switch(fault){
        case 1:support.palm.active=false;break;
        case 2:support.palm.position_valid=false;break;
        case 3:support.palm.position_tracked=false;break;
        case 4:support.palm.orientation_valid=false;break;
        case 5:support.palm.orientation_tracked=false;break;
        case 6:support.palm.pose.position.x=std::numeric_limits<float>::quiet_NaN();break;
        case 7:support.palm.pose.orientation.w=std::numeric_limits<float>::quiet_NaN();break;
        case 8:support.palm.pose.orientation={0,0,0,2};break;
        }
        placement.valid=true;placement.identity=reserved.identity;
        check(a::pose_transform({{31,-47,53},{0,67,0}},&placement.body),"rotated body is rigid");
        reload.supported=true;reload.weapon=shape.weapon;reload.epoch=7;reload.generation=2;reload.updated_ms=1000;
        r::Frame free_frame{};
        check(r::make_frame(reserved,placement,reload,shape.weapon,7,1016,&free_frame)&&free_frame.free_hand&&
            free_frame.palm_surface==(fault==0),"normal free glove validates the same optional palm surface");
        const auto target=bovr::t5::grenade_visual_pose::hand(support,reserved.tracking_anchor,placement.body);
        r::Result free_pose{},held_pose{};
        check(r::pose(row,free_frame,native,&free_pose)&&
            g::pose_primary_hand(profile,shape.arms,target,native,&held_pose),"free and held glove accept the same tracked pose");
        for(unsigned i=0;i<shape.total;++i){
            check(free_pose.changed[i]==held_pose.changed[i],"grenade transition preserves free glove bone selection");
            if(held_pose.changed[i]){
                a::Transform before{},after{};
                check(a::convert_native(free_pose.matrices[i],&before)&&a::convert_native(held_pose.matrices[i],&after)&&
                    a::distance(before.origin,after.origin)<.006F&&a::axis_error(before.axis,after.axis)<.001F,
                    "grab keeps every glove bone aligned with the normal free hand under controller and body rotation");
            }else check(!std::memcmp(&held_pose.matrices[i],&native[i],sizeof(a::NativeMat32)),
                "grenade palm normalization leaves every unaffected bone native");
        }
        const auto local=xr::OpenXrPoseToIwRelative(grip.pose,reserved.tracking_anchor,xr::kIwUnitsPerMeter);
        const auto raw_grip=a::compose(placement.body,{local.position,local.axis});
        if(fault)check(a::distance(target.origin,raw_grip.origin)==0&&a::axis_error(target.axis,raw_grip.axis)==0,
            "unavailable or invalid palm retains the exact prior grip fallback");
        else check(a::distance(target.origin,raw_grip.origin)>1.F&&a::axis_error(target.axis,raw_grip.axis)>.1F,
            "valid distinct palm exercises corrected position and orientation instead of grip fallback");
    }
}

int main(){
    const auto row=synthetic_profile_fixture::row(0);const auto& primary=*b::get(row);const auto& shape=*b::shape(row);
    std::vector<b::Bone> bones(primary.bone.begin(),primary.bone.begin()+shape.arms);
    bones.push_back({"fixture_grenade_root",0,synthetic_profile_fixture::bind()});
    bones.push_back({"fixture_pin",1,synthetic_profile_fixture::bind(.2F,.1F,0)});
    bones.push_back({"fixture_lever",1,synthetic_profile_fixture::bind(-.1F,.2F,.1F)});
    std::array<b::Model,2> models{{primary.model[0],{"fixture_grenade_model",3,1,shape.arms}}};
    const b::Profile profile{models,bones};std::vector<a::NativeMat32> native(bones.size());
    const a::Transform initial{{-1200,480,-79},{{0,1,0},{-1,0,0},{0,0,1}}};
    for(unsigned i=0;i<bones.size();++i){a::NativeMat32 raw{};a::Transform bind{};std::memcpy(&raw,bones[i].bind.data(),32);
        check(a::convert_native(raw,&bind),"authored bind is rigid");native[i]=r::matrix(a::compose(initial,bind));}
    const std::array<a::Transform,2> targets{{{{-1210,490,-70},identity_axis},{{25,35,45},{{0,1,0},{0,0,1},{1,0,0}}}}};
    for(const auto& hand:targets){r::Result posed{};
        check(g::pose(profile,shape.arms,hand,true,native,&posed)&&posed.hand&&posed.device,"grenade and glove follow controller");
        a::Transform palm{},grenade{};check(r::palm(profile,{posed.matrices.data(),bones.size()},&palm)&&a::convert_native(posed.matrices[shape.arms],&grenade),"posed palm and grenade remain rigid");
        check(a::distance(palm.origin,hand.origin)<.01F&&a::axis_error(palm.axis,hand.axis)<.01F,"controller reaches anatomical palm");
        check(a::distance(grenade.origin,hand.origin)<.001F&&a::axis_error(grenade.axis,hand.axis)<.001F,"grenade root follows same controller");
        for(unsigned i=shape.arms+1;i<bones.size();++i){a::NativeMat32 raw{};a::Transform authored{},moved{};std::memcpy(&raw,bones[i].bind.data(),32);
            check(a::convert_native(raw,&authored)&&a::convert_native(posed.matrices[i],&moved),"grenade part stays rigid");
            const auto relative=a::relative(grenade,moved);check(a::distance(relative.origin,authored.origin)<.006F&&a::axis_error(relative.axis,authored.axis)<.001F,
                "pin and lever preserve authored offsets");}
        r::Result released{};check(g::pose(profile,shape.arms,hand,false,native,&released)&&released.count==bones.size()&&!released.device,"release hides complete owned visual");
        for(unsigned i=0;i<bones.size();++i){a::Transform valid{};check(a::convert_native(released.matrices[i],&valid)&&released.changed[i],"released assembly stays finite and rigid");}
    }

    std::vector<a::NativeMat32> primary_native(shape.total);
    for(unsigned i=0;i<shape.total;++i){a::NativeMat32 raw{};a::Transform bind{};std::memcpy(&raw,primary.bone[i].bind.data(),32);
        check(a::convert_native(raw,&bind),"primary authored bind is rigid");primary_native[i]=r::matrix(a::compose(initial,bind));}
    visual_hand_continuity(row,primary_native);
    r::Result primary_pose{};const auto hand=targets[0];
    check(g::pose_primary_hand(primary,shape.arms,hand,primary_native,&primary_pose)&&primary_pose.hand&&!primary_pose.device,"standalone grenade uses primary support glove");
    for(unsigned i=shape.arms;i<shape.total;++i)check(!primary_pose.changed[i]&&!std::memcmp(&primary_pose.matrices[i],&primary_native[i],32),"primary gun and muzzle remain native");
    auto invalid=hand;invalid.origin.x=std::numeric_limits<float>::quiet_NaN();
    check(!g::pose(profile,shape.arms,invalid,true,native,&primary_pose)&&!g::pose_primary_hand(primary,shape.arms,invalid,primary_native,&primary_pose),"nonfinite hand target rejects");
    check(!g::pose(profile,shape.arms-1,hand,true,native,&primary_pose),"wrong arms boundary rejects");
    auto broken=bones;broken[shape.arms+2].parent=3;check(!g::pose({models,broken},shape.arms,hand,true,native,&primary_pose),"cross-model parent rejects");
    std::printf("manual_grenade_pose_checks=%u failures=0\n",checks);return 0;
}
