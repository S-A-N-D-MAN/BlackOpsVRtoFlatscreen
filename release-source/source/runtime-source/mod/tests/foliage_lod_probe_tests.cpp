// SPDX-License-Identifier: GPL-3.0-only
// Actual production bridge and observation policy; no real process/game calls.
#define BOVR_TEST_FOLIAGE_LOD 1
#include "../src/t5/foliage_lod_probe.cpp"
#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>
namespace {std::vector<std::string> logs;}
namespace bovr {void trace(const char* event,const char* format,...) noexcept {
    char text[2048]{};va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);
    try{logs.emplace_back(std::string(event)+" "+text);}catch(...){}
}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&){return {true,"fixture","fixture"};}
bool validate_loaded_headers(HMODULE,std::string&){return true;}
std::wstring executable_path(){return L"private fixture";}
}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{},native_calls{},observer_calls{};
void check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
template<class T>void store(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
unsigned bits(float f){unsigned v{};std::memcpy(&v,&f,4);return v;}
unsigned native_result=3,arguments[4]{0x11223344,0x4455aabb,0x778899aa,1},passed[4]{},callback_passed[4]{};
unsigned regs[7]{},flags{},saved_index{},saved_instance{};std::uintptr_t before_sp{},after_sp{};
__declspec(align(16)) unsigned char native_fx[512]{},returned_fx[512]{};
void* bridge=reinterpret_cast<void*>(&foliage_lod_bridge);
__declspec(naked) void fake_native(){__asm {
    inc dword ptr [native_calls]
    mov eax,[esp+4]
    mov passed,eax
    mov eax,[esp+8]
    mov passed+4,eax
    mov eax,[esp+12]
    mov passed+8,eax
    mov eax,[esp+16]
    mov passed+12,eax
    pcmpeqd xmm0,xmm0
    pcmpeqd xmm1,xmm1
    pcmpeqd xmm2,xmm2
    pcmpeqd xmm3,xmm3
    pcmpeqd xmm4,xmm4
    pcmpeqd xmm5,xmm5
    pcmpeqd xmm6,xmm6
    pcmpeqd xmm7,xmm7
    fld1
    mov eax,native_result
    mov ecx,0x22334455
    mov edx,0x33445566
    stc
    fxsave native_fx
    ret
}}
void __cdecl disturb(std::uint32_t* saved){
    ++observer_calls;std::memcpy(callback_passed,saved+10,16);saved_index=saved[2];saved_instance=saved[0];
    SetLastError(0x99887766);
    __asm {xorps xmm0,xmm0
        xorps xmm1,xmm1
        xorps xmm2,xmm2
        xorps xmm3,xmm3
        xorps xmm4,xmm4
        xorps xmm5,xmm5
        xorps xmm6,xmm6
        xorps xmm7,xmm7
        fninit
        fldpi
        xor eax,eax
        xor ecx,ecx
        xor edx,edx
        clc}
}
__declspec(naked) void invoke_bridge(){__asm {
    pushfd
    pushad
    mov before_sp,esp
    mov ebx,0x44556677
    mov esi,0x55667788
    mov edi,0x66778899
    mov ebp,0x778899aa
    push arguments+12
    push arguments+8
    push arguments+4
    push arguments
    call dword ptr [bridge]
    mov regs,eax
    mov regs+4,ecx
    mov regs+8,edx
    mov regs+12,ebx
    mov regs+16,esi
    mov regs+20,edi
    mov regs+24,ebp
    pushfd
    pop flags
    fxsave returned_fx
    lea esp,[esp+16]
    mov after_sp,esp
    fninit
    popad
    popfd
    ret
}}
bool owned=true;bool is_owned(void*) noexcept{return owned;}
std::uintptr_t base{},buffer{},refdef{},view{},world{},instances{},model{};
void fixture(unsigned eye,bool built){
    buffer=base+first;refdef=base+0x20000;view=base+views_first+eye*0x38e0;
    world=base+0x10000;instances=base+0x12000;model=base+0x14000;
    store(base+0x373708c,static_cast<unsigned>(buffer));store(buffer+0x15d7a0,17u);
    store(buffer+0x16cc3c,built?eye+1:eye);store(buffer+0x16cbe0,built?eye+1:eye);
    store(buffer+0x16cc38,built?eye:0u);store(buffer+0x16cc40,static_cast<unsigned>(base+views_first));
    store(refdef+0x183a4,0u);store(view+0x188,0u);store(view+0x3510,static_cast<unsigned>(buffer));
    const std::array<float,3> origin{10.f,20.f,30.f};const std::array<int,4> viewport{int(eye)*2560,0,2560,2880};
    store(refdef+0x20,origin);store(refdef,viewport);store(view+0x100,origin);store(view+0x140,viewport);
    store(base+0x356ecf8,origin);store(base+0x356ecf8+0xc,2.94519f);store(base+0x356ecf8+0x10,0.f);store(base+0x356ecf8+0x14,2.94519f);
    store(base+0x356a24c,static_cast<unsigned>(world));store(world+0x344,10u);store(world+0x3a4,static_cast<unsigned>(instances));
    store(instances+2*0x4c+0x38,static_cast<unsigned>(model));store(instances+2*0x4c+0x34,2.f);
    store(model+0xd8,std::int16_t{4});for(unsigned i=0;i<4;++i)store(model+0x28+i*0x20,float((i+1)*500));
    store(model,static_cast<unsigned>(model+0x200));const char name[]="fixture_desert_bush";std::memcpy(reinterpret_cast<void*>(model+0x200),name,sizeof name);
}
std::array<unsigned,14> call_args(int lod=2){std::array<unsigned,14> s{};s[0]=static_cast<unsigned>(instances+2*0x4c+0xc);s[2]=2;s[7]=static_cast<unsigned>(lod);
    s[9]=static_cast<unsigned>(base+call_rva+5);s[10]=static_cast<unsigned>(model);s[11]=bits(1700.f);s[12]=bits(577.f);s[13]=0;return s;}
bool begin(FoliageLodProbeScope& scope,unsigned eye=0){fixture(eye,false);return scope.begin(base,buffer,17,refdef,123,eye,{nullptr,&is_owned});}
bool contains(const char* text){for(const auto& line:logs)if(line.find(text)!=std::string::npos)return true;return false;}
void body_fault(void*){throw std::runtime_error("native fixture failure");}
void body_seh(void*){RaiseException(0x12345678,0,0,nullptr);}
bool catch_seh(FoliageLodProbeScope* scope){__try{invoke_foliage_lod_native(scope,&body_seh,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
}
int main(){
    std::array<unsigned,64> values{};unsigned count{};
    check(parse_indices(L"2761,2814",&values,&count)&&count==2&&values[0]==2761&&values[1]==2814,"explicit bounded selected IDs");
    for(const auto* invalid:{L"",L",1",L"1,",L"1,1",L"1, 2",L"65536",L"-1",L"1x",L"4294967297"})check(!parse_indices(invalid,&values,&count),"malformed or duplicate ID rejected");
    check(parse_indices(L"0,65535",&values,&count),"both allowed ID limits");
    native_target=reinterpret_cast<std::uintptr_t>(&fake_native);test_observer=&disturb;
    for(const auto result:{0u,3u,0xffffffffu}){native_result=result;SetLastError(0x12345678);invoke_bridge();const auto error=GetLastError();
        check(native_calls==observer_calls&&native_calls>0,"one native call per observation");
        check(before_sp==after_sp,"original caller cleanup stack balanced");check(!std::memcmp(arguments,passed,16)&&!std::memcmp(arguments,callback_passed,16),"four original arguments forwarded unchanged");
        const unsigned expected[]{result,0x22334455,0x33445566,0x44556677,0x55667788,0x66778899,0x778899aa};
        check(!std::memcmp(regs,expected,sizeof expected),"actual return and all registers preserved");check(flags&1,"native flags preserved");
        check(saved_index==0x778899aa&&saved_instance==0x66778899,"EBP and EDI index fields captured exactly");
        check(error==0x12345678,"native LastError preserved");check(!std::memcmp(native_fx,returned_fx,288),"native x87 SSE MXCSR preserved");
    }
    test_observer=nullptr;
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4300000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!base){printf("fixture allocation failed\n");return 2;}
    hook_base=base;owned_call=original_call;store(base+call_rva,owned_call);hook_ready=true;selected[0]=2;selected_count=1;
    for(unsigned eye=0;eye<2;++eye){logs.clear();last_records={};FoliageLodProbeScope scope;check(begin(scope,eye),"fresh per-eye frontend scope admitted");fixture(eye,true);auto args=call_args();observe_foliage_lod(args.data());scope.finish();
        check(contains("t5_foliage_lod_observed")&&contains("result=2 adjusted=1700 raw=577"),"owned exact native result logged");check(!active_scope,"finished scope releases TLS");}
    // Each independently mutated authority fact must suppress observation.
    for(unsigned fault=0;fault<12;++fault){logs.clear();last_records={};owned=true;FoliageLodProbeScope scope;check(begin(scope),"negative fixture begins");fixture(0,true);auto args=call_args();
        switch(fault){case 0:store(buffer+0x15d7a0,18u);break;case 1:store(buffer+0x16cc38,1u);break;
        case 2:store(buffer+0x16cc3c,2u);break;case 3:store(refdef+0x20,99.f);break;case 4:store(view+0x140,1);break;
        case 5:args[0]+=0x4c;break;case 6:args[10]+=4;break;case 7:args[9]+=1;break;case 8:args[13]=2;break;
        case 9:owned=false;break;case 10:args[11]=0x7fc00000;break;case 11:store(base+0x356ecf8,11.f);break;}
        observe_foliage_lod(args.data());scope.finish();check(!contains("t5_foliage_lod_observed"),"mutated authority or arguments rejected");}
    owned=true;
    {logs.clear();FoliageLodProbeScope scope;check(begin(scope),"unselected starts");fixture(0,true);auto args=call_args();args[2]=3;observe_foliage_lod(args.data());scope.finish();check(!contains("t5_foliage_lod_observed"),"unselected IDs produce no detail logs");}
    {logs.clear();FoliageLodProbeScope scope;check(begin(scope),"foreign thread starts");fixture(0,true);auto args=call_args();std::thread other([&]{observe_foliage_lod(args.data());});other.join();scope.finish();check(!contains("t5_foliage_lod_observed"),"foreign thread has no scope");}
    {logs.clear();FoliageLodProbeScope outer,nested;check(begin(outer),"outer starts");check(!nested.begin(base,buffer,17,refdef,123,0,{nullptr,&is_owned}),"nested scope rejected");fixture(0,true);auto args=call_args();observe_foliage_lod(args.data());outer.finish();check(!contains("t5_foliage_lod_observed")&&!active_scope,"nested ambiguity blocks and retires outer");}
    {FoliageLodProbeScope scope;check(begin(scope),"C++ fault starts");bool caught=false;try{invoke_foliage_lod_native(&scope,&body_fault,nullptr);}catch(const std::runtime_error&){caught=true;}check(caught&&!active_scope,"native C++ exception propagates and scope retires");}
    {FoliageLodProbeScope scope;check(begin(scope),"SEH fault starts");check(catch_seh(&scope)&&!active_scope,"native SEH propagates and scope retires");}
    hook_ready=false;SetEnvironmentVariableW(L"BOVR_FOLIAGE_LOD_PROBE",nullptr);
    check(configure_foliage_lod_probe(nullptr)&&!hook_ready,"disabled configuration never installs instruction patch");
    std::array<unsigned char,5> unchanged{};get(base+call_rva,&unchanged);check(unchanged==original_call,"disabled setup leaves bytes unchanged");
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
