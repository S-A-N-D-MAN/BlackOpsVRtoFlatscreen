// SPDX-License-Identifier: GPL-3.0-only
#include "../src/t5/grenade_throw_hook.cpp"
#include <cstdio>
#include <limits>

namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&) {return {};}
bool validate_loaded_headers(HMODULE,std::string&) {return false;}
std::wstring executable_path() {return {};}
}
namespace {
using namespace bovr::t5;
unsigned checks{},failures{},native_calls{},callback_calls{};
bool accept{},bad_geometry{},raise_fault{};
GrenadeThrowArguments observed{},delivered{};
float* delivered_origin{};float* delivered_velocity{};
DWORD delivered_error{};
int result_token;
void check(bool ok,const char* label){++checks;if(!ok)++failures;std::printf("%s %s\n",ok?"PASS":"FAIL",label);}
bool callback(const GrenadeThrowArguments& in,GrenadeThrowReplacement* out) noexcept {
    ++callback_calls;observed=in;SetLastError(991);
    out->origin={101,102,103};out->velocity={0,900,240};
    if(bad_geometry)out->velocity[2]=std::numeric_limits<float>::quiet_NaN();
    if(raise_fault)RaiseException(0xc0000005,0,0,nullptr);
    return accept;
}
void* __cdecl original(void* parent,float* origin,float* velocity,unsigned weapon,unsigned model,int rotate,int fuse) {
    ++native_calls;delivered={reinterpret_cast<std::uintptr_t>(parent),weapon,model,rotate,fuse};
    std::memcpy(delivered.origin.data(),origin,sizeof(delivered.origin));
    std::memcpy(delivered.velocity.data(),velocity,sizeof(delivered.velocity));
    delivered_origin=origin;delivered_velocity=velocity;delivered_error=GetLastError();
    SetLastError(778);return &result_token;
}
void reset(){native_calls=callback_calls=0;accept=bad_geometry=raise_fault=false;}
}
int main() {
    using namespace bovr::t5;
    native_throw.store(original);throw_callback.store(callback);
    float start[]{1,2,3},velocity[]{400,5,6};
    const std::array<float,3> original_start{1,2,3},original_velocity{400,5,6};
    const auto parent=reinterpret_cast<void*>(0x1a796f8);
    reset();SetLastError(123);
    const auto result=throw_bridge(parent,start,velocity,7,3,1,456);
    check(result==&result_token&&native_calls==1,"cdecl return and exactly one native call");
    check(callback_calls==1&&observed.parent==reinterpret_cast<std::uintptr_t>(parent)&&observed.weapon==7&&
          observed.model==3&&observed.rotate==1&&observed.fuse_time==456,"all seven native argument meanings reach value callback");
    check(delivered_origin==start&&delivered_velocity==velocity&&delivered.origin==original_start&&
          delivered.velocity==original_velocity,"rejected callback preserves original pointer identity and bytes");
    check(delivered_error==123&&GetLastError()==778,"callback Win32 state does not leak into original or overwrite its result");
    reset();accept=true;
    throw_bridge(parent,start,velocity,7,3,1,456);
    check(native_calls==1&&delivered.origin==std::array<float,3>{101,102,103}&&
          delivered.velocity==std::array<float,3>{0,900,240},"accepted release reaches native spawn");
    check(start[0]==101&&velocity[0]==0&&velocity[1]==900&&velocity[2]==240&&
          delivered_origin==start&&delivered_velocity==velocity,"accepted scratch persists for caller normalization and inherited motion");
    std::memcpy(start,original_start.data(),sizeof(start));std::memcpy(velocity,original_velocity.data(),sizeof(velocity));
    reset();accept=bad_geometry=true;
    throw_bridge(parent,start,velocity,7,3,1,456);
    check(native_calls==1&&delivered.origin==original_start&&delivered.velocity==original_velocity,
          "nonfinite replacement does not partially update either native vector");
    reset();accept=raise_fault=true;
    throw_bridge(parent,start,velocity,7,3,1,456);
    check(native_calls==1&&delivered.origin==original_start&&delivered.velocity==original_velocity,
          "host structured exception retains one unmodified native call");
    reset();accept=true;
    throw_bridge(parent,start,start,7,3,1,456);
    check(native_calls==1&&callback_calls==0&&delivered.origin==original_start&&delivered.velocity==original_start,
          "overlapping scratch pointers never enter callback");
    auto* heap=static_cast<float*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(heap!=nullptr,"test native heap allocation");
    if(heap){std::memcpy(heap,original_start.data(),sizeof(start));reset();accept=true;
        throw_bridge(parent,heap,velocity,7,3,1,456);
        check(native_calls==1&&callback_calls==0&&delivered.origin==original_start,
              "non-stack vectors pass through without callback authority");VirtualFree(heap,0,MEM_RELEASE);}
    GrenadeThrowArguments copied{};
    check(copy_arguments(parent,start,velocity,7,3,1,456,&copied),"bounded native stack vectors can be copied");
    GrenadeThrowReplacement replacement{{1,4,9},{2,8,32}};
    velocity[1]=44;
    check(!replace_vectors(start,velocity,copied,replacement)&&start[0]==1&&velocity[1]==44,
          "changed original payload is not overwritten after callback");
    check(!configure_grenade_throw_hook(nullptr,callback)&&
          !configure_grenade_throw_hook(reinterpret_cast<HMODULE>(0x400000),nullptr),
          "configuration requires pinned image and nonnull callback");
    check(!configure_grenade_throw_hook(reinterpret_cast<HMODULE>(0x400000),callback),
          "failed pinned file verification cannot install an instruction patch");
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
