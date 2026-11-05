// SPDX-License-Identifier: GPL-3.0-only
// Isolated CPU test of the production dispatch helper. It neither installs
// either fixed native branch nor invokes a retail absolute address.
#include "../src/t5/manual_reload_begin_hook.cpp"
#include <cstdio>

namespace bovr {void trace(const char*,const char*,...) noexcept {}}

namespace {
unsigned checks{},failures{},callback_calls{},native_calls{};
void* callback_argument{};
void* native_argument{};
bool callback_result{};
DWORD callback_error{},native_entry_error{};
constexpr DWORD caller_error=0x12345678,changed_error=0x87654321,native_error=0x32107654;
constexpr DWORD native_seh_code=0xe0421979;
alignas(16) unsigned char saved_process_fx[512]{};
alignas(16) unsigned char caller_fx[512]{},changed_fx[512]{},native_fx[512]{};
alignas(16) unsigned char callback_entry_fx[512]{},native_entry_fx[512]{},returned_fx[512]{};

void check(bool okay,const char* message) {
    ++checks;
    if(!okay){++failures;std::printf("FAIL %s\n",message);}
}

void make_states() {
    _fxsave(saved_process_fx);
    // Three distinct, valid x87 stacks/control states and all eight XMM
    // registers. Avoid invalid MXCSR bits or unmasked pending exceptions.
    __asm {
        fninit
        fld1
        fldpi
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        pxor xmm2,xmm2
        pxor xmm3,xmm3
        pxor xmm4,xmm4
        pxor xmm5,xmm5
        pxor xmm6,xmm6
        pxor xmm7,xmm7
    }
    _mm_setcsr(0x1f80);
    _fxsave(caller_fx);
    __asm {
        fninit
        fldz
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        pcmpeqd xmm3,xmm3
        pcmpeqd xmm4,xmm4
        pcmpeqd xmm5,xmm5
        pcmpeqd xmm6,xmm6
        pcmpeqd xmm7,xmm7
    }
    _mm_setcsr(0x3f80);
    _fxsave(changed_fx);
    __asm {
        fninit
        fldln2
        fldl2e
        fld1
        psrld xmm0,1
        psrld xmm1,2
        psrld xmm2,3
        psrld xmm3,4
        psrld xmm4,5
        psrld xmm5,6
        psrld xmm6,7
        psrld xmm7,8
    }
    _mm_setcsr(0x5f80);
    _fxsave(native_fx);
    _fxrstor(saved_process_fx);
}

bool deciding_callback(void* argument) noexcept {
    _fxsave(callback_entry_fx);
    callback_error=GetLastError();
    ++callback_calls;callback_argument=argument;
    SetLastError(changed_error);
    _fxrstor(changed_fx);
    return callback_result;
}

void record_native(void* argument) {
    _fxsave(native_entry_fx);
    native_entry_error=GetLastError();
    ++native_calls;native_argument=argument;
    SetLastError(native_error);
    _fxrstor(native_fx);
}
void __cdecl native_returns(void* argument) {record_native(argument);}
void __cdecl native_throws_cpp(void* argument) {record_native(argument);throw 73;}
void __cdecl native_throws_seh(void* argument) {
    record_native(argument);
    RaiseException(native_seh_code,0,0,nullptr);
}

void reset(bool selected) {
    callback_calls=native_calls=0;callback_argument=native_argument=nullptr;
    callback_error=native_entry_error=0;callback_result=selected;
    std::memset(callback_entry_fx,0,sizeof callback_entry_fx);
    std::memset(native_entry_fx,0,sizeof native_entry_fx);
    std::memset(returned_fx,0,sizeof returned_fx);
    SetLastError(caller_error);
    _fxrstor(caller_fx);
}

DWORD invoke_seh(void* argument,bovr::t5::ManualReloadBeginCallback callback) noexcept {
    // No destructible C++ locals in this SEH wrapper; the production native
    // exception must cross dispatch unchanged and must never be retried.
    __try {bovr::t5::dispatch_manual_reload_begin(argument,callback,native_throws_seh);}
    __except(GetExceptionCode()==native_seh_code?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {
        return GetExceptionCode();
    }
    return 0;
}

void assert_callback(bool present,void* expected_argument) {
    check(callback_calls==(present?1u:0u),"callback executes exactly once only when present");
    if(present){
        check(callback_argument==expected_argument,"callback receives exact original pmove argument");
        check(callback_error==caller_error,"callback starts with caller LastError");
        check(!std::memcmp(callback_entry_fx,caller_fx,sizeof caller_fx),"callback starts with complete caller FX state");
    }
}
void assert_native(unsigned expected_calls,void* expected_argument) {
    check(native_calls==expected_calls,"native reload has exact zero-or-one call count");
    if(expected_calls){
        check(native_argument==expected_argument,"native receives original pmove pointer unchanged");
        check(native_entry_error==caller_error,"callback LastError changes do not reach native");
        check(!std::memcmp(native_entry_fx,caller_fx,sizeof caller_fx),"callback full FX changes do not reach native");
    }
}

void returning_paths() {
    std::array<unsigned char,80> payload{};
    for(unsigned i=0;i<payload.size();++i)payload[i]=static_cast<unsigned char>(3u*i+1u);
    const auto original=payload;
    // Include null and interior borrowed pointers: dispatch must not invent,
    // dereference, adjust or copy the native argument itself.
    for(unsigned pointer_kind=0;pointer_kind<3;++pointer_kind){
        void* argument=pointer_kind==0?nullptr:payload.data()+(pointer_kind==1?0:7);
        for(unsigned mode=0;mode<3;++mode){
            const bool present=mode!=0,suppress=mode==2;
            reset(suppress);
            bovr::t5::dispatch_manual_reload_begin(argument,present?deciding_callback:nullptr,native_returns);
            _fxsave(returned_fx);const auto returned_error=GetLastError();
            _fxrstor(saved_process_fx);
            assert_callback(present,argument);assert_native(suppress?0u:1u,argument);
            check(returned_error==(suppress?caller_error:native_error),"suppression preserves caller error; pass-through preserves native output error");
            check(!std::memcmp(returned_fx,suppress?caller_fx:native_fx,sizeof returned_fx),
                "suppression preserves caller FX; pass-through preserves full native output FX");
            check(payload==original,"dispatcher does not modify borrowed pmove storage");
        }
    }
}

void exceptional_paths() {
    unsigned payload=0xa71329c4;
    for(unsigned mode=0;mode<3;++mode){
        const bool present=mode!=0,suppress=mode==2;
        reset(suppress);int caught{};
        try {bovr::t5::dispatch_manual_reload_begin(&payload,present?deciding_callback:nullptr,native_throws_cpp);}
        catch(int value){caught=value;}
        _fxrstor(saved_process_fx);
        check(caught==(suppress?0:73),"native C++ exception propagates unchanged only when native runs");
        assert_callback(present,&payload);assert_native(suppress?0u:1u,&payload);
        check(payload==0xa71329c4,"native C++ unwind does not change argument storage");

        reset(suppress);const auto exception=invoke_seh(&payload,present?deciding_callback:nullptr);
        _fxrstor(saved_process_fx);
        check(exception==(suppress?0u:native_seh_code),"native SEH exception propagates once and is suppressed only with native call");
        assert_callback(present,&payload);assert_native(suppress?0u:1u,&payload);
        check(payload==0xa71329c4,"native SEH unwind does not change argument storage");
    }
}
}

int main() {
    make_states();returning_paths();exceptional_paths();_fxrstor(saved_process_fx);
    std::printf("manual reload begin dispatcher: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
