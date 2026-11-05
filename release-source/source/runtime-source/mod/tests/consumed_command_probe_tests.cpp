// SPDX-License-Identifier: GPL-3.0-only
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_consumed_command_probe install_consumed_command_probe_test_copy
#define set_consumed_command_callbacks set_consumed_command_callbacks_test_copy
#define read_consumed_command_scope read_consumed_command_scope_test_copy
#define prepare_consumed_command_probe prepare_consumed_command_probe_test_copy
#define commit_consumed_command_probe commit_consumed_command_probe_test_copy
#include "../src/t5/consumed_command_probe.cpp"
#undef install_consumed_command_probe
#undef set_consumed_command_callbacks
#undef read_consumed_command_scope
#undef prepare_consumed_command_probe
#undef commit_consumed_command_probe
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
using namespace bovr::t5;
unsigned checks{},failures{},native_calls{},before_calls{},after_calls{};
bool callbacks_hidden=true,native_visible=true,replace_rejected=true;
bool throw_before{},throw_after{},seh_before{},nested_before{},nested_after{},nested_native{},raise_native{};
NativeConsumedCommand before_value{},after_value{},native_value{};
DWORD native_exception=0xe042b035;
std::uintptr_t server_pointer{},command_pointer{};
unsigned native_argument{};
std::array<unsigned char,52> command_bytes{};
struct alignas(16) CpuState {unsigned gp[8]{},flags{},stack_delta{},error{},padding{};unsigned char fx[512]{};};
static_assert(offsetof(CpuState,fx)==48);
CpuState incoming,returned;
alignas(16) unsigned char input_fx[512]{},output_fx[512]{},caller_fx[512]{};
const unsigned xmm_bits[4]{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
const unsigned input_mxcsr=0x3f80,output_mxcsr=0x5f80;
const unsigned short input_cw=0x077f,output_cw=0x0b7f;
void* invoke_target{};unsigned saved_stack{};
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
// The adversarial native deliberately violates C++ nonvolatile-register rules;
// preserve the enclosing test callback independently across a nested invocation.
__declspec(naked) void __cdecl call_nested() {
    __asm {
        pushfd
        pushad
        sub esp,528
        lea eax,[esp+15]
        and eax,0xfffffff0
        fxsave [eax]
        push 0
        call consumed_detour
        lea esp,[esp+4]
        lea eax,[esp+15]
        and eax,0xfffffff0
        fxrstor [eax]
        lea esp,[esp+528]
        popad
        popfd
        ret
    }
}
void native_scope_body() {
    ++native_calls;NativeConsumedCommand value{};
    const bool visible=read_consumed_command_scope_test_copy(&value);
    if(scope.depth==1&&!scope.poisoned&&scope.value.active) {
        native_visible=native_visible&&visible;native_value=value;
    } else native_visible=native_visible&&!visible;
    if(nested_native) {nested_native=false;call_nested();check(!read_consumed_command_scope_test_copy(&value),"outer native scope remains invalid after nested native execution");}
    if(raise_native)RaiseException(native_exception,0,0,nullptr);
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
        mov eax,[esp+40]
        mov native_argument,eax
        popad
        popfd
        cld
        call native_scope_body
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
        push command_pointer
        push server_pointer
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
// A separate ordinary compiler caller verifies native SEH propagation. Avoid
// the adversarial invoker's intentionally invalid nonvolatile register values.
bool propagate_native_exception() {
    __try {reinterpret_cast<void(__cdecl*)(std::uintptr_t,std::uintptr_t)>(invoke_target)(server_pointer,command_pointer);}
    __except(GetExceptionCode()==native_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
void disturb() {
    unsigned flags{};__asm {pushfd} __asm {pop flags}
    check(!(flags&0x400),"callbacks enter with clear direction flag");
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
void before(void*,const NativeConsumedCommand& value) {
    ++before_calls;before_value=value;NativeConsumedCommand hidden{};
    callbacks_hidden=callbacks_hidden&&!read_consumed_command_scope_test_copy(&hidden);
    replace_rejected=replace_rejected&&!set_consumed_command_callbacks_test_copy({});
    disturb();
    if(nested_before)call_nested();
    if(throw_before)throw std::runtime_error("synthetic before callback");
    if(seh_before)RaiseException(0xe042b036,0,0,nullptr);
}
void after(void*,const NativeConsumedCommand& value) {
    ++after_calls;after_value=value;NativeConsumedCommand hidden{};
    callbacks_hidden=callbacks_hidden&&!read_consumed_command_scope_test_copy(&hidden);
    disturb();if(nested_after)call_nested();
    if(throw_after)throw std::runtime_error("synthetic after callback");
}
bool fp_equal(const unsigned char* a,const unsigned char* b) {
    if(memcmp(a,b,5)||memcmp(a+24,b+24,4)||memcmp(a+160,b+160,128))return false;
    for(unsigned n=0;n<8;++n)if(memcmp(a+32+n*16,b+32+n*16,10))return false;
    return true;
}
void check_state(bool entry=true) {
    const unsigned expected_in[]{0x66666666,0x55555555,0x77777777,0,0x22222222,0x44444444,0x33333333,0x11111111};
    const unsigned expected_out[]{0xf6f6f6f6,0xe5e5e5e5,0x87878787,0,0xb2b2b2b2,0xd4d4d4d4,0xc3c3c3c3,0xa1a1a1a1};
    bool gp_out=true,gp_in=true;
    for(unsigned n=0;n<8;++n)if(n!=3){gp_out&=returned.gp[n]==expected_out[n];gp_in&=incoming.gp[n]==expected_in[n];}
    check(gp_out&&returned.stack_delta==0,"all native return GP values and caller ESP preserved");
    check((returned.flags&0xcd5)==0x444,"native return arithmetic and direction flags preserved");
    check(returned.error==2222,"native return LastError survives compiler finally/callbacks");
    check(fp_equal(returned.fx,output_fx),"all native return x87/SSE state preserved");
    check(native_argument==0,"exact single native cdecl client index forwarded");
    if(entry) {
        check(gp_in,"all original entry GP values reach native across C++ dispatch");
        check((incoming.flags&0xcd5)==0x445,"original entry arithmetic and direction flags reach native");
        check(incoming.error==1111,"native entry LastError survives observer");
        check(fp_equal(incoming.fx,input_fx),"all original entry x87/SSE state reaches native");
    }
}
void run(bool entry=true){SetLastError(1111);invoke();check_state(entry);}
void word(void* p,std::uintptr_t value){const auto v=static_cast<unsigned>(value);memcpy(p,&v,4);}
void write_authored_guard_call(unsigned char* site) {
    site[0]=0xe8;word(site+1,0x7b);
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    initialize_fp();constexpr std::size_t image_size=0x2470000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* server=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x12000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* client=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image||!server||!client)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);server_pointer=reinterpret_cast<std::uintptr_t>(server);
    command_pointer=reinterpret_cast<std::uintptr_t>(command_bytes.data());
    check(!set_consumed_command_callbacks_test_copy({nullptr,before,after}),"nonempty callbacks require exact installed seam");
    check(set_consumed_command_callbacks_test_copy({}),"empty callbacks are default");
    check(!guards_match(1)&&!install_at(base),"inaccessible and wrong byte guards reject");
    // The private image begins zero-filled. Only the patched site needs an
    // authored CALL opcode; all other test guards deliberately hash zeros.
    write_authored_guard_call(image+call_rva);
    for(auto rva:{call_rva,native_rva,std::uintptr_t(0x113dc0),std::uintptr_t(0x113e04),std::uintptr_t(0x113e1e),std::uintptr_t(0x113e40),call_rva+5}) {
        image[rva]^=1;unsigned char unchanged_call[5]{};memcpy(unchanged_call,image+call_rva,5);
        check(!install_at(base)&&!memcmp(image+call_rva,unchanged_call,5),"every stack/target/call guard rejects without patching");image[rva]^=1;
    }
    DWORD old{},unused{};VirtualProtect(image+0x113000,0x2000,PAGE_EXECUTE_READ,&old);
    check(install_at(base)&&install_at(base)&&!install_at(base+4096),"exact call installs once for one image");
    MEMORY_BASIC_INFORMATION region{};VirtualQuery(image+call_rva,&region,sizeof(region));
    check(region.Protect==PAGE_EXECUTE_READ,"native patch page protection restored");
    consumed_original=reinterpret_cast<void*>(&fake_native);
    VirtualProtect(image+0x113000,0x2000,PAGE_EXECUTE_READWRITE,&unused);
    // Private SV_ClientThink stand-in reproduces the proven stack exactly.
    // Its continuation skips saved GP values so adversarial native output is
    // observable; no retail/global instruction is ever executed.
    const unsigned char prefix[]{0x56,0x53,0x57,0x68,0xe2,0x04,0x00,0x00,0x6a,0x00,0x6a,0x00};
    memcpy(image+call_rva-sizeof(prefix),prefix,sizeof(prefix));
    const unsigned char continuation[]{0x8d,0x64,0x24,0x18,0xc3};memcpy(image+call_rva+5,continuation,sizeof(continuation));
    FlushInstructionCache(GetCurrentProcess(),image+call_rva-sizeof(prefix),32);invoke_target=image+call_rva-sizeof(prefix);
    for(unsigned i=0;i<52;++i)command_bytes[i]=static_cast<unsigned char>(i*7+3);word(command_bytes.data(),1250);
    word(server,4);memcpy(server+0x10f38,command_bytes.data(),52);
    word(image+0x246d01c,server_pointer);word(image+0x246d014,1300);
    word(image+0x16796f8,0);word(image+0x16796f8+0x13c,reinterpret_cast<std::uintptr_t>(client));
    word(image+0x1803140,reinterpret_cast<std::uintptr_t>(client));word(image+0x1804890,1400);word(client+0x1d00,1299);
    std::array<std::uint32_t,19> saved{};saved[9]=static_cast<unsigned>(base+call_rva+5);saved[12]=1250;
    saved[17]=static_cast<unsigned>(server_pointer);saved[18]=static_cast<unsigned>(command_pointer);
    auto sample=snapshot(saved.data(),123);
    check(sample.active&&sample.readable&&sample.local_identity&&sample.command==command_bytes&&sample.last_server_time==1299&&sample.command_time==1250,
          "copied command identity is independent of clamped native lastServerTime");
    auto changed=saved;changed[9]++;check(snapshot(changed.data(),1).mismatches==consumed_wrong_return,"wrong caller rejects before native identities");
    changed=saved;changed[10]=1;check(snapshot(changed.data(),1).mismatches==consumed_other_client,"other local client rejected");
    changed=saved;changed[11]=1;check(snapshot(changed.data(),1).mismatches==consumed_other_client,"retained setter index verifies caller stack");
    changed=saved;changed[17]++;check(snapshot(changed.data(),1).mismatches==consumed_server_identity,"actual caller serverClient must equal current server base");
    word(server,3);check(snapshot(saved.data(),1).mismatches==consumed_server_inactive,"inactive server rejected before command copy");word(server,4);
    word(image+0x16796f8,1);check(snapshot(saved.data(),1).mismatches==consumed_entity_identity,"nonlocal entity number rejected");word(image+0x16796f8,0);
    client[0x130]=1;check(snapshot(saved.data(),1).mismatches==consumed_client_identity,"player-state client identity rejected");client[0x130]=0;
    word(image+0x1803140,0);check(snapshot(saved.data(),1).mismatches==consumed_client_identity,"entity client must equal level.clients");word(image+0x1803140,reinterpret_cast<std::uintptr_t>(client));
    changed=saved;changed[12]++;check(snapshot(changed.data(),1).mismatches&consumed_command_time,"retained setter time must equal actual consumed command time");
    command_bytes[51]^=1;check(snapshot(saved.data(),1).mismatches&consumed_source_changed,"whole52 bytes match original source command, including final byte");command_bytes[51]^=1;
    changed=saved;changed[18]=1;check(snapshot(changed.data(),1).mismatches&consumed_unreadable,"unreadable source yields explicit copied mismatch");
    NativeConsumedCommand missing{};check(!read_consumed_command_scope_test_copy(&missing)&&!read_consumed_command_scope_test_copy(nullptr),"getter is unavailable outside native execution");
    run();check(native_calls==1&&before_calls==0&&after_calls==0,"default empty callbacks preserve exactly one native call");
    check(set_consumed_command_callbacks_test_copy({nullptr,before,after}),"callbacks install after pinned seam");
    std::uint64_t serial{};
    for(unsigned i=0;i<48;++i) {
        run();check(before_value.scope_serial>serial&&after_value.scope_serial==before_value.scope_serial&&after_value.native_returned,"unique before/after serial with exactly one native return");
        serial=after_value.scope_serial;
        check(native_value.command==command_bytes&&before_value.active&&native_value.scope_serial==serial,"real native stand-in sees exact copied command only within its scope");
        check(!read_consumed_command_scope_test_copy(&missing)&&scope.depth==0,"normal return invalidates scope before leaving detour");
    }
    check(callbacks_hidden&&native_visible&&replace_rejected,"callback phases hide getter and callback contexts cannot change in flight");
    check(native_calls==49&&before_calls==48&&after_calls==48,"one native call and one paired notification per normal scope");
    throw_before=true;run();throw_before=false;check(after_value.mismatches&consumed_callback_failed,"before C++ failure poisons native getter but preserves native call");
    seh_before=true;run();seh_before=false;check(after_value.mismatches&consumed_callback_failed,"before SEH failure is contained and poisons getter");
    throw_after=true;run();throw_after=false;check(scope.depth==0&&callbacks_in_flight==0,"after failure retires callback ownership");
    auto prior_native=native_calls,prior_before=before_calls,prior_after=after_calls;
    nested_before=nested_after=true;run(false);nested_before=nested_after=false;
    check(native_calls==prior_native+3&&before_calls==prior_before+1&&after_calls==prior_after+1&&after_value.nesting_invalidated&&scope.depth==0,
          "nested before/after calls execute once without rearming the outer scope or duplicating callbacks");
    nested_native=true;run(false);check(after_value.nesting_invalidated&&scope.depth==0,"native recursion invalidates outer and inner scope until outer exit");
    raise_native=true;check(propagate_native_exception(),"native exception reaches original outer handler unchanged");raise_native=false;
    check(before_value.active&&!after_value.native_returned&&(after_value.mismatches&consumed_native_unwind)&&!scope.depth&&!callbacks_in_flight&&!read_consumed_command_scope_test_copy(&missing),
          "native unwind always retires TLS and callback lifetime through compiler finally");
    run();check(after_value.active&&after_value.native_returned,"fresh scope works after native unwind without stale command retention");
    DWORD worker_thread{};std::uint64_t worker_serial{};bool worker_local=false;
    set_consumed_command_callbacks_test_copy({});
    std::thread worker([&]{enter_scope(saved.data());NativeConsumedCommand copy{};worker_local=read_consumed_command_scope_test_copy(&copy);worker_thread=copy.native_thread;worker_serial=copy.scope_serial;leave_scope(false);});worker.join();
    check(worker_local&&worker_thread!=GetCurrentThreadId()&&worker_serial>serial&&!read_consumed_command_scope_test_copy(&missing),"scope, identity and getter remain native-thread local");
    check(!memcmp(server+0x10f38,command_bytes.data(),52),"all native command bytes remain unchanged after observers and faults");
    SetEnvironmentVariableW(L"BOVR_CONSUMED_COMMAND_PROBE",L"0");check(!install_consumed_command_probe_test_copy(GetModuleHandleW(nullptr)),"explicit opt-in required");
    SetEnvironmentVariableW(L"BOVR_CONSUMED_COMMAND_PROBE",L"1");check(!install_consumed_command_probe_test_copy(GetModuleHandleW(nullptr)),"fixture executable cannot pass pinned retail identity");
    SetEnvironmentVariableW(L"BOVR_CONSUMED_COMMAND_PROBE",nullptr);
    VirtualFree(client,0,MEM_RELEASE);VirtualFree(server,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::printf("consumed_command_probe_checks=%u failures=%u retail_calls=0 runtime_calls=0\n",checks,failures);return failures?1:0;
}
