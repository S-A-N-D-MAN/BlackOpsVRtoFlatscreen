// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_exposure_scope.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
namespace bovr::t5 {
namespace {
std::atomic<std::uintptr_t> configured_base{};
std::atomic<bool> configured_enabled{};
bool probe{};std::atomic<unsigned> reports{};
thread_local OrdinaryExposureScope* active_scope{};
thread_local bool poisoned{};
template<class T>bool get(std::uintptr_t p,T* out) noexcept {
    if(p<0x10000||p>UINT32_MAX-sizeof(T))return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool replace(std::uintptr_t p,float expected,float value) noexcept {
    __try{float actual{};std::memcpy(&actual,reinterpret_cast<void*>(p),4);
        if(std::memcmp(&actual,&expected,4))return false;
        std::memcpy(reinterpret_cast<void*>(p),&value,4);std::memcpy(&actual,reinterpret_cast<void*>(p),4);
        return !std::memcmp(&actual,&value,4);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool owner_current(const OrdinaryExposureOwner& o) noexcept {
    __try{return o.current&&o.current(o.context);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool sane(float value) noexcept{return std::isfinite(value)&&value>=0.f&&value<=65536.f;}
bool post_update(const std::array<float,8>& s) noexcept {
    return sane(s[0])&&s[1]==0.f&&s[2]==0.f&&s[3]==0.f&&s[4]==s[0]&&s[5]==8.f&&s[6]==8.f&&s[7]==8.f;
}
}
bool configure_ordinary_exposure_scope(HMODULE executable) noexcept {
    NativeReadPreserve preserve;configured_enabled=false;configured_base=0;
    wchar_t value[8]{};const auto setting=GetEnvironmentVariableW(L"BOVR_STEREO_EXPOSURE",value,8);
    const bool requested=!(setting==1&&value[0]==L'0');
    const auto diagnostic=GetEnvironmentVariableW(L"BOVR_STEREO_EXPOSURE_PROBE",value,8);probe=diagnostic==1&&value[0]==L'1';
    const auto base=reinterpret_cast<std::uintptr_t>(executable);
    constexpr native_guard::Span guards[]{
        {0x2c8d69,5,"b018b1616081ef2e39990d35625155bbd15e56f0fedd9d6ad67966b87417026a"},
        {0x2c2170,349,"2e61c135a23843ab4eaa2da4aa4ab7006d60164b5ba32ca6ce935d77e22bf857"},
        {0x2c22d0,48,"20d476060a08db794669d8d71f5c79be1f76601910db4ab020a38a0c4d414b21"}};
    bool accepted=base==0x400000&&executable==GetModuleHandleW(nullptr);
    for(const auto& guard:guards)accepted=accepted&&native_guard::matches(base,guard);
    if(accepted){configured_base=base;configured_enabled=requested;}
    trace("t5_stereo_exposure_configured","accepted=%u enabled=%u probe=%u hooks_added=0 replay_bytes=4 commit_bytes_max=8 gameplay_time_writes=0",accepted,accepted&&requested,probe);
    return accepted;
}
bool ordinary_exposure_enabled() noexcept{return configured_enabled.load();}
bool OrdinaryExposureScope::identity(int count) const noexcept {
    if(!active_||GetCurrentThreadId()!=thread_||!producer_||configured_base.load()!=base_||!owner_current(owner_))return false;
    std::uint32_t buffer{},frame{},cg{},cameras{},parms{},array{},selected{};int time{},frame_time{},refdef_time{};
    if(!get(base_+0x373708c,&buffer)||buffer!=buffer_||!get(buffer_+0x15d7a0,&frame)||frame!=frame_||
       !get(base_+0x2bf5354,&cg)||cg!=cg_||refdef_!=cg_+0x8c100||
       !get(cg_+0x8a348,&time)||time!=time_||!get(cg_+0x8a344,&frame_time)||frame_time!=frame_time_||
       !get(refdef_+0x58,&refdef_time)||refdef_time!=refdef_time_||
       !get(buffer_+0x16cc3c,&cameras)||cameras>2||!get(buffer_+0x16cbe0,&parms)||parms!=cameras||
       (count>=0&&cameras!=static_cast<unsigned>(count))||!get(buffer_+0x16cc40,&array)||array!=views_)return false;
    if(cameras&&(!get(buffer_+0x16cc38,&selected)||selected!=cameras-1))return false;
    return true;
}
bool OrdinaryExposureScope::read_state(std::array<float,8>* out) const noexcept {
    std::array<float,7> fields{};
    if(!get(refdef_+0x18478,&(*out)[0])||!get(refdef_+0x183bc,&fields))return false;
    for(unsigned i=0;i<7;++i)(*out)[1+i]=fields[i];return true;
}
bool OrdinaryExposureScope::begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
    std::uintptr_t refdef,std::uint64_t producer,OrdinaryExposureOwner owner) noexcept {
    NativeReadPreserve preserve;enabled_=ordinary_exposure_enabled();if(!enabled_)return true;
    if(active_||active_scope||poisoned||base<0x10000||base>UINT32_MAX-0x4240000||
       (buffer!=base+0x3740100&&buffer!=base+0x38c6400)||refdef<0x10000||refdef>UINT32_MAX-0x1847c||(refdef&3u)){
        reason_="begin_identity";good_=false;return false;}
    base_=base;buffer_=buffer;refdef_=refdef;producer_=producer;frame_=frame;owner_=owner;thread_=GetCurrentThreadId();
    views_=base+0x420f730+(buffer==base+0x3740100?0:0xe380);
    std::uint32_t cg{};
    if(!get(base+0x2bf5354,&cg)||refdef!=std::uintptr_t(cg)+0x8c100||
       !get(cg+0x8a348,&time_)||!get(cg+0x8a344,&frame_time_)||!get(refdef+0x58,&refdef_time_)){
        reason_="begin_refdef";good_=false;return false;}
    cg_=cg;active_=true;active_scope=this;
    std::array<float,8> again{};
    if(!identity(0)||!read_state(&before_)||!read_state(&again)||std::memcmp(before_.data(),again.data(),32)||!sane(before_[0])){
        reason_="begin_state";good_=false;finish();return false;}
    return true;
}
bool OrdinaryExposureScope::prepare_right() noexcept {
    NativeReadPreserve preserve;if(!enabled_)return true;
    std::array<float,8> now{};
    if(!good_||prepared_||!have_left_||!identity(1)||!read_state(&now)||now!=left_){reason_="right_precondition";good_=false;return false;}
    pending_=true;
    if(!replace(refdef_+0x18478,left_[0],before_[0])){reason_="rewind_write";good_=false;finish();return false;}
    prepared_=true;return true;
}
void OrdinaryExposureScope::after_native(unsigned eye,bool completed) noexcept {
    NativeReadPreserve preserve;if(!enabled_)return;
    if(!completed){reason_="native_exception";good_=false;finish();return;}
    if(eye==0){
        if(have_left_||!identity(1)||!read_state(&left_)||!post_update(left_)){reason_="left_result";good_=false;return;}
        have_left_=true;get(views_+0x3534,&view_exposure_[0]);return;
    }
    if(eye!=1||!prepared_||!identity(2)||!read_state(&right_)||!post_update(right_)){
        reason_="right_result";good_=false;finish();return;}
    right_done_=true;get(views_+0x38e0+0x3534,&view_exposure_[1]);
    if(right_!=left_)reason_="different_native_volume_targets";
    if(!commit_left())good_=false;
}
bool OrdinaryExposureScope::commit_left() noexcept {
    if(!pending_)return true;
    std::array<float,8> now{},again{};
    if(!have_left_||!identity()||!read_state(&now)||!read_state(&again)||now!=again||
       !sane(now[0])||!sane(now[4])||now[1]!=left_[1]||now[2]!=left_[2]||now[3]!=left_[3]||
       now[5]!=left_[5]||now[6]!=left_[6]||now[7]!=left_[7]){reason_="commit_identity_or_conflict";return false;}
    // These are the only two changing outputs of the guarded native routine.
    // Restore its first invocation's value, including a partially completed
    // second invocation. The six fixed black/white RGB floats stay untouched.
    if(!identity()||!replace(refdef_+0x183c8,now[4],left_[4])||
       !identity()||!replace(refdef_+0x18478,now[0],left_[0])){reason_="commit_write";return false;}
    if(!read_state(&again)||again!=left_||!identity()){reason_="commit_readback";return false;}
    pending_=false;return true;
}
void OrdinaryExposureScope::report() noexcept {
    if(!probe||reported_||!enabled_)return;reported_=true;
    const auto serial=reports.fetch_add(1)+1;if(serial>4&&serial%120)return;
    trace("t5_stereo_exposure","sample=%u producer=%llu frame=%u pre=%.9g left=%.9g right=%.9g committed=%.9g view_left=%.9g view_right=%.9g same_native_target=%u complete=%u pending=%u safe=%u reason=%s gameplay_time_writes=0",
        serial,producer_,frame_,before_[0],left_[0],right_[0],pending_?-1.f:have_left_?left_[0]:before_[0],
        view_exposure_[0],view_exposure_[1],right_done_&&right_==left_,right_done_,pending_,safe(),reason_);
}
bool OrdinaryExposureScope::finish() noexcept {
    NativeReadPreserve preserve;if(!enabled_)return true;
    if(active_&&pending_&&!commit_left())good_=false;
    if(active_&&GetCurrentThreadId()!=thread_){good_=false;reason_="finish_thread";return false;}
    if(active_scope==this){if(pending_)poisoned=true;active_scope=nullptr;}
    active_=false;report();return safe();
}
void invoke_ordinary_exposure_native(OrdinaryExposureScope* scope,unsigned eye,void(*body)(void*),void* context) {
    bool completed=false;
    __try{body(context);completed=true;}
    __finally{if(scope)scope->after_native(eye,completed);}
}
}
