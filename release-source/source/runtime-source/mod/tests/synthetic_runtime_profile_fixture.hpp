// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Independently authored test skeletons. These values exercise runtime profile
// admission without embedding names, transforms, or payloads copied from a game.
#include "t5/runtime_profile_catalog.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace synthetic_profile_fixture {
namespace input=bovr::input;
namespace profile=bovr::t5::borrowed_profile;

inline std::array<std::uint32_t,8> bind(float x=0,float y=0,float z=0) {
    return {0,0,0,std::bit_cast<std::uint32_t>(1.0F),std::bit_cast<std::uint32_t>(x),
        std::bit_cast<std::uint32_t>(y),std::bit_cast<std::uint32_t>(z),std::bit_cast<std::uint32_t>(2.0F)};
}

inline input::ProfileBoneCopy bone(std::string name,unsigned id,unsigned parent,
                                    float x=0,float y=0,float z=0) {
    return {{std::move(name),true,true},static_cast<std::uint16_t>(id),1,
        static_cast<std::uint16_t>(parent),bind(x,y,z)};
}

inline input::ProfileModelCopy arms(unsigned variant=0) {
    input::ProfileModelCopy value{{"viewmodel_fixture_arms_"+std::to_string(variant),true,true},1,{}};
    value.bones.reserve(72);
    for(unsigned i=0;i<72;++i)value.bones.push_back(bone("fixture_arm_bone_"+std::to_string(i),i+1,i?1:0));
    value.bones[0].name.bytes="tag_view";
    value.bones[5].name.bytes="j_shoulder_le";
    value.bones[9].name.bytes="tag_weapon";
    value.bones[10].name.bytes="tag_weapon1";
    value.bones[21].name.bytes="j_wrist_le";value.bones[21].bind_bits=bind();
    value.bones[22].name.bytes="j_wrist_ri";
    value.bones[23].name.bytes="j_wristtwist_le";
    value.bones[25].name.bytes="j_index_le_0";value.bones[25].bind_bits=bind(1.0F,-.3F,0);
    value.bones[27].name.bytes="j_mid_le_0";value.bones[27].bind_bits=bind(1.0F,-.1F,0);
    value.bones[33].name.bytes="j_thumb_le_0";value.bones[33].bind_bits=bind(0,1.0F,0);
    value.bones[42].name.bytes="j_pinky_le_0";value.bones[42].bind_bits=bind(1.0F,.3F,0);
    value.bones[44].name.bytes="j_ring_le_0";value.bones[44].bind_bits=bind(1.0F,.1F,0);
    return value;
}

inline input::ProfileModelCopy gun(unsigned variant=0,bool multi_root=false) {
    input::ProfileModelCopy value{{"fixture_weapon_model_"+std::to_string(variant),static_cast<bool>(true),true},
        static_cast<std::uint16_t>(multi_root?2:1),{}};
    value.bones.reserve(10);
    for(unsigned i=0;i<10;++i)value.bones.push_back(bone("fixture_gun_bone_"+std::to_string(i),73+i,i,float(i),0,0));
    value.bones[0].name.bytes="j_gun";
    value.bones[1].name.bytes=multi_root?"fixture_attachment_root":"j_bolt";
    value.bones[1].parent_delta=multi_root?0:1;
    value.bones[2].name.bytes="j_pump";
    value.bones[4].name.bytes="tag_clip";
    value.bones[5].name.bytes="tag_ext_clip";
    value.bones[6].name.bytes="tag_flash";value.bones[6].bind_bits=bind(10.0F+float(variant),0,0);
    value.bones[7].name.bytes="tag_suppressor";
    value.bones[8].name.bytes="tag_iron_sights";
    value.bones[9].name.bytes="tag_upgraded_sights";
    return value;
}

struct Fixture {
    input::ProfileObservationCopies copies;
    input::CanonicalProfileIdentity canonical;
    std::vector<std::uint8_t> parents{255,9};
    std::vector<std::uint8_t> duplicates=std::vector<std::uint8_t>(21);
    unsigned weapon{4},total{82};

    explicit Fixture(unsigned variant=0,bool multi_root=false) {
        weapon=(variant==3||variant==5)?42U:4U;
        copies.image_sha256=input::profile_sp_image_sha256;
        copies.before_stamp={1,2,100,3};copies.after_stamp={1,2,101,3};
        copies.rechecks={true,true,true,true,true,true};
        copies.before={arms(variant),gun(variant,multi_root)};
        copies.selected_model=1;copies.selected_local_bone=6;
        recanonical();
    }
    void recanonical() {
        copies.after=copies.before;canonical={};
        (void)input::canonical_profile_identity(copies,3,2,101,&canonical);
        total=0;for(const auto& model:copies.after)total+=static_cast<unsigned>(model.bones.size());
    }
    profile::AssemblyEvidence assembly() const {return {parents,duplicates,total};}
    unsigned enroll(profile::CandidateStatus* status=nullptr) const {
        return profile::register_runtime_profile(weapon,copies,canonical,assembly(),status);
    }
};

inline const std::array<Fixture,8>& fixtures() {
    static const std::array<Fixture,8> value{Fixture{0},Fixture{1},Fixture{2},Fixture{3},
        Fixture{4},Fixture{5},Fixture{6},Fixture{7}};
    return value;
}
inline const std::array<unsigned,8>& rows() {
    static const std::array<unsigned,8> value=[] {
        std::array<unsigned,8> result{};
        const auto& all=fixtures();for(unsigned i=0;i<result.size();++i)result[i]=all[i].enroll();
        return result;
    }();
    return value;
}
inline unsigned row(unsigned variant) {return rows()[variant%rows().size()];}
inline const Fixture& fixture(unsigned variant) {return fixtures()[variant%fixtures().size()];}

inline input::ProfileModelCopy multi_root_gun(unsigned variant=0) {return gun(100+variant,true);}
}
