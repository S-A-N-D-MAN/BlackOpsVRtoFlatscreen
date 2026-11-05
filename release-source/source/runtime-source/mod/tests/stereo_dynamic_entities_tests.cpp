// SPDX-License-Identifier: GPL-3.0-only
#include "t5/stereo_dynamic_entities.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>
#include <immintrin.h>
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
template<class T>T read(std::uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof v);return v;}
constexpr std::size_t extent=0x4240000;
constexpr std::uintptr_t count_rva=0x371f73c;
struct Fixture {
    std::uintptr_t base{},buffer{},views{},world{},models{},brushes{};std::uint32_t frame=97;
    Fixture(){SetEnvironmentVariableW(L"BOVR_STEREO_DYNAMIC_ENTITIES",nullptr);
        base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,extent,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!base)return;
        for(auto [rva,size]:{std::pair<std::uintptr_t,unsigned>{0x2b22f0,531},{0x2bf820,533},
                            {0x2b1790,252},{0x2b5040,344},{0x2c7391,9}})
            for(unsigned i=0;i<size;++i)*reinterpret_cast<unsigned char*>(base+rva+i)=static_cast<unsigned char>((rva+i)*13+7);
        buffer=base+0x3740100;views=base+0x420f730;world=base+0x1000;models=base+0x10000;brushes=base+0x14000;
        put(base+0x373708c,static_cast<std::uint32_t>(buffer));put(base+0x356a24c,static_cast<std::uint32_t>(world));
        put(buffer+0x15d7a0,frame);put(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        put(world+0x324,static_cast<std::uint32_t>(models));put(world+0x328,static_cast<std::uint32_t>(brushes));
        put(world+0x3bc,1310u);put(world+0x3c0,2u);
        for(unsigned i=0;i<2;++i)put(views+i*0x38e0+0x3510,static_cast<std::uint32_t>(buffer));
        std::memset(reinterpret_cast<void*>(models),0xa5,1310*6);std::memset(reinterpret_cast<void*>(brushes),0x5a,8);
        check(configure_stereo_dynamic_entities(reinterpret_cast<HMODULE>(base)),"synthetic pristine contract configures");}
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);SetEnvironmentVariableW(L"BOVR_STEREO_DYNAMIC_ENTITIES",nullptr);}
    void cameras(unsigned n){put(buffer+0x16cc3c,n);put(buffer+0x16cbe0,n);put(buffer+0x16cc38,n?n-1:0);}
    void counts(unsigned model,unsigned brush){put(base+count_rva,std::array<std::uint32_t,2>{model,brush});}
    bool begin(StereoDynamicEntitiesScope& scope){return scope.begin(base,buffer,frame,101);}
    // Model the independently pinned native contract: counted compact records
    // are replaced completely and draw lists refer to copied buffer surfaces.
    std::vector<unsigned> native_doors(unsigned eye){auto n=read<std::uint32_t>(base+count_rva);
        for(unsigned door=0;door<2;++door){put(models+n*6,std::array<std::uint16_t,3>{1,static_cast<std::uint16_t>(100+eye*20+door),static_cast<std::uint16_t>(door)});++n;}
        put(base+count_rva,n);cameras(eye+1);std::vector<unsigned> result;
        for(unsigned i=0;i<n;++i)result.push_back(read<std::uint16_t>(models+i*6+4));return result;}
};
void success_and_ownership(){Fixture f;if(!f.base){check(false,"fixture allocation");return;}
    StereoDynamicEntitiesScope scope;SetLastError(0x1532);const auto original_mxcsr=_mm_getcsr();
    const auto custom_mxcsr=(original_mxcsr&~0x6000u)|0x6000u;_mm_setcsr(custom_mxcsr);
    const bool begun=f.begin(scope);const auto begin_error=GetLastError();const auto begin_mxcsr=_mm_getcsr();_mm_setcsr(original_mxcsr);
    check(begun,"empty ordinary frontend begins");
    check(begin_error==0x1532&&begin_mxcsr==custom_mxcsr,"begin preserves native LastError and floating state");
    auto left=f.native_doors(0);std::array<unsigned char,64> emitted{};emitted.fill(0x7c);
    put(f.buffer+0x88000,emitted);
    std::array<unsigned char,8> before{};std::memcpy(before.data(),reinterpret_cast<void*>(f.models),8);
    const auto world_before=read<std::array<unsigned char,0x400>>(f.world);
    SetLastError(0x2911);const bool prepared=scope.prepare_right();const auto prepared_error=GetLastError();
    check(prepared,"right native camera gets fresh compact counters");
    check(prepared_error==0x2911,"VirtualQuery and counter preparation preserve native LastError");
    check(read<std::array<std::uint32_t,2>>(f.base+count_rva)==std::array<std::uint32_t,2>{},"only two counters reset");
    check(read<std::array<unsigned char,8>>(f.models)==before,"compact model records are not cleared");
    check(read<std::array<unsigned char,0x400>>(f.world)==world_before,"world pointers capacities visibility caches unchanged");
    check(read<std::array<unsigned char,64>>(f.buffer+0x88000)==emitted,"already emitted left backend surfaces unchanged");
    auto right=f.native_doors(1);check(scope.finish(),"complete ordinary pair retains native right final counters");
    check(left==right&&left.size()==2&&right.size()==2,"same cabinet doors appear once per eye");
    check(scope.left_counts()==std::array<std::uint32_t,2>{2,0}&&scope.right_counts()==std::array<std::uint32_t,2>{2,0},"per-eye populations captured");
    check(read<std::uint16_t>(f.models+2)==120,"native regenerates right buffer-local surface IDs");
    check(read<std::array<unsigned char,64>>(f.buffer+0x88000)==emitted,"right regeneration preserves emitted left surfaces");
    check(scope.finish(),"completion is read-only idempotent");
}
void disabled_baseline(){Fixture f;SetEnvironmentVariableW(L"BOVR_STEREO_DYNAMIC_ENTITIES",L"0");
    check(configure_stereo_dynamic_entities(reinterpret_cast<HMODULE>(f.base)),"explicit baseline flag accepted");
    StereoDynamicEntitiesScope scope;check(scope.begin(0,0,0,0),"disabled module needs no native memory");
    auto left=f.native_doors(0);check(scope.prepare_right(),"disabled right preparation inert");auto right=f.native_doors(1);
    check(scope.finish()&&!scope.enabled(),"disabled completion inert");
    check(left.size()==2&&right.size()==4&&right[0]==right[2]&&right[1]==right[3],"baseline reproduces appended duplicate transparent doors");
}
void all_owned_failures(){
    for(unsigned defect=0;defect<14;++defect){Fixture f;StereoDynamicEntitiesScope scope;check(f.begin(scope),"failure fixture begins");
        f.cameras(1);f.counts(12,1);
        switch(defect){
        case 0:put(f.base+0x373708c,static_cast<std::uint32_t>(f.buffer+0x186300));break;
        case 1:put(f.buffer+0x15d7a0,f.frame+1);break;
        case 2:f.cameras(2);break;
        case 3:put(f.buffer+0x16cbe0,0u);break;
        case 4:put(f.buffer+0x16cc38,1u);break;
        case 5:put(f.buffer+0x16cc40,static_cast<std::uint32_t>(f.views+0xe380));break;
        case 6:put(f.base+0x356a24c,static_cast<std::uint32_t>(f.world+4));break;
        case 7:put(f.world+0x324,static_cast<std::uint32_t>(f.models+2));break;
        case 8:put(f.world+0x3bc,1309u);break;
        case 9:put(f.views+0x3510,0u);break;
        case 10:put(f.views+0x38c4,std::uint8_t{1});break;
        case 11:f.counts(1311,1);break;
        case 12:f.counts(12,3);break;
        case 13:{DWORD old{};VirtualProtect(reinterpret_cast<void*>(f.base+count_rva),8,PAGE_READONLY,&old);break;}
        }
        auto initial=read<std::array<std::uint32_t,2>>(f.base+count_rva);
        check(!scope.prepare_right()&&!scope.good(),"wrong owner frame layout world or limits reject pair");
        check(read<std::array<std::uint32_t,2>>(f.base+count_rva)==initial,"all preflight rejections leave both counters unchanged");
        check(!scope.finish(),"rejected right camera cannot finish for publication");
    }
}
void phase_thread_and_completion(){
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"thread fixture starts");f.cameras(1);f.counts(3,1);bool accepted{};
        std::thread t([&]{accepted=s.prepare_right();});t.join();check(!accepted&&read<std::uint32_t>(f.base+count_rva)==3,"foreign thread cannot reset native counts");}
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"phase fixture starts");check(!s.prepare_right(),"prepare before left camera rejects");}
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"unfinished fixture starts");check(!s.finish(),"finish without right preparation rejects");}
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"duplicate fixture starts");f.cameras(1);f.counts(3,1);check(s.prepare_right(),"first preparation succeeds");
        f.counts(2,1);check(!s.prepare_right()&&read<std::uint32_t>(f.base+count_rva)==2,"second preparation cannot clear newly generated native records");}
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"final guard fixture starts");f.cameras(1);f.counts(3,1);check(s.prepare_right(),"right preparation succeeds");
        f.cameras(2);f.counts(1311,0);check(!s.finish(),"native out-of-world right population rejects publication");}
    {Fixture f;StereoDynamicEntitiesScope s;check(f.begin(s),"second slot final fixture starts");f.cameras(1);check(s.prepare_right(),"empty model population valid");
        f.cameras(2);put(f.views+0x38e0+0x38c4,std::uint8_t{1});check(!s.finish(),"extra view cannot masquerade as ordinary right eye");}
}
void startup_guards_and_initial_state(){
    {Fixture f;for(auto rva:{0x2b22f0u,0x2bf820u,0x2b1790u,0x2b5040u,0x2c7391u}){
        auto* byte=reinterpret_cast<unsigned char*>(f.base+rva);*byte^=1;
        check(!validate_stereo_dynamic_native_guards(reinterpret_cast<HMODULE>(f.base)),"every pinned native contract independently guarded");*byte^=1;}}
    for(auto bad:{0xfffffffcu,0x10000u}){Fixture f;put(f.base+0x356a24c,bad);StereoDynamicEntitiesScope scope;
        check(!f.begin(scope)&&std::strcmp(scope.failure().field,"world_span")==0,"overflowing or unmapped world span refuses before offset reads");
        check(read<std::array<std::uint32_t,2>>(f.base+count_rva)==std::array<std::uint32_t,2>{},"invalid world span leaves counters unchanged");}
    {Fixture f;f.counts(1,0);StereoDynamicEntitiesScope s;check(!f.begin(s)&&read<std::uint32_t>(f.base+count_rva)==1,"nonempty preexisting scene not reset");}
    {Fixture f;f.cameras(1);StereoDynamicEntitiesScope s;check(!f.begin(s),"scope begins only before any camera");}
    {Fixture f;put(f.world+0x3bc,0x10000u);StereoDynamicEntitiesScope s;check(!f.begin(s),"native ushort population overflow rejected");}
    {Fixture f;put(f.world+0x324,0u);StereoDynamicEntitiesScope s;check(!f.begin(s),"missing populated native array rejected");}
    {Fixture f;put(f.world+0x3bc,0u);put(f.world+0x324,0u);put(f.world+0x3c0,0u);put(f.world+0x328,0u);
        StereoDynamicEntitiesScope s;check(f.begin(s),"world with no dynamic entities safe");f.cameras(1);check(s.prepare_right(),"empty right camera preparation safe");f.cameras(2);check(s.finish(),"empty pair completes");}
    {Fixture f;auto* byte=reinterpret_cast<unsigned char*>(f.base+0x2b22f0);*byte^=1;
        check(!configure_stereo_dynamic_entities(reinterpret_cast<HMODULE>(f.base))&&!stereo_dynamic_entities_enabled(),"startup mismatch disables all module writes");}
}
}
int main(){success_and_ownership();disabled_baseline();all_owned_failures();phase_thread_and_completion();startup_guards_and_initial_state();
    std::printf("stereo dynamic entities: %u checks, %u failures\n",checks,failures);return failures?1:0;}
