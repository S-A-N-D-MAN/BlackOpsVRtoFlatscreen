// SPDX-License-Identifier: GPL-3.0-only
#include "t5/stereo_lod_fov_scope.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t first=0x3740100,stride=0x186300,views_first=0x420f730,views_stride=0xe380;
constexpr std::uintptr_t scale_slot=0x371fe74,bias_slot=0x371fde0,threshold_slot=0x371fb24;
constexpr float native_factor=2.1186731f;
std::atomic<std::uintptr_t> configured_base{};
std::atomic<bool> enabled{},probe{};std::atomic<unsigned> reports{},model_reports{};
std::array<std::atomic<unsigned>,2> eye_reports{};
std::array<unsigned,64> selected{};unsigned selected_count{};
thread_local StereoLodFovScope* active_scope{};thread_local bool poisoned{};
struct LastModel {unsigned world{},model{},visible{},lod{};bool valid{};};
thread_local std::array<std::array<LastModel,64>,2> last_models{};
template<class T>bool get(std::uintptr_t p,T* out) noexcept {
    if(p<0x10000||p>UINT32_MAX-sizeof(T))return false;
    __try{_ReadWriteBarrier();std::memcpy(out,reinterpret_cast<void*>(p),sizeof(T));_ReadWriteBarrier();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
unsigned bits(float f) noexcept {unsigned v{};std::memcpy(&v,&f,4);return v;}
float number(unsigned v) noexcept {float f{};std::memcpy(&f,&v,4);return f;}
bool finite(float f) noexcept{return (bits(f)&0x7f800000u)!=0x7f800000u;}
float multiply(float a,float b) noexcept{return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float divide(float a,float b) noexcept{return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a),_mm_set_ss(b)));}
bool replace(std::uintptr_t p,unsigned expected,unsigned value) noexcept {
    if(p<0x10000||p>UINT32_MAX-4||(p&3u))return false;
    __try{return static_cast<unsigned>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(p),static_cast<LONG>(value),static_cast<LONG>(expected)))==expected;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool owner_current(const StereoLodFovOwner& o) noexcept {
    __try{return o.current&&o.current(o.context);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool dvar_read(std::uintptr_t slot,const char* name,unsigned type,std::uintptr_t* pointer,std::array<unsigned char,0x70>* bytes) noexcept {
    unsigned p{},again{},name_pointer{},actual_type{};std::array<unsigned char,0x70> repeated{};char text[48]{};
    if(!get(slot,&p)||!p||(p&3u)||!get(p,bytes))return false;
    std::memcpy(&name_pointer,bytes->data(),4);std::memcpy(&actual_type,bytes->data()+0x10,4);
    const auto length=std::strlen(name);if(length>=sizeof text||actual_type!=type)return false;
    for(std::size_t i=0;i<=length;++i)if(!get(name_pointer+i,&text[i]))return false;
    if(std::strcmp(text,name)||!get(p,&repeated)||repeated!=*bytes||!get(slot,&again)||again!=p)return false;
    *pointer=p;return true;
}
bool parse_indices(const wchar_t* p) noexcept {
    selected_count=0;if(!p||!*p)return true;
    while(*p){if(selected_count==selected.size()||*p<L'0'||*p>L'9')return false;unsigned value{};
        do{const auto digit=unsigned(*p-L'0');if(value>6553u||(value==6553u&&digit>5u))return false;value=value*10+digit;++p;}while(*p>=L'0'&&*p<=L'9');
        for(unsigned i=0;i<selected_count;++i)if(selected[i]==value)return false;selected[selected_count++]=value;
        if(!*p)break;if(*p++!=L','||!*p)return false;
    }return true;
}
}
bool stereo_lod_fov_math(float tangent,float previous,float threshold,float scale,float bias,StereoLodFovMath* out) noexcept {
    if(!out||!finite(tangent)||tangent<=0||!finite(previous)||previous<0||!finite(threshold)||threshold<1||
       !finite(scale)||scale<0||!finite(bias))return false;
    const auto factor=multiply(tangent,native_factor);if(!finite(factor)||factor<=0)return false;
    const auto applied=factor>=previous||divide(previous,factor)>threshold?factor:previous;
    if(!finite(applied)||applied<=0)return false;
    StereoLodFovMath result{applied,divide(scale,applied),divide(bias,applied),0,0};
    if(!finite(result.temporary_scale)||!finite(result.temporary_bias))return false;
    result.expected_scale=multiply(result.temporary_scale,applied);result.expected_bias=multiply(result.temporary_bias,applied);
    if(!finite(result.expected_scale)||!finite(result.expected_bias))return false;
    // Do not accept a division which underflowed away a user's nonzero setting.
    if((scale!=0&&result.expected_scale==0)||(bias!=0&&result.expected_bias==0))return false;
    *out=result;return true;
}
bool configure_stereo_lod_fov_scope(HMODULE executable) noexcept {
    NativeReadPreserve preserve;configured_base=0;enabled=false;probe=false;wchar_t flag[8]{},indices[512]{};
    const bool requested=GetEnvironmentVariableW(L"BOVR_STEREO_LOD_FOV_COMPENSATION",flag,8)==1&&flag[0]==L'1';
    const bool diagnostic=GetEnvironmentVariableW(L"BOVR_STEREO_LOD_PROBE",flag,8)==1&&flag[0]==L'1';
    if(!requested&&!diagnostic)return true;
    const auto base=reinterpret_cast<std::uintptr_t>(executable);float factor{};
    constexpr native_guard::Span spans[]{
        {0x2c88d0,213,"7cb3f0e49e8b49115a21c50a49b6697195c680d5646d4e70998224f04726333c"},
        {0x329901,90,"f9b30f0cd3024280d954c7f8950a6b2002375b05da818da9089e4837be48fa11"},
        {0x123bc0,148,"2b6874ed7f269849bae9eb040be546c3ca97e54c154ccd0f931f64e385fbac93"}};
    bool accepted=base==0x400000&&executable==GetModuleHandleW(nullptr)&&get(base+0x7613c4,&factor)&&bits(factor)==bits(native_factor);
    for(const auto& s:spans)accepted=accepted&&native_guard::matches(base,s);
    const auto length=GetEnvironmentVariableW(L"BOVR_FOLIAGE_LOD_INDICES",indices,512);
    accepted=accepted&&length<512&&parse_indices(indices);
    if(accepted){configured_base=base;enabled=requested;probe=diagnostic;}
    trace("t5_stereo_lod_fov_configured","accepted=%u enabled=%u probe=%u selected=%u hooks_added=0 code_writes=0 current_float_bytes=8 ramp_writes=0 model_range_writes=0",accepted,accepted&&requested,accepted&&diagnostic,selected_count);
    return accepted;
}
bool stereo_lod_fov_scope_enabled() noexcept{return enabled.load()||probe.load();}
bool StereoLodFovScope::frame_current(bool built) const noexcept {
    if(!active_||active_scope!=this||GetCurrentThreadId()!=thread_||configured_base.load()!=base_||!owner_current(owner_))return false;
    unsigned owner{},frame{},count{},index{},array{},parms{},client{},data{},view_client{},tangent{};
    std::array<unsigned,3> origin{},view_origin{};std::array<int,4> viewport{},view_viewport{};
    if(!get(base_+0x373708c,&owner)||owner!=buffer_||!get(buffer_+0x15d7a0,&frame)||frame!=frame_||
       !get(buffer_+0x16cc3c,&count)||count!=(built?eye_+1:eye_)||!get(buffer_+0x16cbe0,&parms)||parms!=count||
       !get(buffer_+0x16cc38,&index)||((built||eye_!=0)&&index!=(built?eye_:0u))||
       !get(buffer_+0x16cc40,&array)||array!=view_-eye_*0x38e0||
       !get(refdef_+0x183a4,&client)||client!=0||!get(refdef_+0x20,&origin)||origin!=origin_||
       !get(refdef_,&viewport)||viewport!=viewport_||!get(refdef_+0x14,&tangent)||tangent!=tangent_bits_)return false;
    // An empty recycled buffer has no selected view. Native frame setup clears
    // the counts, but leaves viewInfoIndex until R_RenderScene selects eye 0.
    // Once any view exists, its selected index remains exact and authoritative.
    return !built||(get(view_+0x3510,&data)&&data==buffer_&&get(view_+0x188,&view_client)&&view_client==0&&
        get(view_+0x100,&view_origin)&&view_origin==origin_&&get(view_+0x140,&view_viewport)&&view_viewport==viewport_);
}
bool StereoLodFovScope::dvar_current(unsigned i,bool temporary) const noexcept {
        const auto& d=dvars_[i];unsigned pointer{};std::array<unsigned char,0x70> bytes{};
        if(!get(d.slot,&pointer)||pointer!=d.pointer||!get(d.pointer,&bytes))return false;
        const auto expected=temporary&&(pending_&(1u<<i))?temporary_[i]:before_[i];unsigned current{};std::memcpy(&current,bytes.data()+0x18,4);
        if(current!=expected)return false;std::memcpy(bytes.data()+0x18,&before_[i],4);if(bytes!=d.bytes)return false;
        return true;
}
bool StereoLodFovScope::dvars_current(bool temporary) const noexcept {return dvar_current(0,temporary)&&dvar_current(1,temporary);}
bool StereoLodFovScope::begin(std::uintptr_t base,std::uintptr_t buffer,unsigned frame,std::uintptr_t refdef,
    std::uint64_t producer,unsigned eye,StereoLodFovOwner owner) noexcept {
    NativeReadPreserve preserve;if(!stereo_lod_fov_scope_enabled())return false;
    reason_="begin_entry";
    if(active_||active_scope){if(active_scope){active_scope->good_=false;active_scope->reason_="nested_scope";}return false;}
    if(poisoned||base!=configured_base.load()||!producer||eye>1||(buffer!=base+first&&buffer!=base+first+stride)||
       refdef<0x10000||refdef>UINT32_MAX-0x184e4||!owner.current)return false;
    base_=base;buffer_=buffer;refdef_=refdef;producer_=producer;frame_=frame;eye_=eye;owner_=owner;thread_=GetCurrentThreadId();
    view_=base+views_first+(buffer==base+first?0:views_stride)+eye*0x38e0;
    reason_="begin_refdef_read";
    if(!get(refdef+0x20,&origin_)||!get(refdef,&viewport_)||!get(refdef+0x14,&tangent_bits_))return false;
    reason_="begin_refdef_shape";
    for(auto v:origin_)if(!finite(number(v)))return false;
    if(viewport_[0]<0||viewport_[1]<0||viewport_[2]<=0||viewport_[3]<=0)return false;
    dvars_[0].slot=base+scale_slot;dvars_[1].slot=base+bias_slot;
    reason_="begin_registered_scale_or_bias";
    if(!dvar_read(dvars_[0].slot,"r_lodScaleRigid",1,&dvars_[0].pointer,&dvars_[0].bytes)||
       !dvar_read(dvars_[1].slot,"r_lodBiasRigid",1,&dvars_[1].pointer,&dvars_[1].bytes)||dvars_[0].pointer==dvars_[1].pointer)return false;
    std::uintptr_t threshold_pointer{},lock_pointer{};std::array<unsigned char,0x70> threshold{},lock{};
    reason_="begin_threshold_or_locked_pvs";
    if(!dvar_read(base+threshold_slot,"r_fovScaleThresholdRigid",1,&threshold_pointer,&threshold)||
       !dvar_read(base+0x371fd24,"r_lockPvs",0,&lock_pointer,&lock)||lock[0x18])return false;
    float previous{},threshold_value{};std::memcpy(&threshold_value,threshold.data()+0x18,4);
    for(unsigned i=0;i<2;++i)std::memcpy(&before_[i],dvars_[i].bytes.data()+0x18,4);
    reason_="begin_native_hysteresis_math";
    if(!get(base+0x356ecf8+0x14,&previous)||!stereo_lod_fov_math(number(tangent_bits_),previous,threshold_value,number(before_[0]),number(before_[1]),&math_))return false;
    temporary_={bits(math_.temporary_scale),bits(math_.temporary_bias)};
    reason_="begin_original_preference_domain";
    for(unsigned i=0;i<2;++i){float minimum{},maximum{};std::memcpy(&minimum,dvars_[i].bytes.data()+0x58,4);std::memcpy(&maximum,dvars_[i].bytes.data()+0x5c,4);
        // The registered range constrains the persistent user preference.
        // Our finite temporary is an internal multiplicand, not a new setting:
        // the pinned native update multiplies it by the predicted FOV factor.
        // Retail scale minimum is 1, while the temporary reciprocal is <1.
        if(!finite(minimum)||!finite(maximum)||minimum>maximum||number(before_[i])<minimum||number(before_[i])>maximum)return false;}
    active_=true;active_scope=this;
    std::array<unsigned char,0x70> threshold_again{},lock_again{};float previous_again{};unsigned pointer{};
    reason_="begin_frame_authority";
    if(!frame_current(false)){active_scope=nullptr;active_=false;return false;}
    reason_="begin_registered_state_changed";
    if(!dvars_current(false)||!get(threshold_pointer,&threshold_again)||threshold_again!=threshold||
       !get(lock_pointer,&lock_again)||lock_again!=lock||!get(base+threshold_slot,&pointer)||pointer!=threshold_pointer||
       !get(base+0x356ecf8+0x14,&previous_again)||bits(previous_again)!=bits(previous)){
        active_scope=nullptr;active_=false;return false;}
    if(!enabled.load()){admitted_=true;reason_="none";return true;}
    changed_=true;
    for(unsigned i=0;i<2;++i){
        if(!frame_current(false)||!dvars_current(true)){good_=false;reason_="write_authority";finish();return false;}
        if(!replace(dvars_[i].pointer+0x18,before_[i],temporary_[i])){good_=false;reason_="write_conflict";finish();return false;}
        pending_|=1u<<i;
    }
    if(!dvars_current(true)||!frame_current(false)){good_=false;reason_="write_readback";finish();return false;}
    admitted_=true;reason_="none";return true;
}
bool StereoLodFovScope::restore() noexcept {
    if(!pending_)return true;
    if(GetCurrentThreadId()!=thread_)return false;bool okay=true;
    // Cleanup authority is the retained registered dvar identity and exact
    // bytes we wrote, even when a native exception invalidated the frame.
    for(unsigned i=2;i-->0;)if(pending_&(1u<<i)){
        if(!dvar_current(i,true)||!replace(dvars_[i].pointer+0x18,temporary_[i],before_[i])){okay=false;continue;}
        pending_&=~(1u<<i);
    }return okay&&dvars_current(false);
}
void StereoLodFovScope::observe() noexcept {
    if(!probe.load()||!frame_current(true))return;
    unsigned world{},count{},instances{},visibility{},lods{};
    if(!get(base_+0x356a24c,&world)||!world||!get(world+0x344,&count)||count>65536||
       !get(world+0x3a4,&instances)||!instances||instances>UINT32_MAX-count*0x4c||
       !get(world+0x370,&visibility)||!visibility||visibility>UINT32_MAX-count||
       !get(world+0x390,&lods)||!lods||lods>UINT32_MAX-4*((count+15)/16))return;
    for(unsigned i=0;i<selected_count;++i){const auto index=selected[i];if(index>=count)continue;
        unsigned model{},packed{},packed_again{},model_again{};unsigned char visible{},visible_again{};
        const auto instance=instances+index*0x4c;
        if(!get(instance+0x38,&model)||!model||!get(visibility+index,&visible)||!get(lods+4*(index/16),&packed)||
           !get(visibility+index,&visible_again)||visible_again!=visible||!get(lods+4*(index/16),&packed_again)||packed_again!=packed||
           !get(instance+0x38,&model_again)||model_again!=model||!frame_current(true))continue;
        const auto lod=(packed>>(2*(index%16)))&3u;auto& last=last_models[eye_][i];
        const bool transition=!last.valid||last.world!=world||last.model!=model||last.visible!=visible||(visible&&last.lod!=lod);
        last={world,model,visible,lod,true};if(!transition&&producer_%120)continue;
        const auto serial=model_reports.fetch_add(1);if(serial>=4096)continue;
        trace("t5_stereo_lod_model","sample=%u producer=%llu frame=%u eye=%u index=%u model=0x%x visible=%u packed_lod=%u packed_valid=%u origin=%.9g,%.9g,%.9g transition=%u interpretation=post_eye_shared_camera_table native_return_observed=0",serial+1,producer_,frame_,eye_,index,model,visible,lod,visible!=0,double(number(origin_[0])),double(number(origin_[1])),double(number(origin_[2])),transition);
    }
}
void StereoLodFovScope::after_native(bool completed) noexcept {
    NativeReadPreserve preserve;if(!active_)return;
    if(!completed){good_=false;reason_="native_exception";finish();return;}
    std::array<unsigned,3> origin{};
    if(!frame_current(true)||!dvars_current(true)||!get(base_+0x356ecf8,&origin)||origin!=origin_||!get(base_+0x356ecf8+0xc,&actual_)){
        good_=false;reason_="post_native_authority";
    }else {
        actual_valid_=true;
        if(changed_&&(bits(actual_[0])!=bits(math_.expected_scale)||bits(actual_[1])!=bits(math_.expected_bias)||bits(actual_[2])!=bits(math_.applied))){
            good_=false;reason_="native_result_mismatch";
        }else observe();
    }
    finish();
}
bool StereoLodFovScope::finish() noexcept {
    NativeReadPreserve preserve;
    if(pending_&&!restore()){good_=false;reason_="restore_conflict";poisoned=true;}
    if(active_&&GetCurrentThreadId()!=thread_){good_=false;return false;}
    if(active_scope==this)active_scope=nullptr;active_=false;
    if(probe.load()&&!reported_&&producer_){reported_=true;const auto serial=reports.fetch_add(1)+1;
        const auto eye_serial=eye_reports[eye_].fetch_add(1)+1;
        if(eye_serial<=8||eye_serial%120==0||!safe())trace("t5_stereo_lod_fov","sample=%u eye_sample=%u producer=%llu frame=%u eye=%u admitted=%u actual_valid=%u changed=%u original=%.9g,%.9g temporary=%.9g,%.9g predicted_applied=%.9g actual=%.9g,%.9g,%.9g restored=%u safe=%u reason=%s hooks_added=0 ramp_writes=0",serial,eye_serial,producer_,frame_,eye_,admitted_,actual_valid_,changed_,double(number(before_[0])),double(number(before_[1])),double(number(temporary_[0])),double(number(temporary_[1])),double(math_.applied),double(actual_[0]),double(actual_[1]),double(actual_[2]),pending_==0,safe(),reason_);
    }return safe();
}
void invoke_stereo_lod_fov_native(StereoLodFovScope* scope,void(*body)(void*),void* context) {
    bool completed=false;__try{body(context);completed=true;}__finally{if(scope)scope->after_native(completed);}
}
}
