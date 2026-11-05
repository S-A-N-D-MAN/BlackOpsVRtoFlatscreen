// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
namespace bovr::native_resolution {
struct Size {unsigned width{},height{};friend bool operator==(const Size&,const Size&)=default;};
inline constexpr Size clarity{3840,2160},recovery{2560,1440},high{5120,2880};
inline constexpr std::uintptr_t api_return=0x6b6881;
inline constexpr unsigned monitor_bytes=40,helper_return_stack_offset=0x3c,monitor_stack_offset=0x14;
inline bool permitted(Size s) noexcept {return s==recovery||s==clarity||s==high;}
inline Size requested(std::string_view text) noexcept {
    return text=="2560x1440"?recovery:text=="3840x2160"?clarity:text=="5120x2880"?high:Size{};
}
inline bool known_caller(std::uintptr_t call) noexcept {
    return call==0x6b7c20||call==0x6b7ca5||call==0x6b756f;
}
struct Rect {std::int32_t left{},top{},right{},bottom{};friend bool operator==(const Rect&,const Rect&)=default;};
struct Context {
    Size requested{},native{},caps{};
    std::uintptr_t api_caller{},helper_caller{};
    bool guarded{},native_values_stable{},windowed{},factory_matches{},monitor_matches{},stack_buffer_matches{};
};
struct Decision {Rect monitor{};bool allowed{},changed{};};
inline Decision plan(const Context& c,Rect monitor,unsigned bytes,bool native_success) noexcept {
    Decision out{monitor};
    if(!native_success||bytes!=monitor_bytes||!c.guarded||!c.native_values_stable||!c.windowed||
       !c.factory_matches||!c.monitor_matches||!c.stack_buffer_matches||c.api_caller!=api_return||!known_caller(c.helper_caller)||
       !permitted(c.requested)||c.native!=c.requested||c.caps.width<c.requested.width||c.caps.height<c.requested.height)return out;
    const auto width=std::int64_t(monitor.right)-monitor.left,height=std::int64_t(monitor.bottom)-monitor.top;
    if(width<=0||height<=0)return out;
    const auto right=std::int64_t(monitor.left)+std::max(width,std::int64_t(c.requested.width));
    const auto bottom=std::int64_t(monitor.top)+std::max(height,std::int64_t(c.requested.height));
    if(right>std::numeric_limits<std::int32_t>::max()||bottom>std::numeric_limits<std::int32_t>::max())return out;
    out.monitor.right=static_cast<std::int32_t>(right);out.monitor.bottom=static_cast<std::int32_t>(bottom);
    out.allowed=true;out.changed=out.monitor!=monitor;return out;
}
}
