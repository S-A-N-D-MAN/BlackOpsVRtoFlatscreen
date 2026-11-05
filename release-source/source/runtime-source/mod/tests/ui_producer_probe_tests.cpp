// SPDX-License-Identifier: GPL-3.0-only
// Synthetic x86 call-site and copied-metadata tests. No retail or XR process.
#define install_ui_producer_probe install_ui_producer_probe_test_copy
#define set_ui_producer_callbacks set_ui_producer_callbacks_test_copy
#define same_ui_production_range same_ui_production_range_test_copy
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/ui_producer_probe.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#undef install_ui_producer_probe
#undef set_ui_producer_callbacks
#undef same_ui_production_range
#include <algorithm>
#include <cstdio>
#include <stdexcept>
namespace {
using bovr::t5::NativeUiProduction;
unsigned failures{},checks{},before_calls{},after_calls{};
volatile unsigned calls{};
unsigned observed_local{},observed_refreshed{},incoming_xmm[4]{};
const unsigned pattern[]{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
NativeUiProduction last_before{},last_after{};
bool throw_before{},throw_after{};
void check(bool pass,const char* text) {++checks;if(!pass){++failures;printf("FAIL %s\n",text);}}
void before(const NativeUiProduction& value) {
    ++before_calls;last_before=value;
    SetLastError(12345);
    __asm {pxor xmm0,xmm0}
    if(throw_before)throw std::runtime_error("synthetic before");
}
void after(const NativeUiProduction& first,const NativeUiProduction& value) {
    ++after_calls;last_before=first;last_after=value;
    SetLastError(54321);
    __asm {
        pxor xmm0,xmm0
        fldz
    }
    if(throw_after)throw std::runtime_error("synthetic after");
}
struct Capture {unsigned eax{},ebx{},esi{},edi{},ebp{},flags{};int stack_delta{};unsigned xmm[4]{};double x87{};};
static_assert(offsetof(Capture,x87)==48);
__declspec(naked) void __cdecl invoke(void*,int,int,Capture*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        push esp
        mov edx,[esp+24]
        mov edi,[esp+32]
        mov ebx,0x11223344
        mov esi,0x55667788
        mov ebp,0x1234abcd
        movdqu xmm0,pattern
        push dword ptr [esp+28]
        call edx
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
        fstp qword ptr [edx+48]
        lea esp,[esp+4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void capture_ok(const Capture& c) {
    check(c.eax==0x12345678,"original EAX retained");
    check(c.ebx==0x11223344 && c.esi==0x55667788 && c.edi==0 && c.ebp==0x1234abcd,"native nonvolatile registers retained");
    check(c.stack_delta==0,"exactly one forwarded cdecl stack argument, no leak");
    check((c.flags&0x8d5)==0x44,"original flags retained despite argument cleanup/callback");
    check(c.x87==1 && memcmp(c.xmm,pattern,sizeof(pattern))==0,"original x87/SSE retained");
    check(memcmp(incoming_xmm,pattern,sizeof(pattern))==0,"incoming SSE restored before native entry");
}
void word(unsigned char* where,const void* value) {const auto v=reinterpret_cast<unsigned>(value);memcpy(where,&v,4);}
void value(unsigned char* where,unsigned v){memcpy(where,&v,4);}
void authored_call(unsigned char* site,const unsigned char* target) {
    site[0]=0xe8;const auto displacement=static_cast<std::uint32_t>(target-site-5);value(site+1,displacement);
}
void authored_producer(unsigned char* producer,unsigned char* array,unsigned char* backend) {
    // Keep the guarded prefix address-independent, then emit a small synthetic
    // cdecl target that records inputs and restores the test's XMM marker.
    std::fill_n(producer,18,static_cast<unsigned char>(0x90));auto* p=producer+18;
    *p++=0xa1;word(p,array);p+=4;
    *p++=0x03;*p++=0x05;word(p,array+4);p+=4;
    *p++=0xa3;word(p,backend+0x16cd24);p+=4;
    *p++=0x83;*p++=0x05;word(p,array+4);p+=4;*p++=0x04;
    *p++=0x89;*p++=0x3d;word(p,&observed_local);p+=4;
    *p++=0x8b;*p++=0x44;*p++=0x24;*p++=0x04;
    *p++=0xa3;word(p,&observed_refreshed);p+=4;
    *p++=0xf3;*p++=0x0f;*p++=0x7f;*p++=0x05;word(p,incoming_xmm);p+=4;
    *p++=0xff;*p++=0x05;word(p,const_cast<unsigned*>(&calls));p+=4;
    *p++=0xb8;value(p,0x12345678u);p+=4;
    *p++=0xf3;*p++=0x0f;*p++=0x6f;*p++=0x05;word(p,pattern);p+=4;
    *p++=0xd9;*p++=0xe8;*p++=0x39;*p++=0xc0;*p++=0xc3;
}
DWORD WINAPI foreign_thread(void*) {
    bovr::t5::before_production(0,73);bovr::t5::after_production();return 0;
}
}
int main() {
    constexpr std::size_t image_size=0x4280000;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* command_bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x18000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image || !command_bytes)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto* site=image+0x3a187e;auto* producer=image+0x3a1590;
    auto* backend=image+0x3740100;auto* array=image+0x3737090;
    check(!bovr::t5::set_ui_producer_callbacks_test_copy(before,after),"callbacks refused before install");
    check(!bovr::t5::install_at(base),"wrong entry/field-layout guards refused");
    authored_call(site,producer);
    // Model the containing native caller as a callable thunk: copy its argument
    // before the exact CALL site and remove that copy afterward without flags.
    const unsigned char caller_prefix[]{0xff,0x74,0x24,0x04};
    const unsigned char caller_suffix[]{0x8d,0x64,0x24,0x04,0xc3};
    memcpy(site-4,caller_prefix,sizeof(caller_prefix));memcpy(site+5,caller_suffix,sizeof(caller_suffix));
    auto* invoke_entry=site-4;
    authored_producer(producer,array,backend);
    constexpr std::array<std::pair<std::uintptr_t,std::size_t>,4> guard_spans{{
        {0x3a187e,5},{0x3a1590,18},{0x2d5bc8,6},{0x2d48e2,12}}};
    for(const auto [offset,size]:guard_spans)for(std::size_t i=0;i<size;++i){
        image[offset+i]^=1;check(!bovr::t5::install_at(base),"every authored guard byte is required before installation");image[offset+i]^=1;}
    word(image+0x373708c,backend);word(backend+0x16cc34,array);word(array,command_bytes);
    *reinterpret_cast<unsigned*>(array+16)=0x18000;*reinterpret_cast<unsigned*>(array+20)=0x12000;
    *reinterpret_cast<unsigned*>(image+0x356a480)=1;
    *reinterpret_cast<unsigned*>(backend+0x15d7a0)=123;
    *reinterpret_cast<unsigned*>(image+0x2510160)=0x10;
    *reinterpret_cast<unsigned*>(image+0x216bf4c)=1;
    *reinterpret_cast<unsigned*>(image+0x216bfd0)=1;
    *reinterpret_cast<unsigned*>(image+0x216cc24)=2;
    word(image+0x216bf50,image+0x300);*reinterpret_cast<unsigned*>(image+0x354)=6;
    DWORD previous{};VirtualProtect(image,image_size,PAGE_EXECUTE_READWRITE,&previous);FlushInstructionCache(GetCurrentProcess(),image,image_size);
    Capture captured{};invoke(invoke_entry,73,0,&captured);capture_ok(captured);
    check(calls==1 && observed_local==0 && observed_refreshed==73,"baseline native EDI and stack argument");
    check(bovr::t5::install_at(base),"exact call-site installs");
    check(bovr::t5::set_ui_producer_callbacks_test_copy(before,after),"copied producer callbacks bind");
    std::uint64_t previous_serial{};
    for(unsigned n=0;n<1000;++n) {
        *reinterpret_cast<unsigned*>(array+4)=12;
        invoke(invoke_entry,73,0,&captured);capture_ok(captured);
        check(last_after.valid && last_after.completed_range && last_before.used_total==12 && last_after.used_total==16,
              "exact pre/post command range completed");
        check(last_after.call_serial>previous_serial && last_before.call_serial==last_after.call_serial,"unique ordered call identity");
        previous_serial=last_after.call_serial;
    }
    check(calls==1001 && before_calls==1000 && after_calls==1000,"one original and one callback pair per invocation");
    check(last_after.command_array==reinterpret_cast<std::uintptr_t>(array) && last_after.native_frame==123 && last_after.thread_id==GetCurrentThreadId(),"backend-owned array/frame and native thread captured");
    auto bad=last_after;bad.native_frame++;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"changed frame rejected");
    bad=last_after;bad.command_array+=24;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"other SMP array rejected");
    bad=last_after;bad.commands+=4;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"different produced stream rejected");
    bad=last_after;bad.used_total=4;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"array reset mid-production rejected");
    bad=last_after;bad.used_total=last_before.used_total;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"empty or allocation-failed production cannot complete");
    bad=last_after;bad.thread_id++;check(!bovr::t5::same_ui_production_range_test_copy(last_before,bad),"foreign owner rejected");
    *reinterpret_cast<unsigned*>(array+16)=0x17fff;check(!bovr::t5::snapshot(0,73,1).valid,"capacity must match configured allocation");
    *reinterpret_cast<unsigned*>(array+16)=0x18000;*reinterpret_cast<unsigned*>(array+4)=0x18004;
    check(!bovr::t5::snapshot(0,73,1).valid,"used exceeds capacity rejected");
    *reinterpret_cast<unsigned*>(array+4)=12;*reinterpret_cast<unsigned*>(array+8)=16;
    check(!bovr::t5::snapshot(0,73,1).valid,"critical exceeds used rejected");
    *reinterpret_cast<unsigned*>(array+8)=0;
    check(!bovr::t5::snapshot(1,73,1).valid,"unsupported client rejected");
    word(backend+0x16cc34,array+24);check(!bovr::t5::snapshot(0,73,1).valid,"frontend slot cannot claim other array");word(backend+0x16cc34,array);
    DWORD page_protect{};
    auto* metadata_page=backend+0x16c000;VirtualProtect(metadata_page,8192,PAGE_NOACCESS,&page_protect);
    check(!bovr::t5::snapshot(0,73,1).valid,"inaccessible native metadata fails closed");VirtualProtect(metadata_page,8192,page_protect,&previous);
    auto* second_backend=image+0x38c6400;auto* second_array=array+24;
    word(image+0x373708c,second_backend);word(second_backend+0x16cc34,second_array);word(second_array,command_bytes);
    *reinterpret_cast<unsigned*>(second_array+4)=8;*reinterpret_cast<unsigned*>(second_array+16)=0x18000;
    *reinterpret_cast<unsigned*>(second_array+20)=0x12000;*reinterpret_cast<unsigned*>(second_backend+0x15d7a0)=124;
    auto second_before=bovr::t5::snapshot(0,73,77);
    word(second_backend+0x16cd24,command_bytes+8);*reinterpret_cast<unsigned*>(second_array+4)=12;
    auto second_after=bovr::t5::snapshot(0,73,77);
    check(bovr::t5::same_ui_production_range_test_copy(second_before,second_after),"second SMP backend owns its corresponding native array");
    word(image+0x373708c,backend);
    SetLastError(1111);bovr::t5::before_production(0,73);
    check(GetLastError()==1111,"before callback preserves its incoming native last-error value");
    word(backend+0x16cd24,command_bytes+*reinterpret_cast<unsigned*>(array+4));*reinterpret_cast<unsigned*>(array+4)+=4;
    *reinterpret_cast<unsigned*>(image+0x216cc24)=3;
    SetLastError(2222);bovr::t5::after_production();
    check(GetLastError()==2222,"after callback preserves the original native result last-error value");
    check(last_after.completed_range && last_before.page.menu_type==2 && last_after.page.menu_type==3,
          "page transition is exposed to consumer; range completion alone never claims page equality");
    *reinterpret_cast<unsigned*>(image+0x216cc24)=2;
    throw_before=true;invoke(invoke_entry,73,0,&captured);capture_ok(captured);
    check(!last_after.valid && !last_after.completed_range,"before callback exception cannot publish completion");throw_before=false;
    throw_after=true;invoke(invoke_entry,73,0,&captured);capture_ok(captured);throw_after=false;
    const auto saved_before=before_calls,saved_after=after_calls;
    bovr::t5::before_production(0,73);SetLastError(3333);bovr::t5::before_production(0,73);
    check(GetLastError()==3333,"nested before early return preserves native last-error");
    SetLastError(4444);bovr::t5::after_production();check(GetLastError()==4444,"nested after early return preserves native last-error");
    bovr::t5::after_production();
    check(before_calls==saved_before+1 && after_calls==saved_after+1 && !last_after.completed_range,"nested production invalidates outer completion without callback recursion");
    HANDLE worker=CreateThread(nullptr,0,foreign_thread,nullptr,0,nullptr);
    if(worker){WaitForSingleObject(worker,5000);CloseHandle(worker);}check(worker && !last_before.valid && !last_after.valid,"unexpected producer thread rejected");
    check(bovr::t5::install_at(base),"installation idempotent for same image");
    check(bovr::t5::set_ui_producer_callbacks_test_copy(nullptr,nullptr),"callbacks can be disabled");
    const auto saved_calls=calls;invoke(invoke_entry,73,0,&captured);capture_ok(captured);check(calls==saved_calls+1,"no-callback native path still executes exactly once");
    check(!bovr::t5::install_ui_producer_probe_test_copy(GetModuleHandleW(nullptr)),"test process cannot pass retail configuration guard");
    VirtualFree(command_bytes,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    printf("ui_producer_checks=%u failures=%u retail_calls=0 runtime_calls=0\n",checks,failures);return failures?1:0;
}
