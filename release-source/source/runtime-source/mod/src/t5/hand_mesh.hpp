// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/manual_reload_pose.hpp"
#include <vector>
namespace bovr::t5::hand_mesh {
enum class FilterMode : std::uint8_t {weapon_split,left_hand_only};
// Derived only from a caller-owned model/bone copy. No string_view or native
// pointer survives the call; exact membership/ancestry also keys mesh caches.
struct ArmsLayout {
    unsigned arms{},roots{};
    std::array<std::uint8_t,borrowed_profile::max_profile_bones> parents{},membership{};
    friend bool operator==(const ArmsLayout&,const ArmsLayout&)=default;
};
inline constexpr std::uint8_t left_arm=1,left_hand=2;
inline bool valid(const ArmsLayout& layout) noexcept {
    if(!layout.arms||layout.arms>layout.parents.size()||!layout.roots||layout.roots>layout.arms)return false;
    bool hand=false;
    for(unsigned i=0;i<layout.arms;++i){const auto flags=layout.membership[i];
        if((flags&~3U)||((flags&left_hand)&&!(flags&left_arm))||
           (i<layout.roots?layout.parents[i]!=0:!layout.parents[i]||layout.parents[i]>i))return false;
        hand|=(flags&left_hand)!=0;
    }
    return hand;
}
inline bool copy_arms_layout(const borrowed_profile::Profile& p,unsigned arms,ArmsLayout* output) noexcept {
    if(!output)return false;*output={};
    if(!arms||arms>borrowed_profile::max_profile_bones||p.bone.size()<arms||
       p.bone.size()>borrowed_profile::max_profile_bones||p.model.empty()||
       p.model[0].offset||p.model[0].count!=arms||!p.model[0].root||p.model[0].root>arms)return false;
    ArmsLayout layout{};layout.arms=arms;layout.roots=p.model[0].root;
    int shoulder=-1,wrist=-1,twist=-1;
    for(unsigned i=0;i<arms;++i){const auto& bone=p.bone[i];
        if(bone.name.empty()||bone.name.size()>255||bone.name.find('\0')!=std::string_view::npos||
           (i<layout.roots?bone.parent!=0:!bone.parent||bone.parent>i))return false;
        layout.parents[i]=bone.parent;
        int* semantic=bone.name=="j_shoulder_le"?&shoulder:bone.name=="j_wrist_le"?&wrist:bone.name=="j_wristtwist_le"?&twist:nullptr;
        if(semantic){if(*semantic>=0)return false;*semantic=int(i);}
    }
    if(shoulder<0||wrist<0||twist<0||wrist==shoulder||twist==shoulder||
       !reload_pose::descendant(p,unsigned(wrist),unsigned(shoulder))||
       !reload_pose::descendant(p,unsigned(twist),unsigned(shoulder)))return false;
    for(unsigned i=0;i<arms;++i){
        if(reload_pose::descendant(p,i,unsigned(shoulder)))layout.membership[i]|=left_arm;
        if(i==unsigned(twist)||reload_pose::descendant(p,i,unsigned(wrist)))layout.membership[i]|=left_hand;
    }
    if(!valid(layout))return false;*output=layout;return true;
}
struct PublicationKey {
    std::uintptr_t model{};unsigned row{};std::uint64_t epoch{},generation{};
    FilterMode mode{FilterMode::weapon_split};ArmsLayout layout{};
    friend bool operator==(const PublicationKey&,const PublicationKey&)=default;
};
struct Weights { float arm{},hand{},outside{}; };
// Copied XRigidVertList fields only; its native collision-tree pointer is not
// retained. bone_offset uses the same 64-byte evaluated-bone stride as blends.
struct RigidGroup {std::uint16_t bone_offset{},vertices{},triangle_offset{},triangles{};};
// COD4's split-hand thresholds: retain the native right arm and the left
// wrist/fingers, excluding sleeve vertices with tiny corrective wrist weights.
inline bool filter(const ArmsLayout& layout,FilterMode mode,unsigned vertices,const std::array<unsigned,4>& groups,
    std::span<const std::uint16_t> blend, std::span<const std::uint16_t> indices,
    std::vector<std::uint16_t>* output, unsigned* removed) {
    if(!output||!removed)return false;output->clear();*removed=0;
    if(!valid(layout)||(mode!=FilterMode::weapon_split&&mode!=FilterMode::left_hand_only)||
       !vertices||vertices>8192||indices.empty()||indices.size()%3||indices.size()>49152)return false;
    unsigned count=0,words=0;for(unsigned g=0;g<4;++g){if(groups[g]>vertices)return false;count+=groups[g];words+=groups[g]*(1+2*g);}
    if(count!=vertices||words!=blend.size())return false;
    std::vector<Weights> weights(vertices);unsigned cursor=0,v=0;
    for(unsigned g=0;g<4;++g)for(unsigned i=0;i<groups[g];++i,++v){
        unsigned total=0;for(unsigned j=1;j<=g;++j)total+=blend[cursor+2*j];if(total>65535)return false;
        for(unsigned j=0;j<=g;++j){const auto encoded=blend[cursor+(j?2*j-1:0)];const unsigned b=encoded>>6;
            if((encoded&63)||b>=layout.arms)return false;
            const float w=float(j?blend[cursor+2*j]:65535-total)/65535.f;
            if(layout.membership[b]&left_arm)weights[v].arm+=w;else weights[v].outside+=w;
            if(layout.membership[b]&left_hand)weights[v].hand+=w;
        }cursor+=1+2*g;
    }
    auto result=std::vector<std::uint16_t>(indices.begin(),indices.end());unsigned dropped=0;
    for(unsigned t=0;t<indices.size();t+=3){float arm=0,hand=0,largest=0,outside=0;
        for(unsigned k=0;k<3;++k){if(indices[t+k]>=vertices)return false;const auto w=weights[indices[t+k]];arm+=w.arm;hand+=w.hand;outside+=w.outside;largest=std::max(largest,w.hand);}
        const bool no_glove=hand<.60f||largest<.35f;
        const bool drop=mode==FilterMode::weapon_split?arm>=.50f&&no_glove:no_glove||outside>=.50f;
        if(drop){result[t+1]=result[t+2]=result[t];++dropped;}
    }
    *output=std::move(result);*removed=dropped;return true;
}
inline bool filter(unsigned row,unsigned vertices,const std::array<unsigned,4>& groups,
    std::span<const std::uint16_t> blend,std::span<const std::uint16_t> indices,
    std::vector<std::uint16_t>* output,unsigned* removed) {
    if(!output||!removed)return false;output->clear();*removed=0;
    const auto* p=borrowed_profile::get(row);const auto* shape=borrowed_profile::shape(row);ArmsLayout layout{};
    return p&&shape&&copy_arms_layout(*p,shape->arms,&layout)&&filter(layout,FilterMode::weapon_split,vertices,groups,blend,indices,output,removed);
}
inline bool filter_rigid(const ArmsLayout& layout,FilterMode mode,unsigned vertices,std::span<const RigidGroup> groups,
    std::span<const std::uint16_t> indices,std::vector<std::uint16_t>* output,unsigned* removed) {
    if(!output||!removed)return false;output->clear();*removed=0;
    if(!vertices||vertices>8192||groups.empty()||groups.size()>borrowed_profile::max_profile_bones||
       indices.empty()||indices.size()%3||indices.size()>49152)return false;
    std::vector<std::uint16_t> blend;blend.reserve(vertices);unsigned triangle_cursor=0;
    for(const auto& group:groups){
        if(!group.vertices||!group.triangles||group.vertices>vertices-blend.size()||
           group.triangle_offset!=triangle_cursor||group.triangles>indices.size()/3-triangle_cursor)return false;
        const auto first_vertex=blend.size(),end_vertex=first_vertex+group.vertices;
        // A native rigid list transforms consecutive vertex groups. Its copied
        // triangle range must refer only to those vertices before it can share
        // the existing whole-surface index replacement.
        for(unsigned i=0;i<unsigned(group.triangles)*3;++i){const auto index=indices[triangle_cursor*3+i];
            if(index<first_vertex||index>=end_vertex)return false;}
        blend.insert(blend.end(),group.vertices,group.bone_offset);triangle_cursor+=group.triangles;
    }
    if(blend.size()!=vertices)return false;
    // Some retail rigid surfaces retain exporter padding after the final
    // XRigidVertList. Admit only an already-degenerate, in-bounds suffix;
    // list offsets above still reject any uncovered interior triangle.
    for(unsigned t=triangle_cursor;t<indices.size()/3;++t){const auto index=indices[t*3];
        if(index>=vertices||indices[t*3+1]!=index||indices[t*3+2]!=index)return false;}
    // A rigid vertex has one full native bone weight. Reuse the accepted glove
    // thresholds: right arm and wrist/watch pieces remain, left sleeve vanishes.
    return filter(layout,mode,vertices,{vertices,0,0,0},blend,indices,output,removed);
}
inline bool filter_rigid(unsigned row,unsigned vertices,std::span<const RigidGroup> groups,
    std::span<const std::uint16_t> indices,std::vector<std::uint16_t>* output,unsigned* removed) {
    if(!output||!removed)return false;output->clear();*removed=0;
    const auto* p=borrowed_profile::get(row);const auto* shape=borrowed_profile::shape(row);ArmsLayout layout{};
    return p&&shape&&copy_arms_layout(*p,shape->arms,&layout)&&filter_rigid(layout,FilterMode::weapon_split,vertices,groups,indices,output,removed);
}
}
