// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "t5/dual_pistol_effects.hpp"
namespace bovr::t5 {
void* dual_pistol_effects_test_entry(void*,void (__cdecl*)(std::uint32_t*)) noexcept;
}
namespace {
unsigned checks{},failures{},native_calls{},after_calls{};
void check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
float angles[3]{3,5,7},forward[3]{},right[3]{},up[3]{};
std::uintptr_t passed[4]{},callback_passed[4]{},before_sp{},after_sp{};
unsigned registers[7]{},flags{};
__declspec(align(16)) unsigned char native_fx[512]{},returned_fx[512]{};
void* wrapper{};
bovr::t5::dual_pistol_effects::OrdinaryMode mode=bovr::t5::dual_pistol_effects::OrdinaryMode::observe;
__declspec(naked) void native_angles() {
    __asm {
        inc dword ptr [native_calls]
        mov eax,[esp+4]
        mov passed,eax
        mov ecx,[esp+8]
        mov passed+4,ecx
        mov edx,[eax]
        mov [ecx],edx
        mov ecx,[esp+12]
        mov passed+8,ecx
        mov edx,[eax+4]
        mov [ecx],edx
        mov ecx,[esp+16]
        mov passed+12,ecx
        mov edx,[eax+8]
        mov [ecx],edx
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        pcmpeqd xmm3,xmm3
        pcmpeqd xmm4,xmm4
        pcmpeqd xmm5,xmm5
        pcmpeqd xmm6,xmm6
        pcmpeqd xmm7,xmm7
        fld1
        mov eax,0x11223344
        mov ecx,0x22334455
        mov edx,0x33445566
        stc
        fxsave native_fx
        ret
    }
}
void __cdecl after(std::uint32_t* saved) {
    ++after_calls;
    const auto entry=reinterpret_cast<std::uintptr_t>(saved)+36;
    memcpy(callback_passed,reinterpret_cast<const void*>(entry+4),sizeof(callback_passed));
    const bovr::t5::NativeBulletGeometry selected{{0,1,0},{1,0,0},{0,0,1},{5,6,7}};
    bovr::t5::dual_pistol_effects::apply_basis(mode,false,selected,
        reinterpret_cast<float*>(callback_passed[1]),reinterpret_cast<float*>(callback_passed[2]),
        reinterpret_cast<float*>(callback_passed[3]));
    // Deliberately destroy volatile registers, flags, SSE values, and the x87
    // stack in the mod callback. The wrapper must restore native-return state.
    SetLastError(0x99887766);
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
        xor eax,eax
        xor ecx,ecx
        xor edx,edx
        clc
    }
}
__declspec(naked) void invoke() {
    __asm {
        pushfd
        pushad
        mov before_sp,esp
        mov ebx,0x44556677
        mov esi,0x55667788
        mov edi,0x66778899
        mov ebp,0x778899aa
        push offset up
        push offset right
        push offset forward
        push offset angles
        call dword ptr [wrapper]
        mov registers,eax
        mov registers+4,ecx
        mov registers+8,edx
        mov registers+12,ebx
        mov registers+16,esi
        mov registers+20,edi
        mov registers+24,ebp
        pushfd
        pop flags
        fxsave returned_fx
        lea esp,[esp+16]
        mov after_sp,esp
        fninit
        popad
        popfd
        ret
    }
}
}
int main(){
    wrapper=bovr::t5::dual_pistol_effects_test_entry(reinterpret_cast<void*>(&native_angles),&after);
    SetLastError(0x12345678);invoke();const auto error=GetLastError();
    check(native_calls==1&&after_calls==1,"original native call and callback each run exactly once");
    check(before_sp==after_sp,"four native arguments retain caller-cleanup stack balance");
    const std::uintptr_t expected[]{reinterpret_cast<std::uintptr_t>(angles),reinterpret_cast<std::uintptr_t>(forward),
        reinterpret_cast<std::uintptr_t>(right),reinterpret_cast<std::uintptr_t>(up)};
    check(!memcmp(passed,expected,sizeof(expected)),"native receives exact four pointer arguments in order");
    check(!memcmp(callback_passed,expected,sizeof(expected)),"saved stack exposes original caller arguments");
    check(forward[0]==3&&right[0]==5&&up[0]==7,"native results remain in original caller output arrays");
    const unsigned expected_registers[]{0x11223344,0x22334455,0x33445566,0x44556677,0x55667788,0x66778899,0x778899aa};
    check(!memcmp(registers,expected_registers,sizeof(registers)),"native volatile return and nonvolatile registers survive mod callback");
    check((flags&1)!=0,"native carry flag survives callback");
    check(error==0x12345678,"last error survives callback");
    check(!memcmp(native_fx,returned_fx,32),"x87 state and MXCSR survive callback");
    check(!memcmp(native_fx+32,returned_fx+32,256),"x87 and SSE register values survive callback");
    mode=bovr::t5::dual_pistol_effects::OrdinaryMode::correct;SetLastError(0x12345678);invoke();
    check(native_calls==2&&after_calls==2&&before_sp==after_sp,"correction still calls native exactly once with balanced stack");
    check(forward[0]==0&&forward[1]==1&&forward[2]==0&&right[0]==1&&right[1]==0&&right[2]==0&&
        up[0]==0&&up[1]==0&&up[2]==1&&angles[0]==3&&angles[1]==5&&angles[2]==7,
        "correction writes only caller-owned basis arrays and leaves input angles untouched");
    check(!memcmp(registers,expected_registers,sizeof(registers))&&(flags&1)&&GetLastError()==0x12345678&&
        !memcmp(native_fx,returned_fx,288),"correcting callback preserves native registers flags FPU SSE and LastError");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
