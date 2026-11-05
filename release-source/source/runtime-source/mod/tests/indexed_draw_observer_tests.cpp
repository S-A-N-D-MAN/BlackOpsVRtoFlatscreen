// SPDX-License-Identifier: GPL-3.0-only
// Actual slot82/observer implementation over private COM vtables; CPU only.
#include "../src/loader/d3d9_observer.cpp"
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
using namespace bovr;
unsigned checks{},failures{},native_calls{},before_calls{},after_calls{},abandon_calls{},com_calls{};
bool before_fault{},after_fault{},abandon_fault{},nested_before{},nested_after{},nested_native{},nested_abandon{};
bool native_seh{},native_cpp{},block_native{},zero_token{};
bool deactivate_before{};
bool nested_unknown{};
std::uint64_t (*volatile accessor_target)() noexcept=&indexed_draw_reentry_serial;
constexpr DWORD fault_code=0xe047c111,entry_error=0x13572468,return_error=0xabcdef01;
constexpr std::uint64_t expected_token=0x123456789abcdef0ULL;
HRESULT native_result=static_cast<HRESULT>(0x88760868L);
struct Fake {void** table{};};
Fake owned{},unknown{};
IndexedDrawCall copied_call{};
IDirect3DDevice9* native_device{};
std::array<std::uint32_t,6> native_args{};
DWORD native_error{};
void* invoke_target{};
unsigned invoke_device{},saved_stack{},stack_after{},returned_hresult{},returned_error{},returned_esi{},returned_edi{},returned_ebx{},returned_ebp{};
std::uintptr_t expected_caller{};
alignas(16) unsigned char input_fp[512]{},output_fp[512]{},dirty_fp[512]{},caller_fp[512]{},native_fp[512]{},returned_fp[512]{};
HANDLE entered{},proceed{};
struct NativeCpp {unsigned value;};
struct alignas(16) ExceptionState {
    DWORD code{},flags{},error{},count{};void* address{};ULONG_PTR parameters[2]{};DWORD padding{};
    __declspec(align(16)) unsigned char fp[512]{};
};
ExceptionState observed_exception{},direct_exception{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
void compare_fp(const unsigned char* a,const unsigned char* b,const char* why){
    // Reserved FXSAVE bytes and instruction/data pointers are not FP values.
    for(unsigned i=0;i<5;++i)check(a[i]==b[i],why);
    for(unsigned i=24;i<28;++i)check(a[i]==b[i],why);
    for(unsigned n=0;n<8;++n)for(unsigned b0=0;b0<10;++b0)check(a[32+16*n+b0]==b[32+16*n+b0],why);
    for(unsigned i=160;i<288;++i)check(a[i]==b[i],why);
}
void initialize_fp(){
    __asm {
        fxsave caller_fp
        fninit
        fld1
        fldpi
        fldl2e
        fldln2
        fldlg2
        fldl2t
        fldz
        fld1
        fxsave input_fp
        fxrstor caller_fp
    }
    for(unsigned i=160;i<288;++i)input_fp[i]=static_cast<unsigned char>(i*7+13);
    std::memcpy(output_fp,input_fp,512);std::memcpy(dirty_fp,input_fp,512);
    const unsigned in_mx=0x3f80,out_mx=0x5f80,dirty_mx=0x7f80;
    const unsigned short in_cw=0x077f,out_cw=0x0b7f,dirty_cw=0x0f7f;
    std::memcpy(input_fp,&in_cw,2);std::memcpy(input_fp+24,&in_mx,4);
    std::memcpy(output_fp,&out_cw,2);std::memcpy(output_fp+24,&out_mx,4);
    std::memcpy(dirty_fp,&dirty_cw,2);std::memcpy(dirty_fp+24,&dirty_mx,4);
    for(unsigned i=160;i<288;++i){output_fp[i]^=0x5a;dirty_fp[i]^=0xa5;}
}
// Concrete caller verifies __stdcall stack cleanup, actual return address,
// nonvolatile GP registers and untouched input/native-return FP state.
__declspec(naked) void invoke(){
    __asm {
        pushad
        mov saved_stack,esp
        mov esi,0x1234abcd
        mov edi,0x5678ef90
        mov ebx,0x2345bcde
        mov ebp,0x6789fa01
        fxrstor input_fp
        push 0x13572468
        call SetLastError
        push 0xf1234567
        push 0xa1234567
        push 0x71234567
        push 0x91234567
        push -1234567
        push 4
        push invoke_device
        mov eax,offset native_return
        mov expected_caller,eax
        call invoke_target
    native_return:
        mov returned_hresult,eax
        fxsave returned_fp
        mov stack_after,esp
        mov returned_esi,esi
        mov returned_edi,edi
        mov returned_ebx,ebx
        mov returned_ebp,ebp
        call GetLastError
        mov returned_error,eax
        fxrstor caller_fp
        popad
        ret
    }
}
void nested(){
    auto* d=reinterpret_cast<IDirect3DDevice9*>(nested_unknown?&unknown:&owned);
    d->DrawIndexedPrimitive(D3DPT_POINTLIST,7,8,9,10,11);
}
std::uint64_t before(void* context,IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept;
void after(void*,std::uint64_t,HRESULT) noexcept;
void abandon(void*,std::uint64_t) noexcept;
IndexedDrawCallbacks tuple(){return {&owned,before,after,abandon};}
void reject_replacement(){
    check(!set_indexed_draw_callbacks({}),"unbind refused throughout copied callback lease");
    auto other=tuple();other.context=&unknown;
    check(!set_indexed_draw_callbacks(other),"context replacement refused throughout copied lease");
}
std::uint64_t before(void* context,IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept {
    ++before_calls;check(context==&owned&&device==reinterpret_cast<IDirect3DDevice9*>(&owned),"before uses exact registered context/device");
    copied_call=call;reject_replacement();
    if(deactivate_before)set_observation_active(false);
    if(nested_before){nested_before=false;nested();}
    if(before_fault)RaiseException(fault_code,0,0,nullptr);
    _fxrstor(dirty_fp);SetLastError(0x99999999);return zero_token?0:expected_token;
}
void after(void* context,std::uint64_t token,HRESULT result) noexcept {
    ++after_calls;check(context==&owned&&token==expected_token&&result==native_result,"after matches exact tuple/token/native HRESULT");
    reject_replacement();if(nested_after){nested_after=false;nested();}
    if(after_fault)RaiseException(fault_code,0,0,nullptr);
    _fxrstor(dirty_fp);SetLastError(0x77777777);
}
void abandon(void* context,std::uint64_t token) noexcept {
    ++abandon_calls;check(context==&owned&&token==expected_token,"abandon matches exact tuple/token");
    const auto prior=com_calls;reject_replacement();
    if(nested_abandon){nested_abandon=false;const bool old=native_seh;native_seh=false;nested();native_seh=old;}
    check(com_calls==prior,"abandon makes no COM resource calls");
    if(abandon_fault)RaiseException(fault_code,0,0,nullptr);
    _fxrstor(dirty_fp);SetLastError(0x66666666);
}
HRESULT STDMETHODCALLTYPE original_draw(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,INT base,UINT min,UINT num,UINT start,UINT count0){
    _fxsave(native_fp);native_error=GetLastError();++native_calls;
    native_device=d;native_args={static_cast<unsigned>(type),static_cast<unsigned>(base),min,num,start,count0};
    if(indexed_in_flight.load())reject_replacement();
    if(nested_native){nested_native=false;nested();}
    if(block_native){SetEvent(entered);WaitForSingleObject(proceed,10000);}
    if(native_cpp){_fxrstor(output_fp);SetLastError(return_error);throw NativeCpp{0x1234};}
    if(native_seh){
        const ULONG_PTR parameters[]{0x12345678,0x87654321};
        _fxrstor(output_fp);SetLastError(return_error);RaiseException(fault_code,0,2,parameters);
    }
    _fxrstor(output_fp);SetLastError(return_error);return native_result;
}
ULONG STDMETHODCALLTYPE fake_release(IUnknown*){++com_calls;return 2;}
HRESULT STDMETHODCALLTYPE fake_get(IDirect3DDevice9*,UINT,IDirect3DSwapChain9** out){++com_calls;*out=nullptr;return E_FAIL;}
HRESULT STDMETHODCALLTYPE fake_create(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9**){++com_calls;return E_FAIL;}
HRESULT STDMETHODCALLTYPE fake_reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*){++com_calls;return S_OK;}
HRESULT STDMETHODCALLTYPE fake_present(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*){++com_calls;return S_OK;}
void* native_exception_address{};
LONG exception_filter(EXCEPTION_POINTERS* p){
    _fxsave(observed_exception.fp);observed_exception.error=GetLastError();
    observed_exception.code=p->ExceptionRecord->ExceptionCode;observed_exception.flags=p->ExceptionRecord->ExceptionFlags;
    observed_exception.address=p->ExceptionRecord->ExceptionAddress;observed_exception.count=p->ExceptionRecord->NumberParameters;
    for(unsigned i=0;i<2&&i<observed_exception.count;++i)observed_exception.parameters[i]=p->ExceptionRecord->ExceptionInformation[i];
    native_exception_address=p->ExceptionRecord->ExceptionAddress;
    check(p->ExceptionRecord->ExceptionCode==fault_code,"native SEH code propagates unchanged");
    check(GetLastError()==return_error,"native SEH LastError restored after abandon");
    return EXCEPTION_EXECUTE_HANDLER;
}
void invoke_seh(bool direct=false){
    __try {
        if(direct)original_draw(reinterpret_cast<IDirect3DDevice9*>(&owned),D3DPT_TRIANGLELIST,1,2,3,4,5);
        else reinterpret_cast<IDirect3DDevice9*>(&owned)->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,1,2,3,4,5);
    }
    __except(exception_filter(GetExceptionInformation())){}
}
void reset_flags(){before_fault=after_fault=abandon_fault=nested_before=nested_after=nested_native=nested_abandon=native_seh=native_cpp=block_native=zero_token=deactivate_before=nested_unknown=false;}
bool invoke_cpp_control(IDirect3DDevice9* device,bool direct,ExceptionState* state){
    try {
        if(direct)original_draw(device,D3DPT_POINTLIST,1,2,3,4,5);
        else device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);
    }catch(const NativeCpp& e){
        _fxsave(state->fp);state->error=GetLastError();return e.value==0x1234;
    }
    return false;
}
void accessor_fp(std::uint64_t expected){
    alignas(16) unsigned char observed_fp[512]{};
    _fxrstor(input_fp);SetLastError(entry_error);
    const auto actual=accessor_target(); // Actual out-of-line ABI, not an inlined model.
    _fxsave(observed_fp);const auto error=GetLastError();_fxrstor(caller_fp);
    check(actual==expected&&error==entry_error,"integer TLS accessor preserves value and LastError");
    compare_fp(observed_fp,input_fp,"integer TLS accessor preserves x87/XMM/MXCSR");
}
void zero_token_tests(IDirect3DDevice9* device){
    const auto old_result=native_result;
    for(HRESULT result:{S_OK,E_FAIL,D3DERR_DEVICELOST})for(unsigned mode=0;mode<6;++mode){
        reset_flags();set_observation_active(true);
        check(set_indexed_draw_callbacks(tuple()),"zero-token fixture binds while idle");
        zero_token=true;before_fault=mode==1;nested_native=mode==5;
        if(mode==2)set_observation_active(false);
        if(mode==3)check(set_indexed_draw_callbacks({}),"zero-token unbound control");
        invoke_device=reinterpret_cast<unsigned>(mode==4?reinterpret_cast<IDirect3DDevice9*>(&unknown):device);
        native_result=result;
        const auto n=native_calls,a=after_calls,b=before_calls,q=abandon_calls,com=com_calls;
        const auto serial=indexed_draw_reentry_serial();
        invoke();
        check(native_calls==n+(mode==5?2:1)&&before_calls==b+((mode<2||mode==5)?1:0),"zero-token native forwarding remains once per invocation");
        check(after_calls==a&&abandon_calls==q&&com_calls==com,"zero-token completion performs no callback or COM resource operation");
        check(!indexed_depth&&!indexed_in_flight.load(),"zero-token normal return retires the complete lease");
        check(indexed_draw_reentry_serial()==serial+(mode==5?1:0),"zero-token nested native call retains reentry witness");
        check(returned_hresult==static_cast<unsigned>(result)&&returned_error==return_error,"zero-token success/failure preserves HRESULT and native LastError");
        check(saved_stack==stack_after&&returned_esi==0x1234abcd&&returned_edi==0x5678ef90&&returned_ebx==0x2345bcde&&returned_ebp==0x6789fa01,"zero-token native return preserves stdcall and GP state");
        if(mode!=5)compare_fp(native_fp,input_fp,"zero-token entry FP survives a dirty/faulted/absent before callback");
        compare_fp(returned_fp,output_fp,"zero-token integer cleanup preserves every native x87/XMM/MXCSR value");
    }
    invoke_device=reinterpret_cast<unsigned>(device);native_result=old_result;
    set_observation_active(true);check(set_indexed_draw_callbacks(tuple()),"zero-token exceptional controls rebind");
    for(unsigned mode=0;mode<3;++mode){
        reset_flags();zero_token=true;native_seh=true;before_fault=mode==1;nested_native=mode==2;
        const auto a=abandon_calls,c=after_calls,n=native_calls;
        invoke_seh();
        check(abandon_calls==a&&after_calls==c&&native_calls==n+(mode==2?2:1),"zero-token native SEH propagates with no completion callback");
        check(!indexed_depth&&!indexed_in_flight.load(),"zero-token native SEH retires the lease");
        check(observed_exception.code==direct_exception.code&&observed_exception.flags==direct_exception.flags&&
              observed_exception.address==direct_exception.address&&observed_exception.count==direct_exception.count&&
              observed_exception.parameters[0]==direct_exception.parameters[0]&&observed_exception.parameters[1]==direct_exception.parameters[1]&&
              observed_exception.error==direct_exception.error,"zero-token SEH record and LastError equal direct native control");
        compare_fp(observed_exception.fp,direct_exception.fp,"zero-token SEH FP equals direct native control");
    }
    ExceptionState cpp_direct{},cpp_observed{};
    reset_flags();native_cpp=true;
    check(invoke_cpp_control(device,true,&cpp_direct),"direct native C++ control captured");
    for(bool nested_call:{false,true}){
        reset_flags();zero_token=true;native_cpp=true;nested_native=nested_call;
        const auto a=abandon_calls,c=after_calls,n=native_calls;
        const bool caught=invoke_cpp_control(device,false,&cpp_observed);
        check(caught&&native_calls==n+(nested_call?2:1)&&abandon_calls==a&&after_calls==c&&
              !indexed_depth&&!indexed_in_flight.load(),"zero-token C++ exception object propagates and retires both nested leases");
        check(cpp_observed.error==cpp_direct.error,"zero-token C++ unwind preserves direct native LastError");
        compare_fp(cpp_observed.fp,cpp_direct.fp,"zero-token C++ unwind FP equals direct native control");
    }
    reset_flags();zero_token=true;block_native=true;ResetEvent(entered);ResetEvent(proceed);
    std::thread worker([&]{device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);});
    check(WaitForSingleObject(entered,10000)==WAIT_OBJECT_0,"zero-token foreign thread reached native call");
    check(!set_indexed_draw_callbacks({})&&!set_indexed_draw_callbacks(tuple()),"zero-token owns callback tuple until native return");
    SetEvent(proceed);worker.join();
    check(set_indexed_draw_callbacks(tuple())&&!indexed_in_flight.load(),"zero-token native return releases callback tuple for replacement");
    reset_flags();
}
void reentry_tests(IDirect3DDevice9* device){
    reset_flags();check(set_indexed_draw_callbacks(tuple()),"witness tests rebind complete callback tuple");
    accessor_fp(indexed_draw_reentry_serial());
    const auto old_result=native_result;
    for(HRESULT result:{S_OK,E_FAIL})for(unsigned mode=0;mode<6;++mode){
        reset_flags();native_result=result;
        nested_before=mode==1||mode==4||mode==5;nested_after=mode==2;nested_native=mode==3;
        nested_unknown=mode==4;deactivate_before=mode==5;
        const auto serial=indexed_draw_reentry_serial();const auto n=native_calls,b=before_calls,a=after_calls;
        invoke();
        check(indexed_draw_reentry_serial()==serial+(mode?1:0),"only suppressed nested slot82 increments witness once");
        check(native_calls==n+(mode?2:1)&&before_calls==b+1&&after_calls==a+1,"successful/failing native calls retain one outer tuple and one call per invocation");
        check(returned_hresult==static_cast<unsigned>(result)&&returned_error==return_error,"witness preserves success/failure HRESULT and LastError");
        check(saved_stack==stack_after&&returned_esi==0x1234abcd&&returned_edi==0x5678ef90&&returned_ebx==0x2345bcde&&returned_ebp==0x6789fa01,"nested witness preserves stdcall and nonvolatile GP");
        compare_fp(returned_fp,output_fp,"nested witness preserves native-return FP");
        set_observation_active(true);
    }
    native_result=old_result;reset_flags();
    auto serial=indexed_draw_reentry_serial();
    reinterpret_cast<IDirect3DDevice9*>(&unknown)->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);
    set_observation_active(false);invoke();set_observation_active(true);
    check(set_indexed_draw_callbacks({}),"witness tests unbind when no lease");invoke();
    check(indexed_draw_reentry_serial()==serial,"top-level unknown/inactive/unbound forwarding does not mint reentry");
    check(set_indexed_draw_callbacks(tuple()),"witness tests rebind after nonnested controls");
    reset_flags();zero_token=true;nested_before=true;const auto after0=after_calls;invoke();
    check(indexed_draw_reentry_serial()==serial+1&&after_calls==after0,"token0 still counts nested before but skips completion");
    reset_flags();native_seh=true;nested_native=true;serial=indexed_draw_reentry_serial();
    const auto n=native_calls,a=abandon_calls,b=before_calls;invoke_seh();
    check(indexed_draw_reentry_serial()==serial+1&&native_calls==n+2&&before_calls==b+1&&abandon_calls==a+1,"nested native SEH counts once and abandons only outer lease");
    check(observed_exception.code==direct_exception.code&&observed_exception.flags==direct_exception.flags&&observed_exception.address==direct_exception.address&&observed_exception.count==direct_exception.count&&observed_exception.parameters[0]==direct_exception.parameters[0]&&observed_exception.parameters[1]==direct_exception.parameters[1]&&observed_exception.error==direct_exception.error,"nested native SEH record unchanged from direct original");
    compare_fp(observed_exception.fp,direct_exception.fp,"nested native SEH FP unchanged from direct original");
    reset_flags();native_cpp=true;nested_native=true;serial=indexed_draw_reentry_serial();
    const auto cpp_native=native_calls,cpp_abandon=abandon_calls;bool caught=false;
    try {device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);}
    catch(const NativeCpp& e){caught=e.value==0x1234;}
    check(caught&&indexed_draw_reentry_serial()==serial+1&&native_calls==cpp_native+2&&abandon_calls==cpp_abandon+1&&!indexed_depth&&!indexed_in_flight.load(),"nested native C++ object propagates and witness/lease retire correctly");
    reset_flags();serial=indexed_draw_reentry_serial();std::uint64_t worker_before{},worker_after{};
    nested_before=true;
    std::thread other([&]{worker_before=indexed_draw_reentry_serial();device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);worker_after=indexed_draw_reentry_serial();});
    other.join();
    check(worker_before==0&&worker_after==1&&indexed_draw_reentry_serial()==serial,"foreign thread starts0 and cannot alter current thread witness");
    reset_flags();
    std::thread fresh([&]{worker_before=indexed_draw_reentry_serial();accessor_fp(0);});fresh.join();
    check(worker_before==0&&indexed_draw_reentry_serial()==serial,"later thread has independent zero-initialized lifetime");
    // Test-only access to the included implementation: no production setter.
    indexed_reentries=indexed_draw_reentry_exhausted-1;nested_before=true;invoke();
    check(indexed_draw_reentry_serial()==indexed_draw_reentry_exhausted,"last representable increment enters permanently unusable sentinel");
    reset_flags();nested_native=true;invoke();
    check(indexed_draw_reentry_serial()==indexed_draw_reentry_exhausted,"overflow saturates, never wraps to apparently unchanged ordinary serial");
    set_observation_active(false);set_observation_active(true);check(set_indexed_draw_callbacks({}),"saturated thread can still retire original callback registration");
    check(set_indexed_draw_callbacks(tuple())&&indexed_draw_reentry_serial()==indexed_draw_reentry_exhausted,"disable/unbind/rebind never rearms saturated witness");
    const auto poisoned_before=indexed_draw_reentry_serial(),poisoned_after=indexed_draw_reentry_serial();
    const bool unchanged_usable=poisoned_before!=indexed_draw_reentry_exhausted&&poisoned_after!=indexed_draw_reentry_exhausted&&poisoned_before==poisoned_after;
    check(!unchanged_usable,"equal poisoned snapshots cannot grant current-call authority");
    accessor_fp(indexed_draw_reentry_exhausted);reset_flags();invoke();
    check(returned_hresult==static_cast<unsigned>(old_result)&&indexed_draw_reentry_serial()==indexed_draw_reentry_exhausted,"poison does not suppress or alter top-level native draw");
    check(set_indexed_draw_callbacks({}),"witness tests leave callback registration idle");
}
}
namespace bovr {void trace(const char*,const char*,...) noexcept {SetLastError(0x88888888);}}
int main(){
    initialize_fp();
    accessor_fp(0);
    auto** table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    check(table!=nullptr,"private COM table allocation");if(!table)return 1;
    owned.table=unknown.table=table;
    table[2]=reinterpret_cast<void*>(&fake_release);table[13]=reinterpret_cast<void*>(&fake_create);table[14]=reinterpret_cast<void*>(&fake_get);
    table[16]=reinterpret_cast<void*>(&fake_reset);table[17]=reinterpret_cast<void*>(&fake_present);table[82]=reinterpret_cast<void*>(&original_draw);
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&owned);
    check(!set_indexed_draw_callbacks(tuple()),"inactive cannot register indexed observer");
    set_observation_active(true);check(observe_device(device,false),"default observer fixture installed");
    check(table[82]==reinterpret_cast<void*>(&original_draw),"default off leaves slot82 native");
    check(!set_indexed_draw_callbacks({&owned,before,nullptr,abandon}),"partial callback tuple rejected");
    check(set_indexed_draw_callbacks(tuple()),"complete separate callback tuple accepted");
    DWORD previous{};VirtualProtect(table,4096,PAGE_READONLY,&previous);
    check(observe_device(device,false),"opt-in slot82 installation succeeds");
    MEMORY_BASIC_INFORMATION info{};VirtualQuery(table,&info,sizeof(info));
    check(info.Protect==PAGE_READONLY&&table[82]==reinterpret_cast<void*>(&indexed_draw),"slot install restores readonly table protection");
    invoke_device=reinterpret_cast<unsigned>(device);invoke_target=table[82];
    const auto initial_native=native_calls;invoke();
    check(native_calls==initial_native+1&&before_calls==1&&after_calls==1&&!abandon_calls,"one native call paired with before/after only");
    check(native_device==device&&native_args==std::array<std::uint32_t,6>{4,static_cast<unsigned>(-1234567),0x91234567,0x71234567,0xa1234567,0xf1234567},"all seven COM arguments forward exact signed/unsigned bits");
    check(copied_call.caller==expected_caller&&copied_call.type==4&&copied_call.base_vertex==-1234567&&copied_call.min_vertex==0x91234567&&copied_call.num_vertices==0x71234567&&copied_call.start_index==0xa1234567&&copied_call.primitive_count==0xf1234567,"before captures exact caller and value arguments");
    check(native_error==entry_error&&returned_error==return_error&&returned_hresult==static_cast<unsigned>(native_result),"entry/native-return LastError and HRESULT preserved");
    check(saved_stack==stack_after&&returned_esi==0x1234abcd&&returned_edi==0x5678ef90&&returned_ebx==0x2345bcde&&returned_ebp==0x6789fa01,"stdcall stack and nonvolatile GP preserved");
    compare_fp(native_fp,input_fp,"native receives exact input FP despite before perturbation");
    compare_fp(returned_fp,output_fp,"caller receives exact native FP despite after perturbation");
    for(unsigned mode=0;mode<5;++mode){reset_flags();
        before_fault=mode==0;after_fault=mode==1;zero_token=mode==2;nested_before=mode==3;nested_after=mode==4;
        const auto a=after_calls,b=before_calls,n=native_calls;invoke();
        check(before_calls==b+1&&after_calls==a+(mode==0||mode==2?0:1),"callback fault/token0/nested callback pairing is bounded");
        check(native_calls==n+(mode>=3?2:1)&&indexed_in_flight.load()==0&&!indexed_depth,"nested draw still native once and lease retires");
        compare_fp(returned_fp,output_fp,"callback variations preserve final native FP");
    }
    reset_flags();nested_native=true;const auto n=native_calls,b=before_calls;invoke();
    check(native_calls==n+2&&before_calls==b+1,"native nested draw gets no second callback scope");
    reset_flags();
    const auto b0=before_calls;reinterpret_cast<IDirect3DDevice9*>(&unknown)->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);
    check(before_calls==b0,"untracked shared-vtable device forwards without diagnostic");
    set_observation_active(false);invoke();check(before_calls==b0,"inactive installed observer forwards only");set_observation_active(true);
    check(set_indexed_draw_callbacks({}),"unbind after all leases retires");invoke();check(before_calls==b0,"unbound installed observer forwards only");
    check(set_indexed_draw_callbacks(tuple()),"callback registration can resume when idle");
    deactivate_before=true;const auto after_before=after_calls;invoke();
    check(after_calls==after_before+1,"deactivation during before cannot replace the captured completion tuple");
    set_observation_active(true);reset_flags();native_seh=true;invoke_seh(true);direct_exception=observed_exception;
    for(unsigned mode=0;mode<3;++mode){reset_flags();native_seh=true;abandon_fault=mode==1;nested_abandon=mode==2;
        const auto a=abandon_calls,c=after_calls,q=com_calls;invoke_seh();
        check(native_exception_address&&abandon_calls==a+1&&after_calls==c&&com_calls==q,"native SEH invokes only scalar abandon and propagates");
        check(indexed_in_flight.load()==0&&!indexed_depth,"native SEH retires context lease and nesting");
        check(set_indexed_draw_callbacks(tuple()),"native SEH leaves registration available");
        check(observed_exception.code==direct_exception.code&&observed_exception.flags==direct_exception.flags&&
              observed_exception.address==direct_exception.address&&observed_exception.count==direct_exception.count&&
              observed_exception.parameters[0]==direct_exception.parameters[0]&&observed_exception.parameters[1]==direct_exception.parameters[1]&&
              observed_exception.error==direct_exception.error,"native SEH record and parameters equal unobserved direct control");
        compare_fp(observed_exception.fp,direct_exception.fp,"native SEH FP equals unobserved direct control");
    }
    reset_flags();native_cpp=true;bool caught=false;const auto a=abandon_calls;
    try {device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);}catch(const NativeCpp& e){caught=e.value==0x1234;}
    check(caught&&abandon_calls==a+1&&!indexed_in_flight.load()&&!indexed_depth,"native C++ object propagates unchanged through finally");
    reset_flags();entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);proceed=CreateEventW(nullptr,TRUE,FALSE,nullptr);block_native=true;
    std::thread worker([&]{device->DrawIndexedPrimitive(D3DPT_POINTLIST,1,2,3,4,5);});
    check(WaitForSingleObject(entered,10000)==WAIT_OBJECT_0,"foreign-thread test entered native original");
    check(!set_indexed_draw_callbacks({})&&!set_indexed_draw_callbacks(tuple()),"foreign-thread replacement blocked while original runs");
    SetEvent(proceed);worker.join();check(set_indexed_draw_callbacks({}),"unbind succeeds after foreign original and after finish");
    zero_token_tests(device);reentry_tests(device);
    CloseHandle(entered);CloseHandle(proceed);VirtualFree(table,0,MEM_RELEASE);_fxrstor(caller_fp);
    std::printf("indexed_draw_observer checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
