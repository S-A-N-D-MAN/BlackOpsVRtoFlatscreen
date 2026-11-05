#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Owned sparse layout and protected native-memory copy only. No game process,
// native helper, GPU, original CreateCmd or scene wrapper runs in this fixture.
#include "t5/weapon_command_profile.hpp"
#include "t5/runtime_profile_catalog.hpp"
#include "input/profile_identity.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace q1_fixture {
namespace cp=bovr::t5::command_profile;
namespace p=bovr::input::profile_admission;
namespace cat=bovr::t5::borrowed_profile;
using U8=std::uint8_t;using U16=std::uint16_t;using U32=std::uint32_t;
unsigned checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
struct Span {U32 at{};std::size_t size{};};
struct Memory {
    std::map<U32,std::vector<U8>> blocks;
    std::vector<Span> reads,exact_bytes;
    bovr::t5::PreCommandSnapshot pre{};
    cp::Request request{};
    std::uint64_t ticks=1000;
    DWORD thread=GetCurrentThreadId();
    std::size_t fail_call{},mutate_call{};
    U32 mutate_address{};U8 mutate_value{};
    void* inaccessible{};
    bool protected_fault{};
    unsigned source_variant{},runtime_profile_row{};
    static constexpr U32 cg=0x6000000,owner=0x7100000,variant=0x7200000,definition=0x7300000;
    static constexpr U32 top=0x7400000,top_name=0x7410000,visibility=0x7500000,table=0x7600000,arena=0x7700000,server=0x7000000;
    static constexpr U16 pool=7;
    static constexpr U32 object=0x2489148+pool*0x7c;
    static constexpr U32 model(unsigned i){return 0x7800000+i*0x10000;}
    U8* pointer(U32 at,std::size_t size) {
        auto it=blocks.upper_bound(at);if(it==blocks.begin())return nullptr;--it;
        const auto offset=std::uint64_t(at)-it->first;
        if(offset>it->second.size()||size>it->second.size()-offset)return nullptr;
        return it->second.data()+static_cast<std::size_t>(offset);
    }
    void block(U32 at,std::size_t size){blocks.emplace(at,std::vector<U8>(size));}
    template<class T>void put(U32 at,T value){auto* ptr=pointer(at,sizeof(value));if(!ptr){block(at,sizeof(value));ptr=pointer(at,sizeof(value));}std::memcpy(ptr,&value,sizeof(value));}
    void bytes(U32 at,const void* value,std::size_t size){auto* ptr=pointer(at,size);if(!ptr){block(at,size);ptr=pointer(at,size);}std::memcpy(ptr,value,size);}
    void text(U32 at,std::string_view value){bytes(at,value.data(),value.size());put<U8>(at+static_cast<U32>(value.size()),0);}
    explicit Memory(unsigned row=0):source_variant(row%synthetic_profile_fixture::rows().size()),
        runtime_profile_row(synthetic_profile_fixture::row(source_variant)) {
        const auto& shape=*cat::shape(runtime_profile_row);const auto& known=*cat::get(runtime_profile_row);
        block(cg,0xc5e40);block(owner,0x34);block(variant,0x10);block(definition,0x600);block(top,0x30);
        block(top_name,64);block(visibility,0x20);block(table,5*p::max_models);block(arena,0x18000);block(server,0x1d00);block(object,0x7c);block(0x1a796f8,0x300);
        put<U32>(0x2ff5354,cg);put<U32>(0xc1c6d8,owner);put<U32>(owner,object);
        put<U16>(0x24e6150+1536*2,pool);put<U8>(0x2488548+pool,1);
        put<int>(0x2993850,20);put<int>(0x2481764,9000);put<int>(0x2910160,0);put<int>(0x2910164,9);put<int>(0x2911e2c,0);
        const auto ps=cg+0x8a364;
        put<int>(ps+0x1c4,100);put<U8>(ps+0x144,static_cast<U8>(shape.weapon));
        put<int>(cg+0x8a348,10000);put<int>(cg+0x8c158,10000);put<int>(cg+0x8a344,16);
        put<U32>(0xbe19a8+4*shape.weapon,variant);put<U32>(variant+8,definition);put<int>(definition+0x20,1);
        put<float>(cg+0x8c100+0x34,1);put<float>(cg+0x8c100+0x44,1);put<float>(cg+0x8c100+0x54,1);
        put<U32>(0xbcd150,top);put<U32>(top,top_name);put<int>(top+0x10,5);text(top_name,"player_topDownCamMode");
        put<U32>(0x2f67f28,visibility);put<U8>(visibility+0x18,1);put<U8>(cg+0xc5e04+2,20);
        put<U32>(0x1a796f8+0x13c,server);put<U32>(0x1c03140,server);put<int>(0x1a796f8+0x184,100);
        put<int>(server+0x1c4,100);put<int>(server+0x1a2c,2);
        put<U8>(object+9,2);put<U8>(object+0xa,static_cast<U8>(shape.total));put<U32>(object+0x78,table);
        put<U16>(object+4,1);put<U8>(object+8,21);put<U32>(0x3067c00,arena);put<U16>(arena+16,1);
        put<U8>(table+8,255);put<U8>(table+9,static_cast<U8>(shape.attachment));exact_bytes.push_back({table+8,2});exact_bytes.push_back({arena+20,21});
        for(unsigned i=0;i<2;++i) {
            const auto m=model(i);block(m,32);block(m+0x1000,256);block(m+0x2000,2*p::max_bones);block(m+0x3000,p::max_bones);block(m+0x4000,p::max_bones*32);
            put<U32>(table+4*i,m);put<U32>(m,m+0x1000);put<U8>(m+4,static_cast<U8>(known.model[i].count));put<U8>(m+5,1);
            put<U32>(m+8,m+0x2000);put<U32>(m+0xc,m+0x3000);put<U32>(m+0x1c,m+0x4000);text(m+0x1000,known.model[i].name);
            exact_bytes.push_back({m+0x1000,known.model[i].name.size()+1});
            for(unsigned j=0;j<known.model[i].count;++j) {
                const auto index=known.model[i].offset+j;const auto& bone=known.bone[index];const auto id=static_cast<U16>((index+1)*8);
                put<U16>(m+0x2000+j*2,id);put<U16>(arena+16*id,1);text(arena+16*id+4,bone.name);
                if(j)put<U8>(m+0x3000+j-1,bone.parent);
                bytes(m+0x4000+j*32,bone.bind.data(),32);
                if(index==shape.flash)put<U16>(0x23a566e,id);
                exact_bytes.push_back({m+0x2000+j*2,2});exact_bytes.push_back({arena+16*id+4,bone.name.size()+1});
                if(j)exact_bytes.push_back({m+0x3000+j-1,1});exact_bytes.push_back({m+0x4000+j*32,32});
            }
        }
        request.command.valid=true;request.command.native_thread=thread;request.command.command_number=20;
        request.command.native_frame_time=9000;request.command.connection_state=9;request.pre_sequence=2;request.gameplay_epoch=3;request.olympia_enabled=true;
        pre.context=request.command;pre.sequence=2;pre.gameplay_epoch=3;pre.observed_ms=ticks;
        pre.native_cg_identity=cg;pre.native_game_time=10000;pre.native_health=100;
        pre.native_gameplay_fields_valid=true;pre.gameplay_active=true;
    }
    void weapon(unsigned id){put<U8>(cg+0x8a364+0x144,static_cast<U8>(id));put<U32>(0xbe19a8+4*id,variant);}
    void gun(const bovr::input::ProfileModelCopy& copied) {
        const auto m=model(1);const unsigned arms=*pointer(model(0)+4,1);
        put<U8>(object+0xa,static_cast<U8>(arms+copied.bones.size()));
        put<U8>(m+4,static_cast<U8>(copied.bones.size()));put<U8>(m+5,static_cast<U8>(copied.root_count));
        text(m+0x1000,copied.name.bytes);
        for(unsigned j=0;j<copied.bones.size();++j){const auto& bone=copied.bones[j];const auto id=static_cast<U16>((arms+j+1)*8);
            put<U16>(m+0x2000+2*j,id);put<U16>(arena+16*id,1);text(arena+16*id+4,bone.name.bytes);
            if(j>=copied.root_count)put<U8>(m+0x3000+j-copied.root_count,static_cast<U8>(bone.parent_delta));
            bytes(m+0x4000+j*32,bone.bind_bits.data(),32);
            if(bone.name.bytes=="tag_flash")put<U16>(0x23a566e,id);
        }
    }
    void attachment(unsigned count=1,bool duplicate=false){
        const auto index=*pointer(object+9,1),offset=*pointer(object+0xa,1);
        std::array<U8,p::max_models> attachment_parents{};
        std::memcpy(attachment_parents.data(),pointer(table+4*index,index),index);
        const auto m=model(index);block(m,32);block(m+0x1000,256);block(m+0x2000,2*p::max_bones);
        block(m+0x3000,p::max_bones);block(m+0x4000,p::max_bones*32);
        put<U32>(table+4*index,m);
        bytes(table+4*(index+1),attachment_parents.data(),index);put<U8>(table+4*(index+1)+index,72);
        put<U8>(object+9,static_cast<U8>(index+1));put<U8>(object+0xa,static_cast<U8>(offset+count));
        put<U32>(m,m+0x1000);text(m+0x1000,"test_weapon_attachment");put<U8>(m+4,static_cast<U8>(count));put<U8>(m+5,1);
        put<U32>(m+8,m+0x2000);put<U32>(m+0xc,m+0x3000);put<U32>(m+0x1c,m+0x4000);
        for(unsigned j=0;j<count;++j){const auto id=static_cast<U16>((offset+j+1)*20);
            put<U16>(m+0x2000+2*j,id);put<U16>(arena+16*id,1);
            text(arena+16*id+4,duplicate&&j==0?"j_gun":"tag_fixture_attachment_"+std::to_string(j));
            if(j)put<U8>(m+0x3000+j-1,1);const auto identity=synthetic_profile_fixture::bind();bytes(m+0x4000+j*32,identity.data(),32);}
        if(duplicate){
            std::array<U8,23> data{};const auto bit=0x80000000U>>(offset%32);
            for(unsigned byte=0;byte<4;++byte)data[(offset/32)*4+byte]=static_cast<U8>(bit>>(8*byte));
            data[20]=static_cast<U8>(offset+1);data[21]=73;
            put<U8>(object+8,23);bytes(arena+20,data.data(),data.size());}
    }
    static constexpr U32 companion_variant=0x8200000,companion_definition=0x8300000,companion_models=0x8400000;
    void dual(unsigned primary=55,unsigned companion=56){
        weapon(primary);attachment(2);put<U8>(table+14,10);
        const unsigned offset=*pointer(object+0xa,1)-2;
        text(arena+16*((offset+1)*20)+4,"j_gun1");text(arena+16*((offset+2)*20)+4,"tag_flash1");
        put<float>(model(2)+0x4000+32+16,12.0F);
        block(companion_variant,16);block(companion_definition,0x600);block(companion_models,4);
        put<U8>(definition+0x56a,1);put<U32>(definition+0x594,companion);put<int>(definition+0x30,1);
        put<U32>(0xbe19a8+4*companion,companion_variant);put<U32>(companion_variant+8,companion_definition);
        put<U8>(companion_definition+0x56a,1);put<U32>(companion_definition+0x594,primary);
        put<int>(companion_definition+0x30,1);put<int>(companion_definition+0x20,1);
        put<U32>(companion_definition+4,companion_models);put<U32>(companion_models,model(2));
    }
    static bool copy(void* opaque,std::uintptr_t address,void* out,std::size_t size)noexcept {
        auto& m=*static_cast<Memory*>(opaque);
        try {
            if(address>UINT32_MAX)return false;
            m.reads.push_back({static_cast<U32>(address),size});const auto call=m.reads.size();
            if(call==m.fail_call) {
                if(m.protected_fault)return cp::protected_copy(nullptr,reinterpret_cast<std::uintptr_t>(m.inaccessible),out,size);
                return false;
            }
            auto* ptr=m.pointer(static_cast<U32>(address),size);if(!ptr)return false;
            if(!cp::protected_copy(nullptr,reinterpret_cast<std::uintptr_t>(ptr),out,size))return false;
            if(call==m.mutate_call)m.put<U8>(m.mutate_address,m.mutate_value);
            return true;
        }catch(...){return false;}
    }
    static bool command(void* opaque,bovr::t5::PreCommandSnapshot* out)noexcept{*out=static_cast<Memory*>(opaque)->pre;return true;}
    static DWORD current_thread(void* opaque)noexcept{return static_cast<Memory*>(opaque)->thread;}
    static std::uint64_t now(void* opaque)noexcept{return static_cast<Memory*>(opaque)->ticks;}
    cp::ReadHost host(){return {this,copy,command,current_thread,now};}
    cp::Status read(cp::Copy* out){reads.clear();return cp::read(host(),request,out);}
    static unsigned row_for(unsigned source_variant){return synthetic_profile_fixture::row(source_variant);}
};
}
