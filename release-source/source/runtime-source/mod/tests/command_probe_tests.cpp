// SPDX-License-Identifier: GPL-3.0-only
// Synthetic x86 register/stack ABI test. No retail process or game files.
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_command_probe install_command_probe_test_copy
#define set_command_callbacks set_command_callbacks_test_copy
#define read_pre_command_snapshot read_pre_command_snapshot_test_copy
#include "../src/t5/command_probe.cpp"
#undef install_command_probe
#undef set_command_callbacks
#undef read_pre_command_snapshot
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
volatile unsigned executed{};
const std::uint32_t xmm_pattern[4]{0x12345678,0x89abcdef,0x31415926,0x27182818};
unsigned failures{};
void check(bool ok,const char* message) { printf("%s %s\n",ok?"PASS":"FAIL",message); if(!ok) ++failures; }
void write_word(unsigned char* destination,std::uint32_t value) {memcpy(destination,&value,4);}
void write_command_fixture(unsigned char* entry) {
    // ADD ESP,-12 is an authored encoding of the same stack allocation. It
    // preserves the eight-byte relocation contract without retail bytes.
    entry[0]=0x83;entry[1]=0xc4;entry[2]=0xf4;entry[3]=0x53;entry[4]=0x8b;entry[5]=0x5c;entry[6]=0x24;entry[7]=0x14;
    entry[8]=0xff;entry[9]=0x05;write_word(entry+10,reinterpret_cast<std::uint32_t>(&executed));
    entry[14]=0x89;entry[15]=0x18;entry[16]=0xc6;entry[17]=0x40;entry[18]=0x1b;entry[19]=0x7f;
    entry[20]=0xc6;entry[21]=0x40;entry[22]=0x1c;entry[23]=0x81;
    entry[24]=0xf3;entry[25]=0x0f;entry[26]=0x6f;entry[27]=0x05;write_word(entry+28,reinterpret_cast<std::uint32_t>(xmm_pattern));
    entry[32]=0xd9;entry[33]=0xe8;entry[34]=0x39;entry[35]=0xdb;entry[36]=0x5b;
    entry[37]=0x8d;entry[38]=0x64;entry[39]=0x24;entry[40]=0x0c;entry[41]=0xc3;
}
#pragma pack(push,4)
struct Capture {
    std::uint32_t eax{},ebx{},esi{},edi{},ebp{},flags{};
    std::int32_t stack_delta{};
    std::uint32_t xmm[4]{};
    double x87{};
};
#pragma pack(pop)
static_assert(offsetof(Capture,xmm)==28 && offsetof(Capture,x87)==44);

