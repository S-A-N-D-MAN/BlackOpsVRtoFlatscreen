// SPDX-License-Identifier: GPL-3.0-only
#include "t5/borrowed_profile_copy.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <thread>
#include <vector>
namespace p=bovr::t5::borrowed_profile;
namespace s=synthetic_profile_fixture;
namespace {
unsigned checks{},failures{},reads{},fail_read{};
void check(bool v,const char* s){++checks;if(!v){++failures;printf("FAIL %s\n",s);}}
bool read(void*,std::uintptr_t address,void* out,std::size_t n)noexcept{
    if(++reads==fail_read)return false;
    __try{memcpy(out,reinterpret_cast<void*>(address),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
struct Memory {
    std::array<std::array<std::uint32_t,8>,2> model{};
    std::array<std::uintptr_t,2> addresses{};
    std::array<unsigned short,82> ids{};
    std::array<std::array<char,64>,2> model_names{};
    std::array<std::array<unsigned short,72>,2> names{};
    std::array<std::array<unsigned char,72>,2> parents{};
    std::array<std::array<std::array<std::uint32_t,8>,72>,2> bind{};
    std::vector<unsigned char> arena=std::vector<unsigned char>(12000);
    explicit Memory(unsigned row){const auto& profile=*p::get(row);
        for(unsigned i=0;i<2;++i){const auto& m=profile.model[i];addresses[i]=reinterpret_cast<std::uintptr_t>(model[i].data());
            memcpy(model_names[i].data(),m.name.data(),m.name.size()+1);
            model[i][0]=reinterpret_cast<std::uintptr_t>(model_names[i].data());
            reinterpret_cast<unsigned char*>(model[i].data())[4]=static_cast<unsigned char>(m.count);
            reinterpret_cast<unsigned char*>(model[i].data())[5]=static_cast<unsigned char>(m.root);
            model[i][2]=reinterpret_cast<std::uintptr_t>(names[i].data());model[i][3]=reinterpret_cast<std::uintptr_t>(parents[i].data());model[i][7]=reinterpret_cast<std::uintptr_t>(bind[i].data());
            for(unsigned j=0;j<m.count;++j){const auto index=m.offset+j;const auto& b=profile.bone[index];
                ids[index]=names[i][j]=static_cast<unsigned short>((index+1)*8);
                const auto entry=16U*ids[index];arena[entry]=1;memcpy(arena.data()+entry+4,b.name.data(),b.name.size()+1);
                if(j)parents[i][j-1]=b.parent;bind[i][j]=b.bind;
            }
        }
    }
    bool run(unsigned row){reads=0;return p::current_payload(row,read,nullptr,addresses,ids,reinterpret_cast<std::uintptr_t>(arena.data()));}
};
}
int main(){
    for(unsigned row=0;row<p::runtime_row_base;++row)
        check(!p::get(row)&&!p::shape(row),"every legacy row number is permanently unassigned");
    s::Fixture enrolled(0);const auto enrolled_row=enrolled.enroll();
    check(enrolled_row>=p::runtime_row_base&&enrolled_row!=p::invalid_runtime_row,"validated current profile publishes a runtime row");
    const auto* enrolled_profile=p::get(enrolled_row);const auto* enrolled_shape=p::shape(enrolled_row);
    check(enrolled_profile&&enrolled_shape&&p::binding(enrolled_row,enrolled.weapon,p::runtime_revision)&&
          p::matches(enrolled_row,72,78,82)&&p::matrix_shape(72,78,82),"runtime shape and weapon binding share one publication");
    check(enrolled.enroll()==enrolled_row&&p::matches_runtime_record(enrolled_row,enrolled.weapon,enrolled.copies,enrolled.canonical,enrolled.assembly()),
          "an identical current profile deduplicates to its immutable row");
    p::CatalogWorkCounts optimized_work{},reference_work{};p::CandidateStatus optimized_status{},reference_status{};
    const auto optimized_row=p::register_runtime_profile(enrolled.weapon,enrolled.copies,enrolled.canonical,enrolled.assembly(),&optimized_status,true,&optimized_work);
    const auto reference_row=p::register_runtime_profile(enrolled.weapon,enrolled.copies,enrolled.canonical,enrolled.assembly(),&reference_status,false,&reference_work);
    check(optimized_row==enrolled_row&&reference_row==enrolled_row&&optimized_status==reference_status&&
          optimized_work.canonical_builds==0&&optimized_work.canonical_verifications==1&&reference_work.canonical_builds==1&&
          reference_work.canonical_verifications==0&&optimized_work.bind_conversions==0&&reference_work.bind_conversions>0,
          "exact immutable geometry is reused only after fresh streaming canonical and assembly verification");
    const auto compare_rejection=[&](const s::Fixture& value,const char* name){p::CandidateStatus a{},b{};
        const auto fast=p::register_runtime_profile(value.weapon,value.copies,value.canonical,value.assembly(),&a,true);
        const auto old=p::register_runtime_profile(value.weapon,value.copies,value.canonical,value.assembly(),&b,false);
        check(fast==p::invalid_runtime_row&&old==p::invalid_runtime_row&&a==b,name);
        check(!p::matches_runtime_record(enrolled_row,value.weapon,value.copies,value.canonical,value.assembly(),true)&&
              !p::matches_runtime_record(enrolled_row,value.weapon,value.copies,value.canonical,value.assembly(),false),
              "neither catalog path treats immutable enrollment as fresh borrow authority");
    };
    for(unsigned mutation=0;mutation<10;++mutation){auto bad=enrolled;
        switch(mutation){case 0:bad.copies.after[0].bones[0].observed_ref_count=0;break;
        case 1:bad.copies.after[1].bones[6].bind_bits[4]^=1;break;
        case 2:++bad.copies.after_stamp.native_epoch;break;case 3:bad.copies.rechecks.pool_equal=false;break;
        case 4:bad.copies.after[0].bones[1].script_id=bad.copies.after[0].bones[0].script_id;break;
        case 5:bad.canonical.model_payloads[0][0]^=1;break;case 6:bad.canonical.assembly_payload.back()^=1;break;
        case 7:bad.canonical.model_payloads[0].push_back(0);break;case 8:bad.copies.selected_local_bone=0;break;
        case 9:bad.copies.before[0].bones[0].name.terminated=false;break;}
        compare_rejection(bad,"reference and streaming catalog paths reject dead changed stale forged and incomplete copied evidence identically");
    }
    for(unsigned field=0;field<6;++field){auto bad=enrolled;
        switch(field){case 0:bad.copies.rechecks.current_native_scope=false;break;
        case 1:bad.copies.rechecks.complete_bounded_reads=false;break;case 2:bad.copies.rechecks.owner_equal=false;break;
        case 3:bad.copies.rechecks.model_storage_equal=false;break;case 4:bad.copies.rechecks.server_string_arena_equal=false;break;
        case 5:++bad.copies.after_stamp.borrow_serial;break;}
        compare_rejection(bad,"each fresh borrow witness still rejects an already enrolled immutable entry");
    }
    auto rebound=enrolled;
    for(auto& model:rebound.copies.before)for(auto& bone:model.bones)bone.script_id+=512;
    rebound.recanonical();p::CatalogWorkCounts rebound_work{};
    check(p::catalog_detail::canonical_equal(rebound.canonical,enrolled.canonical)&&
          p::register_runtime_profile(rebound.weapon,rebound.copies,rebound.canonical,rebound.assembly(),nullptr,true,&rebound_work)==enrolled_row&&
          rebound_work.canonical_verifications==1&&rebound_work.bind_conversions==0,
          "fresh consistent script IDs may change without retaining an earlier native ID or recomputing immutable geometry");
    rebound.copies.after[0].bones[1].script_id=rebound.copies.after[0].bones[0].script_id;
    compare_rejection(rebound,"reused current script ID with a different text cannot borrow existing geometry");
    const auto distinct_geometry=[&](s::Fixture value,const char* name){
        value.recanonical();p::CatalogWorkCounts work{};p::CandidateStatus status{};
        const auto row=p::register_runtime_profile(value.weapon,value.copies,value.canonical,value.assembly(),&status,true,&work);
        check(row!=enrolled_row&&row!=p::invalid_runtime_row&&status==p::CandidateStatus::accepted&&
              work.canonical_verifications==1&&work.bind_conversions>0,name);
        p::CatalogWorkCounts next{};
        check(p::register_runtime_profile(value.weapon,value.copies,value.canonical,value.assembly(),nullptr,true,&next)==row&&
              next.canonical_verifications==1&&next.bind_conversions==0,
              "only a subsequently exact fresh value match reuses the newly proven geometry");
        check(!p::matches_runtime_record(enrolled_row,value.weapon,value.copies,value.canonical,value.assembly()),
              "old runtime row does not match a newly changed value identity");
    };
    {auto value=enrolled;value.copies.before[0].name.bytes+="_new";
        distinct_geometry(value,"changed model name runs full geometry and publishes a distinct immutable row");}
    {auto value=enrolled;value.copies.before[0].bones[2].name.bytes+="_new";
        distinct_geometry(value,"changed bone name is part of exact canonical geometry identity");}
    {auto value=enrolled;value.copies.before[0].bones[3].parent_delta=2;
        distinct_geometry(value,"changed valid parent delta cannot reuse the old geometry");}
    {auto value=enrolled;value.copies.before[1].root_count=2;value.copies.before[1].bones[1].parent_delta=0;
        distinct_geometry(value,"changed valid root forest cannot reuse the old geometry");}
    {auto value=enrolled;value.copies.before[1].bones.push_back(s::bone("fixture_added_bone",83,1));
        distinct_geometry(value,"changed valid bone count cannot reuse the old geometry");}
    for(unsigned component=0;component<8;++component){auto value=enrolled;value.copies.before[1].bones[9].bind_bits[component]^=1;
        distinct_geometry(value,"every raw bind component independently belongs to exact immutable geometry identity");}
    {auto value=enrolled;value.weapon=173;
        distinct_geometry(value,"same canonical assets under another weapon cannot borrow the wrong weapon binding");}
    {auto value=enrolled;value.duplicates[3]=0x40;value.duplicates.pop_back();
        value.duplicates.insert(value.duplicates.end(),{2,1,0});
        distinct_geometry(value,"valid changed DObj duplicate assembly supplements canonical v1 and requires new geometry");}
    {auto bad=enrolled;bad.parents[1]=8;
        compare_rejection(bad,"changed model attachment assembly cannot reuse an entry with otherwise identical canonical bytes");}
    {auto bad=enrolled;bad.copies.before[0].bones[33].bind_bits=s::bind();bad.recanonical();
        const auto count=p::catalog_detail::published.load();p::CandidateStatus status{};
        check(bad.enroll(&status)==p::invalid_runtime_row&&status==p::CandidateStatus::palm_geometry&&
              p::catalog_detail::published.load()==count,"fresh canonical but degenerate new palm never publishes or borrows old geometry");}
    auto changed=enrolled;changed.copies.before[1].bones[6].bind_bits[4]=std::bit_cast<std::uint32_t>(20.0F);changed.recanonical();
    const auto changed_row=changed.enroll();
    check(changed_row!=enrolled_row&&changed_row!=p::invalid_runtime_row&&p::get(enrolled_row)==enrolled_profile,
          "changed current geometry appends without rewriting a published row");
    auto invalid=enrolled;invalid.weapon=0;invalid.recanonical();
    check(invalid.enroll()==p::invalid_runtime_row,"invalid current weapon identity cannot enroll");
    std::array<unsigned,8> concurrent{};std::array<std::thread,8> workers{};
    for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{concurrent[i]=enrolled.enroll();});
    for(auto& worker:workers)worker.join();
    check(std::all_of(concurrent.begin(),concurrent.end(),[&](unsigned value){return value==enrolled_row;}),
          "concurrent identical enrollment publishes one runtime row");
    s::Fixture fresh_parallel(501);const auto count_before=p::catalog_detail::published.load();
    for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{concurrent[i]=fresh_parallel.enroll();});
    for(auto& worker:workers)worker.join();
    check(concurrent[0]!=p::invalid_runtime_row&&p::catalog_detail::published.load()==count_before+1&&
          std::all_of(concurrent.begin(),concurrent.end(),[&](unsigned value){return value==concurrent[0];}),
          "concurrent first geometry derivations recheck publication and append exactly one immutable entry");
    unsigned counts[3]{};
    for(unsigned variant=0;variant<3;++variant){const auto row=synthetic_profile_fixture::row(variant);
        const auto& profile=*p::get(row);const auto& shape=*p::shape(row);Memory m(row);
        check(m.run(row),"every current model bone hierarchy and bind value matches its runtime row");counts[variant]=reads;
        check(profile.model[0].count==shape.arms&&profile.model[1].offset==shape.gun&&profile.bone.size()==shape.total,"row metadata agrees with complete generated active payload");
        check(profile.bone[shape.attachment].name=="tag_weapon"&&profile.bone[shape.gun].name=="j_gun"&&profile.bone[shape.flash].name=="tag_flash","attachment and gun/flash ordinals agree with runtime payload");
        for(unsigned other=0;other<4;++other)if(other!=variant)check(!m.run(synthetic_profile_fixture::row(other)),"wrong runtime row rejects even with equal bone counts");
        check(!p::current_payload(row,nullptr,nullptr,m.addresses,m.ids,0x10000),"missing copy provider rejects");
        for(unsigned i=0;i<2;++i){const auto& model=profile.model[i];
            for(unsigned c=0;c<=model.name.size();++c){m.model_names[i][c]^=1;check(!m.run(row),"every model-name byte and terminator is verified");m.model_names[i][c]^=1;}
            auto* header=reinterpret_cast<unsigned char*>(m.model[i].data());
            ++header[4];check(!m.run(row),"current active model count cannot exceed row capacity");--header[4];
            ++header[5];check(!m.run(row),"current root count must match published runtime row");--header[5];
            for(unsigned j=0;j<model.count;++j){const auto index=model.offset+j,entry=16U*m.ids[index];const auto& b=profile.bone[index];
                for(unsigned c=0;c<=b.name.size();++c){m.arena[entry+4+c]^=1;check(!m.run(row),"every bone-name byte and terminator is checked under current copy");m.arena[entry+4+c]^=1;}
                m.arena[entry]=0;check(!m.run(row),"unreferenced native script string rejected");m.arena[entry]=1;
                for(unsigned k=0;k<8;++k){m.bind[i][j][k]^=1;check(!m.run(row),"every current raw bind component independently checked");m.bind[i][j][k]^=1;}
                ++m.names[i][j];check(!m.run(row),"current ordinal ID must match copied canonical borrow");--m.names[i][j];
                if(j){++m.parents[i][j-1];check(!m.run(row),"every current native parent delta independently checked");--m.parents[i][j-1];}
            }
        }
        for(unsigned i=1;i<=counts[variant];++i){fail_read=i;check(!m.run(row),"each bounded protected read failure returns rejection");}fail_read=0;
        const auto name=m.model[0][0];m.model[0][0]=1;check(!m.run(row),"invalid current model-name pointer rejects without native call");m.model[0][0]=name;
        const auto mat=m.model[1][7];m.model[1][7]=0xfffffff0;check(!m.run(row),"overflowed bind range rejected");m.model[1][7]=mat;
        check(!p::current_payload(row,read,nullptr,m.addresses,m.ids,0xfffffff0),"overflowed SL arena rejected");
        SYSTEM_INFO info{};GetSystemInfo(&info);
        auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,info.dwPageSize*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(page!=nullptr,"protected two-page fixture allocated");
        if(page){DWORD old{};check(VirtualProtect(page+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old)!=0,"last native-page guard applied");
            for(unsigned i=0;i<2;++i){const auto saved=m.model[i][7],bytes=profile.model[i].count*32;
                memcpy(page+info.dwPageSize-bytes,m.bind[i].data(),bytes);m.model[i][7]=reinterpret_cast<std::uintptr_t>(page+info.dwPageSize-bytes);
                check(m.run(row),"active60/72arm or10gun bind records end exactly at protected boundary, no tail reads");
                m.model[i][7]+=4;check(!m.run(row),"truncated final active native matrix rejects");m.model[i][7]=saved;
            }
            VirtualFree(page,0,MEM_RELEASE);
        }
        if(shape.total<82){m.ids[81]=65535;check(m.run(row),"inactive capacity tail is never part of native identity");}
        check(m.run(row),"restored full current row is accepted again");
    }
    const auto unknown_row = p::runtime_row_base+p::max_runtime_profiles;
    check(!p::shape(unknown_row)&&!p::get(unknown_row)&&!p::shape(UINT32_MAX)&&!p::get(UINT32_MAX),"unknown row never indexes a payload or shape");
    printf("borrowed_profile_checks=%u failures=%u row_reads=%u,%u,%u native_calls=0 game_writes=0\n",checks,failures,counts[0],counts[1],counts[2]);return failures?1:0;
}
