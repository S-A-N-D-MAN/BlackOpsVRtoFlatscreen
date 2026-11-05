// SPDX-License-Identifier: GPL-3.0-only
// Private synthetic callsite/trampoline test. No retail executable or runtime.
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_final_ui_probe install_final_ui_probe_test_copy
#define install_event_loop_probe install_event_loop_probe_test_copy
#define set_final_ui_callbacks set_final_ui_callbacks_test_copy
#define set_event_loop_callback set_event_loop_callback_test_copy
#include "../src/t5/final_ui_probe.cpp"
#undef install_final_ui_probe
#undef install_event_loop_probe
#undef set_final_ui_callbacks
#undef set_event_loop_callback
#include <array>
#include <cstdio>
namespace {
volatile unsigned calls{},events{};
std::uint32_t observed_stream{},observed_window{};
const std::uint32_t xmm_pattern[]{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
unsigned failures{},before_calls{},after_calls{},event_callbacks{};
bool contexts_valid=true;
void check(bool ok,const char* label) {if(!ok){++failures;printf("FAIL %s\n",label);}}
struct Capture {std::uint32_t eax{},ebx{},esi{},edi{},ebp{},flags{};int stack_delta{};std::uint32_t xmm[4]{};double x87{};};
static_assert(offsetof(Capture,x87)==48);
__declspec(naked) void __cdecl invoke(void*,void*,void*,Capture*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        push esp
        mov edx,[esp+24]
        mov ecx,[esp+28]
        mov eax,[esp+32]
        mov ebx,0x11223344
        mov esi,0x55667788
        mov edi,0x99aabbcc
        mov ebp,0x1234abcd
        call edx
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
    check(c.eax==0x12345678,"original EAX result retained");
    check(c.ebx==0x11223344 && c.esi==0x55667788 && c.edi==0x99aabbcc && c.ebp==0x1234abcd,"native nonvolatile registers preserved");
    check(c.stack_delta==0,"no phantom native stack arguments");
    check((c.flags&0x8d5)==0x44,"native flags preserved around callback");
    check(memcmp(c.xmm,xmm_pattern,sizeof(xmm_pattern))==0 && c.x87==1,"native x87/SSE result preserved");
}
void before(const bovr::t5::NativeFinalUi& value) {
    ++before_calls;contexts_valid=contexts_valid && value.valid && value.native_frame==123 && value.key_catchers==0x10;
    __asm {pxor xmm0,xmm0}
}
void after(const bovr::t5::NativeFinalUi& before_value,const bovr::t5::NativeFinalUi& value) {
    ++after_calls;contexts_valid=contexts_valid && value.valid && before_value.commands==value.commands &&
        before_value.backend==value.backend && value.page.stack_count==1 && value.page.menu_type==2;
    __asm {pxor xmm0,xmm0}
}
void after_event() {++event_callbacks;__asm {pxor xmm0,xmm0}}
void word(unsigned char* destination,const void* pointer) {const auto value=reinterpret_cast<std::uint32_t>(pointer);memcpy(destination,&value,4);}
void value(unsigned char* destination,std::uint32_t number) {memcpy(destination,&number,4);}
void write_loop_fixture(unsigned char* code) {
    code[0]=0x51;code[1]=0x53;code[2]=0x56;code[3]=0x90;code[4]=0x90;
    code[5]=0xa3;word(code+6,&observed_window);code[10]=0x89;code[11]=0x0d;word(code+12,&observed_stream);
    code[16]=0xff;code[17]=0x05;word(code+18,const_cast<unsigned*>(&calls));
    code[22]=0x5e;code[23]=0x5b;code[24]=0x59;code[25]=0xb8;value(code+26,0x12345678);
    code[30]=0xf3;code[31]=0x0f;code[32]=0x6f;code[33]=0x05;word(code+34,xmm_pattern);
    code[38]=0xd9;code[39]=0xe8;code[40]=0x39;code[41]=0xc0;code[42]=0xc3;
}
void write_event_fixture(unsigned char* code) {
    code[0]=0x55;code[1]=0x8b;code[2]=0xec;code[3]=0x83;code[4]=0xe4;code[5]=0xf0;
    code[6]=0xff;code[7]=0x05;word(code+8,const_cast<unsigned*>(&events));
    code[12]=0xb8;value(code+13,0x12345678);code[17]=0xf3;code[18]=0x0f;code[19]=0x6f;code[20]=0x05;word(code+21,xmm_pattern);
    code[25]=0xd9;code[26]=0xe8;code[27]=0x8b;code[28]=0xe5;code[29]=0x5d;code[30]=0x39;code[31]=0xc0;code[32]=0xc3;
}
}
int main() {
    constexpr std::size_t size=69693440;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto* site=image+0x2eba14;auto* loop=image+0x2eb6a0;auto* event=image+0x189430;
    check(!bovr::t5::install_final_at(base) && !bovr::t5::install_event_at(base),"wrong native bytes refused");
    // Authored native stand-ins exercise the same register ABI and whole
    // instruction trampoline without carrying either retail prologue.
    write_loop_fixture(loop);site[0]=0xe8;value(site+1,0x7b);site[5]=0xc3;
    write_event_fixture(event);
    DWORD old{};VirtualProtect(image,size,PAGE_EXECUTE_READWRITE,&old);FlushInstructionCache(GetCurrentProcess(),image,size);
    Capture captured{};void* stream=image+0x200;
    auto* backend=image+0x3740100;word(image+0x3a5b5f8,backend);word(backend+0x16cd24,stream);
    *reinterpret_cast<unsigned*>(backend+0x15d7a0)=123;
    *reinterpret_cast<unsigned*>(image+0x2510160)=0x10;
    *reinterpret_cast<unsigned*>(image+0x216bf4c)=1;
    *reinterpret_cast<unsigned*>(image+0x216bfd0)=1;
    *reinterpret_cast<unsigned*>(image+0x216cc24)=2;
    word(image+0x216bf50,image+0x300);*reinterpret_cast<unsigned*>(image+0x354)=6;
    invoke(loop,stream,nullptr,&captured);capture_ok(captured);
    check(calls==1 && observed_stream==reinterpret_cast<std::uint32_t>(stream) && observed_window==0,"baseline ECX stream and EAX null window reach native once");
    check(bovr::t5::install_final_at(base),"exact final-only CALL installs");
    check(bovr::t5::set_final_ui_callbacks_test_copy(before,after),"final-only callbacks bind");
    for(unsigned i=0;i<1000;++i){invoke(site,stream,nullptr,&captured);capture_ok(captured);}
    check(calls==1001 && before_calls==1000 && after_calls==1000,"1000 before/original/after calls remain once each");
    check(contexts_valid,"native backend/frame/page identity agrees before and after exact final call");
    check(!bovr::t5::snapshot(stream,reinterpret_cast<void*>(1)).valid,"UI3D-tagged invocation cannot be treated as final2D");
    check(observed_stream==reinterpret_cast<std::uint32_t>(stream) && observed_window==0,"callbacks preserve native register arguments");
    check(bovr::t5::install_final_at(base),"final call install is idempotent");
    check(bovr::t5::install_event_at(base),"event-loop whole-instruction entry trampoline installs");
    check(bovr::t5::set_event_loop_callback_test_copy(after_event),"post-drain callback binds");
    for(unsigned i=0;i<1000;++i){invoke(event,nullptr,nullptr,&captured);capture_ok(captured);}
    check(events==1000 && event_callbacks==1000,"1000 original event drains precede exactly one service each");
    check(bovr::t5::install_event_at(base),"event entry install is idempotent");
    VirtualFree(image,0,MEM_RELEASE);
    printf("final_ui_event_abi_failures=%u retail_calls=0\n",failures);return failures?1:0;
}
