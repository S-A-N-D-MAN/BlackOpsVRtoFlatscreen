// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include "t5/weapon_animation_locked.hpp"
#include <cstdio>
#include <string>
using namespace bovr::t5::animation_detail;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
struct Fixture {
    LockedRequest request{0x1234,72,78,82,45,true};
    CacheCopy state{0x9988,45,44,{{2,3,4},{12,35,7}},false,false};
    unsigned held{},locks{},unlocks{},states{},copies{},creates{},calculations{};
    unsigned fail_state{},change_clock{},change_pointer{},change_pose{},drop_bit{};
    int cached{};bool allocation_failed{},copy_failed{},cpp_fault{};std::string fault{},calls{};
    void point(const char* name){calls+=name;if(fault==name){if(cpp_fault)throw 77;RaiseException(0xE1230042,0,0,nullptr);}}
};
void lock(void* p,std::uintptr_t o){auto& f=*static_cast<Fixture*>(p);check(o==f.request.object&&!f.held,"native lock exact object, no recursive acquire");++f.locks;f.point("L");f.held=1;}
void unlock(void* p,std::uintptr_t o){auto& f=*static_cast<Fixture*>(p);check(o==f.request.object&&f.held,"unlock only acquired current object");++f.unlocks;f.held=0;f.point("U");}
bool state(void* p,const LockedRequest& r,CacheCopy* out){auto& f=*static_cast<Fixture*>(p);check(f.held&&r.object==f.request.object,"identity/clock read stays inside lock");++f.states;f.calls+='R';
    if(f.fail_state==f.states)return false;*out=f.state;
    if(f.change_clock==f.states)++out->clock;if(f.change_pointer==f.states)++out->matrices;if(f.change_pose==f.states)++out->pose.angles.z;
    if(f.drop_bit==f.states)out->gun_computed=false;return true;}
int cache(void* p,std::uintptr_t o,unsigned b,std::uintptr_t* out){auto& f=*static_cast<Fixture*>(p);check(f.held&&o==f.request.object&&b==f.request.flash_bone,"fresh cache uses exact native obj/barrel ordinal");++f.creates;f.point("C");
    if(f.allocation_failed){*out=0;return 1;}f.state.stamp=f.state.clock;*out=f.state.matrices;return f.cached;}
void hierarchy(void* p,std::uintptr_t o,unsigned b,std::uint32_t* bits){auto& f=*static_cast<Fixture*>(p);check(f.held&&o==f.request.object&&b==78,"hierarchy exact obj/tag");f.point("H");bits[0]=0x123;bits[4]=0x456;}
void pose(void* p,std::uintptr_t o,const std::uint32_t* bits){auto& f=*static_cast<Fixture*>(p);check(f.held&&o==f.request.object&&bits[0]==0x123&&bits[4]==0x456,"pose receives all five native hierarchy words");f.point("P");}
void skeleton(void* p,std::uintptr_t o,const std::uint32_t* bits){auto& f=*static_cast<Fixture*>(p);check(f.held&&o==f.request.object&&bits[4]==0x456,"skeleton follows pose under same lock");++f.calculations;f.point("S");f.state.gun_computed=f.state.flash_computed=true;}
bool copy(void* p,const LockedRequest&,std::uintptr_t m,NativeMat32* g,NativeMat32* t){auto& f=*static_cast<Fixture*>(p);check(f.held&&m==f.state.matrices,"two raw32 matrices copied before unlock");++f.copies;f.calls+='B';if(f.copy_failed)return false;
    *g={{0,0,0,3},{1,2,3},2.F/9};*t={{0,0,0,1},{19,2,3},2};return true;}
