// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_commands.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
namespace bovr::render {
namespace {
template<class T>T load(const std::uint8_t* p){T value{};std::memcpy(&value,p,sizeof(T));return value;}
bool finite_values(const std::uint8_t* p,std::initializer_list<unsigned> offsets) {
    for(const auto offset:offsets) if(!std::isfinite(load<float>(p+offset))) return false;
    return true;
}
bool recognized(unsigned id) {
    switch(id){case 0:case 1:case 2:case 3:case 4:case 6:case 7:case 10:case 12:case 15:case 17:case 19:case 20:case 27:return true;default:return false;}
}
bool valid_layout(unsigned id,const std::uint8_t* p,unsigned length) {
    switch(id) {
    case 0:return length==4;
    // Constant index bounds/material shader behavior require additional native
    // preflight before replay. Census only establishes the copied record shape.
    case 1:return length==24&&finite_values(p,{8,12,16,20});
    case 2:return length==20&&finite_values(p,{4,8,12,16});
    case 3:case 4:case 19:case 20:return true; // Exact pinned handler advances header only.
    case 6:return length==20&&load<std::int32_t>(p+12)>0&&load<std::int32_t>(p+16)>0;
    case 7:return length==24&&(load<std::int32_t>(p+4)==0||load<std::int32_t>(p+4)==1)&&
        load<std::int32_t>(p+16)>=0&&load<std::int32_t>(p+20)>=0;
    case 10:return length==48&&load<std::uint32_t>(p+4)!=0&&finite_values(p,{8,12,16,20,24,28,32,36,40})&&
        load<float>(p+16)>0&&load<float>(p+20)>=0&&load<float>(p+24)>=0;
    // STRETCH_PIC_ROTATE_XY rotates via native matrix path; record layout proven by
    // captured packet and native handler signature but still only census-validated here.
    case 12:return length==52&&load<std::uint32_t>(p+4)!=0&&finite_values(p,{8,12,16,20,24,28,32,36,40,48})&&
        load<float>(p+16)>0&&load<float>(p+20)>=0&&load<float>(p+24)>=0;
    // Pinned SP handler6E5A00: material+4, XY pair bases+8/+10/+18/+20,
    // homogeneous W+28, opaque packed color+2C (offsets here are hexadecimal).
    // Corners may be rotated/offscreen; do not reinterpret them as dimensions
    // or UVs, dereference the material, or infer shader/replay safety.
    case 15:return length==48&&load<std::uint32_t>(p+4)!=0&&finite_values(p,{8,12,16,20,24,28,32,36,40})&&
        load<float>(p+40)>0;
    case 17:return length>=96&&load<std::uint32_t>(p+20)!=0&&finite_values(p,{4,8,12,16,24,28})&&
        load<float>(p+12)>0&&load<float>(p+24)>0&&load<float>(p+28)>0&&std::memchr(p+92,0,length-92)!=nullptr;
    case 27:return length==8&&load<std::int32_t>(p+4)==0;
    default:return false;
    }
}
}
CommandCensus census_hud_commands(std::span<const std::uint8_t> bytes) noexcept {
    CommandCensus result{};
    if(bytes.empty()) return result;
    std::size_t offset=0;
    while(offset<bytes.size()) {
        if(bytes.size()-offset<4) {result.status=CommandListStatus::truncated_header;return result;}
        const auto* p=bytes.data()+offset;const auto length=load<std::uint16_t>(p);const unsigned id=p[2];
        if(id>=29) {result.status=CommandListStatus::invalid_id;return result;}
        if(length<4||(length&3)!=0) {result.status=CommandListStatus::invalid_length;return result;}
        if(length>bytes.size()-offset) {result.status=CommandListStatus::overrun;return result;}
        if(id==0&&length!=4) {result.status=CommandListStatus::invalid_length;return result;}
        ++result.records;
        const bool tagged=(p[3]&0x80)!=0;
        ++(tagged?result.tagged[id]:result.active[id]);
        if(!result.shortest[id]||length<result.shortest[id]) result.shortest[id]=length;
        result.longest[id]=std::max(result.longest[id],length);
        if(!tagged&&id) {
            if(!recognized(id)) result.unsupported_layout_mask|=1u<<id;
            else if(!valid_layout(id,p,length)) result.malformed_layout_mask|=1u<<id;
        }
        offset+=length;result.consumed_bytes=offset;
        // Native termination checks ID before window tagging. A tagged ID0
        // still terminates; it is never a command to skip into another list.
        if(id==0) {
            result.status=CommandListStatus::complete;
            result.all_active_layouts_recognized=!result.unsupported_layout_mask&&!result.malformed_layout_mask;
            return result;
        }
    }
    result.status=CommandListStatus::missing_terminator;return result;
}
const char* command_list_status_name(CommandListStatus s) noexcept {
    switch(s) {
    case CommandListStatus::complete:return "complete";case CommandListStatus::empty:return "empty";
    case CommandListStatus::truncated_header:return "truncated_header";case CommandListStatus::invalid_id:return "invalid_id";
    case CommandListStatus::invalid_length:return "invalid_length";case CommandListStatus::overrun:return "overrun";
    case CommandListStatus::missing_terminator:return "missing_terminator";
    }return "unknown";
}
}
