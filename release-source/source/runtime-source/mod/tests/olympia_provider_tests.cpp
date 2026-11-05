// SPDX-License-Identifier: GPL-3.0-only
// Actual copied-provider body over authored owned buffers. No game/native call.
#include "synthetic_runtime_profile_fixture.hpp"
#include "../src/t5/weapon_barrel_probe.cpp"
#include <cstdio>
#include <memory>

namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept{return false;}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&) noexcept{return false;}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&,std::uint64_t,bool) noexcept{return false;}
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity&) noexcept{}
bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept{return false;}
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
bool protected_profile_copy(void*,std::uintptr_t,void*,std::size_t) noexcept{return false;}
ReadStatus read_profile(const ReadHost&,const NativeWeaponContext&,DWORD,std::uint64_t,ProfileRecord*,ReadWorkCounts* counts) noexcept{if(counts)*counts={};return ReadStatus::context;}
}
}

namespace {
using namespace bovr::t5;namespace p=bovr::t5::borrowed_profile;
unsigned checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
template<class T>void put(void* data,unsigned offset,T value){std::memcpy(static_cast<unsigned char*>(data)+offset,&value,sizeof(value));}

profile_detail::ProfileRecord record(unsigned variant){const auto& fixture=synthetic_profile_fixture::fixture(variant);
    profile_detail::ProfileRecord result{};result.copies=fixture.copies;result.canonical=fixture.canonical;
    result.assembly.total_bones=static_cast<unsigned short>(fixture.total);result.assembly.model_parents=fixture.parents;result.assembly.duplicate_data=fixture.duplicates;
    return result;
}

struct Memory {
    std::array<unsigned char,0x7c> object{};
    std::array<std::uintptr_t,3> table{};
    std::array<std::array<std::uint32_t,8>,2> model{};
    std::array<std::array<char,256>,2> model_names{};
    std::array<std::array<unsigned short,72>,2> names{};
    std::array<std::array<unsigned char,72>,2> parents{};
    std::array<std::array<std::array<std::uint32_t,8>,72>,2> bind{};
    std::array<anim::NativeMat32,p::max_profile_bones> matrices{};
    std::vector<unsigned char> arena=std::vector<unsigned char>((p::max_profile_bones+1)*128);
    std::uintptr_t arena_pointer{};
    NativeWeaponContext context{};NativeBorrow borrow{};AnimationProfile animation{};AnimationNative provider{};
    unsigned row{},states{},locks{},unlocks{},cache_calls{};bool full{true};std::string order;
    explicit Memory(unsigned variant=3):row(synthetic_profile_fixture::row(variant)) {
        const auto& shape=*p::shape(row);const auto& known=*p::get(row);
        animation.profile_row=row;animation.weapon_index=shape.weapon;animation.profile_revision=shape.revision;animation.gun_bone=shape.gun;
        context.weapon_index=shape.weapon;borrow.model_count=2;borrow.bone_count=shape.total;borrow.skeleton_clock=7;
        borrow.dobj=reinterpret_cast<std::uintptr_t>(object.data());borrow.models_table=reinterpret_cast<std::uintptr_t>(table.data());
        borrow.tag_model=1;borrow.tag_local_bone=static_cast<std::uint16_t>(shape.flash_local);borrow.tag_bone=shape.flash;
        arena_pointer=reinterpret_cast<std::uintptr_t>(arena.data());put(object.data(),4,static_cast<unsigned short>(1));object[8]=21;arena[16]=1;
        reinterpret_cast<unsigned char*>(table.data())[8]=255;reinterpret_cast<unsigned char*>(table.data())[9]=static_cast<unsigned char>(shape.attachment);
        put(object.data(),0x50,3);put(object.data(),0x54,reinterpret_cast<std::uintptr_t>(matrices.data()));
        unsigned total=0;
        for(unsigned i=0;i<known.model.size();++i){const auto& entry=known.model[i];borrow.models[i]=table[i]=reinterpret_cast<std::uintptr_t>(model[i].data());borrow.model_bones[i]=static_cast<unsigned char>(entry.count);
            animation.counts[i]=static_cast<unsigned char>(entry.count);animation.roots[i]=static_cast<unsigned char>(entry.root);
            std::memcpy(model_names[i].data(),entry.name.data(),entry.name.size()+1);model[i][0]=reinterpret_cast<std::uintptr_t>(model_names[i].data());
            reinterpret_cast<unsigned char*>(model[i].data())[4]=static_cast<unsigned char>(entry.count);reinterpret_cast<unsigned char*>(model[i].data())[5]=static_cast<unsigned char>(entry.root);
            model[i][2]=reinterpret_cast<std::uintptr_t>(names[i].data());model[i][3]=reinterpret_cast<std::uintptr_t>(parents[i].data());model[i][7]=reinterpret_cast<std::uintptr_t>(bind[i].data());
            for(unsigned j=0;j<entry.count;++j){const auto index=entry.offset+j;const auto& bone=known.bone[index];
                animation.bone_ids[index]=borrow.bone_names[index]=names[i][j]=static_cast<unsigned short>((index+1)*8);animation.parent_deltas[index]=bone.parent;
                const auto script=16U*animation.bone_ids[index];arena[script]=1;std::memcpy(arena.data()+script+4,bone.name.data(),bone.name.size()+1);
                if(j>=entry.root)parents[i][j-entry.root]=bone.parent;bind[i][j]=bone.bind;std::memcpy(&matrices[index],bone.bind.data(),32);++total;
            }
        }
        provider={&context,borrow,&animation,{}};
        check(total==shape.total&&animation_assembly_copy(borrow,animation,&provider.assembly,reinterpret_cast<std::uintptr_t>(&arena_pointer)),"authored assembly metadata copies");
    }
    anim::LockedRequest request()const{const auto& shape=*p::shape(row);return {borrow.dobj,shape.gun,shape.flash,shape.total,7,false,true};}
    static Memory& self(void* value){return *static_cast<Memory*>(value);}
    static void lock(void* value,std::uintptr_t){auto& m=self(value);++m.locks;m.order+='L';}
    static void unlock(void* value,std::uintptr_t){auto& m=self(value);++m.unlocks;m.order+='U';}
    static bool state(void* value,const anim::LockedRequest& request,anim::CacheCopy* out){auto& m=self(value);++m.states;m.order+='S';
        return animation_copied_state(m.provider,m.borrow,request,out,reinterpret_cast<std::uintptr_t>(&m.arena_pointer),m.full);}
    static int cache(void* value,std::uintptr_t,unsigned bone,std::uintptr_t* out){auto& m=self(value);++m.cache_calls;m.order+='C';
        check(bone==p::shape(m.row)->flash,"cache targets published flash");*out=reinterpret_cast<std::uintptr_t>(m.matrices.data());put(m.object.data(),0x50,7);return 0;}
    static void hierarchy(void* value,std::uintptr_t,unsigned bone,std::uint32_t* bits){auto& m=self(value);m.order+='H';bits[bone>>5]|=0x80000000U>>(bone&31);}
    static void pose(void* value,std::uintptr_t,const std::uint32_t*){self(value).order+='P';}
    static void skeleton(void* value,std::uintptr_t,const std::uint32_t*){auto& m=self(value);m.order+='K';const auto& shape=*p::shape(m.row);
        for(const auto bone:{shape.gun,shape.flash}){auto bits=*reinterpret_cast<std::uint32_t*>(m.object.data()+0x3c+4*(bone>>5));
            put(m.object.data(),0x3c+4*(bone>>5),bits|(0x80000000U>>(bone&31)));}}
    static bool copy(void* value,const anim::LockedRequest& request,std::uintptr_t address,anim::NativeMat32* gun,anim::NativeMat32* flash){self(value).order+='X';return animation_copy(nullptr,request,address,gun,flash);}
    anim::LockedStatus run(anim::LockedCopy* out){return anim::capture_locked({this,lock,unlock,state,cache,hierarchy,pose,skeleton,copy},request(),out);}
};
}

