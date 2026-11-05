// SPDX-License-Identifier: GPL-3.0-only
#include "input/profile_identity.hpp"
#include "t5/identity.hpp"
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace {
using namespace bovr::input;
using Status=ProfileIdentityStatus;
unsigned checks{},failures{};
void check(bool value,const char* name) {++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
ProfileTextCopy text(std::string value) {return {std::move(value),true,true};}
std::array<std::uint32_t,8> bind(float x=0,float y=0,float z=0) {
    std::array<float,8> values{0,0,0,1,x,y,z,2};std::array<std::uint32_t,8> bits{};
    for(unsigned i=0;i<8;++i)bits[i]=std::bit_cast<std::uint32_t>(values[i]);return bits;
}
ProfileObservationCopies fixture() {
    ProfileObservationCopies c{};c.image_sha256=profile_sp_image_sha256;
    c.before_stamp={77,2,1000,17};c.after_stamp={77,2,1001,17};c.rechecks={true,true,true,true,true,true};
    ProfileModelCopy hands{text("viewmodel_hands"),1,{{text("tag_origin"),1,3,0,bind()}}};
    ProfileModelCopy gun{text("viewmodel_m1911"),1,{{text("tag_origin"),1,3,0,bind()},
        {text("tag_flash"),42,7,1,bind(12.5F,-2.0F,0.25F)}}};
    c.before={hands,gun};c.after=c.before;c.selected_model=1;c.selected_local_bone=1;return c;
}
Status run(const ProfileObservationCopies& c,CanonicalProfileIdentity* out,std::uint32_t thread=17,
           std::uint64_t epoch=2,std::uint64_t now=1002) {
    const auto optimized=canonical_profile_identity(c,thread,epoch,now,out,true);
    CanonicalProfileIdentity reference{};const auto old=canonical_profile_identity(c,thread,epoch,now,&reference,false);
    check(optimized==old,"optimized and reference readers preserve exact rejection precedence");
    if(out)check(out->schema==reference.schema&&out->selected_model==reference.selected_model&&
        out->selected_local_bone==reference.selected_local_bone&&out->model_payloads==reference.model_payloads&&
        out->assembly_payload==reference.assembly_payload,"optimized and reference identity bytes and failure outputs agree");
    check(verify_canonical_profile_identity(c,thread,epoch,now,reference)==old,
        "streaming verification independently retains every identity validation");
    return optimized;
}
void expect(ProfileObservationCopies c,Status expected,const char* name) {
    CanonicalProfileIdentity out{99,99,99,{{1,2,3}},{4,5,6}};
    check(run(c,&out)==expected,name);
    if(expected!=Status::accepted)check(out.schema==0&&out.model_payloads.empty()&&out.assembly_payload.empty()&&
        !out.selected_model&&!out.selected_local_bone,"every rejected copy clears prior identity payload");
}
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& value) {
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(value.data()),static_cast<std::streamsize>(value.size()));
    if(!file)throw std::runtime_error("fixture output failed");
}
}
int main(int argc,char** argv) {
    auto c=fixture();CanonicalProfileIdentity good{};
    check(run(c,&good)==Status::accepted,"paired supported copied values create a canonical identity");
    check(good.schema==1&&good.model_payloads.size()==2&&good.selected_model==1&&good.selected_local_bone==1,"selection and schema remain explicit");
    auto same=c;for(auto& models:{&same.before,&same.after})for(auto& m:*models)for(auto& b:m.bones) {
        b.script_id=static_cast<std::uint16_t>(b.script_id+700);b.observed_ref_count=20;
    }
    same.before_stamp={90,9,3000,99};same.after_stamp={90,9,3001,99};CanonicalProfileIdentity shifted{};
    check(run(same,&shifted,99,9,3002)==Status::accepted&&shifted.model_payloads==good.model_payloads&&
        shifted.assembly_payload==good.assembly_payload,"new process IDs thread epoch time and borrow serial never enter durable payload");
    same=c;same.after[1].bones[1].observed_ref_count=2;
    CanonicalProfileIdentity refs{};
    check(run(same,&refs)==Status::accepted&&refs.assembly_payload==good.assembly_payload,"positive mutable reference count change is excluded from identity");
    check(c.before[1].bones[1].bind_bits==bind(12.5F,-2.0F,0.25F)&&c.before[1].bones[1].script_id==42,"serialization never mutates copied native values");
    check(canonical_profile_identity(c,17,2,1002,nullptr)==Status::invalid_argument,"null output rejects");
    for(unsigned i=0;i<6;++i) {
        auto bad=c;switch(i){case 0:bad.rechecks.current_native_scope=false;break;case 1:bad.rechecks.complete_bounded_reads=false;break;
        case 2:bad.rechecks.owner_equal=false;break;case 3:bad.rechecks.pool_equal=false;break;case 4:bad.rechecks.model_storage_equal=false;break;
        case 5:bad.rechecks.server_string_arena_equal=false;break;}
        expect(bad,Status::unproved_borrow,"each absent native-reader recheck rejects");
    }
    for(unsigned i=0;i<9;++i) {
        auto bad=c;switch(i){case 0:bad.before_stamp.borrow_serial=0;break;case 1:bad.after_stamp.borrow_serial++;break;
        case 2:bad.before_stamp.native_thread++;break;case 3:bad.after_stamp.native_thread++;break;case 4:bad.before_stamp.native_epoch++;break;
        case 5:bad.after_stamp.native_epoch++;break;case 6:bad.before_stamp.observed_ms=0;break;case 7:bad.after_stamp.observed_ms=999;break;
        case 8:bad.after_stamp.observed_ms=1003;break;}
        expect(bad,Status::stale_scope,"scope thread epoch or timestamp mismatch rejects");
    }
    CanonicalProfileIdentity out{};
    check(run(c,&out,17,2,1100)==Status::accepted,"exact100ms original-copy limit accepts");
    check(run(c,&out,17,2,1101)==Status::stale_scope&&out.assembly_payload.empty(),"101ms never refreshes old copied identity");
    check(run(c,&out,0)==Status::stale_scope&&run(c,&out,17,0)==Status::stale_scope,"unknown current thread or epoch rejects");
    auto bad=c;bad.image_sha256[0]='0';expect(bad,Status::unsupported_layout,"different executable/layout is never accepted");
    bad=c;bad.before.clear();expect(bad,Status::count_bound,"empty model list rejects");
    bad=c;bad.before.resize(33,c.before[0]);expect(bad,Status::count_bound,"33 models reject before traversal");
    bad=c;bad.before[1].bones.clear();expect(bad,Status::count_bound,"empty model bone list rejects");
    bad=c;bad.before[1].bones.resize(160,c.before[1].bones[0]);expect(bad,Status::count_bound,"161 total bones reject");
    for(unsigned i=0;i<8;++i) {
        bad=c;auto& t=i<4?bad.before[0].name:bad.before[1].bones[1].name;
        switch(i%4){case 0:t.readable=false;break;case 1:t.terminated=false;break;case 2:t.bytes.clear();break;case 3:t.bytes.resize(256,'x');break;}
        expect(bad,Status::invalid_text,"unreadable unterminated empty or oversized model and bone strings reject");
    }
    for(unsigned v=0;v<256;++v)if(v<0x20||v>0x7e) {
        bad=c;bad.before[1].bones[1].name.bytes[0]=static_cast<char>(v);
        expect(bad,Status::invalid_text,"control embeddedNUL and nonASCII bytes never form an asset key");
    }
    bad=c;bad.after[0].name.terminated=false;expect(bad,Status::invalid_text,"second copy must independently prove bounded termination");
    bad=c;bad.before[0].bones[0].script_id=0;expect(bad,Status::invalid_string_entry,"zero native string ID rejects");
    bad=c;bad.after[1].bones[1].observed_ref_count=0;expect(bad,Status::invalid_string_entry,"dead second string observation rejects");
    bad=c;bad.before[1].bones[1].script_id=1;expect(bad,Status::invalid_string_entry,"one native ID cannot mean two copied names");
    for(unsigned i=0;i<5;++i) {
        bad=c;switch(i){case 0:bad.before[1].root_count=0;break;case 1:bad.before[1].root_count=3;break;
        case 2:bad.before[1].bones[0].parent_delta=1;break;case 3:bad.before[1].bones[1].parent_delta=0;break;
        case 4:bad.before[1].bones[1].parent_delta=2;break;}
        expect(bad,Status::invalid_topology,"root count and parent deltas require earlier in-model ancestors");
    }
    for(unsigned field=0;field<8;++field)for(auto value:{0x7f800000u,0xff800000u,0x7fc00001u}) {
        bad=c;bad.before[1].bones[1].bind_bits[field]=value;
        expect(bad,Status::invalid_bind_geometry,"every nonfinite bind field rejects");
    }
    bad=c;bad.before[1].bones[1].bind_bits[3]=0;expect(bad,Status::invalid_bind_geometry,"zero quaternion rejects");
    bad=c;bad.before[1].bones[1].bind_bits[7]=0;expect(bad,Status::invalid_bind_geometry,"zero native weight rejects");
    bad=c;bad.before[1].bones[1].bind_bits[7]=std::bit_cast<std::uint32_t>(1.0F);expect(bad,Status::invalid_bind_geometry,"inconsistent quaternion conversion weight rejects");
    for(unsigned i=0;i<5;++i) {
        bad=c;switch(i){case 0:bad.after[1].name.bytes+="_other";break;case 1:bad.after[1].bones[1].name.bytes="tag_other";break;
        case 2:bad.after[1].bones[1].script_id=43;break;case 3:bad.after[1].bones[1].bind_bits[4]^=1;break;
        case 4:bad.after.push_back(bad.after[0]);break;}
        expect(bad,Status::changed_copy,"changed model bone ID bind geometry or model list invalidates whole borrow");
    }
    bad=c;bad.selected_model=0;expect(bad,Status::ambiguous_barrel,"caller model selection must match exact canonical barrel");
    bad=c;bad.selected_local_bone=0;expect(bad,Status::ambiguous_barrel,"caller bone selection must match exact canonical barrel");
    bad=c;bad.before[1].bones[1].name.bytes="Tag_Flash";bad.after=bad.before;expect(bad,Status::ambiguous_barrel,"barrel name is case exact");
    bad=c;bad.before[0].bones[0].name.bytes="tag_flash";bad.before[0].bones[0].script_id=42;bad.after=bad.before;
    expect(bad,Status::ambiguous_barrel,"multiple canonical barrels reject even if reader selected one");
    for(unsigned i=0;i<4;++i) {
        same=c;switch(i){case 0:same.before[1].name.bytes="Viewmodel_m1911";break;
        case 1:same.before[1].bones[1].bind_bits[4]^=1;break;case 2:same.before[1].bones[1].bind_bits[0]=0x80000000;break;
        case 3:same.before[1].bones[1].bind_bits[3]=std::bit_cast<std::uint32_t>(-1.0F);break;}
        same.after=same.before;check(run(same,&out)==Status::accepted&&out.model_payloads[1]!=good.model_payloads[1],
            "case exactfloat signedzero and quaternion sign are never silently normalized");
    }
    same=c;same.before[1].bones[1].bind_bits[3]=std::bit_cast<std::uint32_t>(2.0F);
    same.before[1].bones[1].bind_bits[7]=std::bit_cast<std::uint32_t>(0.5F);same.after=same.before;
    check(run(same,&out)==Status::accepted,"nonunit quaternion with correct native weight is supported unchanged");
    same=c;same.before[1].name.bytes=std::string(255,'m');same.after=same.before;
    check(run(same,&out)==Status::accepted,"exact255byte name boundary accepts without truncation");
    same=c;same.before.clear();
    for(unsigned model=0;model<32;++model) {
        ProfileModelCopy m{text(std::string(255,'m')),1,{}};
        for(unsigned bone=0;bone<5;++bone)m.bones.push_back({text(std::string(255,'b')),
            static_cast<std::uint16_t>(1+model*5+bone),1,static_cast<std::uint16_t>(bone?1:0),bind()});
        same.before.push_back(std::move(m));
    }
    same.before[31].bones[4].name.bytes="tag_flash";same.after=same.before;same.selected_model=31;same.selected_local_bone=4;
    check(run(same,&out)==Status::accepted&&out.model_payloads.size()==32&&out.assembly_payload.size()<65536,
        "maximum32models160bones255byte names stays within fixed payload budget");
    // Distant/cross-model duplicate IDs, maximum IDs and arbitrary order must
    // retain the reference nested-loop semantics without a native-ID cache.
    for(unsigned duplicate=0;duplicate<160;++duplicate){
        auto grouped=same;auto& last=grouped.before[31].bones[4];
        const auto& source=grouped.before[duplicate/5].bones[duplicate%5];
        last.script_id=source.script_id;
        grouped.after=grouped.before;CanonicalProfileIdentity grouped_identity{};
        const auto expected=last.name.bytes==source.name.bytes?Status::accepted:Status::invalid_string_entry;
        check(run(grouped,&grouped_identity)==expected,"all160 copied-ID duplicate locations retain full name consistency checks");
    }
    auto descending=same;unsigned ordinal{};
    for(auto& model:descending.before)for(auto& bone:model.bones)bone.script_id=static_cast<std::uint16_t>(65535-ordinal++);
    descending.after=descending.before;check(run(descending,&out)==Status::accepted,
        "maximum16bit IDs descending across32models need no dense ID map or native pointer cache");
    for(unsigned model=0;model<good.model_payloads.size();++model)for(unsigned byte=0;byte<good.model_payloads[model].size();++byte){
        auto altered=good;altered.model_payloads[model][byte]^=1;
        check(verify_canonical_profile_identity(c,17,2,1002,altered)==Status::canonical_mismatch,
            "streaming verifier rejects every changed canonical asset byte");
    }
    for(unsigned byte=0;byte<good.assembly_payload.size();++byte){auto altered=good;altered.assembly_payload[byte]^=1;
        check(verify_canonical_profile_identity(c,17,2,1002,altered)==Status::canonical_mismatch,
            "streaming verifier rejects every changed ordered assembly byte");}
    for(unsigned mutation=0;mutation<7;++mutation){auto altered=good;
        switch(mutation){case 0:++altered.schema;break;case 1:++altered.selected_model;break;case 2:++altered.selected_local_bone;break;
        case 3:altered.model_payloads.pop_back();break;case 4:altered.model_payloads[0].pop_back();break;
        case 5:altered.assembly_payload.pop_back();break;case 6:altered.assembly_payload.push_back(0);break;}
        check(verify_canonical_profile_identity(c,17,2,1002,altered)==Status::canonical_mismatch,
            "streaming verifier rejects metadata missing payload bytes and appended bytes");}
    const std::string asset_hash=bovr::t5::sha256(good.model_payloads[1]);
    const std::string assembly_hash=bovr::t5::sha256(good.assembly_payload);
    check(asset_hash.size()==64&&assembly_hash.size()==64&&asset_hash!=assembly_hash,"separate asset and assembly domains hash distinctly");
    if(argc==2) {
        const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory);
        write(directory/"asset.bin",good.model_payloads[1]);write(directory/"assembly.bin",good.assembly_payload);
        std::ofstream manifest(directory/"hashes.txt");manifest<<asset_hash<<" asset.bin\n"<<assembly_hash<<" assembly.bin\n";
    }
    std::printf("profile_identity_checks=%u failures=%u native_calls=0 process_reads=0 runtime_calls=0\n",checks,failures);
    std::printf("asset_sha256=%s assembly_sha256=%s\n",asset_hash.c_str(),assembly_hash.c_str());
    return failures?1:0;
}
