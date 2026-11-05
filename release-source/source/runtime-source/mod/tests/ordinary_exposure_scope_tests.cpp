// SPDX-License-Identifier: GPL-3.0-only
// Exercise production ownership, replay and exceptional restoration against
// private memory; no running game or renderer is used by this test.
#include "../src/t5/ordinary_exposure_scope.cpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
template<class T>T read(std::uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof v);return v;}
struct Fixture {
    std::uintptr_t base{},buffer{},views{},cg{},refdef{};bool live{true};unsigned calls{};
    Fixture(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4240000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));}
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void seed(unsigned slot=0,float initial=0.f){
        buffer=base+(slot?0x38c6400:0x3740100);views=base+0x420f730+(slot?0xe380:0);cg=base+0x100000;refdef=cg+0x8c100;
        put(base+0x373708c,static_cast<std::uint32_t>(buffer));put(base+0x2bf5354,static_cast<std::uint32_t>(cg));
        put(buffer+0x15d7a0,31u);put(buffer+0x16cc3c,0u);put(buffer+0x16cbe0,0u);put(buffer+0x16cc38,0u);put(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        put(cg+0x8a348,1000);put(cg+0x8a344,11);put(refdef+0x58,1000);
        put(refdef+0x18478,initial);put(refdef+0x183bc,std::array<float,7>{0,0,0,initial,8,8,8});
        put(refdef+0x183b8,0x12345678u);put(refdef+0x183d8,0x23456789u);put(refdef+0x18474,0x3456789au);put(refdef+0x1847c,0x456789abu);
        configured_base=base;configured_enabled=true;poisoned=false;live=true;calls=0;
    }
    bool begin(OrdinaryExposureScope& s){return s.begin(base,buffer,31,refdef,44,{this,[](void* p) noexcept{return static_cast<Fixture*>(p)->live;}});}
    void native(unsigned eye,float target=1.15f){
        ++calls;float e=read<float>(refdef+0x18478);e=((20.f-1.f)*e+target)/20.f;
        put(refdef+0x18478,e);put(refdef+0x183bc,std::array<float,7>{0,0,0,e,8,8,8});
        put(views+eye*0x38e0+0x3534,e);put(buffer+0x16cc3c,eye+1);put(buffer+0x16cbe0,eye+1);put(buffer+0x16cc38,eye);
    }
    void sentinels(){check(read<unsigned>(refdef+0x183b8)==0x12345678u&&read<unsigned>(refdef+0x183d8)==0x23456789u&&
        read<unsigned>(refdef+0x18474)==0x3456789au&&read<unsigned>(refdef+0x1847c)==0x456789abu&&
        read<int>(refdef+0x58)==1000&&read<int>(cg+0x8a344)==11,"unrelated fields and gameplay times stay untouched");}
};
struct Call {Fixture* f{};unsigned eye{},failure{};float target{1.15f};};
void native_body(void* p){auto& c=*static_cast<Call*>(p);c.f->native(c.eye,c.target);
    if(c.failure==1)throw std::runtime_error("native failure");if(c.failure==2)RaiseException(0xe0421771,0,0,nullptr);}