// Save the real caller's nonvolatile registers, place sentinels in all four,
// invoke EAX=result + one stack argument, then capture native output before any
// C++ code runs. The scratch stack marker proves balanced native/hook cleanup.
__declspec(naked) void __cdecl invoke(void*,unsigned char*,int,Capture*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        push esp
        push dword ptr [esp+32]
        mov eax,[esp+32]
        mov ecx,[esp+28]
        mov ebx,0x11223344
        mov esi,0x55667788
        mov edi,0x99aabbcc
        mov ebp,0x1234abcd
        call ecx
        lea esp,[esp+4]
        mov edx,[esp+36]
        mov [edx],eax
        mov [edx+4],ebx
        mov [edx+8],esi
        mov [edx+12],edi
        mov [edx+16],ebp
        pushfd
        pop dword ptr [edx+20]
        lea ecx,[esp+4]
        sub ecx,[esp]
        mov [edx+24],ecx
        movdqu [edx+28],xmm0
        fstp qword ptr [edx+44]
        lea esp,[esp+4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void check_capture(const Capture& c,const void* output) {
    check(c.eax==reinterpret_cast<std::uint32_t>(output),"EAX result pointer survives native call and observation");
    check(c.ebx==0x11223344 && c.esi==0x55667788 && c.edi==0x99aabbcc && c.ebp==0x1234abcd,
          "EBX ESI EDI EBP preserve caller sentinels");
    check(c.stack_delta==0,"one stack argument and wrapper scratch stack remain balanced");
    check((c.flags&0x8d5)==0x44,"native comparison flags survive post-call observation");
    check(memcmp(c.xmm,xmm_pattern,sizeof(xmm_pattern))==0 && c.x87==1.0,
          "native XMM0 and x87 outputs survive observation");
}
struct CallbackTest { unsigned before{},after{}; bool context_valid{},unbind_rejected{},result_correct{}; };
void before_test(void* opaque,const bovr::t5::CommandContext& context) {
    auto& state=*static_cast<CallbackTest*>(opaque);
    ++state.before;
    state.context_valid=context.valid && context.native_thread==GetCurrentThreadId() && context.local_client==51 && executed==3;
    state.unbind_rejected=!bovr::t5::set_command_callbacks_test_copy({});
    SetLastError(0x66112233);
    __asm { pxor xmm0,xmm0 }
}
void after_test(void* opaque,const bovr::t5::CommandContext& context,unsigned char* command) {
    auto& state=*static_cast<CallbackTest*>(opaque);
    ++state.after;
    int client{}; memcpy(&client,command,4);
    state.result_correct=context.valid && context.local_client==51 && client==51 && executed==4;
    SetLastError(0x77445566);
    __asm { pxor xmm0,xmm0 }
}
}
int main() {
    constexpr std::size_t rva=0x4822d0;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,69693440,MEM_RESERVE,PAGE_NOACCESS));
    if(!image) return 1;
    for(const auto offset: {rva,std::size_t{0x2510160},std::size_t{0x2511da8},std::size_t{0x2593850},std::size_t{0x2081764}})
        if(!VirtualAlloc(image+(offset&~std::size_t{0xfff}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    auto* entry=image+rva;
    SetEnvironmentVariableW(L"BOVR_COMMAND_PROBE",L"0");
    check(!bovr::t5::install_command_probe_test_copy(reinterpret_cast<HMODULE>(image)),"command probe is explicitly opt-in");
    SetEnvironmentVariableW(L"BOVR_COMMAND_PROBE",L"1");
    check(!bovr::t5::install_command_probe_test_copy(reinterpret_cast<HMODULE>(image)),"wrong eight-byte prologue rejected without patch");
    write_command_fixture(entry);constexpr std::size_t code_size=42;
    DWORD old{};
    if(!VirtualProtect(entry,code_size,PAGE_EXECUTE_READ,&old)) return 1;
    FlushInstructionCache(GetCurrentProcess(),entry,code_size);
    std::array<unsigned char,0x34> baseline{},hooked{};
    baseline.fill(0xa5); hooked.fill(0xa5);
    Capture before{},after{};
    invoke(entry,baseline.data(),37,&before);
    check(executed==1,"synthetic native command executes exactly once before hook");
    check_capture(before,baseline.data());
    check(bovr::t5::install_command_probe_test_copy(reinterpret_cast<HMODULE>(image)),"guarded eight-byte CreateCmd trampoline installs");
    check(entry[0]==0xe9 && entry[5]==0x90 && entry[6]==0x90 && entry[7]==0x90,"entry patch occupies exact eight-byte span");
    invoke(entry,hooked.data(),37,&after);
    check(executed==2,"hook calls native command exactly once");
    check_capture(after,hooked.data());
    check(baseline==hooked,"all52 original command result bytes remain unchanged by observation");
    check(*reinterpret_cast<const int*>(hooked.data())==37,"native body receives exact local-client stack argument");
    check(bovr::t5::install_command_probe_test_copy(reinterpret_cast<HMODULE>(image)),"same-image install is idempotent");
    invoke(entry,hooked.data(),41,&after);
    check(executed==3 && *reinterpret_cast<const int*>(hooked.data())==41,"idempotent install does not double wrap or retain old arguments");
    check_capture(after,hooked.data());
    CallbackTest callbacks{};
    check(bovr::t5::set_command_callbacks_test_copy({&callbacks,before_test,after_test}),"explicit callbacks bind after hook installation");
    SetLastError(0x13572468);
    invoke(entry,hooked.data(),51,&after);
    const auto callback_error=GetLastError();
    check(callback_error==0x13572468,"pre and post callbacks preserve native LastError despite callback clobbering");
    check(callbacks.before==1 && callbacks.after==1 && callbacks.context_valid && callbacks.result_correct,
          "callbacks run before and after one native call with mapped context and borrowed post result");
    check(callbacks.unbind_rejected,"callback context cannot be unbound while borrowed");
    check_capture(after,hooked.data());
    check(bovr::t5::set_command_callbacks_test_copy({}),"callbacks can be unbound after they return");
    invoke(entry,hooked.data(),61,&after);
    check(executed==5 && callbacks.before==1 && callbacks.after==1,"empty callbacks restore plain observation");
    bovr::t5::PreCommandSnapshot retained{};
    check(bovr::t5::read_pre_command_snapshot_test_copy(&retained)&&retained.sequence&&retained.context.local_client==61&&retained.context.valid,
        "actual pre-native context is copied even with no input callback");
    const int scripted=1;memcpy(image+0x2511e2c,&scripted,4);
    invoke(entry,hooked.data(),0,&after);const int consumed=0;memcpy(image+0x2511e2c,&consumed,4);
    check(bovr::t5::read_pre_command_snapshot_test_copy(&retained)&&retained.context.native_override&&retained.context.local_client==0,
        "pre-native script override survives later native field consumption");
    invoke(entry,hooked.data(),0,&after);
    check(bovr::t5::read_pre_command_snapshot_test_copy(&retained)&&!retained.context.native_override,"later unblocked native command replaces override snapshot without sticky state");
    if(!VirtualAlloc(image+0x2bf5000,4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    std::vector<unsigned char> cg(0xd0000);const auto cg_address=reinterpret_cast<std::uintptr_t>(cg.data());
    memcpy(image+0x2bf5354,&cg_address,4);
    auto native_int=[&](std::size_t offset,int value){memcpy(cg.data()+offset,&value,4);};
    auto global_int=[&](std::size_t offset,int value){memcpy(image+offset,&value,4);};
    native_int(0x8a348,3000);native_int(0x8a364+0x1c4,100);global_int(0x2510164,10);global_int(0x2593850,500);global_int(0x2081764,6000);
    auto sample_gameplay=[&] {invoke(entry,hooked.data(),0,&after);bovr::t5::read_pre_command_snapshot_test_copy(&retained);};
    sample_gameplay();check(retained.gameplay_active&&retained.gameplay_epoch&&retained.native_gameplay_fields_valid&&retained.native_cg_identity==cg_address&&retained.native_health==100,
        "actual precommand mints active gameplay epoch from copied native player identity health and movement");
    auto epoch=retained.gameplay_epoch;sample_gameplay();check(retained.gameplay_epoch==epoch,"repeated active native commands retain gameplay epoch without XR polling");
    native_int(0x8a360,2);sample_gameplay();
    check(retained.gameplay_active&&retained.gameplay_epoch==epoch,"first-person shadow preserves actual command gameplay lifetime");
    native_int(0x8a364+0x10,0x100);sample_gameplay();
    check(retained.gameplay_active,"offhand-disable alone preserves the ordinary primary command lifetime");
    native_int(0x8a364+0x10,0x2000100);sample_gameplay();
    check(retained.gameplay_active,"observed post-breach flags preserve the ordinary primary command lifetime");
    native_int(0x8a364+0x10,0x180);sample_gameplay();
    check(!retained.gameplay_active,"weapon-update disable still retires the ordinary primary command lifetime");
    native_int(0x8a364+0x10,0);
    for(int mode:{1,-1,3}) {
        native_int(0x8a360,mode);sample_gameplay();
        check(!retained.gameplay_active,"model third person and unknown camera modes reject actual commands");
    }
    native_int(0x8a360,2);sample_gameplay();epoch=retained.gameplay_epoch;
    for(std::size_t offset:{std::size_t{0xc},std::size_t{0xa9d44}}) {
        native_int(offset,1);sample_gameplay();check(!retained.gameplay_active,"shadow mode cannot bypass cubemap or draw suppression");
        native_int(offset,0);
    }
    native_int(0x8a364+4,4);sample_gameplay();check(!retained.gameplay_active,"shadow mode does not admit pm4");native_int(0x8a364+4,0);
    native_int(0x8a360,0);sample_gameplay();epoch=retained.gameplay_epoch;
    global_int(0x2511e2c,1);sample_gameplay();check(!retained.gameplay_active&&retained.gameplay_epoch>epoch&&retained.context.native_override,"script override ends active gameplay lifetime before native consumes it");
    epoch=retained.gameplay_epoch;global_int(0x2511e2c,0);bovr::t5::read_pre_command_snapshot_test_copy(&retained);
    check(!retained.gameplay_active&&retained.gameplay_epoch==epoch,"native flag consumption or snapshot reread cannot refresh an inactive epoch");
    sample_gameplay();check(retained.gameplay_active&&retained.gameplay_epoch>epoch,"actual unblocked command starts a new gameplay epoch");
    epoch=retained.gameplay_epoch;native_int(0x8a364+0x1c4,0);sample_gameplay();check(!retained.gameplay_active&&retained.gameplay_epoch>epoch,"native death invalidates epoch despite unchanged catcher and connection");
    native_int(0x8a364+0x1c4,100);sample_gameplay();native_int(0x8a364+0xc,0x800);sample_gameplay();check(!retained.gameplay_active,"native frozen player cannot publish active gameplay metadata");
    native_int(0x8a364+0xc,0);sample_gameplay();epoch=retained.gameplay_epoch;native_int(0x8a348,100);sample_gameplay();
    check(retained.gameplay_active&&retained.gameplay_epoch>epoch,"same-address map game-time regression changes gameplay epoch");
    epoch=retained.gameplay_epoch;global_int(0x2593850,1);sample_gameplay();check(retained.gameplay_epoch>epoch,"native command counter reuse advances gameplay epoch");
    epoch=retained.gameplay_epoch;global_int(0x2081764,1);sample_gameplay();check(retained.gameplay_epoch>epoch,"native key-clock regression advances gameplay epoch");
    auto replacement_cg=cg;const auto replacement_address=reinterpret_cast<std::uintptr_t>(replacement_cg.data());epoch=retained.gameplay_epoch;memcpy(image+0x2bf5354,&replacement_address,4);sample_gameplay();
    check(retained.gameplay_active&&retained.gameplay_epoch>epoch,"different native cg identity advances epoch even with equal times and fields");
    const std::uintptr_t invalid_cg=1;memcpy(image+0x2bf5354,&invalid_cg,4);sample_gameplay();check(!retained.gameplay_active&&!retained.native_gameplay_fields_valid,"invalid native player storage clears gameplay permission without native writes");
    VirtualFree(image,0,MEM_RELEASE);
    printf("retail_processes=0 failures=%u\n",failures);
    return failures?1:0;
}
