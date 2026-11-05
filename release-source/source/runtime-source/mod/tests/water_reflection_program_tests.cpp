// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_reflection_program.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstring>
int main(int argc,char** argv) {
    using namespace bovr::t5;unsigned failures{};
    auto check=[&](bool ok,const char* why){if(!ok){++failures;printf("FAIL %s\n",why);}};
    // Regression: the actual bound shader may be larger than the expected
    // underwater program; a guessed 399/473-DWORD stack destination is unsafe.
    std::vector<std::uint32_t> captured;unsigned returned_bytes{},capture_calls{};
    const auto larger_shader=[&](void* destination,unsigned* bytes) {
        ++capture_calls;if(!destination){*bytes=8192;return true;}
        if(capture_calls!=2||*bytes!=8192)return false;
        std::fill_n(static_cast<std::uint32_t*>(destination),2048,0xaabbccddu);return true;
    };
    check(water_shader_capture(larger_shader,&captured,&returned_bytes)&&capture_calls==2&&returned_bytes==8192&&captured.size()==2048&&captured.back()==0xaabbccddu,
        "larger unknown shader queries then reads exactly sized destination");
    for(unsigned bad_size:{0u,3u,65540u,UINT32_MAX}) {
        capture_calls=0;check(!water_shader_capture([&](void* destination,unsigned* bytes){++capture_calls;*bytes=bad_size;return destination==nullptr;},&captured,&returned_bytes)&&
            capture_calls==1&&captured.empty(),"invalid or excessive size never performs data read");
    }
    capture_calls=0;check(!water_shader_capture([&](void* destination,unsigned* bytes){++capture_calls;if(!destination){*bytes=16;return true;}*bytes=12;return true;},&captured,&returned_bytes)&&captured.empty()&&returned_bytes==12,
        "changed returned shader size rejected without guessed buffer");
    for(unsigned width:{2560u,3840u,5120u})for(unsigned eye:{0u,1u}) {
        WaterReflectionUv uv{};const bovr::render::HudEyeViewport v{int(eye*width/2),0,int(width/2),int(width*9/16)};
        check(water_reflection_uv(v,width,width*9/16,width,width*9/16,&uv),"all installed resolution eye mappings admitted");
        check(uv.affine==std::array<float,4>{.5f,1.f,eye*.5f,0.f},"eye-local affine has exact half width and eye offset");
        const double expected0=eye*.5+.5/width,expected1=(eye+1)*.5-.5/width;
        check(double(uv.bounds[0])>=expected0&&double(uv.bounds[2])<=expected1,"clamp rounds inward at both eye borders");
        check(uv.bounds[0]<uv.bounds[2]&&uv.bounds[1]<uv.bounds[3],"clamp has positive extent");
        WaterReflectionUv scaled{};check(water_reflection_uv(v,width,width*9/16,width/2,width*9/32,&scaled),"uniformly scaled reflection resolve admitted");
        check(scaled.affine==uv.affine&&scaled.bounds[0]>uv.bounds[0]&&scaled.bounds[2]<uv.bounds[2],"resolve texel clamp follows actual sampled dimensions");
    }
    WaterReflectionUv uv{};
    check(!water_reflection_uv({-1,0,10,10},20,10,20,10,&uv),"negative origin rejected");
    check(!water_reflection_uv({11,0,10,10},20,10,20,10,&uv),"viewport crossing target rejected");
    check(!water_reflection_uv({0,0,10,10},20,10,10,10,&uv),"mismatched reflection aspect rejected");
    check(!water_reflection_uv({0,0,1,1},2,2,2,2,&uv),"single texel extent rejected");
    check(water_reflection_uv({0,0,100,100},100,100,100,100,&uv)&&uv.affine==std::array<float,4>{1,1,0,0},"full target UV preserves identity");
    const std::array<std::uint32_t,4> sample{0x03000042,0x802f0002,0x80e40002,0xa0e40801};
    std::vector<std::uint32_t> original{0xffff0300,0x02000001,0x800f0002,0x80e40003};
    original.insert(original.end(),sample.begin(),sample.end());original.push_back(65535);
    const auto frozen=original;std::vector<std::uint32_t> result;
    check(water_reflection_insert(original,4,sample,&result),"synthetic reflected sample transformed");
    check(original==frozen,"original token stream not mutated");
    check(result.size()==original.size()+13&&std::equal(original.begin(),original.begin()+4,result.begin())&&
          std::equal(original.begin()+4,original.end(),result.begin()+17),"exactly one local insertion leaves all original instructions unchanged");
    check(result[4]==0x04000004&&result[9]==0x0300000b&&result[13]==0x0300000a,"affine then lower then upper clamp order");
    check(result[5]==0x80030002&&result[10]==0x80030002&&result[14]==0x80030002,"only original coordinate xy written");
    check(!water_reflection_insert(original,5,sample,&result)&&result.empty(),"unframed sample offset rejected");
    auto collision=original;collision[3]=0xa0e400dc;
    check(!water_reflection_insert(collision,4,sample,&result),"reserved c220 collision rejected");
    collision[3]=0xa0e400dd;check(!water_reflection_insert(collision,4,sample,&result),"reserved c221 collision rejected");
    auto malformed=original;malformed.pop_back();check(!water_reflection_insert(malformed,4,sample,&result),"unterminated shader rejected");
    malformed=original;malformed[1]=0x0f000001;check(!water_reflection_insert(malformed,4,sample,&result),"out of bounds instruction rejected");
    malformed=original;malformed[1]=1;check(!water_reflection_insert(malformed,4,sample,&result),"operandless MOV remains rejected");
    std::vector<std::uint32_t> branches{0xffff0300,0x01000028,0xe0e40800,
        0x02000001,0x800f0002,0x80e40003,0x0000002a,
        0x02000001,0x800f0002,0x80e40004,0x0000002b};
    branches.insert(branches.end(),sample.begin(),sample.end());branches.push_back(65535);
    check(water_reflection_insert(branches,11,sample,&result),"valid IF ELSE ENDIF control flow admitted for shadow shaders");
    check(result.size()==branches.size()+13&&std::equal(branches.begin(),branches.begin()+11,result.begin())&&
        std::equal(branches.begin()+11,branches.end(),result.begin()+24),"shadow branch tokens remain byte identical");
    std::vector<std::uint32_t> literals{0xffff0300,0x05000051,0xa00f0007,0xa0e400dc,0xa0e400dd,0,0};
    literals.insert(literals.end(),original.begin()+1,original.end());
    check(water_reflection_insert(literals,10,sample,&result),"DEF literal bits are not mistaken for c220 or c221 operands");
    for(unsigned p=0;p<water_reflection_programs.size();++p)check(!water_reflection_make_derivative(p,original,&result),"synthetic program cannot pass exact runtime fingerprint gate");
    check(!water_reflection_make_derivative(4,original,&result),"unregistered program rejected");
    // Seven geometry DP4 instructions precede the camera-relative varying.
    // This seam must change neither those instructions nor the later UV move.
    std::vector<std::uint32_t> vertex{0xfffe0300};
    for(unsigned axis=0;axis<3;++axis)vertex.insert(vertex.end(),{0x03000009,0x80000000u|(1u<<(16+axis)),0x90e40000,0xa0e40004u+axis});
    for(unsigned axis=0;axis<4;++axis)vertex.insert(vertex.end(),{0x03000009,0xe0000005u|(1u<<(16+axis)),0x80e40000,0xa0e40000u+axis});
    const unsigned varying=static_cast<unsigned>(vertex.size());
    vertex.insert(vertex.end(),{0x02000001,0xe0070001,0x80e40000,0x02000001,0xe0030004,0x90e40003,65535});
    const auto frozen_vertex=vertex;
    check(water_underwater_vertex_insert(vertex,varying,&result),"synthetic exact XYZ view varying accepts center-view ADD seam");
    check(vertex==frozen_vertex,"vertex input remains immutable");
    check(result.size()==vertex.size()+1&&std::equal(vertex.begin(),vertex.begin()+varying,result.begin())&&
        std::equal(vertex.begin()+varying+3,vertex.end(),result.begin()+varying+4),"all world geometry projection and UV instructions remain byte identical");
    const std::array<std::uint32_t,4> changed_varying{0x03000002,0xe0070001,0x80e40000,0xa0e400de};
    check(result.size()==vertex.size()+1&&std::equal(changed_varying.begin(),changed_varying.end(),result.begin()+varying),"only XYZ o1 camera vector adds reserved c222");
    check(!water_underwater_vertex_make_derivative(vertex,&result)&&result.empty(),"structural fixture cannot pass actual vertex fingerprint gate");
    for(unsigned token:{0xe00f0001u,0xe0030001u,0xe0070002u}){
        auto bad=vertex;bad[varying+1]=token;
        check(!water_underwater_vertex_insert(bad,varying,&result)&&result.empty(),"different varying destination or write mask rejected");
    }
    auto bad_vertex=vertex;bad_vertex[varying+2]=0x80e40001;
    check(!water_underwater_vertex_insert(bad_vertex,varying,&result),"different original camera-vector register rejected");
    bad_vertex=vertex;bad_vertex[0]=0xffff0300;
    check(!water_underwater_vertex_insert(bad_vertex,varying,&result),"pixel program cannot pass vertex seam guard");
    bad_vertex=vertex;bad_vertex[4]=0xa0e400de;
    check(!water_underwater_vertex_insert(bad_vertex,varying,&result),"existing c222 source operand rejects scratch collision");
    std::vector<std::uint32_t> relative_vertex{0xfffe0300,0x04000009,0x80010000,0x90e40000,0xa0e42004,0xb0000000};
    relative_vertex.insert(relative_vertex.end(),vertex.begin()+5,vertex.end());
    check(!water_underwater_vertex_insert(relative_vertex,varying+1,&result),"relative constant addressing rejects possible c222 alias");
    bad_vertex=vertex;bad_vertex.pop_back();
    check(!water_underwater_vertex_insert(bad_vertex,varying,&result),"unterminated vertex program rejected");
    bad_vertex=vertex;bad_vertex[1]=0x0f000009;
    check(!water_underwater_vertex_insert(bad_vertex,varying,&result),"misframed geometry instructions cannot authorize seam");
    check(!water_underwater_vertex_insert(vertex,varying+1,&result),"interior varying operand offset rejected");
    std::vector<std::uint32_t> vertex_literals{0xfffe0300,0x05000051,0xa00f0007,0xa0e400de,0xa0e42004,0,0};
    vertex_literals.insert(vertex_literals.end(),vertex.begin()+1,vertex.end());
    check(water_underwater_vertex_insert(vertex_literals,varying+6,&result),"DEF immediate bits are not mistaken for scratch or relative register uses");
    vertex_literals[2]=0xa00f00de;
    check(!water_underwater_vertex_insert(vertex_literals,varying+6,&result),"DEF destination c222 rejects reserved constant collision");
    std::vector<std::uint32_t> vertex_comment{0xfffe0300,0x0003fffe,0x02000001,0xe0070001,0x80e40000};
    vertex_comment.insert(vertex_comment.end(),vertex.begin()+1,vertex.end());
    check(!water_underwater_vertex_insert(vertex_comment,2,&result),"MOV-shaped comment payload cannot authorize replacement");
    check(water_underwater_vertex_insert(vertex_comment,varying+4,&result),"comment payload remains untouched before real instruction seam");
    // Signed offsets make the view varying cyclopean without touching either
    // eye's original projection. This also affects Fresnel transparency.
    const std::array<float,3> eye0{-30001.25f,36000.f,100.f},eye1{-29998.75f,36000.f,100.f},world{-30000.f,36500.f,75.f};
    for(unsigned axis=0;axis<3;++axis){
        const float left=(world[axis]-eye0[axis])+.5f*(eye0[axis]-eye1[axis]);
        const float right=(world[axis]-eye1[axis])+.5f*(eye1[axis]-eye0[axis]);
        check(left==right,"signed per-eye offset gives common camera-relative view vector");
    }
    // Optional locally supplied assets exercise real hashes without shipping
    // game assets in the repository or release package.
    if(argc>=2)for(unsigned p=0;p<water_reflection_programs.size();++p){
        const auto& record=water_reflection_programs[p];const auto path=std::filesystem::path(argv[1])/("ps-"+std::string(record.sha256)+".bin");
        std::ifstream file(path,std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
        check(bytes.size()==record.bytes,"local fixture exact size");if(bytes.size()!=record.bytes)continue;
        std::vector<std::uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
        const bool accepted=water_reflection_make_derivative(p,words,&result);
        if(!accepted)printf("actual fixture program=%u name=%s\n",p,record.name.data());
        check(accepted,"actual loaded shader hash and injection accepted");
        words.back()^=1;check(!water_reflection_make_derivative(p,words,&result),"single byte program mutation fails closed");
    }
    if(argc>=3){
        const auto path=std::filesystem::path(argv[2])/("vs-"+std::string(water_underwater_vertex_sha256)+".bin");
        std::ifstream file(path,std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
        check(bytes.size()==water_underwater_vertex_bytes,"local underwater vertex fixture exact size");
        if(bytes.size()==water_underwater_vertex_bytes){
            std::vector<std::uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
            const auto frozen_words=words;const unsigned at=water_underwater_vertex_varying_word;
            check(water_underwater_vertex_make_derivative(words,&result),"exact loaded underwater vertex fingerprint and seam admitted");
            check(result.size()==words.size()+1&&std::equal(words.begin(),words.begin()+at,result.begin())&&
                std::equal(words.begin()+at+3,words.end(),result.begin()+at+4),"actual geometry projection fog and UV tokens remain byte identical");
            check(words==frozen_words,"actual asset input is never mutated");
            words[at+1]^=1;check(!water_underwater_vertex_make_derivative(words,&result)&&result.empty(),"single word actual varying mutation fails fingerprint gate");
            words=frozen_words;words.pop_back();check(!water_underwater_vertex_make_derivative(words,&result),"actual vertex length mutation fails closed");
        }
    }
    printf("water reflection program checks: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
