// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/stereo_dynamic_entities.hpp"
#include "t5/native_read_recipe.hpp"
#include <atomic>
#include <cstring>
#include <limits>

namespace bovr::t5 {
namespace {
#include "t5/stereo_dynamic_entities_guards.inc"
constexpr std::uintptr_t front=0x373708c,first=0x3740100,buffer_stride=0x186300;
constexpr std::uintptr_t view_first=0x420f730,view_array_stride=0xe380,view_stride=0x38e0;
constexpr std::uintptr_t frame_offset=0x15d7a0,index_offset=0x16cc38,count_offset=0x16cc3c;
constexpr std::uintptr_t views_offset=0x16cc40,parms_offset=0x16cbe0;
constexpr std::uintptr_t world_pointer=0x356a24c,counts=0x371f73c;
constexpr std::uintptr_t extent=view_first+view_array_stride+2*view_stride;
std::atomic<std::uintptr_t> configured_base{};
std::atomic<bool> configured_enabled{};
template<class T>bool get(std::uintptr_t address,T* out) noexcept {
    __try{std::memcpy(out,reinterpret_cast<const void*>(address),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool valid_range(std::uintptr_t address,std::size_t size,bool writable=false) noexcept {
    if(!address||!size||address>(std::numeric_limits<std::uintptr_t>::max)()-size)return false;
    const auto end=address+size;auto next=address;
    while(next<end){MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<const void*>(next),&region,sizeof region)||region.State!=MEM_COMMIT||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=region.Protect&0xffu;
        const bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        const bool writes=protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        if(!readable||(writable&&!writes))return false;
        const auto start=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>(std::numeric_limits<std::uintptr_t>::max)()-start)return false;
        const auto limit=start+region.RegionSize;if(limit<=next)return false;
        next=limit<end?limit:end;
    }return true;
}
bool reset_counts(std::uintptr_t address,const std::array<std::uint32_t,2>& expected) noexcept {
    // Native counters are adjacent, DWORD aligned, and on one committed page.
    // No array, pointer, draw list, visibility bit or backend surface is edited.
    __try{std::array<std::uint32_t,2> actual{};std::memcpy(actual.data(),reinterpret_cast<void*>(address),8);
        if(actual!=expected)return false;
        const std::uint32_t empty[2]{};std::memcpy(reinterpret_cast<void*>(address),empty,8);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}

bool validate_stereo_dynamic_native_guards(HMODULE executable) noexcept {
    NativeReadPreserve preserve;
    const auto base=reinterpret_cast<std::uintptr_t>(executable);
    for(const auto& guard:dynamic_guards)if(!native_guard::matches(base,guard))return false;
    return true;
}
bool configure_stereo_dynamic_entities(HMODULE executable) noexcept {
    NativeReadPreserve preserve;
    configured_enabled.store(false);configured_base.store(0);
    wchar_t setting[8]{};const auto length=GetEnvironmentVariableW(L"BOVR_STEREO_DYNAMIC_ENTITIES",setting,8);
    if(length==1&&setting[0]==L'0')return true;
    if(!validate_stereo_dynamic_native_guards(executable))return false;
    configured_base.store(reinterpret_cast<std::uintptr_t>(executable));configured_enabled.store(true);return true;
}
bool stereo_dynamic_entities_enabled() noexcept{return configured_enabled.load();}

bool StereoDynamicEntitiesScope::reject(const char* stage,const char* field,std::uintptr_t address,
                                       std::uint32_t expected,std::uint32_t actual) noexcept {
    if(good_)failure_={stage,field,address,expected,actual};good_=false;return false;
}
bool StereoDynamicEntitiesScope::validate(unsigned cameras,const char* stage) noexcept {
    if(!good_||!enabled_||!active_||GetCurrentThreadId()!=thread_||!producer_||
       !configured_enabled.load()||configured_base.load()!=base_)
        return reject(stage,"scope_or_thread");
    std::uint32_t owner{},frame{},count{},index{},parms{},views{},world{};
    const std::uintptr_t addresses[]{base_+front,buffer_+frame_offset,buffer_+count_offset,
        buffer_+parms_offset,buffer_+views_offset,base_+world_pointer};
    std::uint32_t* values[]{&owner,&frame,&count,&parms,&views,&world};
    const std::uint32_t expected[]{static_cast<std::uint32_t>(buffer_),frame_,cameras,cameras,
        static_cast<std::uint32_t>(views_),static_cast<std::uint32_t>(world_)};
    const char* fields[]{"frontend_owner","native_frame","camera_count","parms_count","view_array","world"};
    for(unsigned i=0;i<6;++i){if(!get(addresses[i],values[i]))return reject(stage,"read",addresses[i]);
        if(*values[i]!=expected[i])return reject(stage,fields[i],addresses[i],expected[i],*values[i]);}
    if(cameras){if(!get(buffer_+index_offset,&index))return reject(stage,"selected_read",buffer_+index_offset);
        if(index!=cameras-1)return reject(stage,"selected_view",buffer_+index_offset,cameras-1,index);
        for(unsigned eye=0;eye<cameras;++eye){std::uint32_t data{};std::uint8_t extra{};
            const auto view=views_+eye*view_stride;
            if(!get(view+0x3510,&data)||!get(view+0x38c4,&extra))return reject(stage,"view_read",view);
            if(data!=buffer_||extra)return reject(stage,"ordinary_view",view+0x3510,static_cast<std::uint32_t>(buffer_),data);}}
    const std::uintptr_t world_fields[]{world_+0x324,world_+0x328,world_+0x3bc,world_+0x3c0};
    for(unsigned i=0;i<4;++i){std::uint32_t actual{};const auto wanted=i<2?arrays_[i]:capacities_[i-2];
        if(!get(world_fields[i],&actual))return reject(stage,"world_field_read",world_fields[i]);
        if(actual!=wanted)return reject(stage,i<2?"dynamic_array":"dynamic_capacity",world_fields[i],wanted,actual);}
    std::array<std::uint32_t,2> current{};
    if(!get(base_+counts,&current))return reject(stage,"count_read",base_+counts);
    for(unsigned i=0;i<2;++i)if(current[i]>capacities_[i])
        return reject(stage,"count_capacity",base_+counts+i*4,capacities_[i],current[i]);
    return true;
}
bool StereoDynamicEntitiesScope::begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
                                     std::uint64_t producer) noexcept {
    NativeReadPreserve preserve;
    if(active_||finished_)return reject("begin","scope_reuse");
    enabled_=configured_enabled.load();if(!enabled_)return true;
    if(!base||base>(std::numeric_limits<std::uintptr_t>::max)()-extent||
       configured_base.load()!=base||(buffer!=base+first&&buffer!=base+first+buffer_stride)||!producer)
        return reject("begin","arguments");
    base_=base;buffer_=buffer;frame_=frame;producer_=producer;thread_=GetCurrentThreadId();
    views_=base+view_first+(buffer==base+first?0:view_array_stride);
    std::uint32_t world{};if(!get(base+world_pointer,&world)||!world||world&3u)
        return reject("begin","world_pointer",base+world_pointer);
    world_=world;
    if(world_>(std::numeric_limits<std::uintptr_t>::max)()-0x3c4||!valid_range(world_,0x3c4))
        return reject("begin","world_span",world_);
    if(!get(world_+0x324,&arrays_[0])||!get(world_+0x328,&arrays_[1])||
       !get(world_+0x3bc,&capacities_[0])||!get(world_+0x3c0,&capacities_[1]))
        return reject("begin","world_layout",world_);
    for(unsigned i=0;i<2;++i){const auto stride=i?4u:6u;
        // Native dynEntId is ushort. Client population may exceed 1024, so use
        // the retail world's own population rather than the donor's job limit.
        if(capacities_[i]>0xffffu||(arrays_[i]&1u)||
           (capacities_[i]&&!valid_range(arrays_[i],capacities_[i]*stride)))
            return reject("begin","dynamic_array_range",arrays_[i],capacities_[i]);}
    if(!valid_range(base+counts,8,true))return reject("begin","counter_write_range",base+counts);
    active_=true;if(!validate(0,"begin"))return false;
    std::array<std::uint32_t,2> initial{};if(!get(base+counts,&initial))return reject("begin","initial_count_read",base+counts);
    for(unsigned i=0;i<2;++i)if(initial[i])return reject("begin","initial_count",base+counts+i*4,0,initial[i]);
    return true;
}
bool StereoDynamicEntitiesScope::prepare_right() noexcept {
    NativeReadPreserve preserve;
    if(!enabled_)return good_;
    if(prepared_||finished_)return reject("prepare_right","scope_phase");
    if(!validate(1,"prepare_right")||!get(base_+counts,&left_))return reject("prepare_right","identity_or_count_read");
    if(!valid_range(base_+counts,8,true)||!reset_counts(base_+counts,left_))
        return reject("prepare_right","counter_reset",base_+counts);
    prepared_=true;return true;
}
bool StereoDynamicEntitiesScope::finish() noexcept {
    NativeReadPreserve preserve;
    if(!enabled_)return good_;
    if(finished_)return good_;
    if(!prepared_)return reject("finish","right_not_prepared");
    if(!validate(2,"finish")||!get(base_+counts,&right_))return reject("finish","identity_or_count_read");
    active_=false;finished_=true;return true;
}
}
