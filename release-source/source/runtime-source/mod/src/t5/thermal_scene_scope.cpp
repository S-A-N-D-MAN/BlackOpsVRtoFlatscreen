// SPDX-License-Identifier: GPL-3.0-only
#include "t5/thermal_scene_scope.hpp"
#include "t5/infrared_scope_adapter.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <cstring>
#include <limits>
#if !defined(_M_IX86)
#error Native thermal scene scopes require MSVC x86.
#endif
namespace bovr::t5 {
#if defined(BOVR_THERMAL_SCENE_SCOPE_TEST)
namespace thermal_scene_fixture {
bool read_bytes(std::uintptr_t,void*,std::size_t) noexcept;
bool write_bytes(std::uintptr_t,const void*,std::size_t) noexcept;
bool guards() noexcept;
void apply_vision();
}
#endif
namespace thermal_scene_detail {
constexpr std::uintptr_t cg_slot=0x2ff5354,frontend_slot=0x3b3708c;
constexpr std::uintptr_t backend_slot=0x3e5b5f8;
constexpr std::uintptr_t first_buffer=0x3b40100,buffer_stride=0x186300;
constexpr std::uintptr_t first_views=0x460f730,view_array_stride=0xe380;
constexpr std::uintptr_t frame_offset=0x15d7a0,gfx_count_offset=0x15d7ac,gfx_offset=0x15d7b0;
constexpr std::uintptr_t parms_count_offset=0x16cbe0,view_index_offset=0x16cc38,view_count_offset=0x16cc3c,view_pointer_offset=0x16cc40;
constexpr std::uintptr_t rg_view_count=0x396eecc,scene_count=0x3ad1f2c,scene_entities=0x3ad1f30;
constexpr std::uintptr_t client_entities_slot=0x2ff6684,client_stride=0x31c;
thread_local bool vision_override{},vision_consumed{};
thread_local ThermalSceneScope* scene_owner{};
thread_local ThermalViewSlotScope* slot_owner{};
thread_local std::uintptr_t backend_owner{};
constexpr native_guard::Span guards[]{
    // Exclude only the separately guarded five-byte IR callsite owned by the
    // infrared adapter. All surrounding native conversion code is pinned.
    {0x45ea30,0xa0,"2c558f8084bec1692575a935c4ca954d9364ef8da8a0240ae9b2b0a56ac2a14c"},
    {0x45ead5,0x7f9,"7d8f93535083f651b3a72814e6cc760cbcc115d4461dad9127f21af4a27fbbd7"},
    {0x6bfdf0,0x43a,"b0fe85c677e5a8cc63b596fecc6350529267a29a9d971fa7a5087f7b710df488"},
    {0x6c8d74,0x22,"80aa4d9bf98e08fdc383b2bb073b0eb9697340ef82e70bc320527ecac7db87bb"},
    {0x6c6450,0x4c,"4b577ccb8eb18eb2aca778eb4e1e17cabceaac9a7d8e6194a986c76e16bf0ee7"}
};
template<class T>bool read(std::uintptr_t p,T* out) noexcept {
#if defined(BOVR_THERMAL_SCENE_SCOPE_TEST)
    return thermal_scene_fixture::read_bytes(p,out,sizeof(T));
#else
    __try {std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
template<class T>bool write(std::uintptr_t p,const T& value) noexcept {
#if defined(BOVR_THERMAL_SCENE_SCOPE_TEST)
    return thermal_scene_fixture::write_bytes(p,&value,sizeof(T));
#else
    __try {std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
bool main_thread(std::uint32_t thread) noexcept {
    std::uint32_t expected{};
    return thread&&thread==GetCurrentThreadId()&&read(0x251ca98,&expected)&&expected==thread;
}
bool buffer_owner(std::uintptr_t buffer,std::uint32_t frame,std::uint32_t thread) noexcept {
    if(!main_thread(thread)||(buffer!=first_buffer&&buffer!=first_buffer+buffer_stride))return false;
    std::uintptr_t current{};std::uint32_t actual_frame{};
    return read(frontend_slot,&current)&&current==buffer&&read(buffer+frame_offset,&actual_frame)&&actual_frame==frame;
}
bool apply_vision() noexcept {
    if(vision_override)return false;
    bool okay=false;vision_override=true;vision_consumed=false;
    __try {
        __try {
#if defined(BOVR_THERMAL_SCENE_SCOPE_TEST)
            thermal_scene_fixture::apply_vision();
#else
            reinterpret_cast<void(__cdecl*)(int,bool)>(0x45ea30)(0,false);
#endif
            okay=vision_consumed;
        }
        __finally {vision_override=false;}
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return okay;
}
bool pointer(std::uintptr_t p,std::size_t size) noexcept {
    return p>=0x10000&&size&&p<=(std::numeric_limits<std::uintptr_t>::max)()-size;
}
bool count_values(std::uintptr_t buffer,std::array<std::uint32_t,4>* out) noexcept {
    return read(rg_view_count,&(*out)[0])&&read(buffer+view_count_offset,&(*out)[1])&&
        read(buffer+view_index_offset,&(*out)[2])&&read(buffer+parms_count_offset,&(*out)[3]);
}
bool put_counts(std::uintptr_t buffer,std::uint32_t count,std::uint32_t index,std::uint32_t parms) noexcept {
    // Try every restoration even if an earlier write faults.
    bool okay=write(rg_view_count,count);
    okay=write(buffer+view_count_offset,count)&&okay;
    okay=write(buffer+view_index_offset,index)&&okay;
    return write(buffer+parms_count_offset,parms)&&okay;
}
}
bool thermal_scene_vision_override() noexcept {
    using namespace thermal_scene_detail;
    if(vision_override)vision_consumed=true;
    return vision_override;
}
bool validate_thermal_scene_native_guards() noexcept {
    NativeReadPreserve preserve;
#if defined(BOVR_THERMAL_SCENE_SCOPE_TEST)
    return thermal_scene_fixture::guards();
#else
    for(const auto& span:thermal_scene_detail::guards)if(!native_guard::matches_absolute(span))return false;
    return true;
#endif
}
bool ThermalSceneScope::begin(std::uintptr_t refdef,std::uintptr_t frontend,std::uint32_t native_frame) noexcept {
    using namespace thermal_scene_detail;NativeReadPreserve preserve;
    if(active_||scene_owner||vision_override)return false;
    static const bool native_valid=validate_thermal_scene_native_guards();
    const auto thread=GetCurrentThreadId();std::uintptr_t cg{},centities{};
    std::uint32_t count{},gfx_count{};
    if(!native_valid||!buffer_owner(frontend,native_frame,thread)||!read(cg_slot,&cg)||
       !pointer(cg,0xa4580)||refdef!=cg+0x8c100||!read(client_entities_slot,&centities)||
       !pointer(centities,client_stride*1024)||!read(scene_count,&count)||count>1024||
       !read(frontend+gfx_count_offset,&gfx_count)||!gfx_count||gfx_count>256||
       !ensure_infrared_scope_adapter())return false;
    changed_={};flags_={};actor_count_=0;
    if(!read(cg+0x8c164,&vision_)||!read(cg+0xa457c,&post_brightening_))return false;
    // Match a scene entity to its current client entity, including the exact
    // pose pointer and replicated entity number. The native actor path itself
    // requires highlight eligibility bit 2 and excludes type 0x12 (corpse).
    for(std::uint32_t i=0;i<count;++i){
        const auto entity=scene_entities+i*0x84;
        std::uint16_t gfx{},entnum{},type{};std::uintptr_t pose{};
        if(!read(entity+0x6c,&gfx)||!read(entity+0x6e,&entnum)||!read(entity+0x74,&pose))return false;
        if(!gfx||gfx>=gfx_count||entnum>=1023||pose!=centities+entnum*client_stride)continue;
        std::uint32_t number{},flags{};std::uint8_t eligible{},local{};
        if(!read(pose+0x164,&number)||!read(pose+0x222,&type)||!read(pose+0x310,&eligible)||
           !read(pose+4,&local)||!read(frontend+gfx_offset+gfx*0x18,&flags))return false;
        if(number!=entnum||local||(eligible&2)==0||type==0x12||(flags&0x400004)!=0x400004)continue;
        if(!changed_[gfx]){changed_[gfx]=true;flags_[gfx]=flags;++actor_count_;}
    }
    cg_=cg;buffer_=frontend;frame_=native_frame;thread_=thread;
    active_=true;restored_=false;scene_owner=this;
    for(unsigned i=0;i<changed_.size();++i)if(changed_[i]){
        if(!write(frontend+gfx_offset+i*0x18,flags_[i]|0x80u)){restore();return false;}
    }
    if(!apply_vision()){restore();return false;}
    return true;
}
bool ThermalSceneScope::restore() noexcept {
    using namespace thermal_scene_detail;NativeReadPreserve preserve;
    if(!active_)return restored_;
    std::uintptr_t current_cg{};
    bool okay=scene_owner==this&&buffer_owner(buffer_,frame_,thread_)&&read(cg_slot,&current_cg)&&current_cg==cg_;
    if(okay){
        for(unsigned i=0;i<changed_.size();++i)if(changed_[i])okay=write(buffer_+gfx_offset+i*0x18,flags_[i])&&okay;
        okay=write(cg_+0x8c164,vision_)&&okay;
        okay=write(cg_+0xa457c,post_brightening_)&&okay;
        for(unsigned i=0;i<changed_.size();++i)if(changed_[i]){
            std::uint32_t current_flags{};
            okay=read(buffer_+gfx_offset+i*0x18,&current_flags)&&current_flags==flags_[i]&&okay;
        }
        std::array<std::uint8_t,0x288> current_vision{};std::uint32_t current_brightening{};
        okay=read(cg_+0x8c164,&current_vision)&&current_vision==vision_&&
            read(cg_+0xa457c,&current_brightening)&&current_brightening==post_brightening_&&okay;
    }
    if(scene_owner==this)scene_owner=nullptr;
    active_=false;restored_=okay;
    if(!okay)trace("t5_thermal_scene_restore_rejected","buffer=0x%lx frame=%u owner_thread=%u",static_cast<unsigned long>(buffer_),frame_,thread_);
    return okay;
}
bool ThermalViewSlotScope::begin(std::uintptr_t frontend,std::uint32_t native_frame) noexcept {
    using namespace thermal_scene_detail;NativeReadPreserve preserve;
    if(active_||slot_owner)return false;
    static const bool native_valid=validate_thermal_scene_native_guards();
    const auto thread=GetCurrentThreadId();std::array<std::uint32_t,4> counts{};std::uintptr_t views{};
    const auto expected_views=first_views+(frontend==first_buffer?0:view_array_stride);
    if(!native_valid||!buffer_owner(frontend,native_frame,thread)||!count_values(frontend,&counts)||
       counts!=std::array<std::uint32_t,4>{2,2,1,2}||!read(frontend+view_pointer_offset,&views)||views!=expected_views)return false;
    buffer_=frontend;frame_=native_frame;thread_=thread;slot_=views+3*0x38e0;
    active_=true;restored_=false;built_=false;slot_owner=this;
    if(!put_counts(buffer_,3,1,3)){restore();return false;}
    return true;
}
bool ThermalViewSlotScope::built() const noexcept {
    using namespace thermal_scene_detail;
    if(!active_)return built_;
    std::array<std::uint32_t,4> counts{};
    return slot_owner==this&&buffer_owner(buffer_,frame_,thread_)&&count_values(buffer_,&counts)&&
        counts==std::array<std::uint32_t,4>{4,4,3,4};
}
bool ThermalViewSlotScope::restore() noexcept {
    using namespace thermal_scene_detail;NativeReadPreserve preserve;
    if(!active_)return restored_;
    built_=built();
    bool okay=slot_owner==this&&buffer_owner(buffer_,frame_,thread_);
    if(okay){
        std::array<std::uint32_t,4> counts{};
        const bool expected=count_values(buffer_,&counts)&&
            (counts==std::array<std::uint32_t,4>{3,3,1,3}||counts==std::array<std::uint32_t,4>{4,4,3,4});
        okay=put_counts(buffer_,2,1,2)&&expected;
        okay=count_values(buffer_,&counts)&&counts==std::array<std::uint32_t,4>{2,2,1,2}&&okay;
    }
    if(slot_owner==this)slot_owner=nullptr;
    active_=false;restored_=okay;
    if(!okay)trace("t5_thermal_slot_restore_rejected","buffer=0x%lx frame=%u owner_thread=%u",static_cast<unsigned long>(buffer_),frame_,thread_);
    return okay;
}
void invoke_thermal_scene_native(ThermalSceneScope* thermal,ThermalViewSlotScope* slot,void(*body)(void*),void* context) {
    __try {body(context);}
    __finally {if(thermal)thermal->restore();if(slot)slot->restore();}
}
bool invoke_thermal_backend_native(std::uintptr_t buffer,void(*body)(void*),void* context) {
    using namespace thermal_scene_detail;
    if(!body||backend_owner||(buffer!=first_buffer&&buffer!=first_buffer+buffer_stride))return false;
    std::uintptr_t current{};std::uint32_t frame{},count{},index{};
    if(!read(backend_slot,&current)||current!=buffer||!read(buffer+frame_offset,&frame)||
       !read(buffer+view_count_offset,&count)||!read(buffer+view_index_offset,&index)||
       !((count==2&&index==1)||(count==3&&index==2)))return false;
    bool invoked=false,restored=false;backend_owner=buffer;
    __try {
        if(write(buffer+view_count_offset,4u)&&write(buffer+view_index_offset,3u)){body(context);invoked=true;}
    }__finally {
        std::uint32_t after_frame{},after_count{},after_index{};
        if(read(backend_slot,&current)&&current==buffer&&read(buffer+frame_offset,&after_frame)&&after_frame==frame){
            restored=write(buffer+view_count_offset,count);
            restored=write(buffer+view_index_offset,index)&&restored;
            restored=read(buffer+view_count_offset,&after_count)&&after_count==count&&
                read(buffer+view_index_offset,&after_index)&&after_index==index&&restored;
        }
        backend_owner=0;
        if(!restored)trace("t5_thermal_backend_restore_rejected","buffer=0x%lx frame=%u",static_cast<unsigned long>(buffer),frame);
    }
    return invoked&&restored;
}
}
