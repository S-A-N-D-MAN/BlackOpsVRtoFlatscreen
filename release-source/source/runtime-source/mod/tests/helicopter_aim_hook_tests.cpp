// SPDX-License-Identifier: GPL-3.0-only
// Execute the production five-byte CALL and naked tail bridge in owned memory.
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/helicopter_aim_hook.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#include <cstdio>
#include <stdexcept>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&){return {true,"fixture","fixture"};}
bool validate_loaded_headers(HMODULE,std::string&){return true;}
std::wstring executable_path(){return L"private fixture";}
}
namespace h=bovr::t5::helicopter_aim_detail;
namespace {
unsigned checks{},failures{},callbacks{},native_calls{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
std::uintptr_t fixture_site{},callback_values[6]{},callback_stack_return{},entry_return{};
std::uint32_t entry_args[7]{},entry_regs[7]{},output_regs[7]{},entry_flags{},output_flags{},native_flags{},entry_error{};
__declspec(align(16)) unsigned char caller_fx[512]{},entry_fx[512]{},native_fx[512]{},output_fx[512]{};
std::array<float,3> start{1,2,3},end{4,5,6},target{4,5,6};
bool redirect{};
__declspec(naked) void synthetic_trace(){
    __asm {
        mov dword ptr [entry_regs],eax
        mov dword ptr [entry_regs+4],ebx
        mov dword ptr [entry_regs+8],ecx
        mov dword ptr [entry_regs+12],edx
        mov dword ptr [entry_regs+16],esi
        mov dword ptr [entry_regs+20],edi
        mov dword ptr [entry_regs+24],ebp
        pushfd
        pop dword ptr [entry_flags]
        fxsave [entry_fx]
        mov eax,[esp]
        mov dword ptr [entry_return],eax
        mov eax,[esp+4]
        mov dword ptr [entry_args],eax
        mov eax,[esp+8]
        mov dword ptr [entry_args+4],eax
        mov eax,[esp+12]
        mov dword ptr [entry_args+8],eax
        mov eax,[esp+16]
        mov dword ptr [entry_args+12],eax
        mov eax,[esp+20]
        mov dword ptr [entry_args+16],eax
        mov eax,[esp+24]
        mov dword ptr [entry_args+20],eax
        mov eax,[esp+28]
        mov dword ptr [entry_args+24],eax
        inc dword ptr [native_calls]
        call GetLastError
        mov dword ptr [entry_error],eax
        fld1
        pcmpeqd xmm0,xmm0
        fxsave [native_fx]
        mov eax,13579bdfh
        mov ecx,2468ace0h
        mov edx,11223344h
        stc
        pushfd
        pop dword ptr [native_flags]
        ret
    }
}
__declspec(naked) void invoke(){
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        fxsave [caller_fx]
        mov eax,11224488h
        mov ebx,01020304h
        mov ecx,88774422h
        mov edx,55667788h
        mov esi,20304050h
        mov edi,40302010h
        mov ebp,789abcdeh
        xor eax,eax
        mov eax,11224488h
        stc
        call dword ptr [fixture_site]
        mov dword ptr [output_regs],eax
        mov dword ptr [output_regs+4],ebx
        mov dword ptr [output_regs+8],ecx
        mov dword ptr [output_regs+12],edx
        mov dword ptr [output_regs+16],esi
        mov dword ptr [output_regs+20],edi
        mov dword ptr [output_regs+24],ebp
        pushfd
        pop dword ptr [output_flags]
        fxsave [output_fx]
        fxrstor [caller_fx]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void throwing_trace(void*,const float*,const float*,int,int,int,void*){throw std::runtime_error("native trace C++");}
void seh_trace(void*,const float*,const float*,int,int,int,void*){RaiseException(0xe0421234,0,0,nullptr);}
void invoke_for_unwind(){reinterpret_cast<void(*)()>(fixture_site)();}
bool invoke_seh(){__try{invoke_for_unwind();}__except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
}
namespace bovr::t5 {
void select_helicopter_aim_trace(std::uintptr_t ret,std::uintptr_t vehicle,std::uintptr_t scr,
    std::uintptr_t info,std::uintptr_t player,std::uintptr_t stack) noexcept {
    ++callbacks;callback_values[0]=ret;callback_values[1]=vehicle;callback_values[2]=scr;
    callback_values[3]=info;callback_values[4]=player;callback_values[5]=stack;
    callback_stack_return=*reinterpret_cast<const std::uintptr_t*>(stack);
    if(redirect){
        // Fixture selection changes original borrowed arrays, never arguments.
        auto** arguments=reinterpret_cast<void**>(stack+4);
        const std::array<float,3> wanted_start{10,20,30},wanted_end{40,50,60};
        std::memcpy(arguments[1],wanted_start.data(),12);
        std::memcpy(arguments[2],wanted_end.data(),12);target=wanted_end;
    }
    SetLastError(0xabcdef01);
    __asm {
        fld1
        pcmpeqd xmm0,xmm0
    }
}
}
int main(){
    const auto base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x420000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(base!=0,"owned executable fixture allocated");if(!base)return 1;
    std::array<std::uint8_t,h::trace_caller_guard.size> authored_caller{};
    authored_caller[h::call_offset]=0xe8;const auto native_call=static_cast<std::int32_t>(h::target_rva-h::call_rva-5);
    std::memcpy(authored_caller.data()+h::call_offset+1,&native_call,sizeof(native_call));
    std::memcpy(reinterpret_cast<void*>(base+h::caller_rva),authored_caller.data(),authored_caller.size());
    check(h::guards_match(base,false),"full pinned caller and complete callee accepted");
    for(auto at:{base+h::call_rva-1,base+h::call_rva,base+h::call_rva+5,base+h::target_rva}){
        auto* byte=reinterpret_cast<unsigned char*>(at);const auto saved=*byte;*byte^=1;
        check(!h::install(base),"caller neighbor call opcode or callee mutation refuses installation");*byte=saved;
    }
    SetLastError(0x12345678);
    check(h::install(base)&&h::guards_match(base,true),"actual five-byte CALL installed and owned guards accepted");
    check(h::install(base),"owned install is idempotent");
    auto unchanged=authored_caller;
    std::memcpy(unchanged.data()+h::call_offset,h::owned_call.data(),5);
    check(!std::memcmp(reinterpret_cast<void*>(base+h::caller_rva),unchanged.data(),unchanged.size()),"only the five call bytes changed");
    auto* neighbor=reinterpret_cast<unsigned char*>(base+h::call_rva+5);*neighbor^=1;
    check(!h::guards_match(base,true),"owned validation rejects later neighboring byte changes");*neighbor^=1;
    const std::array<std::uint32_t,7> args{0x11223344u,reinterpret_cast<std::uint32_t>(start.data()),
        reinterpret_cast<std::uint32_t>(end.data()),0u,0x280e091u,0u,0u};
    fixture_site=base+h::call_rva-35;
    auto* code=reinterpret_cast<unsigned char*>(fixture_site);
    for(unsigned i=0;i<7;++i){code[5*i]=0x68;std::memcpy(code+5*i+1,&args[6-i],4);}
    const unsigned char continuation[]{0x8d,0x64,0x24,0x1c,0xc3};
    std::memcpy(reinterpret_cast<void*>(base+h::call_rva+5),continuation,sizeof continuation);
    DWORD old{};check(VirtualProtect(reinterpret_cast<void*>(fixture_site),45,PAGE_EXECUTE_READ,&old)!=FALSE,"owned native-call path executable");
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(fixture_site),45);
    h::native_target=reinterpret_cast<std::uintptr_t>(&synthetic_trace);
    for(bool selected:{false,true}){
        redirect=selected;callbacks=native_calls=0;start={1,2,3};end=target={4,5,6};SetLastError(0x12345678);invoke();
        check(callbacks==1&&native_calls==1,"both selected and refused bridge call native exactly once");
        check(entry_return==base+h::call_rva+5&&callback_values[0]==entry_return,"native receives original exact call return address");
        check(callback_values[1]==0x01020304&&callback_values[2]==0x20304050&&callback_values[3]==0x789abcde&&
              callback_values[4]==0x40302010&&callback_values[5]!=0&&callback_stack_return==entry_return,
              "callback receives original EBX ESI EBP EDI and native entry ESP");
        check(!std::memcmp(entry_args,args.data(),sizeof entry_args),"all seven native argument words and vector pointers preserved");
        check(entry_regs[0]==0x11224488&&entry_regs[1]==0x01020304&&entry_regs[2]==0x88774422&&entry_regs[3]==0x55667788&&
              entry_regs[4]==0x20304050&&entry_regs[5]==0x40302010&&entry_regs[6]==0x789abcde,"all seven native GPR inputs preserved");
        check((entry_flags&0x8d5)==0x45,"incoming arithmetic flags and carry preserved");
        check(!std::memcmp(caller_fx,entry_fx,512),"all incoming x87 SSE MXCSR state preserved despite callback perturbation");
        check(entry_error==0x12345678&&GetLastError()==0x12345678,"LastError survives callback into native and caller");
        check(output_regs[0]==0x13579bdf&&output_regs[2]==0x2468ace0&&output_regs[3]==0x11223344&&
              output_regs[1]==entry_regs[1]&&output_regs[4]==entry_regs[4]&&output_regs[5]==entry_regs[5]&&output_regs[6]==entry_regs[6],
              "native outputs and nonvolatile registers return unchanged");
        check(output_flags==native_flags&&!std::memcmp(native_fx,output_fx,512),"native outgoing flags and FX state survive tail return");
        check(start==(selected?std::array<float,3>{10,20,30}:std::array<float,3>{1,2,3})&&
              end==(selected?std::array<float,3>{40,50,60}:std::array<float,3>{4,5,6})&&target==end,
              "selected original ray and target persist after trace; refusal leaves them unchanged");
    }
    redirect=false;h::native_target=reinterpret_cast<std::uintptr_t>(&throwing_trace);bool caught=false;
    try{invoke_for_unwind();}catch(const std::runtime_error&){caught=true;}check(caught,"native C++ exception unwinds through unchanged tail stack");
    h::native_target=reinterpret_cast<std::uintptr_t>(&seh_trace);check(invoke_seh(),"native SEH reaches original caller through tail bridge");
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::printf("helicopter aim hook: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
