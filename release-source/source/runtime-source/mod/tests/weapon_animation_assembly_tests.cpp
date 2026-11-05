// SPDX-License-Identifier: GPL-3.0-only
// Actual protected reader against owned buffers; no native helper/game call.
#include "synthetic_runtime_profile_fixture.hpp"
#include "../src/t5/weapon_barrel_probe.cpp"
#include <cstdio>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept{return false;}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&) noexcept{return false;}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&,std::uint64_t,bool) noexcept{return false;}
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity&) noexcept{}

bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept {return false;}
std::uint64_t pose_clear_serial() noexcept{return 0;}
bool enable_weapon_native_clear_scope() noexcept{return false;}
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept{return {};}

bool install_pose_clear_probe(HMODULE,PoseClearCallback) noexcept{return false;}
bool copy_pose_clear_weapon_context(std::uintptr_t,int,std::uint64_t,NativeWeaponContext*) noexcept{return false;}
std::wstring executable_path(){return {};}
Validation validate_path(const std::wstring&){return {};}
bool validate_loaded_headers(HMODULE,std::string&){return false;}
std::string sha256(std::span<const std::uint8_t>){return {};}
namespace profile_detail {
bool protected_profile_copy(void*,std::uintptr_t,void*,std::size_t) noexcept {return false;}
ReadStatus read_profile(const ReadHost&,const NativeWeaponContext&,DWORD,std::uint64_t,ProfileRecord*,ReadWorkCounts* counts) noexcept {if(counts)*counts={};return ReadStatus::context;}
}
}
namespace {
unsigned checks{},failures{};
void check(bool v,const char* text){++checks;if(!v){++failures;printf("FAIL %s\n",text);}}
template<class T>void put(void* data,unsigned offset,T value){memcpy(static_cast<unsigned char*>(data)+offset,&value,sizeof(value));}
}
void assembly_case(unsigned row){
    using namespace bovr::t5;
    const unsigned arms=borrowed_profile::shape(row)->arms,total=arms+10;
    const auto attachment=static_cast<unsigned char>(borrowed_profile::shape(row)->attachment);
    std::array<unsigned char,0x7c> object{};
    std::array<unsigned char,12> table{};
    std::array<std::array<unsigned char,32>,2> models{};
    std::array<unsigned short,72> arm_names{};std::array<unsigned short,10> gun_names{};
    std::array<unsigned char,71> arm_parents{};std::array<unsigned char,9> gun_parents{};
    std::array<unsigned char,72*32> arm_bind{};std::array<unsigned char,10*32> gun_bind{};
    std::array<unsigned char,4096> arena{},other_arena{};
    std::uintptr_t arena_pointer=reinterpret_cast<std::uintptr_t>(arena.data());
    NativeBorrow b{};AnimationProfile p{};
    b.dobj=reinterpret_cast<std::uintptr_t>(object.data());b.models_table=reinterpret_cast<std::uintptr_t>(table.data());
    b.model_count=2;b.bone_count=total;b.model_bones[0]=static_cast<unsigned char>(arms);b.model_bones[1]=10;
    const auto& known=*borrowed_profile::get(row);const auto& shape=*borrowed_profile::shape(row);
    p.counts={static_cast<unsigned char>(arms),10};p.roots={1,1};p.gun_bone=arms;p.profile_row=row;
    p.weapon_index=shape.weapon;p.profile_revision=shape.revision;
    for(unsigned i=0;i<total;++i){p.bone_ids[i]=b.bone_names[i]=static_cast<unsigned short>(100+i);p.parent_deltas[i]=known.bone[i].parent;}
    for(unsigned i=0;i<arms;++i){arm_names[i]=p.bone_ids[i];if(i)arm_parents[i-1]=p.parent_deltas[i];}
    for(unsigned i=0;i<10;++i){gun_names[i]=p.bone_ids[arms+i];if(i)gun_parents[i-1]=p.parent_deltas[arms+i];}
    for(unsigned i=0;i<2;++i){b.models[i]=reinterpret_cast<std::uintptr_t>(models[i].data());put(table.data(),i*4,b.models[i]);
        put(models[i].data(),4,p.counts[i]);put(models[i].data(),5,p.roots[i]);}
    put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(arm_names.data()));put(models[1].data(),8,reinterpret_cast<std::uintptr_t>(gun_names.data()));
    put(models[0].data(),12,reinterpret_cast<std::uintptr_t>(arm_parents.data()));put(models[1].data(),12,reinterpret_cast<std::uintptr_t>(gun_parents.data()));
    put(models[0].data(),28,reinterpret_cast<std::uintptr_t>(arm_bind.data()));put(models[1].data(),28,reinterpret_cast<std::uintptr_t>(gun_bind.data()));
    table[8]=255;table[9]=attachment;put(object.data(),4,static_cast<unsigned short>(3));object[8]=21;put(arena.data(),48,static_cast<unsigned short>(1));
    AnimationAssembly expected{},out{};
    const auto read=[&]{return animation_assembly_copy(b,p,&out,reinterpret_cast<std::uintptr_t>(&arena_pointer));};
    check(read(),"known canonical IDs/counts/roots and exact parent/duplicate assembly copy accepted");expected=out;
    for(unsigned i=0;i<total;++i){++p.bone_ids[i];check(!read(),"changed copied canonical bone ordinal rejected");--p.bone_ids[i];}
    for(unsigned i=0;i<arms-1;++i){++arm_parents[i];check(!read(),"changed native arm hierarchy delta rejected before controller");--arm_parents[i];}
    for(unsigned i=0;i<9;++i){++gun_parents[i];check(!read(),"changed native gun hierarchy delta rejected before controller");--gun_parents[i];}
    for(unsigned i=0;i<21;++i){arena[52+i]=1;check(!read(),"every changed duplicate byte rejected");arena[52+i]=0;}
    table[9]=8;check(!read(),"changed attachment parent rejected");table[9]=attachment;
    table[8]=0;check(!read(),"first model independent root parent must remain FF");table[8]=255;
    models[1][5]=2;check(!read(),"changed native root count rejected");models[1][5]=1;
    models[0][4]=71;check(!read(),"changed native model count rejected");models[0][4]=static_cast<unsigned char>(arms);
    ++gun_names[0];check(!read(),"same model pointer cannot hide changed current gun-root ID");--gun_names[0];
    object[8]=23;check(!read(),"different duplicate byte size rejected");object[8]=21;
    put(object.data(),4,static_cast<unsigned short>(0));check(!read(),"absent duplicate handle rejected");put(object.data(),4,static_cast<unsigned short>(3));
    put(arena.data(),48,static_cast<unsigned short>(0));check(!read(),"unreferenced script data rejected");put(arena.data(),48,static_cast<unsigned short>(2));
    check(read()&&out==expected,"positive reference count may change without changing copied assembly identity");
    put(arena.data(),64,static_cast<unsigned short>(1));put(object.data(),4,static_cast<unsigned short>(4));
    check(read()&&!(out==expected),"new zero duplicate handle is still a changed borrow identity");
    put(arena.data(),64,static_cast<unsigned short>(0));put(object.data(),4,static_cast<unsigned short>(3));
    other_arena=arena;arena_pointer=reinterpret_cast<std::uintptr_t>(other_arena.data());
    check(read()&&!(out==expected),"same bytes in another script arena cannot reuse prior metadata proof");arena_pointer=reinterpret_cast<std::uintptr_t>(arena.data());
    auto saved=arm_names;put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(saved.data()));
    check(read()&&!(out==expected),"same bone IDs in changed name storage are separately detected");put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(arm_names.data()));
    put(models[0].data(),12,std::uintptr_t{1});check(!read(),"unreadable hierarchy pointer rejected safely");put(models[0].data(),12,reinterpret_cast<std::uintptr_t>(arm_parents.data()));
    put(models[0].data(),8,std::uintptr_t{0xfffffff0});check(!read(),"overflowed bone-name range rejected before read");put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(arm_names.data()));
    arena_pointer=0xfffffff0;check(!read(),"overflowed script arena range rejected");arena_pointer=reinterpret_cast<std::uintptr_t>(arena.data());
    check(!animation_assembly_copy(b,p,&out,1),"fault in protected arena pointer copy returns failure");
    check(read()&&out==expected,"all fixture-owned source bytes restored and accepted");
    // A70-bone assembly must never compare capacity-only tail entries.
    for(unsigned i=total;i<p.bone_ids.size();++i){p.bone_ids[i]=0xffff;p.parent_deltas[i]=255;}
    check(read(),"unused fixed-capacity profile tail never enters native active reads");
    unsigned active{};check(animation_profile_shape(p,&active)&&active==total,"exact row returns current active count");
    const auto good=p;
    p.profile_row=0;check(!read(),"unpublished legacy row rejected");p=good;
    p.gun_bone=arms+1;check(!read(),"gun offset must equal selected exact arm count");p=good;
    p.counts[0]=static_cast<unsigned char>(arms+1);check(!read(),"shape count alteration rejected");p=good;
    ++b.bone_count;check(!read(),"native active bone total alteration rejected");--b.bone_count;
    // Put the last real name immediately before a protected page. A hardcoded
    //72-name traversal for the60-arm case would fault and reject this success.
    SYSTEM_INFO si{};GetSystemInfo(&si);const auto page=si.dwPageSize;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(memory!=nullptr,"allocate fixture-only protected-boundary pages");
    if(memory){DWORD old{};check(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old)!=0,"protect trailing fixture page");
        auto* names=memory+page-2*arms;memcpy(names,arm_names.data(),2*arms);
        put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(names));
        check(read(),"active native arm-name read ends exactly at60/72 entries");
        put(models[0].data(),8,reinterpret_cast<std::uintptr_t>(arm_names.data()));
        auto* parents=memory+page-(arms-1);memcpy(parents,arm_parents.data(),arms-1);
        put(models[0].data(),12,reinterpret_cast<std::uintptr_t>(parents));
        check(read(),"active native parent read ends exactly at59/71 entries");
        put(models[0].data(),12,reinterpret_cast<std::uintptr_t>(arm_parents.data()));
        auto* matrices=memory+page-32*total;
        for(unsigned i=0;i<total;++i){anim::NativeMat32 m{};m.quaternion[3]=1.F;memcpy(matrices+32*i,&m,32);}
        anim::NativeMat32 gun{},flash{};anim::LockedRequest request{b.dobj,arms,arms+6,total,1,true};
        check(animation_copy(nullptr,request,reinterpret_cast<std::uintptr_t>(matrices),&gun,&flash),"current gun/flash matrices copied within exact active allocation");
        request.flash_bone=total;check(!animation_copy(nullptr,request,reinterpret_cast<std::uintptr_t>(matrices),&gun,&flash),"first nonexistent bone rejected before dereference");
        request.gun_bone=total;request.flash_bone=arms+6;
        check(!animation_copy(nullptr,request,reinterpret_cast<std::uintptr_t>(matrices),&gun,&flash),"nonexistent gun rejected before dereference");
        check(VirtualFree(memory,0,MEM_RELEASE)!=0,"release only fixture-owned protected allocation");
    }
}
int main(){
    using namespace bovr::t5;
    for(unsigned variant=0;variant<3;++variant)assembly_case(synthetic_profile_fixture::row(variant));
    printf("checks=%u failures=%u native_calls=0 game_writes=0\n",checks,failures);return failures?1:0;
}
