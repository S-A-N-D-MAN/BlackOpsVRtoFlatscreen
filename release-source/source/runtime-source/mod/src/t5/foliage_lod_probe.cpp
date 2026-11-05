// SPDX-License-Identifier: GPL-3.0-only
#include "t5/foliage_lod_probe.hpp"
#include "t5/identity.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <cstring>
#include <limits>
#if !defined(_M_IX86)
#error The exact retail foliage diagnostic requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t call_rva=0x329949,target_rva=0x123bc0;
constexpr std::uintptr_t first=0x3740100,stride=0x186300,views_first=0x420f730,views_stride=0xe380;
constexpr std::array<unsigned char,5> original_call{0xe8,0x72,0xa2,0xdf,0xff};
std::uintptr_t hook_base{},native_target{};
std::array<unsigned char,5> owned_call{};
std::array<unsigned,64> selected{};unsigned selected_count{};
std::atomic<bool> hook_ready{};
SRWLOCK install_lock=SRWLOCK_INIT;
thread_local FoliageLodProbeScope* active_scope{};
struct Last {bool valid{};std::uint32_t model{},world{};int result{};unsigned no_cull{};};
thread_local std::array<std::array<Last,64>,2> last_records{};
std::atomic<unsigned> log_count{},scope_count{};
#if defined(BOVR_TEST_FOLIAGE_LOD)
void(__cdecl* test_observer)(std::uint32_t*){};
#endif
template<class T>bool get(std::uintptr_t p,T* value) noexcept {
    __try {_ReadWriteBarrier();std::memcpy(value,reinterpret_cast<const void*>(p),sizeof(T));_ReadWriteBarrier();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool put(std::uintptr_t p,const T& value) noexcept {
    __try {std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool finite(float value) noexcept {std::uint32_t bits{};std::memcpy(&bits,&value,4);return (bits&0x7f800000u)!=0x7f800000u;}
bool parse_indices(const wchar_t* text,std::array<unsigned,64>* output,unsigned* count) noexcept {
    if(!text||!output||!count)return false;
    std::array<unsigned,64> values{};unsigned n{};const auto* p=text;
    while(*p){if(n==values.size()||*p<L'0'||*p>L'9')return false;unsigned v{};
        do{const auto digit=unsigned(*p-L'0');if(v>6553u||(v==6553u&&digit>5u))return false;v=v*10+digit;++p;}while(*p>=L'0'&&*p<=L'9');
        for(unsigned i=0;i<n;++i)if(values[i]==v)return false;values[n++]=v;
        if(!*p)break;if(*p++!=L','||!*p)return false;
    }
    if(!n)return false;*output=values;*count=n;return true;
}
bool owned_current(std::uintptr_t base) noexcept {
    std::array<unsigned char,5> bytes{};
    return hook_ready.load(std::memory_order_acquire)&&base==hook_base&&get(base+call_rva,&bytes)&&bytes==owned_call;
}
bool guards_match(std::uintptr_t base) noexcept {
    return base&&base<=UINTPTR_MAX-0x4240000&&
        native_guard::matches(reinterpret_cast<const void*>(base+call_rva),5,"e4f7fa4c2b6c72cd1eba426c4d71507a42679e2af1106f7590942de0ad40a0aa")&&
        native_guard::matches(reinterpret_cast<const void*>(base+0x329901),72,"0b7cbcb2ba31036306422609987f105b657311fe74a14cf41aed79387d47c808")&&
        native_guard::matches(reinterpret_cast<const void*>(base+0x32994e),13,"3ea63a004739c2bf970879394c4d5980af888e77a41056acb14cccdf3e63f557")&&
        native_guard::matches(reinterpret_cast<const void*>(base+target_rva),160,"8184c05303726b80c7d769716d919ab481d75ca2187287640ef1ae1eabda837c");
}
// The installer never hashes, allocates, or logs while native peer threads are
// suspended. Their instruction pointers must be outside the patched CALL.
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume() noexcept {while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire(std::uintptr_t base) noexcept {
        const auto process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 row{};row.dwSize=sizeof row;bool okay=Thread32First(snap,&row)!=FALSE;
        if(okay)do{if(row.th32OwnerProcessID!=process||row.th32ThreadID==self)continue;
            if(count==handles.size()){okay=false;break;}
            const auto h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,row.th32ThreadID);
            if(!h){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;okay=false;break;}handles[count++]=h;
        }while(Thread32Next(snap,&row));
        if(okay&&GetLastError()!=ERROR_NO_MORE_FILES)okay=false;CloseHandle(snap);if(!okay)return false;
        for(std::size_t i=0;i<count;++i){if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c)||
                (c.Eip>=base+call_rva&&c.Eip<base+call_rva+5))return false;}
        return true;
    }
};
void __cdecl after_native(std::uint32_t* saved) noexcept {
    const auto error=GetLastError();
    __try {
#if defined(BOVR_TEST_FOLIAGE_LOD)
        if(test_observer)test_observer(saved);else
#endif
        observe_foliage_lod(saved);
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    SetLastError(error);
}
// Entry [return][model][adjusted float][raw float][noCull]. Native executes
// exactly once, on duplicated identical arguments. Original caller words and
// nonvolatile EBP (instance index), EDI (instance+0xC) survive for observation.
// The post observer cannot change native EAX, flags, FP/SSE, or LastError.
__declspec(naked) void foliage_lod_bridge() {
    __asm {
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        push dword ptr [esp+16]
        call dword ptr [native_target]
        lea esp,[esp+16]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call after_native
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}
bool install(std::uintptr_t base) noexcept {
    if(hook_ready.load(std::memory_order_acquire))return owned_current(base);
    if(!guards_match(base))return false;
    std::array<unsigned char,72> setup{};std::array<unsigned char,13> continuation{};std::array<unsigned char,160> callee{};
    if(!get(base+0x329901,&setup)||!get(base+0x32994e,&continuation)||!get(base+target_rva,&callee)||
       !native_guard::matches(setup.data(),setup.size(),"0b7cbcb2ba31036306422609987f105b657311fe74a14cf41aed79387d47c808")||
       !native_guard::matches(continuation.data(),continuation.size(),"3ea63a004739c2bf970879394c4d5980af888e77a41056acb14cccdf3e63f557")||
       !native_guard::matches(callee.data(),callee.size(),"8184c05303726b80c7d769716d919ab481d75ca2187287640ef1ae1eabda837c")||!guards_match(base))return false;
    Peers peers;if(!peers.acquire(base))return false;
    std::array<unsigned char,72> s{};std::array<unsigned char,13> c{};std::array<unsigned char,160> f{};std::array<unsigned char,5> before{};
    if(!get(base+0x329901,&s)||s!=setup||!get(base+0x32994e,&c)||c!=continuation||
       !get(base+target_rva,&f)||f!=callee||!get(base+call_rva,&before)||before!=original_call)return false;
    auto patch=original_call;const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&foliage_lod_bridge)-(base+call_rva+5));
    std::memcpy(patch.data()+1,&displacement,4);DWORD protection{},ignored{};auto* site=reinterpret_cast<void*>(base+call_rva);
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection))return false;
    if(!get(base+call_rva,&before)||before!=original_call){VirtualProtect(site,5,protection,&ignored);return false;}
    hook_base=base;native_target=base+target_rva;owned_call=patch;
    const bool wrote=put(base+call_rva,patch),flushed=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
    const bool protected_again=VirtualProtect(site,5,protection,&ignored)!=FALSE;
    if(!wrote||!flushed||!protected_again||!get(base+call_rva,&before)||before!=patch){
        DWORD writable{};bool rolled_back=false;
        if(VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&writable)){
            const bool restored_bytes=put(base+call_rva,original_call);
            const bool restored_cache=FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
            const bool restored_protection=VirtualProtect(site,5,protection,&ignored)!=FALSE;
            rolled_back=restored_bytes&&restored_cache&&restored_protection&&get(base+call_rva,&before)&&before==original_call;
        }
        // Never resume native peers onto an unproven partially written CALL.
        if(!rolled_back)__fastfail(7);
        return false;
    }
    hook_ready.store(true,std::memory_order_release);peers.resume();return true;
}
}
bool configure_foliage_lod_probe(HMODULE executable) noexcept {
    NativeReadPreserve preserve;wchar_t enabled[8]{};
    if(GetEnvironmentVariableW(L"BOVR_FOLIAGE_LOD_PROBE",enabled,8)!=1||enabled[0]!=L'1')return true;
    AcquireSRWLockExclusive(&install_lock);bool okay{};
    try{wchar_t indices[512]{};const auto length=GetEnvironmentVariableW(L"BOVR_FOLIAGE_LOD_INDICES",indices,512);
        std::string reason;okay=length&&length<512&&parse_indices(indices,&selected,&selected_count)&&
            executable==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
            validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&install(reinterpret_cast<std::uintptr_t>(executable));
    }catch(...){okay=false;}
    ReleaseSRWLockExclusive(&install_lock);
    trace("t5_foliage_lod_probe_configured","enabled=1 accepted=%u selected=%u call=00729949 target=00523bc0 observation_only=1 native_calls_added=0 lod_writes=0",okay,selected_count);
    return okay;
}
bool FoliageLodProbeScope::current(bool built) const noexcept {
    if(!active_||blocked_||active_scope!=this||GetCurrentThreadId()!=thread_||!owner_.current||!owner_.current(owner_.context)||!owned_current(base_))return false;
    std::uint32_t owner{},frame{},count{},index{},array{},parms{},client{},data{},vclient{};
    std::array<std::uint32_t,3> origin{},vorigin{};std::array<int,4> viewport{},vviewport{};
    if(!(get(base_+0x373708c,&owner)&&owner==buffer_&&get(buffer_+0x15d7a0,&frame)&&frame==frame_&&
         get(buffer_+0x16cc3c,&count)&&count==(built?eye_+1:eye_)&&
         get(buffer_+0x16cc38,&index)&&index==(built?eye_:0u)&&get(buffer_+0x16cc40,&array)&&array==view_-eye_*0x38e0&&
         get(buffer_+0x16cbe0,&parms)&&parms==count&&get(refdef_+0x183a4,&client)&&client==0&&
         get(refdef_+0x20,&origin)&&origin==origin_&&get(refdef_,&viewport)&&viewport==viewport_))return false;
    if(!built)return true;
    return get(view_+0x3510,&data)&&data==buffer_&&get(view_+0x188,&vclient)&&vclient==0&&
        get(view_+0x100,&vorigin)&&vorigin==origin_&&get(view_+0x140,&vviewport)&&vviewport==viewport_;
}
bool FoliageLodProbeScope::begin(std::uintptr_t base,std::uintptr_t buffer,unsigned frame,std::uintptr_t refdef,
                               std::uint64_t producer,unsigned eye,FoliageLodOwner owner) noexcept {
    if(!hook_ready.load(std::memory_order_acquire))return false;
    NativeReadPreserve preserve;
    if(active_||active_scope){if(active_scope)active_scope->blocked_=true;return false;}
    if(base!=hook_base||!producer||eye>1||(buffer!=base+first&&buffer!=base+first+stride)||!refdef||!owner.current)return false;
    base_=base;buffer_=buffer;frame_=frame;refdef_=refdef;producer_=producer;eye_=eye;owner_=owner;
    view_=base+views_first+(buffer==base+first?0:views_stride)+eye*0x38e0;thread_=GetCurrentThreadId();
    if(!get(refdef+0x20,&origin_)||!get(refdef,&viewport_))return false;
    for(auto bits:origin_)if((bits&0x7f800000u)==0x7f800000u)return false;
    if(viewport_[0]<0||viewport_[1]<0||viewport_[2]<=0||viewport_[3]<=0)return false;
    active_=true;blocked_=false;active_scope=this;
    if(!current(false)){active_scope=nullptr;active_=false;return false;}return true;
}
void observe_foliage_lod(std::uint32_t* saved) noexcept {
    auto* s=active_scope;if(!s||!s->active_||s->blocked_)return;
    const auto index=saved[2];unsigned slot=selected_count;
    for(unsigned i=0;i<selected_count;++i)if(selected[i]==index){slot=i;break;}
    if(slot==selected_count)return;
    ++s->observed_;FoliageLodRecord r{};r.index=index;r.result=static_cast<int>(saved[7]);
    const auto entry=reinterpret_cast<std::uintptr_t>(saved)+36;std::array<std::uint32_t,5> arguments{};
    std::array<std::uint8_t,0x4c> instance{},instance_again{};std::array<std::uint8_t,0x34> lod{},lod_again{};
    std::uint32_t world{},count{},instances{},model_again{},world_again{};
    const auto reject=[&]{++s->rejected_;};
    if(!s->current(true)||!get(entry,&arguments)||arguments[0]!=s->base_+call_rva+5||
       !get(s->base_+0x356a24c,&world)||!world||!get(world+0x344,&count)||count>65536||index>=count||
       !get(world+0x3a4,&instances)||!instances||instances>UINTPTR_MAX-std::uintptr_t(count)*0x4c){reject();return;}
    r.world=world;r.instance=instances+index*0x4c;r.model=arguments[1];r.no_cull=arguments[4];
    std::memcpy(&r.adjusted,&arguments[2],4);std::memcpy(&r.raw,&arguments[3],4);
    if(saved[0]!=r.instance+0xc||!get(r.instance,&instance)||!r.model||
       std::memcmp(instance.data()+0x38,&r.model,4)||r.no_cull>1||r.result< -1||r.result>3||
       !finite(r.raw)||r.raw<0||!finite(r.adjusted)||r.adjusted<0||!get(r.model+0xd8,&r.lod_count)||r.lod_count<1||r.lod_count>4||
       !get(s->base_+0x356ecf8,&lod)||std::memcmp(lod.data(),s->origin_.data(),12)){reject();return;}
    std::memcpy(&r.placement_scale,instance.data()+0x34,4);std::memcpy(&r.corrected_scale,lod.data()+0xc,4);
    std::memcpy(&r.corrected_bias,lod.data()+0x10,4);std::memcpy(&r.applied_inverse,lod.data()+0x14,4);
    std::memcpy(r.origin.data(),s->origin_.data(),12);
    if(!finite(r.placement_scale)||r.placement_scale<=0||!finite(r.corrected_scale)||r.corrected_scale<=0||
       !finite(r.corrected_bias)||!finite(r.applied_inverse)){reject();return;}
    for(int i=0;i<r.lod_count;++i)if(!get(r.model+0x28+std::uintptr_t(i)*0x20,&r.ranges[static_cast<unsigned>(i)])||!finite(r.ranges[static_cast<unsigned>(i)])){reject();return;}
    std::uint32_t name{};if(!get(r.model,&name)||!name){reject();return;}
    for(unsigned i=0;i<r.name.size()-1;++i){if(!get(name+i,&r.name[i])){reject();return;}if(!r.name[i])break;}
    if(!get(r.instance,&instance_again)||instance_again!=instance||!get(s->base_+0x356ecf8,&lod_again)||lod_again!=lod||
       !get(r.instance+0x38,&model_again)||model_again!=r.model||!get(s->base_+0x356a24c,&world_again)||world_again!=world||
       !s->current(true)){reject();return;}
    if(s->records_[slot].captured){reject();return;}r.captured=true;s->records_[slot]=r;
}
void FoliageLodProbeScope::finish() noexcept {
    if(!active_)return;NativeReadPreserve preserve;
    if(GetCurrentThreadId()!=thread_){blocked_=true;return;}
    const bool owned=current(true);if(active_scope==this)active_scope=nullptr;else blocked_=true;active_=false;
    const auto serial=++scope_count;
    if(owned&&!blocked_){for(unsigned i=0;i<selected_count;++i){const auto& r=records_[i];if(!r.captured)continue;
        auto& last=last_records[eye_][i];const bool transition=!last.valid||last.world!=r.world||last.model!=r.model||last.result!=r.result||last.no_cull!=r.no_cull;
        last={true,r.model,r.world,r.result,r.no_cull};
        if(!transition&&serial%120)continue;const auto logged=log_count.fetch_add(1,std::memory_order_relaxed);if(logged>=4096)continue;
        trace("t5_foliage_lod_observed","sample=%u producer=%llu buffer=0x%lx frame=%u eye=%u index=%u model=0x%x name=%s result=%d adjusted=%.9g raw=%.9g placement=%.9g corrected_scale=%.9g bias=%.9g applied_inverse=%.9g no_cull=%u lods=%d ranges=%.9g,%.9g,%.9g,%.9g origin=%.9g,%.9g,%.9g transition=%u native_calls_added=0 result_writes=0",
            logged+1,producer_,static_cast<unsigned long>(buffer_),frame_,eye_,r.index,r.model,r.name.data(),r.result,double(r.adjusted),double(r.raw),double(r.placement_scale),double(r.corrected_scale),double(r.corrected_bias),double(r.applied_inverse),r.no_cull,int(r.lod_count),double(r.ranges[0]),double(r.ranges[1]),double(r.ranges[2]),double(r.ranges[3]),double(r.origin[0]),double(r.origin[1]),double(r.origin[2]),transition);
    }}
    if(serial<=8||serial%120==0)trace("t5_foliage_lod_scope","scope=%u producer=%llu frame=%u eye=%u selected=%u observed=%u rejected=%u owned=%u blocked=%u native_writes=0",serial,producer_,frame_,eye_,selected_count,observed_,rejected_,owned,blocked_);
}
void invoke_foliage_lod_native(FoliageLodProbeScope* scope,void(*body)(void*),void* context) {
    __try {body(context);}__finally {if(scope){if(AbnormalTermination())scope->blocked_=true;scope->finish();}}
}
}
