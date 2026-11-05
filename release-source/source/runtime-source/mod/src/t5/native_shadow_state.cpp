// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/native_shadow_state.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t front=0x373708c,back=0x3a5b5f8,first=0x3740100,stride=0x186300;
constexpr std::uintptr_t views_first=0x420f730,views_stride=0xe380,view_stride=0x38e0;
constexpr std::uintptr_t sun=0x183f40,mask=0x15d780,techniques=0x88500;
constexpr std::uintptr_t spot_count=0x184390,spots=0x1843a0,lights=0x16cf20,light_count=0x183db0;
constexpr std::uintptr_t frame_offset=0x15d7a0,index_offset=0x16cc38,count_offset=0x16cc3c,views_offset=0x16cc40;
constexpr std::uintptr_t read_extent=views_first+views_stride+3*view_stride+0x3514;
constexpr std::uintptr_t inner_call_rva=0x2eb7c8,inner_draw_rva=0x2d2ec0;
std::uintptr_t inner_hook_base{},inner_native_target{};
std::array<std::uint8_t,5> inner_call_original{};
std::array<std::uint8_t,5> inner_call_owned{};
thread_local NativeShadowScope* inner_scope{};
template<class T>bool get(std::uintptr_t address,T* output) noexcept {
    __try {std::memcpy(output,reinterpret_cast<const void*>(address),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool put(std::uintptr_t address,const T& value) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::uint32_t word(const std::uint8_t* p) noexcept {std::uint32_t v{};std::memcpy(&v,p,4);return v;}
bool identity(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,std::uintptr_t view,
              bool frontend,unsigned eye=0,bool selected=true,bool inner=false,bool thermal=false) noexcept {
    if(!base||base>(std::numeric_limits<std::uintptr_t>::max)()-read_extent||
       (buffer!=base+first&&buffer!=base+first+stride))return false;
    const auto slot=buffer==base+first?0u:1u;
    std::uint32_t owner{},stamp{},count{},index{},array{},data{};
    const auto expected=base+views_first+slot*views_stride;
    if(view<expected||view>=expected+4*view_stride||(view-expected)%view_stride)return false;
    if(!(get(base+(frontend?front:back),&owner)&&owner==buffer&&get(buffer+frame_offset,&stamp)&&stamp==frame&&
        get(buffer+count_offset,&count)&&count>0&&count<=4&&get(buffer+index_offset,&index)&&index<count&&
        get(buffer+views_offset,&array)&&array==expected&&view<expected+count*view_stride&&
        get(view+0x3510,&data)&&data==buffer))return false;
    if(thermal)return count==4&&index==3&&view==expected+3*view_stride;
    if(frontend)return eye<2&&index==eye&&view==expected+eye*view_stride;
    if(!selected)return true;
    const auto physical=static_cast<unsigned>((view-expected)/view_stride);
    std::uint8_t ordinary_extra{};
    if(physical>1||!get(view+0x38c4,&ordinary_extra)||ordinary_extra)return false;
    if(index==physical)return true;
    // RB_Draw3D selected extra slot2 draws it and then ordinary slot1 in one
    // native call. The extra camera may not consume a different shadow setup.
    std::uint8_t extra{};std::uint32_t extra_data{},extra_shadow_type{};
    const auto auxiliary=expected+2*view_stride;
    return index==2&&count==3&&physical==1&&get(auxiliary+0x38c4,&extra)&&extra==1&&
        get(auxiliary+0x3510,&extra_data)&&extra_data==buffer&&
        get(auxiliary+0x184,&extra_shadow_type)&&(inner||extra_shadow_type==0);
}
bool inner_hook_current(std::uintptr_t base) noexcept {
    std::array<std::uint8_t,5> current{};
    return base&&base==inner_hook_base&&get(base+inner_call_rva,&current)&&current==inner_call_owned;
}
void record_failure(ShadowFailure* out,const char* stage,const char* field,std::uintptr_t address,
             std::uint32_t expected=0,std::uint32_t actual=0) noexcept {
    if(out)*out={stage,field,address,expected,actual};
}
bool compare_bytes(const void* expected,const void* actual,std::size_t count,std::uintptr_t address,
                   const char* stage,const char* field,ShadowFailure* out) noexcept {
    const auto* a=static_cast<const std::uint8_t*>(expected);const auto* b=static_cast<const std::uint8_t*>(actual);
    for(std::size_t i=0;i<count;++i)if(a[i]!=b[i]){record_failure(out,stage,field,address+i,a[i],b[i]);return false;}
    return true;
}
bool compare_light(const ShadowLightSlot& expected,const ShadowLightSlot& actual,std::uintptr_t buffer,
                   bool permit_slot_change,const char* stage,ShadowFailure* out) noexcept {
    const auto address=buffer+lights+expected.index*shadow_light_bytes;
    if(expected.index!=actual.index){record_failure(out,stage,"light_index",address,expected.index,actual.index);return false;}
    // Exact retail R_SetLightProperties stores the current eye-relative view
    // translation at +110,+114,+118, only in its type2 spot-light branch.
    // This is native cache state: never replay or overwrite it with our journal.
    const bool native_cache=expected.proof[0]==2&&actual.proof[0]==2;
    for(std::size_t i=0;i<shadow_light_bytes;++i){
        if(permit_slot_change&&i>=0x38&&i<0x3c)continue;
        if(native_cache&&i>=0x110&&i<0x11c)continue;
        if(expected.proof[i]!=actual.proof[i]){
            record_failure(out,stage,"light_proof",address+i,expected.proof[i],actual.proof[i]);return false;
        }
    }
    if(!permit_slot_change&&expected.value!=actual.value){
        record_failure(out,stage,"light_slot",address+0x38,expected.value,actual.value);return false;}
    return true;
}
bool owned_state_equal(const NativeShadowState& expected,const NativeShadowState& actual,std::uintptr_t buffer,
                       const char* stage,ShadowFailure* out) noexcept {
    if(!compare_bytes(expected.sun.data(),actual.sun.data(),expected.sun.size(),buffer+sun,stage,"sun",out)||
       !compare_bytes(expected.mask.data(),actual.mask.data(),sizeof(expected.mask),buffer+mask,stage,"mask",out)||
       !compare_bytes(expected.techniques.data(),actual.techniques.data(),expected.techniques.size(),buffer+techniques,stage,"techniques",out)||
       !compare_bytes(expected.spots.data(),actual.spots.data(),expected.spots.size(),buffer+spots,stage,"spots",out))return false;
    if(expected.spot_count!=actual.spot_count){record_failure(out,stage,"spot_count",buffer+spot_count,expected.spot_count,actual.spot_count);return false;}
    if(expected.light_count!=actual.light_count){record_failure(out,stage,"light_count",buffer+light_count,expected.light_count,actual.light_count);return false;}
    for(unsigned i=0;i<expected.spot_count;++i)
        if(!compare_light(expected.light_slots[i],actual.light_slots[i],buffer,false,stage,out))return false;
    return true;
}
void identity_failure(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,std::uintptr_t view,
                      const char* stage,ShadowFailure* out) noexcept {
    const auto slot=buffer==base+first?0u:1u;
    const std::uintptr_t addresses[]{base+back,buffer+frame_offset,buffer+views_offset,view+0x3510};
    const std::uint32_t expected[]{static_cast<std::uint32_t>(buffer),frame,
        static_cast<std::uint32_t>(base+views_first+slot*views_stride),static_cast<std::uint32_t>(buffer)};
    const char* names[]{"backend_owner","native_frame","view_array","view_data"};
    for(unsigned i=0;i<4;++i){std::uint32_t actual{};
        if(!get(addresses[i],&actual)){record_failure(out,stage,"identity_read",addresses[i],expected[i]);return;}
        if(expected[i]!=actual){record_failure(out,stage,names[i],addresses[i],expected[i],actual);return;}}
    std::uint32_t selected{};get(buffer+index_offset,&selected);
    record_failure(out,stage,"selected_view_or_layout",buffer+index_offset,
        static_cast<std::uint32_t>((view-(base+views_first+slot*views_stride))/view_stride),selected);
}
bool read_slot(std::uintptr_t buffer,std::uint32_t index,std::uint32_t count,ShadowLightSlot* output) noexcept {
    if(index>=count||count>255)return false;
    ShadowLightSlot s{};s.index=index;
    if(!get(buffer+lights+index*shadow_light_bytes,&s.proof))return false;
    s.value=word(s.proof.data()+0x38);*output=s;return true;
}
bool bounded_state(std::uintptr_t buffer,const NativeShadowState& s) noexcept {
    if(s.light_count>255||s.spot_count>4||word(s.sun.data()+0xa0)>2)return false;
    for(unsigned i=0;i<s.spot_count;++i){const auto* record=s.spots.data()+i*shadow_spot_bytes;
        const auto& slot=s.light_slots[i];const std::uint32_t index=record[0x180];
        if(index>=s.light_count||slot.index!=index||slot.value!=i||word(slot.proof.data()+0x38)!=i||
           !(s.mask[index/32]&(1u<<(index%32)))||word(record+0x184)!=buffer+lights+index*shadow_light_bytes||
           (slot.proof[0]!=2&&slot.proof[0]!=3))return false;
        for(unsigned j=0;j<i;++j)if(s.light_slots[j].index==index)return false;
    }
    return true;
}
bool read_state(std::uintptr_t buffer,NativeShadowState* output) noexcept {
    NativeShadowState s{};
    if(!get(buffer+sun,&s.sun)||!get(buffer+mask,&s.mask)||!get(buffer+techniques,&s.techniques)||
       !get(buffer+spot_count,&s.spot_count)||!get(buffer+light_count,&s.light_count)||!get(buffer+spots,&s.spots)||
       s.light_count>255||s.spot_count>4||word(s.sun.data()+0xa0)>2)return false;
    for(unsigned i=0;i<s.spot_count;++i){const auto* record=s.spots.data()+i*shadow_spot_bytes;
        const std::uint32_t index=record[0x180];
        if(index>=s.light_count||!(s.mask[index/32]&(1u<<(index%32)))||
           word(record+0x184)!=buffer+lights+index*shadow_light_bytes||
           !read_slot(buffer,index,s.light_count,&s.light_slots[i])||s.light_slots[i].value!=i||
           (s.light_slots[i].proof[0]!=2&&s.light_slots[i].proof[0]!=3))return false;
        for(unsigned j=0;j<i;++j)if(s.light_slots[j].index==index)return false;
    }
    if(!bounded_state(buffer,s))return false;*output=s;return true;
}
bool write_state(std::uintptr_t buffer,const NativeShadowState& s,ShadowFailure* out=nullptr,const char* stage="write") noexcept {
    auto store=[&](std::uintptr_t address,const auto& value,const char* field){
        if(put(address,value))return true;record_failure(out,stage,field,address);return false;};
    if(!store(buffer+sun,s.sun,"sun")||!store(buffer+mask,s.mask,"mask")||!store(buffer+techniques,s.techniques,"techniques")||
       !store(buffer+spots,s.spots,"spots")||!store(buffer+spot_count,s.spot_count,"spot_count"))return false;
    for(unsigned i=0;i<s.spot_count;++i)
        if(!store(buffer+lights+s.light_slots[i].index*shadow_light_bytes+0x38,s.light_slots[i].value,"light_slot"))return false;
    return true;
}
}
static bool capture_shadow(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,unsigned eye,bool thermal,EyeShadowState* output) noexcept {
    if(!output)return false;*output={};if(thermal?eye!=3:eye>1)return false;
    EyeShadowState s{};s.buffer=buffer;s.frame=frame;s.eye=eye;s.thermal_scope=thermal;
    s.view=base+views_first+(buffer==base+first?0:views_stride)+eye*view_stride;
    if(!identity(base,buffer,frame,s.view,true,eye,true,false,thermal)||!read_state(buffer,&s.state))return false;
    std::uint32_t world{};std::array<std::uint8_t,32> constants{};
    if(!get(base+0x356a24c,&world)||!world||!get(world+0xfc,&s.sun_index)||s.sun_index>=256)return false;
    if(s.state.mask[s.sun_index/32]&(1u<<(s.sun_index%32))){
        if(word(s.state.sun.data()+0xa0)!=2||!get(s.view+0x2980,&constants)||
           std::memcmp(constants.data(),s.state.sun.data()+0x80,constants.size()))return false;
    }
    NativeShadowState again{};
    if(!read_state(buffer,&again)||again!=s.state||!identity(base,buffer,frame,s.view,true,eye,true,false,thermal))return false;
    s.valid=true;*output=s;return true;
}
bool capture_eye_shadow(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,unsigned eye,EyeShadowState* output) noexcept {return capture_shadow(base,buffer,frame,eye,false,output);}
bool capture_thermal_shadow(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,EyeShadowState* output) noexcept {return capture_shadow(base,buffer,frame,3,true,output);}
bool NativeShadowScope::begin(std::uintptr_t base,const EyeShadowState& eye) noexcept {
    failure_={};
    if(deferred_){record_failure(&failure_,"dispatch_begin","already_armed",view_);good_=false;inner_phase_=5;return false;}
    std::uint32_t selected{};
    if(!active_&&!deferred_&&eye.valid&&eye.eye==1&&bounded_state(eye.buffer,eye.state)&&
       get(eye.buffer+index_offset,&selected)&&selected==2&&inner_hook_current(base)&&
       identity(base,eye.buffer,eye.frame,eye.view,false,0,true,true)) {
        if(inner_scope){record_failure(&failure_,"dispatch_begin","nested_scope",eye.view);return false;}
        // The extra camera must see native shared state. The journal begins
        // only when the unchanged native loop reaches ordinary view1.
        base_=base;buffer_=eye.buffer;view_=eye.view;frame_=eye.frame;thread_=GetCurrentThreadId();
        deferred_eye_=&eye;deferred_=true;inner_phase_=0;inner_callback_depth_=0;good_=true;restored_=true;
        inner_scope=this;return true;
    }
    return begin_journal(base,eye,false);
}
bool NativeShadowScope::begin_journal(std::uintptr_t base,const EyeShadowState& eye,bool inner) noexcept {
    failure_={};
    if(active_||!eye.valid||(eye.thermal_scope?eye.eye!=3:eye.eye>1)||!bounded_state(eye.buffer,eye.state)){
        record_failure(&failure_,"begin","copied_bounds",eye.buffer);return false;}
    if(!identity(base,eye.buffer,eye.frame,eye.view,false,0,true,inner,eye.thermal_scope)){
        identity_failure(base,eye.buffer,eye.frame,eye.view,"begin_identity",&failure_);return false;}
    base_=base;buffer_=eye.buffer;view_=eye.view;frame_=eye.frame;thread_=GetCurrentThreadId();
    inner_identity_=inner;thermal_identity_=eye.thermal_scope;
    good_=false;restored_=false;touched_count_=0;
    if(!read_state(buffer_,&before_)){record_failure(&failure_,"before_read","state",buffer_);return false;}
    if(before_.light_count!=eye.state.light_count){
        record_failure(&failure_,"before_read","light_count",buffer_+light_count,eye.state.light_count,before_.light_count);return false;}
    selected_=eye.state;
    for(unsigned i=0;i<selected_.spot_count;++i){
        if(!read_slot(buffer_,selected_.light_slots[i].index,before_.light_count,&touched_[i])){
            record_failure(&failure_,"light_identity","light_read",buffer_+lights+selected_.light_slots[i].index*shadow_light_bytes);return false;}
        if(!compare_light(selected_.light_slots[i],touched_[i],buffer_,true,"light_identity",&failure_))return false;
        ++touched_count_;
    }
    NativeShadowState again{};
    // Repeated-read stability remains byte-exact, including native cache bytes.
    if(!read_state(buffer_,&again)||again!=before_){record_failure(&failure_,"prewrite_stability","state",buffer_);return false;}
    if(!identity(base_,buffer_,frame_,view_,false,0,true,inner_identity_,thermal_identity_)){
        identity_failure(base_,buffer_,frame_,view_,"prewrite_identity",&failure_);return false;}
    // Journal is complete before the first temporary native write.
    active_=true;
    if(!write_state(buffer_,selected_,&failure_,"install_write")){finish_journal();return false;}
    if(!read_state(buffer_,&again)){record_failure(&failure_,"install_verify","state_read",buffer_);finish_journal();return false;}
    if(!owned_state_equal(selected_,again,buffer_,"install_verify",&failure_)){finish_journal();return false;}
    if(!identity(base_,buffer_,frame_,view_,false,0,true,inner_identity_,thermal_identity_)){
        identity_failure(base_,buffer_,frame_,view_,"install_identity",&failure_);finish_journal();return false;}
    good_=true;return true;
}
bool NativeShadowScope::finish() noexcept {
    if(deferred_&&GetCurrentThreadId()!=thread_){
        record_failure(&failure_,"dispatch_finish","thread",0,thread_,GetCurrentThreadId());good_=false;return false;}
    const bool restored=finish_journal();
    if(deferred_) {
        if(inner_scope==this)inner_scope=nullptr;
        else {record_failure(&failure_,"dispatch_finish","scope_owner",view_);good_=false;}
        if(inner_phase_!=4){record_failure(&failure_,"dispatch_finish","incomplete_native_order",view_,4,inner_phase_);good_=false;}
        deferred_=false;deferred_eye_=nullptr;inner_callback_depth_=0;
    }
    return restored;
}
bool NativeShadowScope::finish_journal() noexcept {
    if(!active_)return restored_;
    if(GetCurrentThreadId()!=thread_){record_failure(&failure_,"finish_owner","thread",0,thread_,GetCurrentThreadId());good_=false;return false;}
    if(!identity(base_,buffer_,frame_,view_,false,0,false)){
        identity_failure(base_,buffer_,frame_,view_,"finish_identity",&failure_);good_=false;return false;}
    // An outer native exception guard may already restore its selected index.
    // That invalidates this eye, but the same owned buffer/frame still requires
    // its shadow journal restored before the exception propagates.
    if(!identity(base_,buffer_,frame_,view_,false,0,true,inner_identity_,thermal_identity_)){
        identity_failure(base_,buffer_,frame_,view_,"finish_selection",&failure_);good_=false;}
    NativeShadowState current{};
    if(!read_state(buffer_,&current)){record_failure(&failure_,"native_mutation","state_read",buffer_);good_=false;}
    else if(!owned_state_equal(selected_,current,buffer_,"native_mutation",&failure_))good_=false;
    bool okay=write_state(buffer_,before_,&failure_,"restore_write");
    for(unsigned i=0;i<touched_count_;++i){const auto address=buffer_+lights+touched_[i].index*shadow_light_bytes+0x38;
        if(!put(address,touched_[i].value)){record_failure(&failure_,"restore_write","light_slot",address);okay=false;}}
    if(!read_state(buffer_,&current)){record_failure(&failure_,"restore_verify","state_read",buffer_);okay=false;}
    else if(!owned_state_equal(before_,current,buffer_,"restore_verify",&failure_))okay=false;
    for(unsigned i=0;i<touched_count_;++i){ShadowLightSlot restored{};
        if(!read_slot(buffer_,touched_[i].index,before_.light_count,&restored)){
            record_failure(&failure_,"restore_verify","light_read",buffer_+lights+touched_[i].index*shadow_light_bytes);okay=false;}
        else if(!compare_light(touched_[i],restored,buffer_,false,"restore_verify",&failure_))okay=false;}
    if(!identity(base_,buffer_,frame_,view_,false,0,false)){
        identity_failure(base_,buffer_,frame_,view_,"restore_identity",&failure_);okay=false;}
    restored_=okay;good_=good_&&okay;if(okay)active_=false;return okay;
}
bool prepare_shadow_inner_view(std::uintptr_t view) noexcept {
    auto* s=inner_scope;if(!s)return true;
    ++s->inner_callback_depth_;
    if(s->inner_callback_depth_!=1){
        record_failure(&s->failure_,"dispatch_inner","nested_native_call",view,1,s->inner_callback_depth_);
        s->good_=false;s->inner_phase_=5;return !s->active_;
    }
    if(GetCurrentThreadId()!=s->thread_||!s->deferred_||!s->deferred_eye_||!inner_hook_current(s->base_)||
       !identity(s->base_,s->buffer_,s->frame_,s->view_,false,0,true,true)) {
        record_failure(&s->failure_,"dispatch_inner","current_owner",view);s->good_=false;s->inner_phase_=5;return !s->active_;
    }
    if(s->inner_phase_==0&&view==s->view_+view_stride){s->inner_phase_=1;return true;}
    if(s->inner_phase_==2&&view==s->view_){
        s->inner_phase_=3;
        if(!s->begin_journal(s->base_,*s->deferred_eye_,true)){s->good_=false;s->inner_phase_=5;}
        return !s->active_||s->good_;
    }
    record_failure(&s->failure_,"dispatch_inner","native_view_order",view,s->inner_phase_==0?2:1,
        view>=s->view_-view_stride?static_cast<std::uint32_t>((view-(s->view_-view_stride))/view_stride):UINT32_MAX);
    s->good_=false;s->inner_phase_=5;return !s->active_;
}
void complete_shadow_inner_view() noexcept {
    auto* s=inner_scope;if(!s)return;
    if(s->inner_callback_depth_>1){--s->inner_callback_depth_;return;}
    if(s->inner_callback_depth_!=1){s->good_=false;s->inner_phase_=5;return;}
    --s->inner_callback_depth_;
    if(s->inner_phase_==1)s->inner_phase_=2;
    else if(s->inner_phase_==3){s->finish_journal();s->inner_phase_=4;}
}
namespace {
// The original inner call has EAX=view, no stack arguments, and returns void.
// Preserve every incoming/outgoing register, flags and FP/SIMD state around
// our callbacks. The native return address is now in this bridge, adding one
// return slot; no native callee argument is changed. The existing outer
// __finally restores/disarms the journal on native C++/SEH unwind.
__declspec(naked) void shadow_inner_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        push dword ptr [ebp+28]
        call prepare_shadow_inner_view
        add esp,4
        test eax,eax
        jz skip_native
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        call dword ptr [inner_native_target]
        jmp native_returned
    skip_native:
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
    native_returned:
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        call complete_shadow_inner_view
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
}
bool install_shadow_inner_dispatch(HMODULE executable) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(executable);
    if(inner_hook_base)return base==inner_hook_base&&inner_hook_current(base)&&validate_shadow_native_guards(executable);
    if(!validate_shadow_native_guards(executable))return false;
    std::array<std::uint8_t,5> before{};
    if(!get(base+inner_call_rva,&before))return false;
    inner_call_original=before;
    auto owned=before;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&shadow_inner_bridge)-(base+inner_call_rva+5));
    std::memcpy(owned.data()+1,&displacement,4);
    auto* call=reinterpret_cast<void*>(base+inner_call_rva);DWORD previous{},ignored{};
    if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&previous))return false;
    if(!get(base+inner_call_rva,&before)||before!=inner_call_original){VirtualProtect(call,5,previous,&ignored);return false;}
    inner_native_target=base+inner_draw_rva;inner_call_owned=owned;inner_hook_base=base;
    if(!put(base+inner_call_rva,owned)){
        put(base+inner_call_rva,inner_call_original);FlushInstructionCache(GetCurrentProcess(),call,5);
        VirtualProtect(call,5,previous,&ignored);
        // A reachable bridge must always retain its valid native passthrough
        // target, even when an attempted rollback cannot be proved complete.
        if(get(base+inner_call_rva,&before)&&before==inner_call_original)inner_hook_base=0;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(),call,5);
    if(!VirtualProtect(call,5,previous,&ignored)){
        put(base+inner_call_rva,inner_call_original);FlushInstructionCache(GetCurrentProcess(),call,5);
        VirtualProtect(call,5,previous,&ignored);
        if(get(base+inner_call_rva,&before)&&before==inner_call_original)inner_hook_base=0;
        return false;
    }
    return inner_hook_current(base);
}
void invoke_shadow_native(NativeShadowScope* scope,void(*body)(void*),void* context) {
    __try {body(context);}
    __finally {if(scope)scope->finish();}
}
bool validate_shadow_native_guards(HMODULE executable) noexcept {
    if(!executable)return false;
    const native_guard::Span guards[]{
#include "t5/native_shadow_guards.inc"
    };
    __try {const auto base=reinterpret_cast<std::uintptr_t>(executable);
        const bool owned=inner_hook_current(base);
        for(const auto& g:guards){
            if(!owned||g.address>inner_call_rva||g.address+g.size<inner_call_rva+inner_call_original.size()){
                if(!native_guard::matches(base,g))return false;
                continue;
            }
            if(g.size>308)return false;
            std::array<std::uint8_t,308> reconstructed{};
            std::memcpy(reconstructed.data(),reinterpret_cast<const void*>(base+g.address),g.size);
            std::memcpy(reconstructed.data()+inner_call_rva-g.address,inner_call_original.data(),inner_call_original.size());
            if(!native_guard::matches(reconstructed.data(),g.size,g.sha256))return false;
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
