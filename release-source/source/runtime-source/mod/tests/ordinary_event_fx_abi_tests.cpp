// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
namespace bovr::t5 {void* ordinary_event_test_entry(unsigned,void*,void(__cdecl*)(unsigned,std::uint32_t*),void(__cdecl*)(unsigned,bool))noexcept;}
namespace {
unsigned checks{},failures{},argc{},native_calls{},before_calls{},after_calls{},abnormal_calls{};
unsigned input[8]{},passed[7]{},observed[7]{},output[7]{},native_flags{},returned_flags{},before_sp{},after_sp{};
alignas(16) unsigned char input_fx[512]{},native_fx[512]{},returned_fx[512]{},expected_input_fx[512]{};
void* wrapper{};bool borrowed{};
void check(bool b,const char* why){++checks;if(!b){++failures;printf("FAIL %s\n",why);}}
__declspec(naked) void native(){
    __asm {
        pushfd
        pushad
        mov esi,esp
        mov edi,offset input
        mov ecx,8
        cld
        rep movsd
        mov eax,[esp+32]
        mov native_flags,eax
        fxsave input_fx
        lea esi,[esp+40]
        mov edi,offset passed
        mov ecx,argc
        rep movsd
        popad
        popfd
        inc native_calls
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
void damage_observer_state(){SetLastError(0x87654321);__asm{
    fninit
    fldpi
    xorps xmm0,xmm0
    xorps xmm1,xmm1
    xorps xmm2,xmm2
    xorps xmm3,xmm3
    xorps xmm4,xmm4
    xorps xmm5,xmm5
    xorps xmm6,xmm6
    xorps xmm7,xmm7
    xor eax,eax
    xor ecx,ecx
    xor edx,edx
    clc
}}
void __cdecl before(unsigned n,std::uint32_t* saved){++before_calls;borrowed=true;
    memcpy(observed,saved+10,n*4);damage_observer_state();}
void __cdecl after(unsigned,bool abnormal){++after_calls;abnormal_calls+=abnormal;borrowed=false;damage_observer_state();}
__declspec(naked) void invoke(){__asm{
    pushfd
    pushad
    mov before_sp,esp
    fninit
    fldpi
    fxsave expected_input_fx
    cmp argc,1
    je one
    push 0x77
    push 0x66
    push 0x55
    push 0x44
    push 0x33
    push 0x22
one:
    push 0x11
    mov eax,0x11111111
    mov ecx,0x22222222
    mov edx,0x33333333
    mov ebx,0x44444444
    mov esi,0x55555555
    mov edi,0x66666666
    mov ebp,0x77777777
    std
    stc
    call dword ptr [wrapper]
    mov output,eax
    mov output+4,ecx
    mov output+8,edx
    mov output+12,ebx
    mov output+16,esi
    mov output+20,edi
    mov output+24,ebp
    pushfd
    pop returned_flags
    fxsave returned_fx
    cld
    mov eax,argc
    lea esp,[esp+eax*4]
    mov after_sp,esp
    fninit
    popad
    popfd
    ret
}}
void __cdecl throwing(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned){++native_calls;RaiseException(0xe042bf01,0,0,nullptr);}
bool propagated(){__try{reinterpret_cast<void(__cdecl*)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned)>(wrapper)(1,2,3,4,5,6,7);return false;}
    __except(GetExceptionCode()==0xe042bf01?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}}
}
int main(){
    for(const auto n:{1u,7u}){argc=n;native_calls=before_calls=after_calls=abnormal_calls=0;
        wrapper=bovr::t5::ordinary_event_test_entry(n,reinterpret_cast<void*>(&native),&before,&after);
        SetLastError(0x12345678);invoke();const auto error=GetLastError();
        check(native_calls==1&&before_calls==1&&after_calls==1&&!abnormal_calls&&!borrowed,"one native call, before/after, normal cleanup");
        check(before_sp==after_sp,"caller stack balance exact");
        const unsigned args[]{0x11,0x22,0x33,0x44,0x55,0x66,0x77};
        check(!memcmp(passed,args,n*4)&&!memcmp(observed,args,n*4),"all native argument words preserved");
        check(input[0]==0x66666666&&input[1]==0x55555555&&input[2]==0x77777777&&input[4]==0x44444444&&input[5]==0x33333333&&input[6]==0x22222222&&input[7]==0x11111111,"input GP preserved across before observer");
        check((native_flags&0x401)==0x401,"input direction and carry flags preserved");
        check(!memcmp(input_fx,expected_input_fx,288),"input x87 SSE MXCSR preserved");
        const unsigned expected[]{0x11223344,0x22334455,0x33445566,0x44444444,0x55555555,0x66666666,0x77777777};
        check(!memcmp(output,expected,sizeof expected),"native output GP preserved across after observer");
        check((returned_flags&0x401)==0x401,"native output carry and direction flags preserved");
        check(!memcmp(native_fx,returned_fx,288),"native output x87 SSE MXCSR preserved");
        check(error==0x12345678,"LastError unaffected by both observers");
    }
    argc=7;native_calls=before_calls=after_calls=abnormal_calls=0;
    wrapper=bovr::t5::ordinary_event_test_entry(7,reinterpret_cast<void*>(&throwing),&before,&after);
    check(propagated(),"native SEH reaches original caller");
    check(native_calls==1&&before_calls==1&&after_calls==1&&abnormal_calls==1&&!borrowed,"SEH finally retires scope once without replay");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
