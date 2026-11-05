// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hud_compass_material.hpp"
#include "render/hud_eye_commands.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
namespace {
using Bytes=std::vector<std::uint8_t>;
unsigned checks{};
void check(bool v,const char* reason){++checks;if(!v){std::printf("FAIL %s\n",reason);std::exit(1);}}
Bytes file(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"captured fixture opens");return Bytes(std::istreambuf_iterator<char>(f),{});}
template<class T>void put(Bytes& b,unsigned at,T v){std::memcpy(b.data()+at,&v,sizeof v);}
unsigned decimal_after(std::string_view text,std::string_view key,std::size_t& cursor) {
    const auto key_at=text.find(key,cursor);check(key_at!=std::string_view::npos,"captured argument field exists");
    auto at=key_at+key.size();while(at<text.size()&&(text[at]==' '||text[at]=='\t'))++at;
    unsigned value{};const auto begin=at;
    while(at<text.size()&&text[at]>='0'&&text[at]<='9'){value=value*10u+unsigned(text[at]-'0');++at;}
    check(at!=begin,"captured argument decimal parses");cursor=at;return value;
}
unsigned hex_digit(char c) {
    if(c>='0'&&c<='9')return unsigned(c-'0');
    if(c>='a'&&c<='f')return unsigned(c-'a'+10);
    if(c>='A'&&c<='F')return unsigned(c-'A'+10);
    check(false,"captured argument hex parses");return 0;
}
Bytes captured_arguments(const std::filesystem::path& directory) {
    const auto encoded=file(directory/"capture.json");const std::string document(encoded.begin(),encoded.end());
    const auto material=document.find("\"name\": \"hud_border_dpad_compass\"");
    check(material!=std::string::npos,"compass material exists in private capture manifest");
    const auto list_begin=document.find("\"args\": [",material);check(list_begin!=std::string::npos,"compass argument list exists");
    const auto list_end=document.find(']',list_begin);check(list_end!=std::string::npos,"compass argument list terminates");
    const std::string_view list(document.data()+list_begin,list_end-list_begin);
    Bytes result;std::size_t cursor{};
    while((cursor=list.find("\"type\":",cursor))!=std::string_view::npos) {
        const auto type=decimal_after(list,"\"type\":",cursor);
        const auto destination=decimal_after(list,"\"dest\":",cursor);
        check(type<=0xffffu&&destination<=0xffffu,"captured argument tag fits");
        const auto tag=std::uint32_t(type|(destination<<16));const auto old=result.size();result.resize(old+4);put(result,static_cast<unsigned>(old),tag);
        const auto raw_key=list.find("\"raw\":",cursor);check(raw_key!=std::string_view::npos,"captured argument raw field exists");
        const auto quote=list.find('"',raw_key+6);check(quote!=std::string_view::npos&&quote+9<list.size()&&list[quote+9]=='"',"captured argument raw field is four bytes");
        for(unsigned i=0;i<4;++i)result.push_back(static_cast<std::uint8_t>((hex_digit(list[quote+1+i*2])<<4)|hex_digit(list[quote+2+i*2])));
        cursor=quote+10;
    }
    check(result.size()==40,"private manifest supplies ten compass argument words");return result;
}
struct Fixture {
    std::map<std::uint32_t,Bytes> blocks;
    unsigned material_reads{},calls{};bool changed{},failure{},throws{};
    static constexpr std::uint32_t material=0xea4008;
    Fixture(const std::filesystem::path& p) {
        blocks[material]=Bytes(192);put(blocks[material],0,0x20000u);put(blocks[material],0xb0,0x21000u);
        blocks[0x20000]=Bytes(24);std::memcpy(blocks[0x20000].data(),"hud_border_dpad_compass",23);
        blocks[0x21000]=Bytes(528);put(blocks[0x21000],0x18,0x22000u);
        auto& t=blocks[0x22000]=Bytes(28);put(t,6,std::uint16_t{1});put(t,12,0x23000u);put(t,16,0x24000u);t[20]=2;t[21]=0;t[22]=3;put(t,24,0x25000u);
        for(auto [s,code,size]:{std::tuple{0x23000u,0x26000u,120},std::tuple{0x24000u,0x27000u,110}}) {
            auto& b=blocks[s]=Bytes(16);put(b,4,0x33330000u);put(b,8,code);put(b,12,static_cast<std::uint16_t>(size));
        }
        blocks[0x25000]=captured_arguments(p);
        blocks[0x26000]=file(p/"hud_border_dpad_compass-0-vs.bin");blocks[0x27000]=file(p/"hud_border_dpad_compass-0-ps.bin");
    }
    bool read(std::uint32_t address,void* out,std::size_t size) {
        ++calls;if(throws)throw 1;if(failure)return false;
        if(address==material&&++material_reads==2&&changed)blocks[material][8]^=1;
        auto i=blocks.find(address);if(i==blocks.end()||size>i->second.size())return false;
        std::memcpy(out,i->second.data(),size);return true;
    }
    bool verify(std::uint32_t m=material){return bovr::t5::is_hud_compass_material(m,[&](auto a,void* o,auto n){return read(a,o,n);});}
};
bool policy(void* context,std::uint32_t m) noexcept {return static_cast<Fixture*>(context)->verify(m);}
}
int main(int argc,char** argv) {
    check(argc==3,"fixture directory and objective packet supplied");const Fixture original(argv[1]);
    {auto f=original;check(f.verify()&&f.material_reads==2,"exact shader and arguments admit after two complete reads");}
    for(auto [address,offset]:{std::pair{0x20000u,0u},std::pair{0x25000u,0u},std::pair{0x25000u,36u},std::pair{0x26000u,32u},std::pair{0x27000u,32u}}) {
        auto f=original;f.blocks[address][offset]^=1;check(!f.verify(),"wrong material name argument or shader rejects");
    }
    for(auto [address,offset,value]:{std::tuple{0x22000u,6u,2u},std::tuple{0x23000u,12u,119u},std::tuple{0x24000u,12u,111u}}) {
        auto f=original;put(f.blocks[address],offset,static_cast<std::uint16_t>(value));check(!f.verify(),"wrong pass count or shader size rejects");
    }
    for(auto [address,offset]:{std::pair{Fixture::material,0xb0u},std::pair{0x21000u,0x18u},std::pair{0x22000u,24u},std::pair{0x23000u,8u},std::pair{0x24000u,8u}})
        for(auto ptr:{0u,0xfffffff0u}) {auto f=original;put(f.blocks[address],offset,ptr);check(!f.verify(),"null or overflowing link rejects before dereference");}
    for(auto ptr:{0u,0xfffffff0u}) {auto f=original;check(!f.verify(ptr)&&f.calls==0,"material address bound precedes reads");}
    {auto f=original;f.changed=true;check(!f.verify(),"changed second capture rejects");}
    {auto f=original;f.failure=true;check(!f.verify(),"read failure rejects");}
    {auto f=original;f.throws=true;check(!f.verify(),"reader exception rejects");}
    {auto f=original;put(f.blocks[0x23000],4,0u);check(!f.verify(),"unloaded shader rejects");}
    {auto f=original;f.blocks[0x22000][22]=4;check(!f.verify(),"extra unreviewed argument rejects");}
    const auto packet=file(std::filesystem::path(argv[1]).parent_path()/"hud-active-native-01.bin");
    for(int eye=0;eye<2;++eye) {
        auto f=original;const auto out=bovr::render::fit_hud_commands_to_eye(packet,1280,720,{eye*640,0,640,720},{},{&f,policy});
        check(out.status==bovr::render::HudEyeStatus::accepted&&out.draws==26&&out.skipped_tagged==1,"captured campaign packet composes with actual verified shader policy");
    }
    const auto objective_packet=file(argv[2]);
    for(int eye=0;eye<2;++eye) {
        const auto out=bovr::render::fit_hud_commands_to_eye(objective_packet,1280,720,{eye*640,0,640,720});
        check(out.status==bovr::render::HudEyeStatus::accepted&&out.draws==6&&out.skipped_tagged==2,"actual objective-distance packet preserves depth and composes both eyes");
    }
    std::printf("hud_compass_checks=%u failures=0 native_calls=0\n",checks);
}
