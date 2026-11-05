// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_reflection_program.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <d3d9types.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace bovr::t5 {
namespace {
bool digest(std::span<const std::uint32_t> data,std::string_view expected,std::array<char,65>* output=nullptr) noexcept {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};std::array<unsigned char,32> bytes{};
    bool good=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(good)good=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(good)good=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<std::uint32_t*>(data.data())),static_cast<ULONG>(data.size_bytes()),0)>=0;
    if(good)good=BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr char hex[]="0123456789abcdef";
    if(!good)return false;
    if(output){for(unsigned i=0;i<32;++i){(*output)[2*i]=hex[bytes[i]>>4];(*output)[2*i+1]=hex[bytes[i]&15];}(*output)[64]=0;return true;}
    if(expected.size()!=64)return false;
    for(unsigned i=0;i<32;++i)if(expected[2*i]!=hex[bytes[i]>>4]||expected[2*i+1]!=hex[bytes[i]&15])return false;
    return true;
}
}
bool water_shader_sha256(std::span<const std::uint32_t> words,std::array<char,65>* output) noexcept {
    if(output)*output={};return output&&!words.empty()&&words.size()<=16384&&digest(words,{},output);
}
bool water_shader_fingerprint(std::span<const std::uint32_t> words,std::string_view expected) noexcept {
    return !words.empty()&&words.size()<=16384&&digest(words,expected);
}
bool water_reflection_uv(const render::HudEyeViewport& v,unsigned tw,unsigned th,unsigned iw,unsigned ih,WaterReflectionUv* out) noexcept {
    if(!out)return false;*out={};
    if(!tw||!th||tw>16384||th>16384||!iw||!ih||iw>16384||ih>16384||
       v.x<0||v.y<0||v.width<=0||v.height<=0||
       static_cast<std::uint64_t>(v.x)+v.width>tw||static_cast<std::uint64_t>(v.y)+v.height>th)return false;
    // Require the same aspect ratio, allowing a uniformly scaled resolve.
    if(static_cast<std::uint64_t>(tw)*ih!=static_cast<std::uint64_t>(th)*iw)return false;
    const double x=double(v.x)/tw,y=double(v.y)/th,w=double(v.width)/tw,h=double(v.height)/th;
    const double dx=.5/iw,dy=.5/ih;if(w<=2*dx||h<=2*dy)return false;
    const auto inward=[](double exact,bool lower){float f=static_cast<float>(exact);
        return (lower?double(f)<exact:double(f)>exact)?std::nextafter(f,lower?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity()):f;};
    out->affine={float(w),float(h),float(x),float(y)};
    out->bounds={inward(x+dx,true),inward(y+dy,true),inward(x+w-dx,false),inward(y+h-dy,false)};
    return true;
}
bool water_reflection_insert(std::span<const std::uint32_t> words,unsigned at,const std::array<std::uint32_t,4>& sample,std::vector<std::uint32_t>* out) noexcept {
    if(!out)return false;out->clear();
    if(words.size()<6||words.size()>16384||words[0]!=0xffff0300||at<1||at>words.size()-4||
       !std::equal(sample.begin(),sample.end(),words.begin()+at)||sample[0]!=0x03000042||
       (sample[1]&0x7ff)!=(sample[2]&0x7ff)||
       (sample[1]&0x70001800)!=0||(sample[2]&0x70001800)!=0||
       (sample[2]&0x00ff0000)!=0x00e40000)return false;
    bool site=false,ended=false;
    for(std::size_t i=1;i<words.size();) {
        const unsigned op=words[i]&65535;
        if(op==65535){ended=words[i]==65535&&i+1==words.size();break;}
        const unsigned n=op==65534?((words[i]>>16)&32767):((words[i]>>24)&15);
        const bool no_operands=op==D3DSIO_NOP||op==D3DSIO_RET||op==D3DSIO_ENDLOOP||
            op==D3DSIO_ENDREP||op==D3DSIO_ELSE||op==D3DSIO_ENDIF||op==D3DSIO_BREAK;
        if(n>=words.size()-i||(!n&&!no_operands)||(op>D3DSIO_BREAKP&&op!=D3DSIO_COMMENT))return false;
        if(i==at)site=true;
        // DEF/DEFI/DEFB words after their destination are literal values.
        const unsigned registers=(op==D3DSIO_DEF||op==D3DSIO_DEFI||op==D3DSIO_DEFB)?1:n;
        if(op!=65534)for(unsigned k=1;k<=registers;++k){const auto token=words[i+k];
            const auto kind=((token>>28)&7)|((token>>8)&24),reg=token&2047;
            if((token&0x80000000)&&kind==2&&(reg==water_affine_register||reg==water_bounds_register))return false;}
        i+=1+n;
    }
    if(!site||!ended)return false;
    const auto r=sample[2],dst=(r&0x70001800)|(r&0x7ff)|0x80030000;
    constexpr std::uint32_t ca=0xa0e40000|water_affine_register,cb=0xa0e40000|water_bounds_register;
    const std::array<std::uint32_t,13> added{0x04000004,dst,r,ca,(ca&~0x00ff0000)|0x00ee0000,
        0x0300000b,dst,r,cb,0x0300000a,dst,r,(cb&~0x00ff0000)|0x00ee0000};
    try{out->reserve(words.size()+added.size());out->insert(out->end(),words.begin(),words.begin()+at);
        out->insert(out->end(),added.begin(),added.end());out->insert(out->end(),words.begin()+at,words.end());return true;}
    catch(...){out->clear();return false;}
}
bool water_reflection_make_derivative(unsigned id,std::span<const std::uint32_t> words,std::vector<std::uint32_t>* out) noexcept {
    if(out)out->clear();if(!out||id>=water_reflection_programs.size())return false;
    const auto& p=water_reflection_programs[id];
    return words.size_bytes()==p.bytes&&digest(words,p.sha256)&&water_reflection_insert(words,p.sample_word,p.sample,out);
}
bool water_underwater_vertex_insert(std::span<const std::uint32_t> words,unsigned at,std::vector<std::uint32_t>* out) noexcept {
    if(!out)return false;out->clear();
    constexpr std::array<std::uint32_t,3> original{0x02000001,0xe0070001,0x80e40000}; // MOV o1.xyz,r0
    constexpr std::array<std::uint32_t,4> replacement{0x03000002,0xe0070001,0x80e40000,
        0xa0e40000|water_underwater_view_register}; // ADD o1.xyz,r0,c222
    if(words.size()<5||words.size()>16384||words[0]!=0xfffe0300||at<1||at>words.size()-3||
       !std::equal(original.begin(),original.end(),words.begin()+at))return false;
    bool site=false,ended=false;
    for(std::size_t i=1;i<words.size();) {
        const unsigned op=words[i]&65535;
        if(op==D3DSIO_END){ended=words[i]==D3DSIO_END&&i+1==words.size();break;}
        const unsigned n=op==D3DSIO_COMMENT?((words[i]>>16)&32767):((words[i]>>24)&15);
        const bool no_operands=op==D3DSIO_NOP||op==D3DSIO_RET||op==D3DSIO_ENDLOOP||
            op==D3DSIO_ENDREP||op==D3DSIO_ELSE||op==D3DSIO_ENDIF||op==D3DSIO_BREAK;
        if(n>=words.size()-i||(!n&&!no_operands)||(op>D3DSIO_BREAKP&&op!=D3DSIO_COMMENT))return false;
        if(op==D3DSIO_DP4){
            // Geometry DP4 has one destination and two source operands. A
            // relative address adds one explicit address operand; an oversized
            // count must never swallow several following geometry instructions.
            unsigned k=1;
            for(unsigned operand=0;operand<3;++operand){
                if(k>n||!(words[i+k]&0x80000000))return false;
                const bool relative=(words[i+k]&D3DSHADER_ADDRESSMODE_MASK)!=0;++k;
                if(relative){if(k>n||!(words[i+k]&0x80000000))return false;++k;}
            }
            if(k!=n+1)return false;
        }
        if(i==at)site=true;
        // Immediate DEF values and COMMENT payloads are not register operands.
        const unsigned registers=(op==D3DSIO_DEF||op==D3DSIO_DEFI||op==D3DSIO_DEFB)?1:n;
        if(op!=D3DSIO_COMMENT)for(unsigned k=1;k<=registers;++k){const auto token=words[i+k];
            if(!(token&0x80000000))continue;
            const auto kind=((token>>28)&7)|((token>>8)&24),reg=token&2047;
            const bool constant=kind==D3DSPR_CONST||kind==D3DSPR_CONSTINT||kind==D3DSPR_CONSTBOOL||
                kind==D3DSPR_CONST2||kind==D3DSPR_CONST3||kind==D3DSPR_CONST4;
            if((kind==D3DSPR_CONST&&reg==water_underwater_view_register)||
               (constant&&(token&D3DSHADER_ADDRESSMODE_MASK)))return false;
        }
        i+=1+n;
    }
    if(!site||!ended)return false;
    try {
        out->reserve(words.size()+1);
        out->insert(out->end(),words.begin(),words.begin()+at);
        out->insert(out->end(),replacement.begin(),replacement.end());
        out->insert(out->end(),words.begin()+at+original.size(),words.end());
        return true;
    }catch(...){out->clear();return false;}
}
bool water_underwater_vertex_make_derivative(std::span<const std::uint32_t> words,std::vector<std::uint32_t>* out) noexcept {
    if(out)out->clear();
    return out&&words.size_bytes()==water_underwater_vertex_bytes&&digest(words,water_underwater_vertex_sha256)&&
        water_underwater_vertex_insert(words,water_underwater_vertex_varying_word,out);
}
}
