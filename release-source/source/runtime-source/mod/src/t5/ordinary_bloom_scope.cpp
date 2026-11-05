// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/ordinary_bloom_scope.hpp"
#include "t5/postfx_resolution.hpp"
#include "t5/native_read_recipe.hpp"
#include <cstring>

namespace bovr::t5 {
namespace {
thread_local OrdinaryBloomScope* active_scope{};
thread_local bool poisoned_scope{};
constexpr std::array<unsigned,6> rgb_words{0,1,2,4,5,6};
template<class T>bool get(std::uintptr_t p,T* value) noexcept {
    __try {_ReadWriteBarrier();std::memcpy(value,reinterpret_cast<const void*>(p),sizeof(T));_ReadWriteBarrier();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool put(std::uintptr_t p,std::uint32_t value) noexcept {
    __try {*reinterpret_cast<volatile std::uint32_t*>(p)=value;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool current_seh(const OrdinaryBloomOwner& value) noexcept {
    __try {return value.current&&value.current(value.context);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool finite_words(const std::array<std::uint32_t,8>& values) noexcept {
    for(auto value:values)if((value&0x7f800000u)==0x7f800000u)return false;
    return true;
}
}
bool validate_ordinary_bloom_guards(HMODULE module) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    if(base!=0x400000||module!=GetModuleHandleW(nullptr))return false;
    // Exact retail RB_SetBloomConstants: view color/tint RGB -> constants135/136.
    __try {return !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x32dc67),15,"07810938be7a4afaf6aa52eb93812a986a8a3430ea05809566572a0d21f52af0")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x32dc96),24,"da8b73979b6d9b4233ab7cd8dc056c9cdcb2aa3cb723f5af26c0649a2f7167d3");}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool OrdinaryBloomScope::owner() const noexcept {return current_seh(owner_)&&!reentered_;}
bool OrdinaryBloomScope::identity(bool selected) const noexcept {
    if(GetCurrentThreadId()!=thread_||!base_||base_>UINT32_MAX-auxiliary_read_extent||eye_>1||layout_.count!=2||layout_.selected!=1)return false;
    const unsigned slot=layout_.buffer==base_+0x3740100?0u:layout_.buffer==base_+0x38c6400?1u:2u;
    if(slot>1||layout_.view_array!=base_+0x420f730+slot*0xe380||view_!=layout_.view_array+eye_*0x38e0)return false;
    std::uint32_t buffer{},frame{},count{},index{},array{};
    if(!get(base_+0x3a5b5f8,&buffer)||buffer!=layout_.buffer||!get(buffer+0x15d7a0,&frame)||frame!=layout_.frame||
       !get(buffer+0x16cc3c,&count)||count!=2||!get(buffer+0x16cc38,&index)||index>1||(selected&&index!=eye_)||
       !get(buffer+0x16cc40,&array)||array!=layout_.view_array)return false;
    for(unsigned i=0;i<2;++i){AuxiliaryViewSlot value{};value.address=layout_.view_array+i*0x38e0;std::uint32_t data{};
        if(!get(value.address+0x140,&value.scene)||!get(value.address+0x150,&value.display)||!get(value.address+0x160,&value.scissor)||
           !get(value.address+0x35f0,&value.targets)||!get(value.address+0x38c4,&value.extra)||
           !get(value.address+0x3510,&data)||data!=buffer||value!=layout_.slots[i])return false;}
    return true;
}
bool OrdinaryBloomScope::begin(std::uintptr_t base,const AuxiliaryViewLayout& layout,unsigned eye,OrdinaryBloomOwner current) noexcept {
    NativeReadPreserve preserve;
    if(poisoned_scope){failure_="thread_quarantined";return false;}
    if(claimed_||touched_||active_scope){if(active_scope){active_scope->reentered_=true;active_scope->good_=false;}failure_="nested";return false;}
    base_=base;layout_=layout;eye_=eye;view_=layout.view_array+eye*0x38e0;owner_=current;thread_=GetCurrentThreadId();
    good_=restored_=reentered_=false;failure_="entry";
    const std::array<render::HudEyeViewport,2> eyes{layout.slots[0].scene,layout.slots[1].scene};
    const std::array<AuxiliaryViewSlot,2> slots{layout.slots[0],layout.slots[1]};
    const auto& left=eyes[0];const auto& right=eyes[1];
    // Only supported side-by-side source allocations; both eyes share one preset.
    if(!exact_postfx_layout(layout,eyes,slots)||layout.count!=2||left.x||left.y||right.y||right.x!=left.width||
       right.width!=left.width||right.height!=left.height||
       left.width<=0||left.height<=0||left.width>2560||
       !postfx_resolution(static_cast<unsigned>(left.width)*2,static_cast<unsigned>(left.height)))return false;
    active_scope=this;claimed_=true;
    std::array<std::uint32_t,8> second{};
    if(!owner()||!identity(true)||!get(view_+0x3638,&before_)||!get(view_+0x3638,&second)||before_!=second||
       !finite_words(before_)||!owner()||!identity(true)){finish();return false;}
    failure_="apply";
    for(unsigned i=0;i<rgb_words.size();++i){const auto word=rgb_words[i];std::uint32_t actual{};
        if(!owner()||!identity(true)||!get(view_+0x3638+word*4,&actual)||actual!=before_[word]){finish();return false;}
        touched_|=1u<<i;
        if(!put(view_+0x3638+word*4,0)||!get(view_+0x3638+word*4,&actual)||actual){finish();return false;}}
    std::array<std::uint32_t,8> expected=before_,actual{};for(auto word:rgb_words)expected[word]=0;
    if(!owner()||!identity(true)||!get(view_+0x3638,&actual)||actual!=expected){finish();return false;}
    failure_="none";good_=true;return true;
}
bool OrdinaryBloomScope::finish() noexcept {
    NativeReadPreserve preserve;
    if(!claimed_&&!touched_)return restored_;
    if(GetCurrentThreadId()!=thread_){good_=false;failure_="restore_thread";return false;}
    if(!owner()){good_=false;if(!std::strcmp(failure_,"none"))failure_="owner_retired";}
    const bool owned=identity(false);
    if(!owned){good_=false;failure_="restore_identity";}
    if(owned){
        for(unsigned i=0;i<rgb_words.size();++i)if(touched_&(1u<<i)){
            const auto word=rgb_words[i];const auto address=view_+0x3638+word*4;std::uint32_t actual{};
            if(!get(address,&actual)||(actual!=0&&actual!=before_[word])){good_=false;failure_="restore_conflict";continue;}
            if(actual!=before_[word]&&!put(address,before_[word])){good_=false;failure_="restore_write";continue;}
            if(!get(address,&actual)||actual!=before_[word]){good_=false;failure_="restore_readback";continue;}
            touched_&=~(1u<<i);
        }
        if(!identity(false)){good_=false;failure_="restore_identity";}
    }
    restored_=touched_==0;
    if(restored_){claimed_=false;if(active_scope==this)active_scope=nullptr;}
    return restored_;
}
OrdinaryBloomScope::~OrdinaryBloomScope(){close();}
void OrdinaryBloomScope::close() noexcept {
    NativeReadPreserve preserve;
    finish();
    if(GetCurrentThreadId()==thread_&&active_scope==this){
        if(touched_)poisoned_scope=true;
        active_scope=nullptr;claimed_=false;
    }
}
void invoke_ordinary_bloom_native(OrdinaryBloomScope* scope,void(*body)(void*),void* context) {
    __try {body(context);}__finally {if(scope)scope->close();}
}
}
