// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace bovr::t5 {
void* dual_pistol_projectile_test_entry(void*,bool (__cdecl*)(const std::uint32_t*,unsigned,unsigned char*)) noexcept;
}
namespace {
unsigned checks{},failures{},native_calls{},selector_calls{},selected_event{},native_event{},native_error{};
void check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
bool selection{};
std::array<unsigned char,68> original{},observed{};
std::uint32_t selected_args[7]{},native_args[7]{},native_result{},registers[4]{};
std::uintptr_t before_sp{},after_sp{};
__declspec(align(16)) unsigned char before_fx[512]{},native_fx[512]{};
void* wrapper{};
void __cdecl observe_native(const std::uint32_t* stack,unsigned event) {
    ++native_calls;native_event=event;native_error=GetLastError();memcpy(native_args,stack+1,sizeof(native_args));
    memcpy(observed.data(),reinterpret_cast<const void*>(stack[4]),observed.size());
}
__declspec(naked) void native_projectile() {
    __asm {
        fxsave native_fx
        pushfd
        pushad
        lea eax,[esp+36]
        push ebp
        push eax
        call observe_native
        lea esp,[esp+8]
        popad
        popfd
        mov eax,0x12345678
        ret
    }
}
bool __cdecl select(const std::uint32_t* stack,unsigned event,unsigned char* scratch) {
    ++selector_calls;selected_event=event;memcpy(selected_args,stack+1,sizeof(selected_args));
    memset(scratch,0x72,68);SetLastError(0x99887766);
    __asm {
        xorps xmm0,xmm0
        xorps xmm1,xmm1
        xorps xmm2,xmm2
        xorps xmm3,xmm3
        xorps xmm4,xmm4
        xorps xmm5,xmm5
        xorps xmm6,xmm6
        xorps xmm7,xmm7
        fninit
        fldpi
    }
    return selection;
}
__declspec(naked) void invoke() {
    __asm {
        pushfd
        pushad
        mov before_sp,esp
        mov ebx,0x44556677
        mov esi,0x55667788
        mov edi,0x66778899
        mov ebp,31
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        pcmpeqd xmm3,xmm3
        pcmpeqd xmm4,xmm4
        pcmpeqd xmm5,xmm5
        pcmpeqd xmm6,xmm6
        pcmpeqd xmm7,xmm7
        fld1
        fxsave before_fx
        push 0x77777777
        push 0x66666666
        push 0x55555555
        push offset original
        push 0x33333333
        push 0x22222222
        push 0x11111111
        call dword ptr [wrapper]
        mov native_result,eax
        mov registers,ebx
        mov registers+4,esi
        mov registers+8,edi
        mov registers+12,ebp
        lea esp,[esp+28]
        mov after_sp,esp
        fninit
        popad
        popfd
        ret
    }
}
}
int main(){
    wrapper=bovr::t5::dual_pistol_projectile_test_entry(reinterpret_cast<void*>(&native_projectile),&select);
    original.fill(0x35);
    for(bool selected:{false,true}){
        selection=selected;native_calls=selector_calls=0;SetLastError(0xabcdef01);invoke();
        check(native_calls==1&&selector_calls==1,"original projectile call occurs exactly once with or without replacement");
        check(before_sp==after_sp,"seven arguments and invocation scratch leave native caller stack balanced");
        check(native_result==0x12345678,"native projectile entity return is preserved");
        const unsigned expected_registers[]{0x44556677,0x55667788,0x66778899,31};
        check(!memcmp(registers,expected_registers,sizeof(registers)),"callee preserved registers and event EBP survive");
        check(selected_event==31&&native_event==31,"actual event EBP reaches selector and native function unchanged");
        const std::uint32_t expected[]{0x11111111,0x22222222,0x33333333,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(original.data())),0x55555555,0x66666666,0x77777777};
        check(!memcmp(selected_args,expected,sizeof(expected)),"selector sees exact seven original arguments");
        bool other=true;for(unsigned i=0;i<7;++i)if(i!=3)other=other&&native_args[i]==expected[i];
        check(other,"all six unrelated native arguments remain bit identical");
        check(selected?native_args[3]!=expected[3]:native_args[3]==expected[3],"only accepted selection substitutes private WP pointer");
        bool bytes=true;for(auto value:observed)bytes=bytes&&value==(selected?0x72:0x35);
        check(bytes,"native receives complete private or original 68-byte weapon parameters");
        bytes=true;for(auto value:original)bytes=bytes&&value==0x35;
        check(bytes,"original weapon parameters remain untouched after selected and rejected calls");
        check(native_error==0xabcdef01,"selector preserves caller last error for native callee");
        check(!memcmp(before_fx,native_fx,288),"selector preserves full x87 and SSE state before native call");
    }
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