int main(){
    olympia_enabled=true;
    for(unsigned variant=0;variant<8;++variant){const auto row=synthetic_profile_fixture::row(variant);const auto& shape=*p::shape(row);
        NativeWeaponContext context{};context.weapon_index=shape.weapon;AnimationProfile output{};auto copied=record(variant);
        check(animation_profile_record(context,copied,row,&output)&&output.profile_row==row&&output.profile_revision==p::runtime_revision&&output.gun_bone==shape.gun,
            "runtime record binds current weapon and copied profile");
        auto changed=copied;changed.canonical.model_payloads[1][0]^=1;
        check(!animation_profile_record(context,changed,row,&output)&&output==AnimationProfile{},"changed canonical payload rejects empty");
        ++context.weapon_index;check(!animation_profile_record(context,copied,row,&output),"different weapon cannot reuse row");
        context.weapon_index=shape.weapon;copied.assembly.model_parents[1]^=1;check(!animation_profile_record(context,copied,row,&output),"different assembly cannot reuse row");
    }
    for(unsigned variant=0;variant<8;++variant){auto memory=std::make_unique<Memory>(variant);anim::LockedCopy output{};
        check(memory->run(&output)==anim::LockedStatus::accepted&&memory->order=="LSCSHPKSXSU"&&memory->unlocks==1&&output.calculated,
            "runtime rows keep lock state cache hierarchy pose skeleton copy unlock order");
    }
    {auto memory=std::make_unique<Memory>();anim::CacheCopy output{};memory->full=false;
        check(!Memory::state(memory.get(),memory->request(),&output),"runtime provider requires full current payload");}
    {auto memory=std::make_unique<Memory>();anim::LockedCopy output{};memory->bind[1][6][4]^=1;
        check(memory->run(&output)!=anim::LockedStatus::accepted&&memory->unlocks==1&&!output.calculated,"changed current bind rejects and unlocks once");}
    {auto memory=std::make_unique<Memory>();anim::LockedCopy output{};++memory->borrow.tag_local_bone;
        check(memory->run(&output)!=anim::LockedStatus::accepted&&memory->unlocks==1,"changed selected muzzle rejects");}
    std::printf("runtime_provider_checks=%u failures=%u production_provider=1 native_helpers=0\n",checks,failures);return failures?1:0;
}
