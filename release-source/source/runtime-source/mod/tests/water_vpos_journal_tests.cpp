// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_vpos_journal.hpp"
#include "t5/water_owned_call.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace bovr::t5;
namespace {
void check(bool condition,const char* message){if(!condition){std::cerr<<message<<'\n';std::exit(1);}}
WaterVPosJournal make(){WaterVPosJournal j;for(unsigned i=0;i<5;++i){j.saved[i]={float(i+1),2,3,-0.f};j.written[i]={float(i+8),6,7,-0.f};}
    j.saved_shader=100;j.written_shader=200;return j;}
}
int main(){
    auto j=make();auto gpu=j.saved;std::uintptr_t shader=j.saved_shader;
    std::array<unsigned,6> order{};unsigned count{};
    auto set=[&](unsigned i,const VPosVector& value) noexcept {gpu[i]=value;return true;};
    auto set_shader=[&](std::uintptr_t value) noexcept {shader=value;return true;};
    auto get=[&](unsigned i,VPosVector& out) noexcept {out=gpu[i];return true;};
    auto get_shader=[&](std::uintptr_t& out) noexcept {out=shader;return true;};
    check(j.apply(set,set_shader)&&j.mask==63&&gpu==j.written&&shader==200,"five constants and shader applied atomically before draw");
    check(!j.apply(set,set_shader)&&j.mask==63,"nested application refused without dropping journal");
    check(j.restore([&](unsigned i,const VPosVector& value) noexcept {order[count++]=i;return set(i,value);},
                    [&](std::uintptr_t value) noexcept {order[count++]=5;return set_shader(value);}),"restore succeeded");
    check(order==std::array<unsigned,6>{5,4,3,2,1,0}&&gpu==j.saved&&shader==100&&!j.mask,"shader then five constants restore in reverse order");
    for(unsigned fail=0;fail<6;++fail){j=make();gpu=j.saved;shader=j.saved_shader;
        check(!j.apply([&](unsigned i,const VPosVector& value) noexcept {gpu[i]=value;return i!=fail;},
                       [&](std::uintptr_t value) noexcept {shader=value;return fail!=5;}),"setter failure may occur after mutation");
        check(j.mask==((1u<<(fail+1))-1),"failed setter remains a restoration obligation");
        check(j.restore(set,set_shader)&&gpu==j.saved&&shader==100&&!j.mask,"failure after every constant/shader setter restored fully");}
    for(unsigned fail=0;fail<6;++fail){j=make();gpu=j.saved;shader=j.saved_shader;check(j.apply(set,set_shader),"apply before each restore failure");
        check(!j.restore([&](unsigned i,const VPosVector& value) noexcept {if(i==fail)return false;return set(i,value);},
                         [&](std::uintptr_t value) noexcept {if(fail==5)return false;return set_shader(value);}),"injected restore fails");
        check(j.mask==((1u<<(fail+1))-1),"reverse failure retains only unresolved obligations");
        check(j.reconcile(get,set,get_shader,set_shader)&&gpu==j.saved&&shader==100&&!j.mask,"reconciliation recovers shader and remaining vectors");}
    j=make();gpu=j.saved;shader=j.saved_shader;check(j.apply(set,set_shader),"apply before native replacement");
    gpu[1]={42,43,44,45};shader=300;
    check(j.reconcile(get,set,get_shader,set_shader)&&gpu[1]==VPosVector{42,43,44,45}&&shader==300&&!j.mask,
          "later native shader and constant writes are preserved");
    j=make();gpu=j.saved;shader=j.saved_shader;check(j.apply(set,set_shader),"apply before unavailable device");
    check(!j.reconcile(get,set,[](std::uintptr_t&) noexcept {return false;},set_shader)&&j.mask==63,
          "failed shader getter preserves complete journal for reset recovery");
    check(!j.reconcile([](unsigned,VPosVector&) noexcept {return false;},set,get_shader,set_shader)&&j.mask==31&&shader==100,
          "failed constant getter preserves constants after shader reconciliation");
    check(j.reconcile(get,set,get_shader,set_shader)&&!j.mask,"later device recovery clears journal");
    j=make();gpu=j.saved;shader=j.saved_shader;check(j.apply(set,set_shader),"apply before failed reconciliation setter");
    check(!j.reconcile(get,set,get_shader,[](std::uintptr_t) noexcept {return false;})&&j.mask==63,
          "failed shader restoration cannot clear its journal bit");
    check(j.reconcile(get,set,get_shader,set_shader)&&!j.mask,"retry restores shader and constants");
    j=make();j.written_shader=j.saved_shader;
    check(!j.apply(set,set_shader)&&!j.mask,"same shader is not accepted as a derivative transaction");
    check(WaterVPosJournal::same(gpu[0],j.saved[0]),"unused w bit pattern preserved");
    // The production call seam must detect invalidation AFTER a successful
    // setter, leaving its journal bit alive until authority is re-established.
    for(unsigned defect=0;defect<5;++defect){
        struct Context {unsigned owner=1,epoch=2,serial=3,device=4;bool operator==(const Context&) const=default;};
        Context active{},expected=active;j=make();gpu=j.saved;shader=j.saved_shader;
        auto current=[&]() noexcept {return active==expected;};
        unsigned calls{};
        check(!j.apply([&](unsigned i,const VPosVector& value) noexcept {
            return water_owned_call(current,[&]() noexcept {
                ++calls;gpu[i]=value;
                if(defect==0)++active.owner;if(defect==1)++active.epoch;
                if(defect==2)++active.serial;if(defect==3)++active.device;
                return defect!=4;
            });},set_shader),"failed or stale post-setter call refuses draw");
        check(calls==1&&j.mask==1&&gpu[0]==j.written[0]&&shader==j.saved_shader,
              "post-call invalidation retains exactly the mutated first constant");
        if(defect!=4){
            check(!j.restore([&](unsigned i,const VPosVector& value) noexcept {
                return water_owned_call(current,[&]() noexcept {return set(i,value);});},set_shader)&&j.mask==1,
                "stale owner epoch reentry or device cannot restore under another context");
        }
        active=expected;
        check(j.restore([&](unsigned i,const VPosVector& value) noexcept {
            return water_owned_call(current,[&]() noexcept {return set(i,value);});},set_shader)&&!j.mask&&gpu==j.saved,
            "re-established exact authority restores the pending constant");
    }
    unsigned guarded_calls{};
    check(!water_owned_call([]() noexcept {return false;},[&]() noexcept {++guarded_calls;return true;})&&!guarded_calls,
          "invalid pre-call ownership performs no COM operation");
    std::cout<<"water ray/UV/shader journal contracts passed\n";
}
