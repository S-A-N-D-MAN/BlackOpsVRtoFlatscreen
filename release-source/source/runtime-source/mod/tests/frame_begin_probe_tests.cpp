// SPDX-License-Identifier: GPL-3.0-only
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_frame_begin_probe install_frame_begin_probe_test_copy
#define set_frame_begin_callback set_frame_begin_callback_test_copy
#define set_frame_begin_pre_callback set_frame_begin_pre_callback_test_copy
#include "../src/t5/frame_begin_probe.cpp"
#undef install_frame_begin_probe
#undef set_frame_begin_callback
#undef set_frame_begin_pre_callback
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{},native_publications{},native_passes{},command_used{},callback_calls{};
bool callback_df_clear=true,throw_callback{},seh_callback{},nested_callback{};
FrameBeginPoint observed{},pre_observed{};
unsigned pre_calls{};
bool pre_cpp{},pre_seh{},pre_nested{};
struct alignas(16) CpuState {unsigned gp[8]{},flags{},stack_delta{},error{},padding{};unsigned char fx[512]{};};
static_assert(offsetof(CpuState,fx)==48);
CpuState incoming,returned;
alignas(16) unsigned char input_fx[512]{},output_fx[512]{},caller_fx[512]{};
const unsigned xmm_bits[4]{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
const unsigned input_mxcsr=0x3f80,output_mxcsr=0x5f80;
const unsigned short input_cw=0x077f,output_cw=0x0b7f;
void* invoke_target{};
unsigned saved_stack{};
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
        call dword ptr [invoke_target]
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
        call dword ptr [esp+568]
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
void observe(const FrameBeginPoint& point) {
    ++callback_calls;observed=point;disturb();
    if(nested_callback)nested_invoke(reinterpret_cast<void*>(&frame_begin_detour));
    if(throw_callback)throw std::runtime_error("synthetic observer exception");
    if(seh_callback)RaiseException(0xe043be01,0,0,nullptr);
}
void observe_pre(const FrameBeginPoint& point) {
    ++pre_calls;pre_observed=point;disturb();
    if(pre_nested)nested_invoke(reinterpret_cast<void*>(&frame_begin_detour));
    if(pre_cpp)throw std::runtime_error("synthetic pre observer exception");
    if(pre_seh)RaiseException(0xe043be01,0,0,nullptr);
}
void __cdecl native_exception(){++native_passes;RaiseException(0xe043be02,0,0,nullptr);}
bool native_unwind(void* target) noexcept {
    __try {reinterpret_cast<void(__cdecl*)()>(target)();}
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
void write_authored_entry(unsigned char* entry) {
    // An authored two-instruction prologue exercises the trampoline without
    // carrying the retail prologue. It compares a private byte, saves ECX and
    // branches to a private fixture body using a different displacement.
    entry[0]=0x80;entry[1]=0x3d;word(entry+2,0x0396a641);entry[6]=0x5a;entry[7]=0x51;
    entry[8]=0x0f;entry[9]=0x84;word(entry+10,0x82);
}
}
int main() {
    initialize_fp();
    constexpr std::size_t image_size=0x3b0000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* flag_page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x3960000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(image&&flag_page==reinterpret_cast<void*>(0x3960000),"private image and exact synthetic CMP flag storage allocated");
    if(!image||flag_page!=reinterpret_cast<void*>(0x3960000))return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    check(!set_frame_begin_callback_test_copy(observe)&&set_frame_begin_callback_test_copy(nullptr),"callback begins empty and needs installed hook");
    check(!install_at(base)&&!guards_match(1),"wrong/inaccessible prologue rejected");
    write_authored_entry(image+entry_rva);
    for(const auto caller:callers) {image[caller]=0xe8;word(image+caller+1,entry_rva-caller-5);image[caller+5]=0xc3;}
    const unsigned char not_equal[]{0xb8,0x88,0x77,0x66,0x55,0x59,0xc3};
    const unsigned char equal[]{0xb8,0x44,0x33,0x22,0x11,0x59,0xc3};
    memcpy(image+entry_rva+14,not_equal,sizeof(not_equal));memcpy(image+entry_rva+0x90,equal,sizeof(equal));
    for(const auto offset:{entry_rva,entry_rva+7,entry_rva+8,callers[0],callers[1],callers[2]}) {
        image[offset]^=1;check(!install_at(base),"each native instruction/caller guard rejects without patching");image[offset]^=1;
    }
    DWORD old{};VirtualProtect(image,image_size,PAGE_EXECUTE_READ,&old);
    check(install_at(base)&&install_at(base)&&!install_at(base+4096),"verified function installs exactly once for one image");
    const auto trampoline=native_begin;
    const auto* trampoline_bytes=static_cast<const unsigned char*>(trampoline);unsigned compared_address{};
    memcpy(&compared_address,trampoline_bytes+2,4);
    check(trampoline_bytes[0]==0x80&&trampoline_bytes[1]==0x3d&&compared_address==0x0396a641&&
          trampoline_bytes[6]==0x5a&&trampoline_bytes[7]==0x51,
          "trampoline copies both complete authored instructions before its jump");
    MEMORY_BASIC_INFORMATION entry_memory{},trampoline_memory{};
    VirtualQuery(image+entry_rva,&entry_memory,sizeof(entry_memory));VirtualQuery(trampoline,&trampoline_memory,sizeof(trampoline_memory));
    check(entry_memory.Protect==PAGE_EXECUTE_READ&&trampoline_memory.Protect==PAGE_EXECUTE_READ,"entry and trampoline executable pages are readonly after patch");
    auto* flag=reinterpret_cast<unsigned char*>(0x396a641);
    for(unsigned value=0;value<2;++value) {
        *flag=static_cast<unsigned char>(0x5a+value);const auto result=reinterpret_cast<unsigned(__cdecl*)()>(image+entry_rva)();
        check(result==(value?0x55667788u:0x11223344u),"copied CMP flags survive jump and select the authored conditional branch");
    }
    // Override only the private test-copy trampoline pointer for adversarial
    // return-state/native-unwind tests; production retains the real trampoline.
    native_begin=reinterpret_cast<void*>(&fake_pass);
    run(image+entry_rva);check(native_passes==1&&callback_calls==0,"default empty observer forwards native exactly once");
    check(set_frame_begin_callback_test_copy(observe),"after-only observer binds");
    std::uint64_t serial{};
    for(unsigned iteration=0;iteration<64;++iteration)for(unsigned route=0;route<callers.size();++route) {
        run(image+callers[route]);
        check(observed.caller_return==base+callers[route]+5&&observed.known_direct_caller&&observed.ordinary_screen_frame==(route==2),"all three native callers return transparently but only ordinary route marks production");
        check(observed.call_serial>serial&&observed.image_base==base&&observed.native_thread==GetCurrentThreadId(),"after-return identity is copied once on actual native thread");serial=observed.call_serial;
    }
    check(native_passes==193&&callback_calls==192&&callback_df_clear,"one native call and one observer for each ordinary invocation");
    run(image+entry_rva);check(!observed.known_direct_caller&&!observed.ordinary_screen_frame,"unknown caller still forwards and cannot mint ordinary production identity");
    throw_callback=true;run(image+entry_rva);throw_callback=false;
    seh_callback=true;run(image+entry_rva);seh_callback=false;
    check(!observing,"both callback exception kinds retire callback-only scope");
    // Nested native execution intentionally replaces the fixture's one global
    // incoming snapshot. Verify the retained OUTER return state here; ordinary
    // entry preservation was independently checked on all three routes above.
    const auto count=callback_calls,native_count=native_passes;nested_callback=true;run(image+entry_rva,false);nested_callback=false;
    check(native_passes==native_count+2&&callback_calls==count+1&&!observing,"callback-triggered nested native call runs once without recursive observers");
    native_begin=reinterpret_cast<void*>(&native_exception);const auto before_exception=callback_calls;
    check(native_unwind(image+entry_rva)&&callback_calls==before_exception&&!observing,"original native SEH propagates unchanged without observer or stranded TLS");
    native_begin=reinterpret_cast<void*>(&fake_pass);run(image+callers[2]);check(observed.ordinary_screen_frame&&!observing,"normal observer remains usable after caller handles native unwind");
    const auto parent_thread=GetCurrentThreadId();FrameBeginPoint foreign{};
    std::thread worker([&]{after_native(base+callers[2]+5);foreign=observed;});worker.join();
    check(foreign.native_thread!=parent_thread&&foreign.ordinary_screen_frame,"native caller metadata uses actual foreign thread and independent callback scope");
    check(set_frame_begin_pre_callback_test_copy(observe_pre),"optional pre callback binds independently");
    for(unsigned route=0;route<callers.size();++route){
        const auto prior_native=native_passes,prior_pre=pre_calls,prior_after=callback_calls;
        run(image+callers[route]);
        check(native_passes==prior_native+1&&pre_calls==prior_pre+1&&callback_calls==prior_after+1,
            "pre callback never skips or duplicates original and existing after callback");
        check(pre_observed.caller_return==base+callers[route]+5&&pre_observed.ordinary_screen_frame==(route==2),
            "pre caller classification is exact");
    }
    pre_cpp=true;run(image+callers[2]);pre_cpp=false;
    pre_seh=true;run(image+callers[2]);pre_seh=false;
    check(!observing,"pre C++ and SEH failures retire callback-only scope");
    const auto pre_nested_native=native_passes,pre_nested_pre=pre_calls,pre_nested_after=callback_calls;
    pre_nested=true;run(image+callers[2]);pre_nested=false;
    check(native_passes==pre_nested_native+2&&pre_calls==pre_nested_pre+1&&callback_calls==pre_nested_after+1&&!observing,
        "pre reentry executes nested native once but suppresses nested observers");
    native_begin=reinterpret_cast<void*>(&native_exception);
    const auto native_exception_pre=pre_calls,native_exception_after=callback_calls,native_exception_count=native_passes;
    check(native_unwind(image+callers[2])&&pre_calls==native_exception_pre+1&&
        callback_calls==native_exception_after&&native_passes==native_exception_count+1&&!observing,
        "pre callback does not consume native SEH or retain scope over unwind");
    native_begin=reinterpret_cast<void*>(&fake_pass);
    check(set_frame_begin_pre_callback_test_copy(nullptr),"pre callback disables independently");
    const auto disabled_pre=pre_calls;run(image+callers[2]);
    check(pre_calls==disabled_pre,"disabled pre path preserves original entry and native return state");
    check(set_frame_begin_callback_test_copy(nullptr),"callback unbind succeeds");
    SetEnvironmentVariableW(L"BOVR_MENU_FRAME_EPOCH_PROBE",nullptr);
    check(!install_frame_begin_probe_test_copy(GetModuleHandleW(nullptr)),"public installer remains explicit opt-in even after private fixture installation");
    VirtualFree(trampoline,0,MEM_RELEASE);native_begin=nullptr;VirtualFree(image,0,MEM_RELEASE);VirtualFree(flag_page,0,MEM_RELEASE);
    std::printf("frame_begin_checks=%u failures=%u native_game_calls=0 graphics_calls=0\n",checks,failures);return failures?1:0;
}
