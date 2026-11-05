// SPDX-License-Identifier: GPL-3.0-only
// Exact-call patch guards and adversarial x86 ABI tests in private memory only.
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_hud_pass_probe install_hud_pass_probe_test_copy
#define set_hud_pass_callbacks set_hud_pass_callbacks_test_copy
#include "../src/t5/hud_pass_probe.cpp"
#undef install_hud_pass_probe
#undef set_hud_pass_callbacks
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
using namespace bovr::t5;
unsigned checks{},failures{},published_calls{},before_calls{},after_calls{},alternate_calls{};
unsigned native_publications{},native_passes{},command_used{};
bool throw_before{},throw_after{},throw_published{},seh_before{},nested_before{},nested_after{},replace_callbacks{};
bool copied_points=true,published_before_reset=true,callback_df_clear=true;
HudPassPoint before_point{},after_point{},published_point{};
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
void alternate(const HudPassPoint&){++alternate_calls;disturb();}
void before(const HudPassPoint& point) {
    ++before_calls;before_point=point;disturb();
    if(replace_callbacks) set_hud_pass_callbacks_test_copy(nullptr,alternate,alternate);
    if(nested_before) nested_invoke(reinterpret_cast<void*>(&pass_detour));
    if(throw_before) throw std::runtime_error("synthetic before failure");
    if(seh_before) RaiseException(0xe042b001,0,0,nullptr);
}
void after(const HudPassPoint& point) {
    ++after_calls;after_point=point;
    copied_points=copied_points&&point.image_base==before_point.image_base&&
        point.native_thread==before_point.native_thread&&point.call_serial==before_point.call_serial;
    disturb();
    if(nested_after) nested_invoke(reinterpret_cast<void*>(&pass_detour));
    if(throw_after) throw std::runtime_error("synthetic after failure");
}
void published(const HudPassPoint& point) {
    ++published_calls;published_point=point;published_before_reset=published_before_reset&&command_used==16;
    disturb();if(throw_published) throw std::runtime_error("synthetic publication failure");
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
void authored_call(unsigned char* site,unsigned displacement) {site[0]=0xe8;word(site+1,displacement);}
}
int main() {
    initialize_fp();
    constexpr std::size_t image_size=0x3b0000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image) return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    check(!set_hud_pass_callbacks_test_copy(published,before,after),"nonempty callbacks require both installed native seams");
    check(set_hud_pass_callbacks_test_copy(nullptr,nullptr,nullptr),"empty callbacks are the default");
    check(!install_at(base)&&!guards_match(1),"wrong/inaccessible native bytes fail before patching");
    // The non-call test spans intentionally remain zero-filled. Distinct
    // authored CALL displacements avoid retaining any retail instruction.
    authored_call(image+publish_call_rva,0x7b);authored_call(image+pass_call_rva,0x37);
    authored_call(image+ordinary_end_call_rva,0x55);
    image[pass_call_rva+5]=0xeb;image[pass_call_rva+6]=0x08;
    unsigned char publish_operand[4]{},pass_operand[4]{};
    memcpy(publish_operand,image+publish_call_rva+1,4);memcpy(pass_operand,image+pass_call_rva+1,4);
    for(auto rva:{publish_call_rva,publish_native_rva,publish_call_rva+5,pass_call_rva,pass_native_rva,pass_call_rva+5,enclosing_prefix_rva,ordinary_end_call_rva}) {
        image[rva]^=1;
        check(!install_at(base)&&!memcmp(image+publish_call_rva+1,publish_operand,4)&&
              !memcmp(image+pass_call_rva+1,pass_operand,4),"each independent guard rejects without a partial patch");
        image[rva]^=1;
    }
    DWORD old{};VirtualProtect(image,image_size,PAGE_EXECUTE_READ,&old);
    check(install_at(base)&&install_at(base)&&!install_at(base+4096),"both verified CALL sites install once for one image");
    check(native_publish==image+publish_native_rva&&native_pass==image+pass_native_rva,"exact native targets retained");
    MEMORY_BASIC_INFORMATION first{},second{};VirtualQuery(image+publish_call_rva,&first,sizeof(first));VirtualQuery(image+pass_call_rva,&second,sizeof(second));
    check(first.Protect==PAGE_EXECUTE_READ&&second.Protect==PAGE_EXECUTE_READ,"both patch pages regain original executable protection");
    // Private native stand-ins replace only these test-copy target variables.
    // Retail absolute-global reads are never executed by this executable.
    native_publish=reinterpret_cast<void*>(&fake_publish);native_pass=reinterpret_cast<void*>(&fake_pass);
    VirtualProtect(image,image_size,PAGE_EXECUTE_READWRITE,&old);
    auto* pub=image+publish_call_rva;
    const unsigned char reset[]{0xc7,0x05,0,0,0,0,0,0,0,0,0xc3};
    memcpy(pub+5,reset,sizeof(reset));word(pub+7,reinterpret_cast<std::uintptr_t>(&command_used));
    auto* view=image+pass_call_rva;view[15]=0xc3; // Authored EB08 continuation.
    FlushInstructionCache(GetCurrentProcess(),image,image_size);
    command_used=12;run(pub);check(command_used==0&&native_publications==1&&published_calls==0,"default empty publication executes native once then caller reset");
    run(view);check(native_passes==1&&before_calls==0&&after_calls==0,"default empty outer pass executes native once");
    check(set_hud_pass_callbacks_test_copy(published,before,after),"observer tuple binds after both patches");
    std::uint64_t serial{};
    for(unsigned n=0;n<64;++n) {
        command_used=12;run(pub);
        check(command_used==0&&published_point.call_serial>serial,"publication callback precedes caller reset with unique serial");serial=published_point.call_serial;
        run(view);check(before_point.call_serial>serial&&after_point.call_serial==before_point.call_serial,"outer callback pair shares next unique serial");serial=after_point.call_serial;
    }
    check(native_publications==65&&native_passes==65&&published_calls==64&&before_calls==64&&after_calls==64,"one native call and one outer callback pair each");
    check(copied_points&&published_before_reset&&callback_df_clear&&after_point.image_base==base&&after_point.native_thread==GetCurrentThreadId(),"copied native identity and callback ordering match");
    throw_before=true;run(view);throw_before=false;
    throw_after=true;run(view);throw_after=false;
    seh_before=true;run(view);seh_before=false;
    throw_published=true;command_used=12;run(pub);throw_published=false;
    check(publication.depth==0&&pass.depth==0,"C++ and structured callback exceptions leave no stuck depth");
    auto saved_before=before_calls,saved_after=after_calls,saved_native=native_passes;
    nested_before=nested_after=true;run(view,false);nested_before=nested_after=false;
    check(before_calls==saved_before+1&&after_calls==saved_after+1&&native_passes==saved_native+3&&pass.depth==0,"nested before/after native calls run once without observer recursion or lost outer pairing");
    saved_after=after_calls;replace_callbacks=true;run(view);replace_callbacks=false;
    check(after_calls==saved_after+1&&alternate_calls==0,"after uses the tuple captured before a registration change");
    run(view);check(alternate_calls==2,"replacement tuple applies only to a later native call");
    set_hud_pass_callbacks_test_copy(nullptr,nullptr,nullptr);
    DWORD other_thread{};std::uint64_t foreign_serial{};
    std::thread worker([&]{enter_pass();other_thread=pass.point.native_thread;foreign_serial=pass.point.call_serial;leave_pass();});worker.join();
    check(other_thread&&other_thread!=GetCurrentThreadId()&&foreign_serial>serial&&pass.depth==0,"native-thread identity and depth remain thread local");
    SetEnvironmentVariableW(L"BOVR_HUD_COMPOSITION_PROBE",L"0");
    check(!install_hud_pass_probe_test_copy(GetModuleHandleW(nullptr)),"explicit opt-in is required");
    SetEnvironmentVariableW(L"BOVR_HUD_COMPOSITION_PROBE",L"1");
    check(!install_hud_pass_probe_test_copy(GetModuleHandleW(nullptr)),"test executable cannot pass pinned retail identity");
    SetEnvironmentVariableW(L"BOVR_HUD_COMPOSITION_PROBE",nullptr);
    // Exact enclosing layout: normal caller -> EndFrame(push EBX) -> inner
    // publication CALL. No invented helper frame between the saved native EBX
    // and the original enclosing return address.
    set_hud_pass_callbacks_test_copy(published,nullptr,nullptr);
    auto* enclosing=image+enclosing_prefix_rva;enclosing[0]=0x53;enclosing[1]=0xe9;
    word(enclosing+2,publish_call_rva-(enclosing_prefix_rva+6));
    pub[15]=0x5b;pub[16]=0xc3; // Native saved EBX pop then enclosing return.
    authored_call(image+ordinary_end_call_rva,static_cast<unsigned>(enclosing_prefix_rva-ordinary_end_call_rva-5));
    image[ordinary_end_call_rva+5]=0xc3;
    auto* alternate=image+0x100;alternate[0]=0xe8;word(alternate+1,enclosing_prefix_rva-0x105);alternate[5]=0xc3;
    FlushInstructionCache(GetCurrentProcess(),image,image_size);
    for(auto* caller:{image+ordinary_end_call_rva,alternate}) {
        command_used=12;invoke_target=caller;SetLastError(1111);invoke();
        check(published_point.enclosing_return==reinterpret_cast<std::uintptr_t>(caller+5),"original enclosing return copied from exact saved-EBX native stack");
        check(returned.stack_delta==0&&returned.gp[4]==0x22222222&&returned.gp[7]==0xa1a1a1a1&&returned.error==2222&&fp_equal(returned.fx,output_fx),"enclosing native saved-register and inner return state preserved");
    }
    enter_publication(1);leave_publication();
    check(published_point.enclosing_return==0&&publication.depth==0,"unreadable original stack fails closed and retires observer scope");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("hud_pass_probe_checks=%u failures=%u retail_calls=0 runtime_calls=0\n",checks,failures);
    return failures?1:0;
}
