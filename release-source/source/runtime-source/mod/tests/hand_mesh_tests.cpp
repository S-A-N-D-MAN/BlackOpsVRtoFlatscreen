// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hand_mesh.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace h=bovr::t5::hand_mesh;
namespace b=bovr::t5::borrowed_profile;
unsigned checks{};
void check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}

int main(){
    const auto row=synthetic_profile_fixture::row(0);const auto& profile=*b::get(row);const auto& shape=*b::shape(row);
    h::ArmsLayout layout{};check(h::copy_arms_layout(profile,shape.arms,&layout)&&h::valid(layout),"runtime arms copy creates a valid owned layout");
    check((layout.membership[5]&h::left_arm)&&!(layout.membership[5]&h::left_hand),"shoulder belongs to left arm but not glove");
    check((layout.membership[21]&h::left_hand)&&!layout.membership[0],"wrist is glove and unrelated root stays outside");

    const auto encoded=[](unsigned bone){return static_cast<std::uint16_t>(bone<<6);};
    const std::vector<std::uint16_t> blend{
        encoded(5),encoded(5),encoded(5),
        encoded(21),encoded(21),encoded(21),
        encoded(0),encoded(0),encoded(0)};
    const std::vector<std::uint16_t> indices{0,1,2,3,4,5,6,7,8};
    std::vector<std::uint16_t> output;unsigned removed{};
    check(h::filter(layout,h::FilterMode::weapon_split,9,{9,0,0,0},blend,indices,&output,&removed)&&removed==1,
        "weapon split removes sleeve and keeps glove plus other arm");
    check(output==std::vector<std::uint16_t>({0,0,0,3,4,5,6,7,8}),"weapon split preserves accepted triangle order");
    check(h::filter(layout,h::FilterMode::left_hand_only,9,{9,0,0,0},blend,indices,&output,&removed)&&removed==2,
        "left-hand-only mode removes sleeve and outside arm");
    check(output==std::vector<std::uint16_t>({0,0,0,3,4,5,6,6,6}),"left-hand-only mode keeps glove triangle");

    std::vector<std::uint16_t> weighted;
    for(unsigned i=0;i<3;++i){weighted.push_back(encoded(5));weighted.push_back(encoded(21));weighted.push_back(12000);}
    const std::array<std::uint16_t,3> one{0,1,2};
    check(h::filter(layout,h::FilterMode::weapon_split,3,{0,3,0,0},weighted,one,&output,&removed)&&removed==1,
        "small corrective wrist weights do not turn a sleeve into a glove");
    for(unsigned i=2;i<weighted.size();i+=3)weighted[i]=30000;
    check(h::filter(layout,h::FilterMode::weapon_split,3,{0,3,0,0},weighted,one,&output,&removed)&&removed==0,
        "strong glove weights preserve the triangle");

    const std::array<h::RigidGroup,3> rigid{{{encoded(5),3,0,1},{encoded(21),3,1,1},{encoded(0),3,2,1}}};
    check(h::filter_rigid(layout,h::FilterMode::weapon_split,9,rigid,indices,&output,&removed)&&removed==1,
        "rigid groups use the same sleeve/glove policy");
    check(h::filter_rigid(layout,h::FilterMode::left_hand_only,9,rigid,indices,&output,&removed)&&removed==2,
        "rigid left-hand-only filtering removes outside arm");

    auto bad=layout;bad.membership[21]=h::left_hand;check(!h::valid(bad),"hand membership without arm rejects");
    bad=layout;bad.parents[21]=0;check(!h::valid(bad),"invalid nonroot parent rejects");
    check(!h::filter(h::ArmsLayout{},h::FilterMode::weapon_split,9,{9,0,0,0},blend,indices,&output,&removed)&&output.empty(),
        "unvalidated layout cannot publish indices");
    auto invalid_indices=indices;invalid_indices.back()=9;
    check(!h::filter(layout,h::FilterMode::weapon_split,9,{9,0,0,0},blend,invalid_indices,&output,&removed)&&output.empty(),
        "out-of-range triangle rejects all output");

    const auto owned=[](const b::Profile& source,unsigned arms){auto models=std::vector<b::Model>(source.model.begin(),source.model.end());
        auto bones=std::vector<b::Bone>(source.bone.begin(),source.bone.end());h::ArmsLayout copied{};
        check(h::copy_arms_layout({models,bones},arms,&copied),"temporary caller-owned profile validates");return copied;}(profile,shape.arms);
    check(owned==layout&&h::filter(owned,h::FilterMode::left_hand_only,9,{9,0,0,0},blend,indices,&output,&removed),
        "owned layout survives destruction of temporary profile arrays");
    std::printf("hand_mesh_checks=%u failures=0 native_reads=0 game_writes=0\n",checks);return 0;
}
