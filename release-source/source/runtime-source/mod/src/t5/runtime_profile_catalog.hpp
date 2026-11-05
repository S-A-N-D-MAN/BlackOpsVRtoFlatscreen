// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/borrowed_profile_shape.hpp"
#include "input/profile_identity.hpp"
#include "t5/weapon_animation_math.hpp"
#include <memory>
#include <bit>
#include <cstring>
#include <string>
#include <vector>

namespace bovr::t5::borrowed_profile {
// Callers construct this from protected copies, never native addresses. These
// bytes supplement canonical v1, which intentionally excludes DObj assembly.
struct AssemblyEvidence {
    std::span<const std::uint8_t> model_parents,duplicate_data;
    unsigned total_bones{};
};
enum class CandidateStatus : unsigned {
    accepted,bounds,assembly,canonical,arms_root,gun_root,hand_chain,
    attachment,barrel,bind_geometry,palm_geometry,barrel_geometry,capacity,allocation
};
struct CatalogWorkCounts {unsigned canonical_builds{},canonical_verifications{},bind_conversions{};};

// Same bounded duplicate contract as profile_detail::valid_assembly. Keeping
// this value-only avoids linking a native reader into command/profile tests.
inline bool valid_runtime_assembly(AssemblyEvidence a,std::span<const input::ProfileModelCopy> model_copies) noexcept {
    if(model_copies.empty()||model_copies.size()>max_profile_models||a.model_parents.size()!=model_copies.size()||
       !a.total_bones||a.total_bones>max_profile_bones||a.duplicate_data.size()<21||
       a.duplicate_data.size()>255||!(a.duplicate_data.size()&1)||a.duplicate_data.back())return false;
    unsigned start=0;
    for(std::size_t i=0;i<model_copies.size();++i){
        if(model_copies[i].bones.empty()||model_copies[i].bones.size()>max_profile_bones-start||
           (a.model_parents[i]!=0xff&&a.model_parents[i]>=start))return false;
        start+=static_cast<unsigned>(model_copies[i].bones.size());
    }
    if(start!=a.total_bones)return false;
    std::array<std::uint32_t,5> expected{};
    int previous=-1;
    for(std::size_t i=20;i+1<a.duplicate_data.size();i+=2){
        const unsigned child=a.duplicate_data[i],parent=a.duplicate_data[i+1];
        if(!child||!parent||child>a.total_bones||parent>=child||static_cast<int>(child)<=previous)return false;
        previous=static_cast<int>(child);const auto ordinal=child-1;
        expected[ordinal/32]|=0x80000000U>>(ordinal%32);
    }
    for(unsigned i=0;i<5;++i){
        std::uint32_t actual{};
        for(unsigned j=0;j<4;++j)actual|=static_cast<std::uint32_t>(a.duplicate_data[i*4+j])<<(j*8);
        if(actual!=expected[i])return false;
    }
    return true;
}

namespace catalog_detail {
struct Entry {
    std::vector<std::string> model_names,bone_names;
    std::vector<Model> models;
    std::vector<Bone> bones;
    std::vector<std::uint8_t> parents,duplicates;
    input::CanonicalProfileIdentity canonical;
    Profile profile{};
};
// Entries allocate only after acceptance. Neither entries nor their vectors are
// modified after publication, and slots are never recycled during the process.
inline std::array<std::unique_ptr<Entry>,max_runtime_profiles> entries{};

inline bool canonical_equal(const input::CanonicalProfileIdentity& a,const input::CanonicalProfileIdentity& b) noexcept {
    return a.schema==b.schema&&a.selected_model==b.selected_model&&a.selected_local_bone==b.selected_local_bone&&
        a.model_payloads==b.model_payloads&&a.assembly_payload==b.assembly_payload;
}
inline unsigned unique_bone(const input::ProfileModelCopy& m,std::string_view name) noexcept {
    unsigned found=invalid_runtime_row;
    for(unsigned i=0;i<m.bones.size();++i)if(m.bones[i].name.bytes==name){
        if(found!=invalid_runtime_row)return invalid_runtime_row;found=i;
    }
    return found;
}
inline bool descendant(const input::ProfileModelCopy& m,unsigned child,unsigned parent) noexcept {
    if(child>=m.bones.size()||parent>=m.bones.size())return false;
    for(unsigned i=0;i<m.bones.size();++i){
        if(child==parent)return true;
        if(child<m.root_count||!m.bones[child].parent_delta||m.bones[child].parent_delta>child)return false;
        child-=m.bones[child].parent_delta;
    }
    return false;
}
// Fresh copied evidence is always verified before any immutable catalog reuse.
// Canonical v1 contains every model/bone name, root/count, parent delta and raw
// bind bit; DObj parents/duplicates remain separately exact assembly evidence.
inline bool copied_candidate(unsigned weapon,const input::ProfileObservationCopies& copies,
    const input::CanonicalProfileIdentity& canonical,AssemblyEvidence assembly,
    CandidateStatus* diagnostic=nullptr,bool optimized=true,CatalogWorkCounts* counts=nullptr) noexcept {
    if(diagnostic)*diagnostic=CandidateStatus::accepted;
    const auto reject=[&](CandidateStatus reason){if(diagnostic)*diagnostic=reason;return false;};
    if(!weapon||weapon>=256||copies.before.size()<2||copies.before.size()>max_profile_models||
       copies.after.size()!=copies.before.size()||canonical.schema!=input::profile_identity_schema||
       canonical.selected_model!=1||canonical.assembly_payload.size()>input::profile_max_payload_bytes||
       canonical.model_payloads.size()!=copies.after.size())return reject(CandidateStatus::bounds);
    if(!valid_runtime_assembly(assembly,copies.after))return reject(CandidateStatus::assembly);
    const auto& stamp=copies.after_stamp;
    if(optimized){
        if(counts)++counts->canonical_verifications;
        if(input::verify_canonical_profile_identity(copies,stamp.native_thread,stamp.native_epoch,stamp.observed_ms,canonical)!=
           input::ProfileIdentityStatus::accepted)return reject(CandidateStatus::canonical);
    }else{
        input::CanonicalProfileIdentity verified;if(counts)++counts->canonical_builds;
        if(input::canonical_profile_identity(copies,stamp.native_thread,stamp.native_epoch,stamp.observed_ms,&verified,false)!=
           input::ProfileIdentityStatus::accepted||!canonical_equal(verified,canonical))return reject(CandidateStatus::canonical);
    }
    return true;
}
// Called only after copied_candidate succeeds. These transforms depend solely
// on the exact canonical and assembly values, never native IDs or addresses.
inline bool geometry_candidate(unsigned weapon,const input::ProfileObservationCopies& copies,
    const input::CanonicalProfileIdentity& canonical,AssemblyEvidence assembly,Shape& out,
    CandidateStatus* diagnostic=nullptr,CatalogWorkCounts* counts=nullptr) noexcept {
    out={};
    const auto reject=[&](CandidateStatus reason){if(diagnostic)*diagnostic=reason;return false;};
    const auto& arms=copies.after[0];const auto& gun=copies.after[1];
    if(!arms.name.bytes.starts_with("viewmodel_")||arms.root_count!=1||arms.bones[0].name.bytes!="tag_view")
        return reject(CandidateStatus::arms_root);
    // Native XModels can have additional independent roots (M14 and AK47
    // include an attachment root). The complete validated forest is retained;
    // only the aimed barrel must belong to the unique primary j_gun root.
    if(gun.bones[0].name.bytes!="j_gun"||unique_bone(gun,"j_gun")!=0)
        return reject(CandidateStatus::gun_root);
    const auto attachment=unique_bone(arms,"tag_weapon");
    const auto shoulder=unique_bone(arms,"j_shoulder_le"),wrist=unique_bone(arms,"j_wrist_le");
    if(attachment==invalid_runtime_row||!descendant(arms,wrist,shoulder)||
       unique_bone(arms,"j_wrist_ri")==invalid_runtime_row||
       !descendant(arms,unique_bone(arms,"j_wristtwist_le"),shoulder))return reject(CandidateStatus::hand_chain);
    for(const auto name:{"j_thumb_le_0","j_index_le_0","j_mid_le_0","j_ring_le_0","j_pinky_le_0"})
        if(!descendant(arms,unique_bone(arms,name),wrist))return reject(CandidateStatus::hand_chain);
    const auto flash=unique_bone(gun,"tag_flash");
    if(flash!=canonical.selected_local_bone||!flash||!descendant(gun,flash,0))return reject(CandidateStatus::barrel);
    if(assembly.model_parents[0]!=255||assembly.model_parents[1]!=attachment)return reject(CandidateStatus::attachment);
    // Bind records are absolute model-space transforms. A hierarchy need not
    // put tag_flash directly below j_gun; preserve its complete native chain.
    animation_detail::Transform root{},barrel{};
    for(const auto& model:copies.after)for(const auto& bone:model.bones){
        animation_detail::NativeMat32 raw{};std::memcpy(&raw,bone.bind_bits.data(),sizeof(raw));
        animation_detail::Transform converted{};
        if(counts)++counts->bind_conversions;
        if(!animation_detail::convert_native(raw,&converted))return reject(CandidateStatus::bind_geometry);
    }
    const auto arm_origin=[&](unsigned index){const auto& bits=arms.bones[index].bind_bits;
        return animation_detail::Vec{std::bit_cast<float>(bits[4]),std::bit_cast<float>(bits[5]),std::bit_cast<float>(bits[6])};};
    animation_detail::Vec center{};
    for(const auto name:{"j_index_le_0","j_mid_le_0","j_ring_le_0","j_pinky_le_0"})
        center=animation_detail::add(center,arm_origin(unique_bone(arms,name)));
    center={center.x*.25F,center.y*.25F,center.z*.25F};
    const auto forward=animation_detail::sub(center,arm_origin(wrist));
    const auto thumb=animation_detail::sub(arm_origin(unique_bone(arms,"j_thumb_le_0")),arm_origin(wrist));
    const auto normal=animation_detail::cross(forward,thumb);
    if(!animation_detail::finite(normal)||!std::isfinite(animation_detail::dot(forward,forward))||
       !std::isfinite(animation_detail::dot(normal,normal))||
       animation_detail::dot(forward,forward)<1.e-10F||animation_detail::dot(normal,normal)<1.e-10F)return reject(CandidateStatus::palm_geometry);
    animation_detail::NativeMat32 root_raw{},flash_raw{};
    std::memcpy(&root_raw,gun.bones[0].bind_bits.data(),sizeof(root_raw));
    std::memcpy(&flash_raw,gun.bones[flash].bind_bits.data(),sizeof(flash_raw));
    if(counts)counts->bind_conversions+=2;
    if(!animation_detail::convert_native(root_raw,&root)||!animation_detail::convert_native(flash_raw,&barrel))return reject(CandidateStatus::barrel_geometry);
    const auto relative=animation_detail::relative(root,barrel);
    if(!animation_detail::finite(relative.origin)||!animation_detail::rigid(relative.axis)||
       !std::isfinite(animation_detail::distance(root.origin,barrel.origin)))return reject(CandidateStatus::barrel_geometry);
    const auto arms_count=static_cast<unsigned>(arms.bones.size());
    out={arms_count,arms_count,arms_count+flash,assembly.total_bones,attachment,
        static_cast<unsigned>(gun.bones.size()),flash,weapon,runtime_revision};
    return true;
}
inline bool candidate(unsigned weapon,const input::ProfileObservationCopies& copies,
    const input::CanonicalProfileIdentity& canonical,AssemblyEvidence assembly,Shape& out,
    CandidateStatus* diagnostic=nullptr,bool optimized=true,CatalogWorkCounts* counts=nullptr) noexcept {
    out={};
    return copied_candidate(weapon,copies,canonical,assembly,diagnostic,optimized,counts)&&
        geometry_candidate(weapon,copies,canonical,assembly,out,diagnostic,counts);
}
inline bool assembly_equal(const Entry& e,AssemblyEvidence a) noexcept {
    return e.profile.bone.size()==a.total_bones&&e.parents.size()==a.model_parents.size()&&
        e.duplicates.size()==a.duplicate_data.size()&&
        std::equal(e.parents.begin(),e.parents.end(),a.model_parents.begin())&&
        std::equal(e.duplicates.begin(),e.duplicates.end(),a.duplicate_data.begin());
}
}

// A candidate must already have passed the current native reader/borrow checks.
// Rechecking copied evidence here cannot extend its lifetime or authorize writes.
// Registration remembers asset values only; IDs, reference counts, stamps and
// native addresses are deliberately not retained in a durable catalog entry.
inline unsigned register_runtime_profile(unsigned weapon,const input::ProfileObservationCopies& copies,
    const input::CanonicalProfileIdentity& canonical,AssemblyEvidence assembly,CandidateStatus* diagnostic=nullptr,
    bool optimized=true,CatalogWorkCounts* counts=nullptr) noexcept {
    try {
        if(!catalog_detail::copied_candidate(weapon,copies,canonical,assembly,diagnostic,optimized,counts))return invalid_runtime_row;
        if(optimized){
            std::lock_guard lock(catalog_detail::registration_mutex);
            const auto n=catalog_detail::published.load(std::memory_order_relaxed);
            for(unsigned i=0;i<n;++i){const auto& e=*catalog_detail::entries[i];
                if(catalog_detail::slots[i].shape.weapon==weapon&&catalog_detail::canonical_equal(e.canonical,canonical)&&
                   catalog_detail::assembly_equal(e,assembly))return runtime_row_base+i;
            }
        }
        Shape selected{};
        if(!catalog_detail::geometry_candidate(weapon,copies,canonical,assembly,selected,diagnostic,counts))return invalid_runtime_row;
        // A concurrent publisher may have enrolled these exact values while
        // geometry was derived. Recheck under the original publication lock.
        std::lock_guard lock(catalog_detail::registration_mutex);
        const auto n=catalog_detail::published.load(std::memory_order_relaxed);
        for(unsigned i=0;i<n;++i){const auto& e=*catalog_detail::entries[i];
            if(catalog_detail::slots[i].shape.weapon==weapon&&catalog_detail::canonical_equal(e.canonical,canonical)&&
               catalog_detail::assembly_equal(e,assembly))return runtime_row_base+i;
        }
        if(n==max_runtime_profiles){if(diagnostic)*diagnostic=CandidateStatus::capacity;return invalid_runtime_row;}
        auto entry=std::make_unique<catalog_detail::Entry>();
        entry->canonical=canonical;
        entry->parents.assign(assembly.model_parents.begin(),assembly.model_parents.end());
        entry->duplicates.assign(assembly.duplicate_data.begin(),assembly.duplicate_data.end());
        entry->model_names.reserve(copies.after.size());entry->models.reserve(copies.after.size());
        entry->bone_names.reserve(selected.total);entry->bones.reserve(selected.total);
        for(const auto& model:copies.after){
            entry->model_names.push_back(model.name.bytes);
            entry->models.push_back({entry->model_names.back(),static_cast<unsigned>(model.bones.size()),model.root_count,
                static_cast<unsigned>(entry->bones.size())});
            for(const auto& bone:model.bones){entry->bone_names.push_back(bone.name.bytes);
                entry->bones.push_back({entry->bone_names.back(),static_cast<std::uint8_t>(bone.parent_delta),bone.bind_bits});}
        }
        entry->profile={entry->models,entry->bones,entry->parents,entry->duplicates};
        const auto* profile=&entry->profile;
        catalog_detail::entries[n]=std::move(entry);
        catalog_detail::slots[n]={selected,profile};
        catalog_detail::published.store(n+1,std::memory_order_release);
        return runtime_row_base+n;
    }catch(...){if(diagnostic)*diagnostic=CandidateStatus::allocation;return invalid_runtime_row;}
}

inline bool matches_runtime_record(unsigned row,unsigned weapon,const input::ProfileObservationCopies& copies,
    const input::CanonicalProfileIdentity& canonical,AssemblyEvidence assembly,bool optimized=true,CatalogWorkCounts* counts=nullptr) noexcept {
    try {
        Shape ignored{};
        if(!catalog_detail::candidate(weapon,copies,canonical,assembly,ignored,nullptr,optimized,counts))return false;
        const auto* slot=catalog_detail::runtime(row);
        if(!slot||slot->shape.weapon!=weapon)return false;
        const auto& entry=*catalog_detail::entries[row-runtime_row_base];
        return catalog_detail::canonical_equal(entry.canonical,canonical)&&catalog_detail::assembly_equal(entry,assembly);
    }catch(...){return false;}
}
}