LockedHost host(Fixture& f){return {&f,lock,unlock,state,cache,hierarchy,pose,skeleton,copy};}
bool seh(Fixture* f,LockedCopy* out){__try{capture_locked(host(*f),f->request,out);return false;}__except(GetExceptionCode()==0xE1230042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}}
}
int main(){
    Fixture f;LockedCopy out{};auto result=capture_locked(host(f),f.request,&out);
    check(result==LockedStatus::accepted&&f.locks==1&&f.unlocks==1&&!f.held&&f.calculations==1,"fresh pre calculates once and releases normally");
    check(f.calls=="LRCRHPSRBRU","exact one-lock fresh chain and protected rechecks");
    check(out.old_stamp==44&&out.final_stamp==45&&out.clock==45&&out.calculated&&out.gun.quaternion[3]==3,"copied final output preserves cache evidence/raw native representation");
    for(bool absent:{false,true}){Fixture c;c.state.stamp=45;if(absent)c.state.matrices=0;capture_locked(host(c),c.request,&out);check(c.creates==0&&c.calculations==0&&c.unlocks==1&&!c.held,"all same-clock pre states rejected, including null cache");}
    {Fixture c;c.allocation_failed=true;check(capture_locked(host(c),c.request,&out)==LockedStatus::cache_unavailable&&c.unlocks==1&&c.calculations==0,"native return1 plus null matrix is allocation failure");}
    {Fixture c;c.cached=1;check(capture_locked(host(c),c.request,&out)==LockedStatus::cache_changed&&c.unlocks==1,"unexpected cached pre return does not prove fresh work");}
    {Fixture c;c.request.require_new_clock=false;c.state.stamp=45;c.cached=1;c.state.gun_computed=c.state.flash_computed=true;check(capture_locked(host(c),c.request,&out)==LockedStatus::accepted&&!out.calculated&&c.calculations==0&&c.unlocks==1,"post may copy current computed native cache inside lock");}
    for(unsigned phase=1;phase<=4;++phase){Fixture c;c.fail_state=phase;check(capture_locked(host(c),c.request,&out)!=LockedStatus::accepted&&c.unlocks==1&&!c.held,"protected state-copy failure unlocks exactly once");}
    for(unsigned phase=2;phase<=4;++phase){Fixture c;c.change_clock=phase;check(capture_locked(host(c),c.request,&out)!=LockedStatus::accepted&&c.unlocks==1,"changed native clock rejects normally");Fixture d;d.change_pointer=phase;check(capture_locked(host(d),d.request,&out)!=LockedStatus::accepted&&d.unlocks==1,"changed matrix identity rejects normally");Fixture e;e.change_pose=phase;check(capture_locked(host(e),e.request,&out)!=LockedStatus::accepted&&e.unlocks==1,"changed copied persistent cpose rejects normally");}
    {Fixture c;c.drop_bit=3;check(capture_locked(host(c),c.request,&out)==LockedStatus::bones_missing&&c.unlocks==1&&c.copies==0,"both gun and barrel must be computed before copy");}
    {Fixture c;c.copy_failed=true;check(capture_locked(host(c),c.request,&out)==LockedStatus::copy_failed&&c.unlocks==1&&!c.held,"protected raw copy rejection releases lock normally");}
    for(const char* phase:{"L","C","H","P","S","U"}){
        Fixture c;c.fault=phase;check(seh(&c,&out),"native SEH reaches external handler unchanged");
        check(c.unlocks==(phase[0]=='U'?1u:0u)&&c.held==(phase[0]=='L'||phase[0]=='U'?0u:1u),"native fault retains exact original acquire/unwind lock semantics");
        Fixture d;d.fault=phase;d.cpp_fault=true;bool caught=false;try{capture_locked(host(d),d.request,&out);}catch(int v){caught=v==77;}
        check(caught,"native C++ reaches external handler unchanged");
        check(d.unlocks==(phase[0]=='U'?1u:0u)&&d.held==(phase[0]=='L'||phase[0]=='U'?0u:1u),"native C++ does not invent an unwind unlock");
    }
    {Fixture c;c.request.gun_bone=160;check(capture_locked(host(c),c.request,&out)==LockedStatus::invalid&&c.locks==0,"bad bone rejected before acquire");}
    {Fixture c;c.request.require_completed_native_clear=true;check(capture_locked(host(c),c.request,&out)==LockedStatus::invalid&&c.locks==0,"native-clear and old new-clock modes cannot be combined");}
    for(unsigned word=0;word<15;++word)for(unsigned bit=0;bit<32;++bit){Fixture c;c.request.require_new_clock=false;c.request.require_completed_native_clear=true;c.state.stamp=45;
        c.state.part_bits[word]=1U<<bit;check(capture_locked(host(c),c.request,&out)==LockedStatus::native_clear_not_empty&&c.creates==0&&c.calculations==0&&c.unlocks==1,"every ancestor/control/skeleton bit must be zero after witnessed native clear");}
    {Fixture c;c.request.require_new_clock=false;c.request.require_completed_native_clear=true;c.state.stamp=45;
        check(capture_locked(host(c),c.request,&out)==LockedStatus::accepted&&out.old_stamp==45&&out.calculated&&c.calls=="LRCRHPSRBRU","witnessed native clear permits current-stamp retained matrix but forces native calculation");}
    {Fixture c;c.request.require_new_clock=false;c.request.require_completed_native_clear=true;c.state.stamp=45;c.cached=1;
        check(capture_locked(host(c),c.request,&out)==LockedStatus::cache_changed&&c.calculations==0&&c.unlocks==1,"unexpected native cache hit after all-zero observation fails closed");}
    for(unsigned phase=1;phase<=4;++phase){Fixture c;c.request.require_new_clock=false;c.request.require_completed_native_clear=true;c.state.stamp=45;c.fail_state=phase;
        check(capture_locked(host(c),c.request,&out)!=LockedStatus::accepted&&c.unlocks==1&&!c.held,"clear-mode protected-copy rejection still unlocks exactly once");}
    for(const char* phase:{"L","C","H","P","S","U"})for(bool cpp:{false,true}){Fixture c;c.request.require_new_clock=false;c.request.require_completed_native_clear=true;c.state.stamp=45;c.fault=phase;c.cpp_fault=cpp;
        bool caught{};if(cpp)try{capture_locked(host(c),c.request,&out);}catch(int v){caught=v==77;}else caught=seh(&c,&out);
        check(caught&&c.unlocks==(phase[0]=='U'?1U:0U)&&c.held==(phase[0]=='L'||phase[0]=='U'?0U:1U),"clear-mode native exceptions preserve original native lock disposition");}
    printf("checks=%u failures=%u game_native_calls=0\n",checks,failures);return failures?1:0;
}
