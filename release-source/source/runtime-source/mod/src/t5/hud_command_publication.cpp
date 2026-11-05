// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hud_command_publication.hpp"
#include "render/hud_commands.hpp"
#include <windows.h>
#include <cstring>
#include <limits>
namespace bovr::t5 {
namespace {
template<class T>void read(std::uintptr_t address,T* out){std::memcpy(out,reinterpret_cast<const void*>(address),sizeof(T));}
bool snapshot(std::uintptr_t base,bool frontend,NativeHudFrame* output) noexcept {
    NativeHudFrame s{};
    __try {
        read(base+0x356a641,&s.renderer_active);
        read(base+(frontend?0x373708c:0x3a5b5f8),&s.buffer);
        if(s.buffer!=base+0x3740100&&s.buffer!=base+0x38c6400)return false;
        const unsigned slot=s.buffer==base+0x3740100?0u:1u;
        read(s.buffer+0x15d7a0,&s.frame);read(s.buffer+0x16cc34,&s.array);
        read(s.buffer+0x16cc3c,&s.view_count);read(s.buffer+0x16cc40,&s.view_array);
        read(s.buffer+0x16cd24,&s.shared_commands);
        if(s.array!=base+0x3737090+slot*24||s.view_count>4||s.view_array!=base+0x420f730+slot*0xe380)return false;
        if(frontend){std::uintptr_t current{};read(base+0x37370c0,&current);if(current!=s.array)return false;}
        read(s.array,&s.bytes);read(s.array+4,&s.used);read(s.array+8,&s.critical);read(s.array+12,&s.last);
        read(s.array+16,&s.capacity);read(s.array+20,&s.warning);read(base+0x356a480,&s.configured_views);
        if(s.configured_views<1||s.configured_views>4||s.capacity!=s.configured_views*0x18000||
           !s.bytes||(s.bytes&3)||s.used<0||s.critical<0||(s.used&3)||(s.critical&3)||s.critical>s.used||s.used>s.capacity||s.warning<0||s.warning>s.capacity||
           static_cast<std::uint64_t>(s.bytes)+static_cast<std::uint32_t>(s.capacity)>UINT32_MAX)return false;
        for(unsigned i=0;i<s.view_count;++i) {
            auto& v=s.views[i];v.address=s.view_array+i*0x38e0;
            read(v.address+0x2220,&v.commands);read(v.address+0x140,&v.scene);read(v.address+0x150,&v.display);read(v.address+0x38c4,&v.missile);
            if(v.commands&&((v.commands&3)||v.commands<s.bytes||v.commands-s.bytes>static_cast<unsigned>(s.capacity-4)))return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    *output=s;return true;
}
bool copy(std::uintptr_t address,void* output,std::size_t size) noexcept {
    __try{std::memcpy(output,reinterpret_cast<void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool equal_native(std::uintptr_t address,std::span<const std::uint8_t> bytes) noexcept {
    __try{return !std::memcmp(reinterpret_cast<void*>(address),bytes.data(),bytes.size());}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool capture_native_hud_publication(std::uintptr_t base,HudCommandPublication* output) noexcept {
    if(!output)return false;*output={};
    try {
        NativeHudFrame before{},after{};
        if(!snapshot(base,true,&before)||before.used<4||before.last!=before.bytes+before.used-4)return false;
        HudCommandPublication value{};value.native=before;value.bytes.resize(before.used);
        if(!copy(before.bytes,value.bytes.data(),value.bytes.size())||!snapshot(base,true,&after)||before!=after)return false;
        const auto* end=value.bytes.data()+value.bytes.size()-4;std::uint16_t length{};std::memcpy(&length,end,2);
        if(length!=4||end[2]!=0)return false; // Native ID0 terminates regardless of tag.
        *output=std::move(value);return true;
    } catch(...){return false;}
}
std::span<const std::uint8_t> published_hud_list(const HudCommandPublication& p,unsigned view) noexcept {
    if(view>=p.native.view_count||view>=4||p.native.used<4||p.bytes.size()!=static_cast<std::size_t>(p.native.used))return {};
    const auto commands=p.native.views[view].commands;
    if(!commands||commands<p.native.bytes||(commands&3)||commands-p.native.bytes>p.bytes.size()-4)return {};
    const auto start=static_cast<std::size_t>(commands-p.native.bytes);
    const auto bytes=std::span<const std::uint8_t>(p.bytes).subspan(start);
    const auto census=render::census_hud_commands(bytes);
    if(census.status!=render::CommandListStatus::complete)return {};
    return bytes.first(census.consumed_bytes);
}
bool read_native_command_frame(std::uintptr_t base,bool frontend,NativeHudFrame* output) noexcept {
    if(!output)return false;*output={};NativeHudFrame before{},after{};
    if(!snapshot(base,frontend,&before)||!snapshot(base,frontend,&after)||before!=after)return false;
    *output=before;return true;
}
std::span<const std::uint8_t> published_shared_list(const HudCommandPublication& p) noexcept {
    const auto commands=p.native.shared_commands;
    if(p.native.used<4||p.bytes.size()!=static_cast<std::size_t>(p.native.used)||!commands||
       commands<p.native.bytes||(commands&3)||commands-p.native.bytes>p.bytes.size()-4)return {};
    const auto all=std::span<const std::uint8_t>(p.bytes).subspan(commands-p.native.bytes);
    std::size_t offset{};
    while(offset+4<=all.size()) {
        std::uint16_t length{};memcpy(&length,all.data()+offset,2);
        const auto id=all[offset+2];
        if(length<4||(length&3)||length>all.size()-offset||id>=29)return {};
        if(id==0)return length==4?all.first(offset+4):std::span<const std::uint8_t>{};
        offset+=length;
    }
    return {};
}
bool match_native_shared_backend(std::uintptr_t base,const HudCommandPublication& p) noexcept {
    NativeHudFrame before{},after{};
    if(!snapshot(base,false,&before))return false;
    const auto& original=p.native;
    const auto same=[&](const NativeHudFrame& v){return v.buffer==original.buffer&&v.array==original.array&&
        v.bytes==original.bytes&&v.view_array==original.view_array&&v.frame==original.frame&&
        v.view_count==original.view_count&&v.capacity==original.capacity&&v.configured_views==original.configured_views&&
        v.shared_commands==original.shared_commands&&v.views==original.views&&v.renderer_active==1&&original.renderer_active==1;};
    const auto list=published_shared_list(p);
    return same(before)&&!list.empty()&&equal_native(original.bytes,p.bytes)&&
        snapshot(base,false,&after)&&before==after&&same(after);
}
bool match_native_hud_backend(std::uintptr_t base,const HudCommandPublication& publication,unsigned view) noexcept {
    NativeHudFrame before{},after{};
    if(!snapshot(base,false,&before))return false;
    const auto& published=publication.native;
    auto same_frame=[&](const NativeHudFrame& current){
        return current.buffer==published.buffer&&current.array==published.array&&current.bytes==published.bytes&&
            current.view_array==published.view_array&&current.frame==published.frame&&current.view_count==published.view_count&&
            current.capacity==published.capacity&&current.configured_views==published.configured_views&&current.views==published.views;
    };
    if(!same_frame(before))return false;
    const auto list=published_hud_list(publication,view);
    return !list.empty()&&equal_native(published.views[view].commands,list)&&snapshot(base,false,&after)&&before==after&&same_frame(after);
}
} // namespace bovr::t5
