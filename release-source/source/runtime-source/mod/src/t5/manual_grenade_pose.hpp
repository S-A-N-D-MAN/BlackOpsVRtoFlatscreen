// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/manual_reload_pose.hpp"

namespace bovr::t5::grenade_pose {
namespace a=animation_detail;
namespace b=borrowed_profile;
// Pose only the support glove of the already validated primary viewmodel.  The
// gun, attachments, dominant arm and their evaluated matrices remain byte-for-
// byte native; the standalone renderer supplies the grenade model separately.
inline bool pose_primary_hand(const b::Profile& profile,unsigned arms,const a::Transform& hand,
    std::span<const a::NativeMat32> original,reload_pose::Result* output) noexcept {
    if(!output)return false;*output={};
    if(profile.model.size()<2||profile.model.size()>b::max_profile_models||
       profile.bone.size()!=original.size()||original.size()>b::max_profile_bones||
       !arms||arms>=original.size()||profile.model[0].offset||profile.model[0].count!=arms||
       !profile.model[0].root||profile.model[0].root>arms||!a::finite(hand.origin)||!a::rigid(hand.axis))return false;
    unsigned total=0;
    for(const auto& model:profile.model){
        if(model.offset!=total||!model.count||model.count>original.size()-total||
           !model.root||model.root>model.count)return false;
        for(unsigned j=0;j<model.count;++j){const auto delta=profile.bone[total+j].parent;
            if(j<model.root?delta!=0:(!delta||delta>j))return false;}
        total+=model.count;
    }
    if(total!=original.size())return false;
    for(const auto* name:{"j_shoulder_le","j_wrist_le","j_thumb_le_0","j_index_le_0","j_mid_le_0","j_ring_le_0","j_pinky_le_0"}){
        unsigned matches=0;
        for(unsigned i=0;i<profile.bone.size();++i)if(profile.bone[i].name==name){if(i>=arms)return false;++matches;}
        if(matches!=1)return false;
    }
    reload_pose::Result result{};std::copy(original.begin(),original.end(),result.matrices.begin());
    for(const auto& matrix:original){a::Transform checked{};if(!a::convert_native(matrix,&checked))return false;}
    std::array<a::NativeMat32,b::max_profile_bones> bind{};
    for(unsigned i=0;i<arms;++i){std::memcpy(&bind[i],profile.bone[i].bind.data(),32);
        a::Transform checked{};if(!a::convert_native(bind[i],&checked))return false;}
    const b::Profile arms_profile{{profile.model.data(),1},{profile.bone.data(),arms}};
    const int shoulder=reload_pose::bone(arms_profile,"j_shoulder_le");a::Transform palm{};
    if(shoulder<0||unsigned(shoulder)>=arms||!reload_pose::palm(arms_profile,{bind.data(),arms},&palm))return false;
    for(unsigned i=0;i<arms;++i)if(reload_pose::descendant(arms_profile,i,unsigned(shoulder))){
        a::Transform current{};if(!a::convert_native(bind[i],&current))return false;
        const auto target=a::compose(hand,a::relative(palm,current));
        if(!a::finite(target.origin)||!a::rigid(target.axis))return false;
        result.matrices[i]=reload_pose::matrix(target);result.changed[i]=true;++result.count;
    }
    if(!result.count)return false;
    for(unsigned i=arms;i<original.size();++i)if(result.changed[i]||std::memcmp(&result.matrices[i],&original[i],32))return false;
    result.hand=true;*output=result;return true;
}
// Copied values only. Native allocation, model identity and lock ownership are
// independently proved by the normal-clear adapter before this result is used.
inline bool pose(const b::Profile& profile,unsigned arms,const a::Transform& hand,bool holding,
    std::span<const a::NativeMat32> original,reload_pose::Result* output) noexcept {
    if(!output)return false;*output={};
    if(profile.model.size()!=2||profile.bone.size()!=original.size()||original.size()>b::max_profile_bones||
       !arms||arms>=original.size()||profile.model[0].offset||profile.model[0].count!=arms||
       profile.model[1].offset!=arms||profile.model[1].count!=original.size()-arms||
       !profile.model[1].root||profile.model[1].root>profile.model[1].count||
       !a::finite(hand.origin)||!a::rigid(hand.axis))return false;
    for(const auto& model:profile.model){
        if(!model.root||model.root>model.count)return false;
        for(unsigned j=0;j<model.count;++j){const auto delta=profile.bone[model.offset+j].parent;
            if(j<model.root?delta!=0:(!delta||delta>j))return false;}
    }
    reload_pose::Result result{};std::copy(original.begin(),original.end(),result.matrices.begin());
    for(const auto& matrix:original){a::Transform checked{};if(!a::convert_native(matrix,&checked))return false;}
    if(!holding){
        // A release ticket still belongs to this exact current viewmodel. Move
        // its complete evaluated assembly a finite 416 m below its former root;
        // never write zero quaternions, scale, native hide bits or world entities.
        a::Transform root{};if(!a::convert_native(original[0],&root))return false;
        const auto target=a::add(hand.origin,{0,0,-16384.f});
        const auto delta=a::sub(target,root.origin);
        for(unsigned i=0;i<original.size();++i){
            result.matrices[i].origin=a::add(original[i].origin,delta);
            if(!a::finite(result.matrices[i].origin))return false;
            result.changed[i]=true;++result.count;
        }
        *output=result;return true;
    }
    // The canonical open glove cannot replay the native reach/pull animation.
    // All fingers retain their authored geometry and the same anatomical palm
    // convention already used by the free support hand.
    std::array<a::NativeMat32,b::max_profile_bones> bind{};
    for(unsigned i=0;i<profile.bone.size();++i){
        std::memcpy(&bind[i],profile.bone[i].bind.data(),32);
        a::Transform checked{};if(!a::convert_native(bind[i],&checked))return false;
    }
    for(const auto* name:{"j_shoulder_le","j_wrist_le","j_thumb_le_0","j_index_le_0","j_mid_le_0","j_ring_le_0","j_pinky_le_0"}){
        unsigned matches=0;
        for(unsigned i=0;i<profile.bone.size();++i)if(profile.bone[i].name==name){if(i>=arms)return false;++matches;}
        if(matches!=1)return false;
    }
    const int shoulder=reload_pose::bone(profile,"j_shoulder_le");a::Transform palm{},root{};
    if(shoulder<0||unsigned(shoulder)>=arms||
       !reload_pose::palm(profile,{bind.data(),arms},&palm)||!a::convert_native(bind[arms],&root))return false;
    const auto move=[&](unsigned i,const a::Transform& from){
        a::Transform value{};if(!a::convert_native(bind[i],&value))return false;
        const auto target=a::compose(hand,a::relative(from,value));
        if(!a::finite(target.origin)||!a::rigid(target.axis))return false;
        result.matrices[i]=reload_pose::matrix(target);result.changed[i]=true;++result.count;return true;};
    for(unsigned i=0;i<arms;++i)if(reload_pose::descendant(profile,i,unsigned(shoulder))&&!move(i,palm))return false;
    // Grenade root, pin and lever retain their current authored bind geometry.
    // No animation time or native fuse state participates in this visual edit.
    for(unsigned i=arms;i<original.size();++i)if(!move(i,root))return false;
    result.hand=result.device=true;*output=result;return true;
}
}