bool catch_seh(OrdinaryExposureScope* s,Call* c){
    __try{invoke_ordinary_exposure_native(s,c->eye,native_body,c);}
    __except(GetExceptionCode()==0xe0421771?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
void normal_and_boundary(Fixture& f){
    for(unsigned slot=0;slot<2;++slot)for(float start:{0.f,.3f,2.f}){
        f.seed(slot,start);OrdinaryExposureScope scope;check(f.begin(scope),"both native frontend buffers admitted");
        Call left{&f,0};invoke_ordinary_exposure_native(&scope,0,native_body,&left);
        const float committed=read<float>(f.refdef+0x18478);check(scope.prepare_right(),"right rewinds after proved left output");
        check(read<float>(f.refdef+0x18478)==start&&read<float>(f.refdef+0x183c8)==committed,"only accumulated exposure is replayed before right");
        Call right{&f,1};invoke_ordinary_exposure_native(&scope,1,native_body,&right);
        check(scope.finish()&&scope.safe()&&f.calls==2,"two native calls complete with one temporal step");
        check(read<float>(f.views+0x3534)==read<float>(f.views+0x38e0+0x3534)&&read<float>(f.refdef+0x18478)==committed&&
              read<float>(f.refdef+0x183c8)==committed,"both eyes and committed exposure match first update");f.sentinels();
    }
    f.seed();OrdinaryExposureScope differing;check(f.begin(differing),"boundary fixture begins");
    Call left{&f,0};invoke_ordinary_exposure_native(&differing,0,native_body,&left);const auto committed=read<float>(f.refdef+0x18478);
    check(differing.prepare_right(),"boundary fixture rewinds");Call right{&f,1,0,1.4f};invoke_ordinary_exposure_native(&differing,1,native_body,&right);
    check(differing.finish()&&differing.safe()&&read<float>(f.views+0x3534)!=read<float>(f.views+0x38e0+0x3534)&&
          read<float>(f.refdef+0x18478)==committed&&read<float>(f.refdef+0x183c8)==committed,
          "different native volume targets retain their views without blackout and commit one step");f.sentinels();
    f.seed();configured_enabled=false;OrdinaryExposureScope disabled;check(f.begin(disabled),"disabled scope is passthrough");
    Call a{&f,0};invoke_ordinary_exposure_native(&disabled,0,native_body,&a);const float first=read<float>(f.refdef+0x18478);
    check(disabled.prepare_right(),"disabled right passthrough");Call b{&f,1};invoke_ordinary_exposure_native(&disabled,1,native_body,&b);
    check(disabled.finish()&&read<float>(f.refdef+0x18478)>first*1.94f,"disabled A/B retains native double advance");
}
void rejection_and_exceptions(Fixture& f){
    for(unsigned defect=0;defect<4;++defect){
        f.seed();OrdinaryExposureScope s;check(f.begin(s),"rejection fixture begins");Call left{&f,0};invoke_ordinary_exposure_native(&s,0,native_body,&left);
        if(defect==0)put(f.buffer+0x15d7a0,32u);
        if(defect==1)put(f.base+0x2bf5354,static_cast<std::uint32_t>(f.cg+4));
        if(defect==2)put(f.refdef+0x183cc,7.f);
        if(defect==3)f.live=false;
        const auto accumulator=read<float>(f.refdef+0x18478);
        check(!s.prepare_right()&&read<float>(f.refdef+0x18478)==accumulator,"changed frame/refdef/result/owner refuses rewind without writes");s.finish();
    }
    f.seed();put(f.refdef+0x18478,std::numeric_limits<float>::quiet_NaN());OrdinaryExposureScope invalid;
    check(!f.begin(invalid),"nonfinite initial exposure refuses mutation");
    for(unsigned failure=1;failure<=2;++failure){
        f.seed();OrdinaryExposureScope s;check(f.begin(s),"native exception fixture begins");Call left{&f,0};invoke_ordinary_exposure_native(&s,0,native_body,&left);
        const auto committed=read<float>(f.refdef+0x18478);check(s.prepare_right(),"native exception fixture rewinds");Call right{&f,1,failure,1.4f};bool caught=false;
        if(failure==1){try{invoke_ordinary_exposure_native(&s,1,native_body,&right);}catch(const std::runtime_error&){caught=true;}}
        else caught=catch_seh(&s,&right);
        check(caught&&!s.safe()&&read<float>(f.refdef+0x18478)==committed&&read<float>(f.refdef+0x183c8)==committed&&active_scope==nullptr,
              "native C++/SEH exception propagates after exact first-step restoration and TLS retirement");f.sentinels();
    }
    f.seed();OrdinaryExposureScope wrong_thread;check(f.begin(wrong_thread),"thread fixture begins");Call left{&f,0};invoke_ordinary_exposure_native(&wrong_thread,0,native_body,&left);
    const auto before=read<float>(f.refdef+0x18478);bool result=true;std::thread other([&]{result=wrong_thread.prepare_right();});other.join();
    check(!result&&read<float>(f.refdef+0x18478)==before,"foreign thread cannot rewind");wrong_thread.finish();
}
}
int main(){Fixture f;check(f.base!=0,"private fixture allocated");if(f.base){normal_and_boundary(f);rejection_and_exceptions(f);}
    std::printf("ordinary exposure scope: %u checks, %u failures\n",checks,failures);return failures?1:0;}
