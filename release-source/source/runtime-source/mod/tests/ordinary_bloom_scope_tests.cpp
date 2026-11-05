// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_bloom_scope.hpp"
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <stdexcept>
#include <thread>
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof value);}
template<class T>T read(std::uintptr_t p){T value{};std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof value);return value;}
constexpr std::size_t extent=0x4240000,view_bytes=0x38e0;
constexpr std::array<unsigned,6> rgb_words{0,1,2,4,5,6};
using Bloom=std::array<std::uint32_t,8>;
using Views=std::array<unsigned char,2*view_bytes>;
struct Fixture {
    std::uintptr_t base{},buffer{},views{};
    AuxiliaryViewLayout layout{};
    std::array<Bloom,2> before{};
    Fixture(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,extent,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));}
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void seed(unsigned slot=0,unsigned scale=1){
        buffer=base+(slot?0x38c6400:0x3740100);views=base+0x420f730+slot*0xe380;
        std::memset(reinterpret_cast<void*>(views),0xa5,2*view_bytes);
        layout={};layout.buffer=buffer;layout.view_array=views;layout.frame=71+slot;layout.count=2;layout.selected=1;
        put(base+0x3a5b5f8,static_cast<std::uint32_t>(buffer));put(buffer+0x15d7a0,layout.frame);
        put(buffer+0x16cc3c,2u);put(buffer+0x16cc38,1u);put(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        for(unsigned eye=0;eye<2;++eye){auto& v=layout.slots[eye];v.address=views+eye*view_bytes;
            v.scene={static_cast<int>(eye*640*scale),0,static_cast<int>(640*scale),static_cast<int>(720*scale)};
            v.display=v.scissor=v.scene;v.targets={3,3,9,7,22,20,19,2};v.extra=0;
            put(v.address+0x140,v.scene);put(v.address+0x150,v.display);put(v.address+0x160,v.scissor);
            put(v.address+0x35f0,v.targets);put(v.address+0x38c4,v.extra);put(v.address+0x3510,static_cast<std::uint32_t>(buffer));
            before[eye]={0x3f200001u+eye,0x3f400002u+eye,0xbf600003u+eye,0x3e800004u+eye,
                         0x3fa00005u+eye,0x3fc00006u+eye,0x3fe00007u+eye,0x3f000008u+eye};
            put(v.address+0x3638,before[eye]);}
    }
    void select(unsigned eye){put(buffer+0x16cc38,eye);}
    std::uintptr_t bloom(unsigned eye)const{return views+eye*view_bytes+0x3638;}
    Bloom colors(unsigned eye)const{return read<Bloom>(bloom(eye));}
    Views snapshot()const{return read<Views>(views);}
    bool protect(unsigned eye,DWORD access,DWORD* old)const{
        return VirtualProtect(reinterpret_cast<void*>(bloom(eye)&~std::uintptr_t{0xfff}),4096,access,old)!=0;
    }
    bool zeroed(unsigned eye)const{auto expected=before[eye];for(auto word:rgb_words)expected[word]=0;return colors(eye)==expected;}
};
struct Owner {
    Fixture* f{};unsigned eye{},calls{},refuse_call{},readonly_call{};bool live=true,mutate_frame{},alter_environment{},seh{};
    DWORD old_access{};bool protected_page{};
    static bool current(void* value) noexcept {
        auto& o=*static_cast<Owner*>(value);++o.calls;
        if(o.alter_environment){SetLastError(0x1942);_mm_setcsr((_mm_getcsr()&~0x6000u)|0x4000u);}
        if(o.mutate_frame){put(o.f->buffer+0x15d7a0,o.f->layout.frame+1);o.mutate_frame=false;}
        if(o.readonly_call==o.calls)o.protected_page=o.f->protect(o.eye,PAGE_READONLY,&o.old_access);
        if(o.seh)RaiseException(0xe0421002,0,0,nullptr);
        return o.live&&o.calls!=o.refuse_call;
    }
    OrdinaryBloomOwner bind(){return {this,current};}
    void unprotect(){if(protected_page){DWORD ignored{};check(f->protect(eye,old_access,&ignored),"fixture page protection restored");protected_page=false;}}
};
struct NativeCall {Fixture* f{};Owner* owner{};unsigned eye{},calls{},mode{};bool observed{};};
void native_body(void* value){auto& n=*static_cast<NativeCall*>(value);++n.calls;
    n.observed=read<unsigned>(n.f->buffer+0x16cc38)==n.eye&&n.f->zeroed(n.eye)&&n.f->colors(1-n.eye)==n.f->before[1-n.eye];
    if(n.mode==1)throw std::runtime_error("native C++ exception");
    if(n.mode==2)RaiseException(0xe0421001,0,0,nullptr);
    if(n.mode==3)n.owner->live=false;
    if(n.mode==4){n.owner->protected_page=n.f->protect(n.eye,PAGE_READONLY,&n.owner->old_access);}
}
// Models the existing history thunk's authoritative selected-index finally,
// nested inside the actual production bloom thunk, including native SEH.
void native_with_index_finally(void* value){
    __try {native_body(value);}
    __finally {if(AbnormalTermination())put(static_cast<NativeCall*>(value)->f->buffer+0x16cc38,1u);}
}
bool catch_native_seh(OrdinaryBloomScope* scope,NativeCall* call){
    __try {invoke_ordinary_bloom_native(scope,native_with_index_finally,call);}
    __except(GetExceptionCode()==0xe0421001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
void count_only(void* p){++*static_cast<unsigned*>(p);}
void ordinary_paths(Fixture& f){
    for(unsigned slot=0;slot<2;++slot)for(unsigned scale=1;scale<=4;++scale)for(unsigned eye=0;eye<2;++eye){
        f.seed(slot,scale);f.select(eye);const auto original=f.snapshot();Owner owner{&f,eye};OrdinaryBloomScope scope;
        check(scope.begin(f.base,f.layout,eye,owner.bind()),"both eyes in both SMP stores at 1280/2560/3840 source widths admit");
        check(scope.pending()&&scope.good()&&!scope.restored()&&f.zeroed(eye),"only selected eye six RGB words become positive zero");
        auto expected=original;for(auto word:rgb_words){const std::uint32_t zero{};std::memcpy(expected.data()+eye*view_bytes+0x3638+word*4,&zero,4);}
        check(f.snapshot()==expected,"entire two-view memory preserves alpha, film, exposure, metadata, and other eye");
        NativeCall native{&f,&owner,eye};invoke_ordinary_bloom_native(&scope,native_with_index_finally,&native);
        check(native.calls==1&&native.observed,"actual production thunk invokes original native exactly once under selected RGB journal");
        check(scope.good()&&scope.restored()&&!scope.pending()&&f.snapshot()==original,"normal return restores exact original bits before capture");
        scope.close();check(scope.finish()&&f.snapshot()==original,"finish and close are idempotent");
    }
    unsigned calls{};invoke_ordinary_bloom_native(nullptr,count_only,&calls);check(calls==1,"disabled journal remains exact-once native passthrough");
}
void admission_mutations(Fixture& f){
    for(unsigned mutation=0;mutation<22;++mutation){f.seed();f.select(0);auto copied=f.layout;unsigned eye=0;std::uintptr_t base=f.base;Owner owner{&f};
        switch(mutation){
        case 0:++copied.frame;break;
        case 1:copied.buffer+=4;break;
        case 2:copied.view_array+=4;break;
        case 3:copied.count=3;copied.selected=2;break;
        case 4:copied.selected=0;break;
        case 5:copied.slots[2].address=f.views+2*view_bytes;break;
        case 6:copied.slots[0].targets[4]=23;break;
        case 7:copied.slots[0].extra=1;break;
        case 8:put(f.buffer+0x16cc38,1u);break;
        case 9:put(f.buffer+0x16cc3c,3u);break;
        case 10:put(f.buffer+0x16cc40,static_cast<std::uint32_t>(f.views+0xe380));break;
        case 11:put(f.base+0x3a5b5f8,static_cast<std::uint32_t>(f.buffer+4));break;
        case 12:put(f.views+0x3510,static_cast<std::uint32_t>(f.buffer+4));break;
        case 13:put(f.views+view_bytes+0x160,bovr::render::HudEyeViewport{0,0,640,720});break;
        case 14:put(f.views+view_bytes+0x35f0,std::uint8_t{22});break;
        case 15:put(f.views+view_bytes+0x38c4,std::uint8_t{1});break;
        case 16:eye=2;break;
        case 17:base=UINT32_MAX-10u;break;
        case 18:base=0;break;
        case 19:copied.slots[0].scene.width=641;copied.slots[0].display=copied.slots[0].scissor=copied.slots[0].scene;break;
        case 20:owner.live=false;break;
        case 21:owner.seh=true;break;
        }
        const auto original=f.snapshot();OrdinaryBloomScope scope;
        check(!scope.begin(base,copied,eye,owner.bind()),"wrong immutable layout, eye, extent, native identity, or owner rejects");
        check(!scope.pending()&&!scope.good()&&f.snapshot()==original,"entry rejection leaves all native view bytes untouched");
    }
    for(unsigned word=0;word<8;++word)for(auto bad:{0x7f800000u,0xff800000u,0x7fc00001u}){
        f.seed();f.select(0);put(f.bloom(0)+word*4,bad);const auto original=f.snapshot();Owner owner{&f};OrdinaryBloomScope scope;
        check(!scope.begin(f.base,f.layout,0,owner.bind())&&!scope.pending()&&f.snapshot()==original,"any nonfinite RGB or alpha refuses without native changes");
    }
    check(!validate_ordinary_bloom_guards(nullptr)&&!validate_ordinary_bloom_guards(reinterpret_cast<HMODULE>(f.base)),"non-retail module cannot claim pinned native RGB seam");
}
void restoration_contract(Fixture& f){
    f.seed();f.select(0);Owner owner{&f};
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"owner retirement fixture begins");owner.live=false;
        check(scope.finish()&&!scope.good()&&!scope.pending()&&f.colors(0)==f.before[0],"retired owner still restores same immutable native allocation but denies pair");owner.live=true;}
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"foreign-thread fixture begins");bool other=true;
        std::thread thread([&]{other=scope.finish();});thread.join();
        check(!other&&scope.pending()&&f.zeroed(0),"foreign thread cannot restore selected memory");
        check(scope.finish()&&!scope.good()&&f.colors(0)==f.before[0],"original thread restores after foreign refusal, pair stays denied");}
    {OrdinaryBloomScope outer,inner;check(outer.begin(f.base,f.layout,0,owner.bind()),"nested fixture begins");
        check(!inner.begin(f.base,f.layout,0,owner.bind())&&!inner.pending(),"nested scope cannot borrow or overwrite active journal");
        check(outer.finish()&&!outer.good()&&f.colors(0)==f.before[0],"nesting invalidates outer pair while restoring its own exact RGB");}
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"immutable frame mutation fixture begins");put(f.buffer+0x15d7a0,f.layout.frame+1);
        check(!scope.finish()&&scope.pending()&&f.zeroed(0),"replaced frame never receives old RGB restoration");put(f.buffer+0x15d7a0,f.layout.frame);
        check(scope.finish()&&!scope.good()&&f.colors(0)==f.before[0],"recoverable identity restores only original frame, still denies pair");}
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"restore owner callback mutation fixture begins");owner.mutate_frame=true;
        check(!scope.finish()&&scope.pending()&&f.zeroed(0),"identity is rechecked after owner callback before any restore write");
        put(f.buffer+0x15d7a0,f.layout.frame);check(scope.finish()&&!scope.good()&&f.colors(0)==f.before[0],"callback identity mutation remains rejected after eventual restore");}
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"unexpected RGB conflict fixture begins");put(f.bloom(0)+4,0x3f123456u);
        check(!scope.finish()&&scope.pending()&&!scope.good()&&read<std::uint32_t>(f.bloom(0)+4)==0x3f123456u,"restore refuses to overwrite unexpected third-party RGB mutation");
        put(f.bloom(0)+4,0u);check(scope.finish()&&!scope.good()&&f.colors(0)==f.before[0],"only cleared conflict can finish journal; rejected status is sticky");}
    {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"selected index restoration fixture begins");f.select(1);
        check(scope.finish()&&scope.good()&&f.colors(0)==f.before[0],"native history may restore authoritative index1 before eye0 RGB finally");f.select(0);}
    {OrdinaryBloomScope scope;const auto mxcsr=_mm_getcsr();SetLastError(0x8721);owner.alter_environment=true;
        check(scope.begin(f.base,f.layout,0,owner.bind())&&GetLastError()==0x8721&&_mm_getcsr()==mxcsr,"begin preserves caller last error and floating-point control across owner callback");
        check(scope.finish()&&GetLastError()==0x8721&&_mm_getcsr()==mxcsr,"finish preserves caller last error and floating-point control");owner.alter_environment=false;}
}
void partial_apply(Fixture& f){
    // The admission callback precedes each store. Stop after one successful RGB
    // write and verify rollback, rather than only testing entry rejection.
    f.seed();f.select(0);Owner owner{&f};owner.refuse_call=4;OrdinaryBloomScope scope;
    check(!scope.begin(f.base,f.layout,0,owner.bind()),"owner loss during partial six-word apply rejects");
    check(owner.calls>=4&&scope.restored()&&!scope.pending()&&!scope.good()&&f.colors(0)==f.before[0],"partial apply restores first written RGB word and preserves all originals");
    Owner fault{&f};fault.readonly_call=4;OrdinaryBloomScope guarded;
    check(!guarded.begin(f.base,f.layout,0,fault.bind())&&fault.protected_page,"readonly fault after first applied word refuses protected store");
    check(guarded.pending()&&!guarded.good()&&read<std::uint32_t>(f.bloom(0))==0&&read<std::uint32_t>(f.bloom(0)+4)==f.before[0][1],"partial failed write tracks changed word and does not fabricate restoration");
    fault.unprotect();check(guarded.finish()&&guarded.restored()&&!guarded.good()&&f.colors(0)==f.before[0],"restoring write access permits exact pending rollback without admitting pair");
}
void native_unwinds(Fixture& f){
    for(unsigned eye=0;eye<2;++eye)for(unsigned mode=1;mode<=2;++mode){f.seed(1,3);f.select(eye);const auto before=f.snapshot();Owner owner{&f,eye};OrdinaryBloomScope scope;
        check(scope.begin(f.base,f.layout,eye,owner.bind()),"native unwind journal begins on either eye");NativeCall call{&f,&owner,eye,0,mode};bool caught=false;
        if(mode==1){try{invoke_ordinary_bloom_native(&scope,native_with_index_finally,&call);}catch(const std::runtime_error&){caught=true;}}
        else caught=catch_native_seh(&scope,&call);
        check(caught&&call.calls==1&&call.observed,"original native C++ or SEH propagates once with selected RGB active");
        check(scope.restored()&&!scope.pending()&&f.snapshot()==before&&read<unsigned>(f.buffer+0x16cc38)==1,"actual finally restores RGB after inner authoritative native index1 unwind");
        f.select(eye);OrdinaryBloomScope next;check(next.begin(f.base,f.layout,eye,owner.bind())&&next.finish(),"successful native exception cleanup leaves no dangling active TLS scope");
    }
}
void pair_admission(Fixture& f){
    for(unsigned failed_eye=0;failed_eye<3;++failed_eye){f.seed(0,3);Owner owner{&f};unsigned calls{},copies{},step{};bool started=false,rgb_complete=true;
        const auto before=f.snapshot();StereoDrawPlan plan{1,{0,1},{0,1},false};
        const bool copied=execute_stereo_draw_plan(plan,[&](std::uint32_t index){f.select(index);},[&]{
            const auto eye=step++;owner.eye=eye;owner.live=true;OrdinaryBloomScope scope;
            const bool applied=scope.begin(f.base,f.layout,eye,owner.bind());NativeCall call{&f,&owner,eye,0,eye==failed_eye?3u:0u};
            invoke_ordinary_bloom_native(&scope,native_with_index_finally,&call);calls+=call.calls;
            check(call.observed,"production stereo plan observes each selected eye journal in native order0,1");
            rgb_complete=rgb_complete&&applied&&scope.good()&&scope.restored()&&!scope.pending();
        },[&](unsigned){if(!rgb_complete)return false;++copies;return true;},&started);
        check(started&&calls==2&&read<unsigned>(f.buffer+0x16cc38)==1,"both native calls run once and selected index1 restores after rejected eye");
        check((copied&&rgb_complete)==(failed_eye==2)&&copies==(failed_eye==0?0u:failed_eye==1?1u:2u),"either rejected eye prevents complete pair; copy never occurs before restored journal");
        check(f.snapshot()==before,"pair success or rejection preserves both native views exactly");
    }
}
void quarantined_lifetime(){
    // There is intentionally no product test-reset API for sticky TLS poison.
    // Each scenario gets a fresh render thread; all assertions join before the
    // next thread so the simple test counters are never accessed concurrently.
    for(unsigned scenario=0;scenario<3;++scenario){std::thread thread([scenario]{Fixture f;check(f.base!=0,"quarantine fixture allocation succeeds");if(!f.base)return;
        f.seed();f.select(0);Owner owner{&f};
        {OrdinaryBloomScope scope;check(scope.begin(f.base,f.layout,0,owner.bind()),"pending lifetime fixture begins");
            if(scenario==0){owner.protected_page=f.protect(0,PAGE_READONLY,&owner.old_access);check(owner.protected_page,"destructor write failure is armed");
                check(!scope.finish()&&scope.pending(),"failed restoration remains pending before destructor");}
            if(scenario==1){NativeCall call{&f,&owner,0,0,4};invoke_ordinary_bloom_native(&scope,native_with_index_finally,&call);
                check(call.calls==1&&call.observed&&scope.pending()&&!scope.good(),"normal native finally closes pending journal and refuses playable pair");}
            if(scenario==2){owner.protected_page=f.protect(0,PAGE_READONLY,&owner.old_access);NativeCall call{&f,&owner,0,0,2};
                check(catch_native_seh(&scope,&call)&&call.calls==1&&call.observed&&scope.pending(),"native SEH finally closes failed restore without swallowing exception");}
        }
        owner.unprotect();const auto untouched=f.snapshot();
        for(unsigned retry=0;retry<3;++retry){OrdinaryBloomScope next;
            check(!next.begin(f.base,f.layout,0,owner.bind())&&!next.pending()&&!std::strcmp(next.failure(),"thread_quarantined"),"expired pending scope leaves sticky refusal, never a dereferenced stack pointer");
            check(f.snapshot()==untouched,"new scope cannot overwrite abandoned RGB journal after quarantine");}
    });thread.join();}
    // A poisoned worker must not quarantine another thread's owned frame.
    Fixture fresh;check(fresh.base!=0,"fresh main-thread fixture allocation succeeds");if(!fresh.base)return;fresh.seed();fresh.select(0);Owner owner{&fresh};OrdinaryBloomScope scope;
    check(scope.begin(fresh.base,fresh.layout,0,owner.bind())&&scope.finish(),"quarantine is confined to the failed render thread");
}
void failed_restore_denies_pair(){
    for(unsigned failed_eye=0;failed_eye<2;++failed_eye){std::thread thread([failed_eye]{Fixture f;
        check(f.base!=0,"failed pair fixture allocation succeeds");if(!f.base)return;f.seed(1,3);
        std::array<Owner,2> owners{{{&f,0},{&f,1}}};unsigned calls{},copies{},step{};bool started=false,accepted=true;
        const bool copied=execute_stereo_draw_plan(StereoDrawPlan{1,{0,1},{0,1},false},[&](std::uint32_t index){f.select(index);},[&]{
            const auto eye=step++;auto& owner=owners[eye];OrdinaryBloomScope scope;
            const bool applied=scope.begin(f.base,f.layout,eye,owner.bind());NativeCall call{&f,&owner,eye,0,eye==failed_eye?4u:0u};
            invoke_ordinary_bloom_native(&scope,native_with_index_finally,&call);calls+=call.calls;
            if(eye==failed_eye)check(applied&&call.observed&&scope.pending()&&!scope.restored(),"actual native return encounters selected-eye readonly restoration failure");
            accepted=accepted&&applied&&scope.good()&&scope.restored()&&!scope.pending();
        },[&](unsigned){if(!accepted)return false;++copies;return true;},&started);
        check(started&&calls==2&&!copied&&!accepted&&copies==failed_eye,"failed restoration on either eye denies complete pair while native calls remain exactly two");
        check(read<unsigned>(f.buffer+0x16cc38)==1,"actual failed restoration still preserves draw-plan authoritative selected index1");
        for(auto& owner:owners)owner.unprotect();
    });thread.join();}
}
}
int main(){Fixture f;check(f.base!=0,"native memory fixture allocation succeeds");if(!f.base)return 1;
    ordinary_paths(f);admission_mutations(f);restoration_contract(f);partial_apply(f);native_unwinds(f);pair_admission(f);quarantined_lifetime();failed_restore_denies_pair();
    std::printf("ordinary bloom selected-view journal: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
