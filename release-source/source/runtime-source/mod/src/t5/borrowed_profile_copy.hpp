// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/borrowed_profile_shape.hpp"
#include <cstddef>
#include <cstring>
#include <limits>
namespace bovr::t5::borrowed_profile {
using Read=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
// All reads are bounded, no-allocation protected copies. Caller owns one native
// DObj lock and separately rechecks current pool, assembly and command identity.
// This checks EVERY known model/bone name, bind bit and native hierarchy byte.
template<std::size_t N,std::size_t M> inline bool current_payload(unsigned row,Read read,void* context,const std::array<std::uintptr_t,M>& model,
    const std::array<unsigned short,N>& ids,std::uintptr_t arena) noexcept {
    const auto* profile=get(row);if(!profile||!read||arena<0x10000||N>max_profile_bones||M>max_profile_models||
        profile->bone.size()>N||profile->model.empty()||profile->model.size()>M)return false;
    const auto copy=[&](std::uintptr_t p,void* out,std::size_t n){
        return p>=0x10000&&n&&n<=UINT32_MAX&&p<=UINT32_MAX-n+1&&read(context,p,out,n);};
    const auto text=[&](std::uintptr_t p,std::string_view expected){
        std::array<char,256> data{};return expected.size()+1<=data.size()&&copy(p,data.data(),expected.size()+1)&&
            std::memcmp(data.data(),expected.data(),expected.size())==0&&data[expected.size()]==0;};
    for(unsigned i=0;i<profile->model.size();++i){
        std::array<std::uint32_t,8> header{};if(!copy(model[i],header.data(),32))return false;
        const auto& m=profile->model[i];const auto* bytes=reinterpret_cast<const unsigned char*>(header.data());
        if(!m.count||!m.root||m.root>m.count||m.offset>profile->bone.size()||m.count>profile->bone.size()-m.offset)return false;
        if(bytes[4]!=m.count||bytes[5]!=m.root||!text(header[0],m.name))return false;
        for(unsigned j=0;j<m.count;++j){const auto index=m.offset+j;const auto& b=profile->bone[index];
            unsigned short id{};if(!copy(std::uintptr_t(header[2])+2*j,&id,2)||!id||id!=ids[index])return false;
            const std::uint64_t entry=std::uint64_t(arena)+16ULL*id;
            unsigned short refs{};if(entry>UINT32_MAX-4||!copy(static_cast<std::uintptr_t>(entry),&refs,2)||!refs||
               !text(static_cast<std::uintptr_t>(entry)+4,b.name))return false;
            if(j<m.root){if(b.parent)return false;}
            else {unsigned char parent{};if(!copy(std::uintptr_t(header[3])+j-m.root,&parent,1)||parent!=b.parent)return false;}
            std::array<std::uint32_t,8> bind{};
            if(!copy(std::uintptr_t(header[7])+32*j,bind.data(),32)||bind!=b.bind)return false;
        }
        std::array<std::uint32_t,8> after{};
        if(!copy(model[i],after.data(),32)||after!=header)return false;
    }
    return true;
}
}
