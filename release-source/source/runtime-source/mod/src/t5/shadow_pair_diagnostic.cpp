// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/shadow_pair_diagnostic.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t first=0x3740100,stride=0x186300,views_first=0x420f730,views_stride=0xe380;
constexpr std::uintptr_t view_stride=0x38e0,lod_rva=0x356ecf8;
template<class T>bool get(std::uintptr_t address,T* output) noexcept {
    __try {std::memcpy(output,reinterpret_cast<const void*>(address),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::uint32_t word(const std::uint8_t* p) noexcept {std::uint32_t v{};std::memcpy(&v,p,4);return v;}
bool current(const ShadowPairDiagnosticSample& s,unsigned eye,bool complete=false) noexcept {
    if(!s.base||s.base>(std::numeric_limits<std::uintptr_t>::max)()-0x4240000||
       (s.buffer!=s.base+first&&s.buffer!=s.base+first+stride)||eye>1||s.thread!=GetCurrentThreadId())return false;
    const auto slot=s.buffer==s.base+first?0u:1u;
    const auto views=s.base+views_first+slot*views_stride;
    std::uint32_t owner{},frame{},count{},index{},array{},parms{},data{};
    return get(s.base+0x373708c,&owner)&&owner==s.buffer&&get(s.buffer+0x15d7a0,&frame)&&frame==s.frame&&
        get(s.buffer+0x16cc3c,&count)&&count==(complete?2u:eye+1)&&get(s.buffer+0x16cc38,&index)&&index==eye&&
        get(s.buffer+0x16cc40,&array)&&array==views&&get(s.buffer+0x16cbe0,&parms)&&parms==count&&
        get(views+eye*view_stride+0x3510,&data)&&data==s.buffer;
}
bool extra_copy(const ShadowPairDiagnosticSample& s,unsigned eye,ShadowPairDiagnosticEye* out) noexcept {
    const auto slot=s.buffer==s.base+first?0u:1u;
    out->view=s.base+views_first+slot*views_stride+eye*view_stride;
    return get(out->view,&out->camera)&&get(out->view+0x140,&out->viewports)&&
        get(out->view+0x2980,&out->constants)&&get(out->view+0x184,&out->shadow_type)&&
        get(out->view+0x188,&out->local_client)&&get(out->view+0x38c4,&out->extra)&&
        get(s.base+lod_rva,&out->corrected_lod)&&get(out->view+0x24b0,&out->sun_lists)&&
        get(out->view+0x2550,&out->spot_lists)&&get(s.buffer+0x184390,&out->spot_count)&&
        get(s.buffer+0x1843a0,&out->spots);
}
bool guards(std::uintptr_t base) noexcept {
    // Extra diagnostic mappings only; adapter already validated the full sun guards.
#if defined(BOVR_TEST_NATIVE_GUARDS)
    constexpr const char* zero21="c90232586b801f9558a76f2f963eccd831d9fe6775e4c8f1446b2331aa2132f2";
    return base&&native_guard::matches(reinterpret_cast<const void*>(base+0x2c8da6),21,zero21)&&
        native_guard::matches(reinterpret_cast<const void*>(base+0x329b1a),21,zero21);
#else
    return base&&native_guard::matches(reinterpret_cast<const void*>(base+0x2c8da6),21,"e1037a50be1a7b917ceebdb6d80f38dfa8e76b89037e1b2d8e0cfc7c82c48196")&&
        native_guard::matches(reinterpret_cast<const void*>(base+0x329b1a),21,"32276dcd759d1ef2d831c0ec9729f9a5a52521feeea974d9467c43789dd05861");
#endif
}
struct Text {
    char* p{};std::size_t used{},capacity{};bool good{true};
    void add(const char* format,...) noexcept {
        if(!good)return;va_list args;va_start(args,format);
        const int n=vsnprintf_s(p+used,capacity-used,_TRUNCATE,format,args);va_end(args);
        if(n<0){good=false;return;}used+=static_cast<std::size_t>(n);
    }
    template<class T>void hex(const T& value) noexcept {
        constexpr char digits[]="0123456789abcdef";
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        if(!good||sizeof(T)*2>=capacity-used){good=false;return;}
        for(std::size_t i=0;i<sizeof(T);++i){p[used++]=digits[bytes[i]>>4];p[used++]=digits[bytes[i]&15];}p[used]=0;
    }
};
bool write_pair(const wchar_t* path,const ShadowPairDiagnosticSample& s,unsigned number) noexcept {
    auto bytes=std::unique_ptr<char[]>(new(std::nothrow) char[32768]);if(!bytes)return false;
    Text t{bytes.get(),0,32768};
    t.add("{\n  \"schema\":2,\"sample\":%u,\"producer\":%llu,\"native_frame\":%u,\"buffer\":\"0x%lx\",\"capture_thread\":%lu,\n",number,s.producer,s.frame,static_cast<unsigned long>(s.buffer),s.thread);
    t.add("  \"scope\":\"accepted ordinary frontend pair: count2/parms2/selected1; later backend, auxiliary append, GPU and XR acceptance not claimed\",\n  \"encoding\":\"raw little-endian bytes as hex; matrices are row-major float32\",\n  \"eyes\":[\n");
    for(unsigned eye=0;eye<2;++eye){const auto& e=s.eyes[eye];
        t.add("    {\"eye\":%u,\"view\":\"0x%lx\",\"capture_stage\":\"immediately after this eye native frontend and accepted immutable shadow capture\",\"local_client_view_188\":%u,\"shadow_type_view_184\":%u,\"extra_view_38c4\":%u,\"sun_index\":%u,\n",eye,static_cast<unsigned long>(e.view),e.local_client,e.shadow_type,e.extra,e.sun_index);
        t.add("     \"camera_view_000_140_hex\":\"");t.hex(e.camera);t.add("\",\n     \"scene_display_scissor_view_140_30_hex\":\"");t.hex(e.viewports);
        t.add("\",\n     \"sun_constants_view_2980_20_hex\":\"");t.hex(e.constants);
        t.add("\",\n     \"corrected_lod_global_rva356ecf8_slots0_1_each34_hex\":\"");t.hex(e.corrected_lod);
        t.add("\",\n     \"immutable_sun_buffer_183f40_450_hex\":\"");t.hex(e.sun);
        t.add("\",\n     \"immutable_sun_mask_buffer_15d780_20_hex\":\"");t.hex(e.mask);
        t.add("\",\n     \"sun_lists\":[");
        for(unsigned p=0;p<2;++p){const auto& list=e.sun_lists[p];
            t.add("%s{\"partition\":%u,\"source_view_offset\":\"0x%x\",\"drawsurfs_pointer\":\"0x%x\",\"drawsurf_count\":%u,\"header_50_hex\":\"",p?",":"",p,0x24b0+p*0x50,word(list.data()),word(list.data()+4));t.hex(list);t.add("\"}");}
        t.add("],\n     \"spot_count\":%u,\"immutable_spots_buffer_1843a0_740_hex\":\"",e.spot_count);t.hex(e.spots);
        t.add("\",\n     \"spot_lists\":[");
        for(unsigned slot=0;slot<4;++slot){const auto& list=e.spot_lists[slot];const auto* record=e.spots.data()+slot*shadow_spot_bytes;
            t.add("%s{\"slot\":%u,\"active\":%s,\"light_index\":%u,\"source_view_offset\":\"0x%x\",\"drawsurfs_pointer\":\"0x%x\",\"drawsurf_count\":%u,\"header_50_hex\":\"",
                slot?",":"",slot,slot<e.spot_count?"true":"false",record[0x180],0x2550+slot*0x50,word(list.data()),word(list.data()+4));t.hex(list);t.add("\"}");}
        t.add("]}%s\n",eye==0?",":"");
    }
    t.add("  ]\n}\n");if(!t.good)return false;
    const auto file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written{};const bool okay=WriteFile(file,bytes.get(),static_cast<DWORD>(t.used),&written,nullptr)&&written==t.used;
    CloseHandle(file);return okay;
}
}
struct ShadowPairDiagnostic::State {
    std::uintptr_t base{};SRWLOCK lock=SRWLOCK_INIT;unsigned count{};std::atomic<bool> exhausted{};
    struct Key {std::uintptr_t buffer{};std::uint32_t frame{};};std::array<Key,3> keys{};
    std::array<wchar_t,32768> directory{};
};
ShadowPairDiagnostic::ShadowPairDiagnostic() noexcept=default;
ShadowPairDiagnostic::~ShadowPairDiagnostic()=default;
void ShadowPairDiagnostic::configure(std::uintptr_t base) noexcept {
    NativeReadPreserve preserve;wchar_t flag[8]{};
    if(state_||GetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",flag,8)!=1||flag[0]!=L'1')return;
    auto next=std::unique_ptr<State>(new(std::nothrow) State);if(!next)return;
    const auto n=GetEnvironmentVariableW(L"BOVR_TRACE_FILE",next->directory.data(),static_cast<DWORD>(next->directory.size()));
    auto* slash=n&&n<next->directory.size()?wcsrchr(next->directory.data(),L'\\'):nullptr;
    if(!slash&&n&&n<next->directory.size())slash=wcsrchr(next->directory.data(),L'/');
    if(!slash||!guards(base)){trace("t5_shadow_pair_diagnostic","configured=0 reason=trace_directory_or_native_mapping new_hooks=0");return;}
    slash[1]=0;next->base=base;state_=std::move(next);
    trace("t5_shadow_pair_diagnostic","configured=1 pair_cap=3 ordinary_frontend=1 output=trace_directory_json native_writes=0 new_hooks=0");
}
std::unique_ptr<ShadowPairDiagnosticSample> ShadowPairDiagnostic::begin(std::uintptr_t buffer,std::uint32_t frame,std::uint64_t producer) noexcept {
    if(!state_||state_->exhausted.load(std::memory_order_relaxed))return {};NativeReadPreserve preserve;
    bool allowed=true;AcquireSRWLockShared(&state_->lock);
    if(state_->count>=3)allowed=false;
    for(unsigned i=0;i<state_->count;++i)if(state_->keys[i].buffer==buffer&&state_->keys[i].frame==frame)allowed=false;
    ReleaseSRWLockShared(&state_->lock);if(!allowed)return {};
    auto s=std::unique_ptr<ShadowPairDiagnosticSample>(new(std::nothrow) ShadowPairDiagnosticSample);
    if(s){s->base=state_->base;s->buffer=buffer;s->frame=frame;s->producer=producer;s->thread=GetCurrentThreadId();}return s;
}
void ShadowPairDiagnostic::capture(ShadowPairDiagnosticSample* s,const EyeShadowState& shadow) noexcept {
    if(!s||!state_)return;NativeReadPreserve preserve;
    if(!shadow.valid||shadow.eye>1||shadow.buffer!=s->buffer||shadow.frame!=s->frame||
       shadow.sun_index>=256||shadow.state.spot_count>4||!current(*s,shadow.eye))return;
    const bool sun_active=(shadow.state.mask[shadow.sun_index/32]&(1u<<(shadow.sun_index%32)))!=0;
    if(word(shadow.state.sun.data()+0xa0)!=(sun_active?2u:0u)||(!sun_active&&!shadow.state.spot_count))return;
    ShadowPairDiagnosticEye first_copy{},again{};
    if(!extra_copy(*s,shadow.eye,&first_copy)||!extra_copy(*s,shadow.eye,&again)||first_copy!=again||
       first_copy.view!=shadow.view||first_copy.extra||first_copy.local_client!=0||first_copy.shadow_type!=1||
       first_copy.spot_count!=shadow.state.spot_count||first_copy.spots!=shadow.state.spots||
       (sun_active&&std::memcmp(first_copy.constants.data(),shadow.state.sun.data()+0x80,32))||!current(*s,shadow.eye))return;
    first_copy.sun=shadow.state.sun;first_copy.mask=shadow.state.mask;first_copy.sun_index=shadow.sun_index;first_copy.valid=true;
    s->eyes[shadow.eye]=first_copy;
}
void ShadowPairDiagnostic::complete(ShadowPairDiagnosticSample* s,bool accepted) noexcept {
    if(!s||!state_||!accepted)return;NativeReadPreserve preserve;
    if(!s->eyes[0].valid||!s->eyes[1].valid||!current(*s,1,true))return;
    unsigned number{};AcquireSRWLockExclusive(&state_->lock);
    bool duplicate=false;for(unsigned i=0;i<state_->count;++i)if(state_->keys[i].buffer==s->buffer&&state_->keys[i].frame==s->frame)duplicate=true;
    if(!duplicate&&state_->count<3){state_->keys[state_->count]={s->buffer,s->frame};number=++state_->count;if(number==3)state_->exhausted.store(true,std::memory_order_relaxed);}
    ReleaseSRWLockExclusive(&state_->lock);if(!number)return;
    auto path=std::unique_ptr<wchar_t[]>(new(std::nothrow) wchar_t[32768]);if(!path)return;
    const int n=swprintf_s(path.get(),32768,L"%lsshadow-pair-%lu-%llu-%u.json",state_->directory.data(),GetCurrentProcessId(),s->producer,number);
    const bool written=n>0&&write_pair(path.get(),*s,number);
    trace("t5_shadow_pair_diagnostic","sample=%u producer=%llu frame=%u buffer=0x%lx written=%u filename=shadow-pair-%lu-%llu-%u.json source=immutable_frontend_pair spot_records=4 spot_lists=4 backend_acceptance=unobserved native_writes=0",
        number,s->producer,s->frame,static_cast<unsigned long>(s->buffer),written,GetCurrentProcessId(),s->producer,number);
}
}
