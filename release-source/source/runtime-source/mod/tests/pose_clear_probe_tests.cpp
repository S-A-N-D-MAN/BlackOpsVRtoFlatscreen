// SPDX-License-Identifier: GPL-3.0-only
#define install_pose_clear_probe install_pose_clear_probe_test_copy
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/pose_clear_probe.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#undef install_pose_clear_probe
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{},native_publications{},native_passes{},command_used{},callback_calls{};
bool callback_df_clear=true,throw_callback{},seh_callback{},nested_callback{};
PoseClearPoint observed{};
struct alignas(16) CpuState {unsigned gp[8]{},flags{},stack_delta{},error{},padding{};unsigned char fx[512]{};};
static_assert(offsetof(CpuState,fx)==48);
CpuState incoming,returned;
alignas(16) unsigned char input_fx[512]{},output_fx[512]{},caller_fx[512]{};
const unsigned xmm_bits[4]{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
const unsigned input_mxcsr=0x3f80,output_mxcsr=0x5f80;
const unsigned short input_cw=0x077f,output_cw=0x0b7f;
void* invoke_target{};
unsigned saved_stack{}, argument_object=0x12345678, argument_local=0x42, received_object{},received_local{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
void initialize_fp() {
    __asm {
        fxsave caller_fx
        fninit
        fld1
        fldpi
        fldl2e
        fldcw input_cw
        ldmxcsr input_mxcsr
        movdqu xmm0,xmm_bits
        movdqu xmm1,xmm_bits
        movdqu xmm2,xmm_bits
        movdqu xmm3,xmm_bits
        movdqu xmm4,xmm_bits
        movdqu xmm5,xmm_bits
        movdqu xmm6,xmm_bits
        movdqu xmm7,xmm_bits
        fxsave input_fx
        fninit
        fldln2
        fldz
        fldcw output_cw
        ldmxcsr output_mxcsr
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        pcmpeqd xmm3,xmm3
        pcmpeqd xmm4,xmm4
        pcmpeqd xmm5,xmm5
        pcmpeqd xmm6,xmm6
        pcmpeqd xmm7,xmm7
        fxsave output_fx
        fxrstor caller_fx
    }
}
__declspec(naked) void fake_native() {
    __asm {
        pushfd
        pushad
        mov esi,esp
        mov edi,offset incoming
        mov ecx,8
        cld
        rep movsd
        mov eax,[esi]
        mov [edi],eax
        mov eax,[esp+40]
        mov received_object,eax
        mov eax,[esp+44]
        mov received_local,eax
        fxsave [incoming+48]
        mov eax,fs:[0x34]
        mov [incoming+40],eax
        popad
        popfd
        cld
        push 2222
        call SetLastError
        fxrstor output_fx
        mov eax,0xa1a1a1a1
        mov ebx,0xb2b2b2b2
        mov ecx,0xc3c3c3c3
        mov edx,0xd4d4d4d4
        mov esi,0xe5e5e5e5
        mov edi,0xf6f6f6f6
        mov ebp,0x87878787
        cmp eax,eax
        std
        ret
    }
}
__declspec(naked) void fake_publish() {
    __asm {
        pushfd
        inc native_publications
        add command_used,4
        popfd
        jmp fake_native
    }
}
__declspec(naked) void fake_pass() {
    __asm {pushfd}
    __asm {inc native_passes}
    __asm {popfd}
    __asm {jmp fake_native}
}
// Deliberately let the fake native change every GP register. This private
// invoker captures that state and restores the C++ caller's complete context.
__declspec(naked) void invoke() {
    __asm {
        pushfd
        pushad
        mov saved_stack,esp
        fxsave caller_fx
        fxrstor input_fx
        mov eax,0x11111111
        mov ebx,0x22222222
        mov ecx,0x33333333
        mov edx,0x44444444
        mov esi,0x55555555
        mov edi,0x66666666
        mov ebp,0x77777777
        cmp eax,eax
        stc
        std
        push argument_local
        push argument_object
        call dword ptr [invoke_target]
        lea esp,[esp+8]
        pushfd
        pushad
        mov esi,esp
        mov edi,offset returned
        mov ecx,8
        cld
        rep movsd
        mov eax,[esi]
        mov [edi],eax
        mov eax,[returned+12]
        add eax,4
        sub eax,saved_stack
        mov [returned+36],eax
        fxsave [returned+48]
        mov eax,fs:[0x34]
        mov [returned+40],eax
        mov esp,saved_stack
        fxrstor caller_fx
        popad
        popfd
        ret
    }
}
// Separate stack-local save area permits observer-triggered nested invocations.
__declspec(naked) void __cdecl nested_invoke(void*) {
    __asm {
        pushfd
        pushad
        sub esp,528
        lea eax,[esp+15]
        and eax,0xfffffff0
        fxsave [eax]
        push argument_local
        push argument_object
        call dword ptr [esp+576]
        lea esp,[esp+8]
        lea eax,[esp+15]
        and eax,0xfffffff0
        fxrstor [eax]
        lea esp,[esp+528]
        popad
        popfd
        ret
    }
}
void disturb() {
    unsigned flags{};
    __asm {pushfd}
    __asm {pop flags}
    callback_df_clear=callback_df_clear&&!(flags&0x400);
    SetLastError(9999);
    __asm {
        fninit
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        pxor xmm2,xmm2
        pxor xmm3,xmm3
        pxor xmm4,xmm4
        pxor xmm5,xmm5
        pxor xmm6,xmm6
        pxor xmm7,xmm7
    }
}
void observe(const PoseClearPoint& point) {
    ++callback_calls;observed=point;disturb();
    if(nested_callback)nested_invoke(reinterpret_cast<void*>(&pose_clear_detour));
    if(throw_callback)throw 77;
    if(seh_callback)RaiseException(0xe043be01,0,0,nullptr);
}
void __cdecl native_exception(){++native_passes;RaiseException(0xe043be02,0,0,nullptr);}
bool native_unwind(void* target) noexcept {
    __try {reinterpret_cast<void(__cdecl*)(void*,int)>(target)(nullptr,0);}
    __except(GetExceptionCode()==0xe043be02?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
bool fp_equal(const unsigned char* a,const unsigned char* b) {
    // Compare defined x87 control/status/tag, MXCSR, all eight 80-bit x87 slots
    // and all eight XMM registers; exclude reserved bytes and instruction PCs.
    if(memcmp(a,b,5)||memcmp(a+24,b+24,4)||memcmp(a+160,b+160,128)) return false;
    for(unsigned n=0;n<8;++n) if(memcmp(a+32+n*16,b+32+n*16,10)) return false;
    return true;
}
void check_state(bool entry=true) {
    const unsigned expected_in[]{0x66666666,0x55555555,0x77777777,0,0x22222222,0x44444444,0x33333333,0x11111111};
    const unsigned expected_out[]{0xf6f6f6f6,0xe5e5e5e5,0x87878787,0,0xb2b2b2b2,0xd4d4d4d4,0xc3c3c3c3,0xa1a1a1a1};
    bool gp_out=true,gp_in=true;
    for(unsigned n=0;n<8;++n) if(n!=3) {gp_out&=returned.gp[n]==expected_out[n];gp_in&=incoming.gp[n]==expected_in[n];}
    check(gp_out&&returned.stack_delta==0,"all native return GP values and caller ESP preserved");
    check((returned.flags&0xcd5)==0x444,"native return arithmetic and direction flags preserved");
    check(returned.error==2222,"native return LastError survives callback");
    check(fp_equal(returned.fx,output_fx),"all native return x87/SSE state preserved");
    if(entry) {
        check(gp_in,"all original entry GP values restored before native");
        check((incoming.flags&0xcd5)==0x445,"original entry arithmetic and direction flags reach native");
        check(incoming.error==1111,"native entry LastError survives callback");
        check(fp_equal(incoming.fx,input_fx),"all original entry x87/SSE state reaches native");
    }
}
void run(void* target,bool check_entry=true) {
    invoke_target=target;SetLastError(1111);invoke();check_state(check_entry);
}
void word(unsigned char* p,std::uintptr_t value){const auto v=static_cast<unsigned>(value);memcpy(p,&v,4);}
void authored_clear_routine(unsigned char* entry) {
    // Seven inert displaced instructions lead to a synthetic cdecl function
    // that returns one marker for null and another for non-null object input.
    std::fill_n(entry,7,static_cast<unsigned char>(0x90));
    auto* p=entry+7;
    *p++=0x8b;*p++=0x44;*p++=0x24;*p++=0x04; // mov eax,[esp+4]
    *p++=0x85;*p++=0xc0;                       // test eax,eax
    *p++=0x74;*p++=0x06;                       // jz null_result
    *p++=0xb8;word(p,0x55667788u);p+=4;*p++=0xc3;
    *p++=0xb8;word(p,0x11223344u);p+=4;*p++=0xc3;
}
}

namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {std::wstring executable_path(){return {};} Validation validate_path(const std::wstring&){return {};} bool validate_loaded_headers(HMODULE,std::string&){return false;}}
void __cdecl safe_original(void*,int){SetLastError(2222);}

bool callback_unwind(void* target) {
    __try {reinterpret_cast<void(__cdecl*)(void*,int)>(target)(nullptr,0);}
    __except(GetExceptionCode()==0xe043be01?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
int main(){
    initialize_fp();
    constexpr std::size_t image_size=0x300000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(image!=nullptr,"owned synthetic image allocated");if(!image)return 1;const auto base=reinterpret_cast<std::uintptr_t>(image);
    check(!clear_install_at(base,observe)&&!clear_guards(1),"wrong and inaccessible native bytes rejected");
    authored_clear_routine(image+clear_rva);
    const unsigned char copyargs[]{0xff,0x74,0x24,0x08,0xff,0x74,0x24,0x08},cleanup[]{0x8d,0x64,0x24,0x08,0xc3};
    for(const auto caller:clear_callers){memcpy(image+caller-8,copyargs,8);image[caller]=0xe8;word(image+caller+1,clear_rva-caller-5);memcpy(image+caller+5,cleanup,5);}
    for(const auto offset:{clear_rva,clear_rva+6,clear_rva+7,clear_callers[0],clear_callers[1],clear_callers[2],clear_callers[3]}){
        image[offset]^=1;check(!clear_install_at(base,observe),"every entry/resume/direct-caller guard rejects without installation");image[offset]^=1;}
    DWORD old{};VirtualProtect(image,image_size,PAGE_EXECUTE_READ,&old);
    check(clear_install_at(base,observe)&&clear_install_at(base,observe)&&!clear_install_at(base+4096,observe),"exact routine installs once for one image and callback");
    bool displaced=true;for(unsigned i=0;i<7;++i)displaced=displaced&&static_cast<unsigned char*>(native_clear)[i]==0x90;
    check(displaced,"trampoline contains the complete authored displaced span");
    MEMORY_BASIC_INFORMATION entry_memory{},trampoline_memory{};VirtualQuery(image+clear_rva,&entry_memory,sizeof(entry_memory));VirtualQuery(native_clear,&trampoline_memory,sizeof(trampoline_memory));
    check(entry_memory.Protect==PAGE_EXECUTE_READ&&trampoline_memory.Protect==PAGE_EXECUTE_READ,"native entry/trampoline protections restored");
    for(unsigned value=0;value<2;++value){const auto result=reinterpret_cast<unsigned(__cdecl*)(void*,int)>(image+clear_rva)(reinterpret_cast<void*>(value),17);
        check(result==(value?0x55667788U:0x11223344U),"copied TEST flags reach exact native JE for null and nonnull objects");}
    native_clear=reinterpret_cast<void*>(&fake_pass);callback_calls=0;native_passes=0;
    std::uint64_t serial{};
    for(unsigned iteration=0;iteration<16;++iteration)for(unsigned route=0;route<clear_callers.size();++route){
        argument_object=0x12345678+iteration;argument_local=iteration;run(image+clear_callers[route]-8);
        check(received_object==argument_object&&received_local==argument_local,"both original cdecl argument words reach native exactly");
        check(observed.object==argument_object&&observed.local_client==static_cast<int>(argument_local)&&observed.caller_return==base+clear_callers[route]+5,
              "after-only observation copies actual arguments and return address");
        const auto expected=route==0?PoseClearCaller::camera:route==1?PoseClearCaller::normal_weapon:PoseClearCaller::other;
        check(observed.caller==expected&&observed.serial>serial&&observed.thread==GetCurrentThreadId(),"only two supported callers classified, all completions get serial");serial=observed.serial;
    }
    check(native_passes==64&&callback_calls==64&&callback_df_clear,"native once and callback once with complete output preservation");
    native_clear=reinterpret_cast<void*>(&native_exception);const auto calls=callback_calls;const auto pre_serial=pose_clear_serial();
    check(native_unwind(image+clear_rva)&&callback_calls==calls&&pose_clear_serial()==pre_serial,"native fault forwards before TLS/serial/callback work");
    native_clear=reinterpret_cast<void*>(&safe_original);throw_callback=true;bool cpp{};
    try{reinterpret_cast<void(__cdecl*)(void*,int)>(image+clear_rva)(nullptr,0);}catch(int n){cpp=n==77;}
    throw_callback=false;check(cpp&&!clear_observing&&GetLastError()==2222,"native-lower C++ through callback propagates and clears only observer state");
    seh_callback=true;check(callback_unwind(image+clear_rva)&&!clear_observing&&GetLastError()==2222,"native-lower SEH through callback propagates with exact LastError");seh_callback=false;
    // Use an ABI-preserving simple original for exception tests' normal C++ callers.
    native_clear=reinterpret_cast<void*>(&fake_pass);nested_callback=true;const auto nested_calls=callback_calls;const auto nested_serial=pose_clear_serial();run(image+clear_rva,false);nested_callback=false;
    check(callback_calls==nested_calls+1&&pose_clear_serial()==nested_serial+2&&!clear_observing,"nested native completes but cannot recursively calculate; serial invalidates outer sample");
    clear_callback=nullptr;const auto empty_calls=callback_calls;run(image+clear_rva);check(callback_calls==empty_calls,"empty default callback still executes original once");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("pose_clear_abi_checks=%u failures=%u game_launches=0\n",checks,failures);return failures?1:0;
}
