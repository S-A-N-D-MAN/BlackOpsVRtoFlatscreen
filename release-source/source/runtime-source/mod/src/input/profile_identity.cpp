// SPDX-License-Identifier: GPL-3.0-only
#include "input/profile_identity.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>

namespace bovr::input {
namespace {
using Status=ProfileIdentityStatus;
bool text_valid(const ProfileTextCopy& text) noexcept {
    if(!text.readable||!text.terminated||text.bytes.empty()||text.bytes.size()>profile_max_name_bytes)return false;
    for(unsigned char value:text.bytes)if(value<0x20||value>0x7e)return false;
    return true;
}
bool geometry_valid(const std::array<std::uint32_t,8>& bits) noexcept {
    std::array<double,8> values{};
    for(unsigned i=0;i<8;++i) {
        const auto value=std::bit_cast<float>(bits[i]);
        if(!std::isfinite(value))return false;
        values[i]=value;
    }
    double squared{};for(unsigned i=0;i<4;++i)squared+=values[i]*values[i];
    // Native conversion uses transWeight rather than assuming a unit quaternion.
    // The product is 2 for a supported normalized conversion; no values change.
    return squared>1e-12&&values[7]>0&&std::fabs(squared*values[7]-2.0)<0.003;
}
Status validate_models(const std::vector<ProfileModelCopy>& models,bool optimized) noexcept {
    if(models.empty()||models.size()>profile_max_models)return Status::count_bound;
    std::size_t total{};
    for(const auto& model:models) {
        if(!text_valid(model.name))return Status::invalid_text;
        if(model.bones.empty()||model.bones.size()>profile_max_bones-total)return Status::count_bound;
        total+=model.bones.size();
        if(!model.root_count||model.root_count>model.bones.size())return Status::invalid_topology;
        for(std::size_t i=0;i<model.bones.size();++i) {
            const auto& bone=model.bones[i];
            if(!text_valid(bone.name))return Status::invalid_text;
            if(!bone.script_id||!bone.observed_ref_count)return Status::invalid_string_entry;
            if(i<model.root_count ? bone.parent_delta!=0 : !bone.parent_delta||bone.parent_delta>i)
                return Status::invalid_topology;
            if(!geometry_valid(bone.bind_bits))return Status::invalid_bind_geometry;
        }
    }
    // A single copied server string ID cannot simultaneously mean two texts.
    // This is a consistency check only; IDs are never serialized.
    if(optimized) {
        std::array<const ProfileBoneCopy*,profile_max_bones> sorted{};unsigned count{};
        for(const auto& model:models)for(const auto& bone:model.bones)sorted[count++]=&bone;
        std::sort(sorted.begin(),sorted.begin()+count,[](const auto* a,const auto* b){return a->script_id<b->script_id;});
        for(unsigned i=1;i<count;++i)if(sorted[i-1]->script_id==sorted[i]->script_id&&
            sorted[i-1]->name.bytes!=sorted[i]->name.bytes)return Status::invalid_string_entry;
        return Status::accepted;
    }
    for(std::size_t m=0;m<models.size();++m)for(std::size_t b=0;b<models[m].bones.size();++b)
        for(std::size_t n=0;n<=m;++n)for(std::size_t c=0;c<models[n].bones.size();++c) {
            if(n==m&&c>=b)break;
            const auto& x=models[m].bones[b];const auto& y=models[n].bones[c];
            if(x.script_id==y.script_id&&x.name.bytes!=y.name.bytes)return Status::invalid_string_entry;
        }
    return Status::accepted;
}
bool same_copies(const std::vector<ProfileModelCopy>& before,const std::vector<ProfileModelCopy>& after) noexcept {
    if(before.size()!=after.size())return false;
    for(std::size_t i=0;i<before.size();++i) {
        const auto& a=before[i];const auto& b=after[i];
        if(a.name!=b.name||a.root_count!=b.root_count||a.bones.size()!=b.bones.size())return false;
        for(std::size_t j=0;j<a.bones.size();++j) {
            const auto& x=a.bones[j];const auto& y=b.bones[j];
            if(x.name!=y.name||x.script_id!=y.script_id||x.parent_delta!=y.parent_delta||x.bind_bits!=y.bind_bits)return false;
            // Ref counts are live mutable metadata. Both were checked positive;
            // their equality is not an asset identity or lifetime requirement.
        }
    }
    return true;
}
void u32(std::vector<std::uint8_t>& out,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)out.push_back(static_cast<std::uint8_t>(value>>shift));
}
void bytes(std::vector<std::uint8_t>& out,std::string_view value) {
    u32(out,static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(),value.begin(),value.end());
}
void prefix(std::vector<std::uint8_t>& out,std::string_view domain) {
    bytes(out,domain);u32(out,profile_identity_schema);bytes(out,profile_sp_image_sha256);
}
bool scope_valid(const ProfileObservationCopies& copies,std::uint32_t thread,
                 std::uint64_t epoch,std::uint64_t now) noexcept {
    const auto& a=copies.before_stamp;const auto& b=copies.after_stamp;
    return thread&&epoch&&a.borrow_serial&&a.borrow_serial==b.borrow_serial&&a.native_thread==thread&&b.native_thread==thread&&
        a.native_epoch==epoch&&b.native_epoch==epoch&&a.observed_ms&&a.observed_ms<=b.observed_ms&&
        b.observed_ms<=now&&now-a.observed_ms<=profile_max_copy_age_ms;
}
Status validate_identity(const ProfileObservationCopies& copies,std::uint32_t thread,std::uint64_t epoch,
                         std::uint64_t now,bool optimized) noexcept {
    if(copies.image_sha256!=profile_sp_image_sha256)return Status::unsupported_layout;
    const auto& check=copies.rechecks;
    if(!check.current_native_scope||!check.complete_bounded_reads||!check.owner_equal||!check.pool_equal||
       !check.model_storage_equal||!check.server_string_arena_equal)return Status::unproved_borrow;
    if(!scope_valid(copies,thread,epoch,now))return Status::stale_scope;
    auto status=validate_models(copies.before,optimized);if(status!=Status::accepted)return status;
    status=validate_models(copies.after,optimized);if(status!=Status::accepted)return status;
    if(!same_copies(copies.before,copies.after))return Status::changed_copy;
    unsigned barrels{};std::size_t selected_model{},selected_bone{};
    for(std::size_t i=0;i<copies.before.size();++i)for(std::size_t j=0;j<copies.before[i].bones.size();++j)
        if(copies.before[i].bones[j].name.bytes=="tag_flash"){++barrels;selected_model=i;selected_bone=j;}
    if(barrels!=1||selected_model!=copies.selected_model||selected_bone!=copies.selected_local_bone)return Status::ambiguous_barrel;
    return Status::accepted;
}
constexpr std::size_t prefix_size(std::string_view domain) noexcept {
    return 4+domain.size()+4+4+sizeof(profile_sp_image_sha256)-1;
}
std::size_t model_payload_size(const ProfileModelCopy& model) noexcept {
    std::size_t result=prefix_size("BOVR-asset-bind-v1")+4+model.name.bytes.size()+8;
    for(const auto& bone:model.bones)result+=4+bone.name.bytes.size()+4+32;
    return result;
}
std::size_t assembly_payload_size(const ProfileObservationCopies& copies) noexcept {
    std::size_t result=prefix_size("BOVR-profile-assembly-v1")+12;
    for(const auto& model:copies.before)result+=4+model_payload_size(model);
    return result;
}
struct CompareStream {
    const std::vector<std::uint8_t>& expected;
    std::size_t offset{};bool equal{true};
    void append(std::uint8_t value) noexcept {
        if(offset>=expected.size()||expected[offset]!=value)equal=false;
        ++offset;
    }
    bool complete() const noexcept {return equal&&offset==expected.size();}
};
void u32(CompareStream& out,std::uint32_t value) noexcept {
    for(unsigned shift=0;shift<32;shift+=8)out.append(static_cast<std::uint8_t>(value>>shift));
}
void bytes(CompareStream& out,std::string_view value) noexcept {
    u32(out,static_cast<std::uint32_t>(value.size()));
    for(unsigned char ch:value)out.append(ch);
}
void prefix(CompareStream& out,std::string_view domain) noexcept {
    bytes(out,domain);u32(out,profile_identity_schema);bytes(out,profile_sp_image_sha256);
}
void model_payload(CompareStream& out,const ProfileModelCopy& model) noexcept {
    prefix(out,"BOVR-asset-bind-v1");bytes(out,model.name.bytes);
    u32(out,static_cast<std::uint32_t>(model.bones.size()));u32(out,model.root_count);
    for(const auto& bone:model.bones){bytes(out,bone.name.bytes);u32(out,bone.parent_delta);
        for(auto bits:bone.bind_bits)u32(out,bits);}
}
}
ProfileIdentityStatus canonical_profile_identity(const ProfileObservationCopies& copies,
    std::uint32_t thread,std::uint64_t epoch,std::uint64_t now,CanonicalProfileIdentity* output,bool optimized) noexcept {
    if(!output)return Status::invalid_argument;
    *output={};
    try {
        const auto status=validate_identity(copies,thread,epoch,now,optimized);if(status!=Status::accepted)return status;

        CanonicalProfileIdentity result{};result.schema=profile_identity_schema;
        result.selected_model=copies.selected_model;result.selected_local_bone=copies.selected_local_bone;
        if(optimized){const auto size=assembly_payload_size(copies);
            if(size>profile_max_payload_bytes)return Status::payload_bound;
            result.assembly_payload.reserve(size);result.model_payloads.reserve(copies.before.size());}
        prefix(result.assembly_payload,"BOVR-profile-assembly-v1");
        u32(result.assembly_payload,static_cast<std::uint32_t>(copies.before.size()));
        u32(result.assembly_payload,copies.selected_model);u32(result.assembly_payload,copies.selected_local_bone);
        for(const auto& model:copies.before) {
            std::vector<std::uint8_t> payload;
            if(optimized){const auto size=model_payload_size(model);if(size>profile_max_payload_bytes)return Status::payload_bound;payload.reserve(size);}
            prefix(payload,"BOVR-asset-bind-v1");bytes(payload,model.name.bytes);
            u32(payload,static_cast<std::uint32_t>(model.bones.size()));u32(payload,model.root_count);
            for(const auto& bone:model.bones) {
                bytes(payload,bone.name.bytes);u32(payload,bone.parent_delta);
                for(auto bits:bone.bind_bits)u32(payload,bits);
            }
            if(payload.size()>profile_max_payload_bytes||result.assembly_payload.size()+4+payload.size()>profile_max_payload_bytes)
                return Status::payload_bound;
            u32(result.assembly_payload,static_cast<std::uint32_t>(payload.size()));
            result.assembly_payload.insert(result.assembly_payload.end(),payload.begin(),payload.end());
            result.model_payloads.push_back(std::move(payload));
        }
        *output=std::move(result);return Status::accepted;
    }catch(const std::bad_alloc&) {return Status::allocation_failure;}
     catch(...) {return Status::invalid_argument;}
}
ProfileIdentityStatus verify_canonical_profile_identity(const ProfileObservationCopies& copies,
    std::uint32_t thread,std::uint64_t epoch,std::uint64_t now,const CanonicalProfileIdentity& identity,bool optimized) noexcept {
    const auto status=validate_identity(copies,thread,epoch,now,optimized);if(status!=Status::accepted)return status;
    if(assembly_payload_size(copies)>profile_max_payload_bytes)return Status::payload_bound;
    if(identity.schema!=profile_identity_schema||identity.selected_model!=copies.selected_model||
       identity.selected_local_bone!=copies.selected_local_bone||identity.model_payloads.size()!=copies.before.size()||
       identity.assembly_payload.size()>profile_max_payload_bytes)return Status::canonical_mismatch;
    CompareStream assembly{identity.assembly_payload};prefix(assembly,"BOVR-profile-assembly-v1");
    u32(assembly,static_cast<std::uint32_t>(copies.before.size()));u32(assembly,copies.selected_model);u32(assembly,copies.selected_local_bone);
    for(unsigned i=0;i<copies.before.size();++i){const auto& model=copies.before[i];const auto size=model_payload_size(model);
        if(size>profile_max_payload_bytes)return Status::payload_bound;
        CompareStream asset{identity.model_payloads[i]};model_payload(asset,model);
        if(!asset.complete())return Status::canonical_mismatch;
        u32(assembly,static_cast<std::uint32_t>(size));model_payload(assembly,model);
    }
    return assembly.complete()?Status::accepted:Status::canonical_mismatch;
}
} // namespace bovr::input
