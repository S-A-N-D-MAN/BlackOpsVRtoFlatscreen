// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_shadow_state.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <initializer_list>
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
template<class T>T read(std::uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof v);return v;}
constexpr std::size_t extent=0x4240000;
struct Fixture {
    std::uintptr_t base{},buffer{},views{};std::uint32_t frame=19;
    Fixture(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,extent,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!base)return;buffer=base+0x3740100;views=base+0x420f730;
        put(base+0x373708c,static_cast<std::uint32_t>(buffer));put(base+0x3a5b5f8,static_cast<std::uint32_t>(buffer));
        put(buffer+0x15d7a0,frame);put(buffer+0x16cc3c,3u);put(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        put(base+0x356a24c,static_cast<std::uint32_t>(base+0x1000));put(base+0x10fc,1u);
        for(unsigned i=0;i<4;++i)put(views+i*0x38e0+0x3510,static_cast<std::uint32_t>(buffer));
        put(views+2*0x38e0+0x38c4,std::uint8_t{1});
        for(unsigned i=0;i<8;++i){std::array<std::uint8_t,shadow_light_bytes> light{};light[0]=2;
            put(buffer+0x16cf20+i*shadow_light_bytes,light);put(slot(i),17u+i);}}
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    std::uintptr_t slot(unsigned i)const{return buffer+0x16cf20+i*shadow_light_bytes+0x38;}
    EyeShadowState build(unsigned eye,unsigned active,std::uint8_t value,unsigned spotCount=1,bool thermal=false){
        std::array<std::uint8_t,shadow_sun_bytes> sun{};sun.fill(value);std::uint32_t two=2;std::memcpy(sun.data()+0xa0,&two,4);
        std::array<std::uint32_t,8> mask{};mask[0]=(1u<<1);
        std::array<std::uint8_t,shadow_technique_bytes> tech{};tech.fill(value);
        std::array<std::uint8_t,4*shadow_spot_bytes> spots{};
        for(unsigned i=0;i<spotCount;++i){auto* record=spots.data()+i*shadow_spot_bytes;
            record[0x180]=static_cast<std::uint8_t>(active+i);mask[0]|=1u<<(active+i);
            const auto pointer=static_cast<std::uint32_t>(buffer+0x16cf20+(active+i)*shadow_light_bytes);
            std::memcpy(record+0x184,&pointer,4);put(slot(active+i),i);}
        put(buffer+0x183f40,sun);put(buffer+0x15d780,mask);put(buffer+0x88500,tech);put(buffer+0x1843a0,spots);
        put(buffer+0x184390,spotCount);put(buffer+0x183db0,8u);
        std::memcpy(reinterpret_cast<void*>(views+eye*0x38e0+0x2980),sun.data()+0x80,32);
        put(buffer+0x16cc38,eye);EyeShadowState out{};
        check(thermal?capture_thermal_shadow(base,buffer,frame,&out):capture_eye_shadow(base,buffer,frame,eye,&out),
            "capture selected frontend eye with exact constants and buffer-local spots");return out;
    }
};
void throws(void*){throw std::runtime_error("native C++ unwind fixture");}
void seh(void*){RaiseException(0xe0421001,0,0,nullptr);}
bool invoke_seh(NativeShadowScope* scope){
    __try {invoke_shadow_native(scope,seh,nullptr);}
    __except(GetExceptionCode()==0xe0421001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
std::uintptr_t fixture_site{};
volatile unsigned bridge_calls{};
std::array<std::uint32_t,8> native_entry_regs{},bridge_return_regs{};
std::array<std::uint32_t,8> native_views{},native_sun_words{};
std::uint32_t native_entry_flags{},bridge_return_flags{};
__declspec(align(16)) std::uint8_t caller_fx[512]{},native_entry_fx[512]{},native_return_fx[512]{},bridge_return_fx[512]{};
__declspec(naked) void synthetic_inner() {
    __asm {
        mov dword ptr [native_entry_regs+0],eax
        mov dword ptr [native_entry_regs+4],ebx
        mov dword ptr [native_entry_regs+8],ecx
        mov dword ptr [native_entry_regs+12],edx
        mov dword ptr [native_entry_regs+16],esi
        mov dword ptr [native_entry_regs+20],edi
        mov dword ptr [native_entry_regs+24],ebp
        pushfd
        pop dword ptr [native_entry_flags]
        fxsave [native_entry_fx]
        mov edx,dword ptr [bridge_calls]
        mov dword ptr [native_views+edx*4],eax
        mov ecx,dword ptr [eax+3510h]
        mov ecx,dword ptr [ecx+183f40h]
        mov dword ptr [native_sun_words+edx*4],ecx
        inc dword ptr [bridge_calls]
        mov eax,13579bdfh
        mov ecx,2468ace0h
        mov edx,11223344h
        fld1
        pcmpeqd xmm0,xmm0
        fxsave [native_return_fx]
        stc
        ret
    }
}
__declspec(naked) void call_fixture_site(std::uintptr_t) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        fxsave [caller_fx]
        mov eax,dword ptr [ebp+8]
        mov ebx,01020304h
        mov ecx,11224488h
        mov edx,88774422h
        mov esi,10203040h
        mov edi,40302010h
        stc
        call dword ptr [fixture_site]
        mov dword ptr [bridge_return_regs+0],eax
        mov dword ptr [bridge_return_regs+4],ebx
        mov dword ptr [bridge_return_regs+8],ecx
        mov dword ptr [bridge_return_regs+12],edx
        mov dword ptr [bridge_return_regs+16],esi
        mov dword ptr [bridge_return_regs+20],edi
        mov dword ptr [bridge_return_regs+24],ebp
        pushfd
        pop dword ptr [bridge_return_flags]
        fxsave [bridge_return_fx]
        fxrstor [caller_fx]
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
void call_fixture_body(void* view){call_fixture_site(reinterpret_cast<std::uintptr_t>(view));}
bool invoke_seh_site(NativeShadowScope* scope,std::uintptr_t view){
    __try {invoke_shadow_native(scope,call_fixture_body,reinterpret_cast<void*>(view));}
    __except(GetExceptionCode()==0xe0421001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
void set_synthetic_native(std::uintptr_t base,void* target){
    auto* entry=reinterpret_cast<std::uint8_t*>(base+0x2d2ec0);DWORD previous{},ignored{};
    check(VirtualProtect(entry,5,PAGE_EXECUTE_READWRITE,&previous)!=0,"synthetic native entry can be prepared");
    entry[0]=0xe9;const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-(base+0x2d2ec0+5));
    std::memcpy(entry+1,&displacement,4);FlushInstructionCache(GetCurrentProcess(),entry,5);
    check(VirtualProtect(entry,5,PAGE_EXECUTE_READ,&ignored)!=0,"synthetic native entry becomes executable read-only");
}
void test_thermal_slot3() {
    Fixture f;check(f.base!=0,"thermal slot fixture allocation");if(!f.base)return;
    put(f.buffer+0x16cc3c,4u);
    const auto thermal=f.build(3,2,7,2,true);
    check(thermal.valid&&thermal.thermal_scope&&thermal.eye==3&&thermal.view==f.views+3*0x38e0,
        "explicit thermal snapshot preserves slot3 identity and scope marker");
    EyeShadowState rejected{};
    check(!capture_eye_shadow(f.base,f.buffer,f.frame,3,&rejected)&&!rejected.valid,
        "ordinary eye capture never admits thermal slot3");
    for(const auto state:{std::array<unsigned,2>{3,2},{4,2},{4,0},{5,3}}){
        put(f.buffer+0x16cc3c,state[0]);put(f.buffer+0x16cc38,state[1]);
        check(!capture_thermal_shadow(f.base,f.buffer,f.frame,&rejected)&&!rejected.valid,
            "thermal capture requires exactly four views with slot3 selected");
    }
    put(f.buffer+0x16cc3c,4u);put(f.buffer+0x16cc38,3u);
    check(!capture_thermal_shadow(f.base,f.buffer,f.frame+1,&rejected)&&!rejected.valid,
        "thermal snapshot rejects a different native frame");
    put(f.views+3*0x38e0+0x3510,static_cast<std::uint32_t>(f.buffer+0x186300));
    check(!capture_thermal_shadow(f.base,f.buffer,f.frame,&rejected)&&!rejected.valid,
        "thermal snapshot rejects a slot owned by the other native buffer");
    put(f.views+3*0x38e0+0x3510,static_cast<std::uint32_t>(f.buffer));
    const auto ordinary=f.build(1,3,2,2);
    const auto sun=read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40);
    const auto mask=read<std::array<std::uint32_t,8>>(f.buffer+0x15d780);
    const auto techniques=read<std::array<std::uint8_t,shadow_technique_bytes>>(f.buffer+0x88500);
    const auto spots=read<std::array<std::uint8_t,4*shadow_spot_bytes>>(f.buffer+0x1843a0);
    const auto slot2=read<unsigned>(f.slot(2)),slot3=read<unsigned>(f.slot(3));
    auto restored=[&]{return read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40)==sun&&
        read<std::array<std::uint32_t,8>>(f.buffer+0x15d780)==mask&&
        read<std::array<std::uint8_t,shadow_technique_bytes>>(f.buffer+0x88500)==techniques&&
        read<std::array<std::uint8_t,4*shadow_spot_bytes>>(f.buffer+0x1843a0)==spots&&
        read<unsigned>(f.slot(2))==slot2&&read<unsigned>(f.slot(3))==slot3;};
    put(f.buffer+0x16cc38,3u);
    for(unsigned kind=0;kind<3;++kind){NativeShadowScope scope;
        check(scope.begin(f.base,thermal),"thermal slot3 installs its own shadow journal");
        check(read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40)==thermal.state.sun&&
            read<std::array<std::uint8_t,shadow_technique_bytes>>(f.buffer+0x88500)==thermal.state.techniques,
            "thermal draw sees its copied sun and techniques before ordinary eyes run");
        bool completed=false;
        if(kind==0)completed=scope.finish();
        if(kind==1){try{invoke_shadow_native(&scope,throws,nullptr);}catch(const std::runtime_error&){completed=true;}}
        if(kind==2)completed=invoke_seh(&scope);
        check(completed&&scope.restored()&&!scope.pending()&&restored(),
            "thermal normal return and C++/SEH unwind restore all ordinary shadow fields and sparse slots");
    }
    for(unsigned why=0;why<5;++why){auto invalid=thermal;NativeShadowScope scope;
        if(why==0)invalid.thermal_scope=false;
        if(why==1){invalid.eye=1;invalid.view=f.views+0x38e0;}
        if(why==2)put(f.buffer+0x16cc38,1u);
        if(why==3)put(f.buffer+0x16cc3c,3u);
        if(why==4)++invalid.frame;
        check(!scope.begin(f.base,invalid)&&!scope.pending()&&restored(),
            "thermal flag, physical slot, selected view, view count, and native frame cannot be substituted");
        put(f.buffer+0x16cc3c,4u);put(f.buffer+0x16cc38,3u);
    }
    {NativeShadowScope scope;
        check(!scope.begin(f.base,ordinary)&&restored(),"ordinary eye cannot borrow thermal slot3 selection");}
    put(f.buffer+0x16cc3c,2u);put(f.buffer+0x16cc38,1u);
    {NativeShadowScope scope;check(scope.begin(f.base,ordinary),"ordinary eye journal still works after thermal scope exits");
        check(scope.finish()&&scope.good()&&restored(),"ordinary eye cleanup retains original strict journal behavior");}
}
}
int main(){
    test_thermal_slot3();
    Fixture f;check(f.base!=0,"fixture allocation succeeds");if(!f.base)return 1;
    const auto left=f.build(0,2,1);const auto right=f.build(1,3,2);put(f.slot(2),29u);
    put(f.buffer+0x16cc38,2u); // Auxiliary native order 2 then 0 uses physical eye1 then eye0.
    const auto preserved=read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40);
    for(const auto* eye:{&right,&left}){NativeShadowScope scope;
        put(f.buffer+0x16cc38,eye->eye==1?2u:0u);
        check(scope.begin(f.base,*eye),"auxiliary native order2,0 installs physical eye1,0 respectively");
        check(read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40)==eye->state.sun,"sun projection belongs to selected eye");
        check(read<std::array<std::uint32_t,8>>(f.buffer+0x15d780)==eye->state.mask,"light mask belongs to selected eye");
        check(read<std::array<std::uint8_t,shadow_technique_bytes>>(f.buffer+0x88500)==eye->state.techniques,"techniques follow same eye mask");
        check(read<std::array<std::uint8_t,4*shadow_spot_bytes>>(f.buffer+0x1843a0)==eye->state.spots,"spot layout belongs to selected eye");
        check(scope.finish()&&scope.good()&&scope.restored(),"complete journal restores normal return");
        check(read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40)==preserved&&read<unsigned>(f.slot(2))==29,"unselected light's original sparse slot restored");
        check(scope.finish(),"restoration is idempotent");}
    {NativeShadowScope s;put(f.buffer+0x16cc38,1u);
        check(!s.begin(f.base,left),"selected right view cannot install left-eye shadows");
        put(f.buffer+0x16cc38,2u);
        check(!s.begin(f.base,left),"extra2 then ordinary1 cannot install eye0 shadows");
        put(f.views+2*0x38e0+0x184,1u);
        check(!s.begin(f.base,right),"separately shadowed auxiliary camera cannot borrow ordinary eye1 setup");
        put(f.views+2*0x38e0+0x184,0u);put(f.views+2*0x38e0+0x38c4,std::uint8_t{0});
        check(!s.begin(f.base,right),"index2 without native extra dispatch cannot borrow previous eye setup");
        put(f.views+2*0x38e0+0x38c4,std::uint8_t{1});put(f.buffer+0x16cc38,0u);}
    {NativeShadowScope s;check(s.begin(f.base,left),"C++ unwind scope begins");bool caught=false;
        try{invoke_shadow_native(&s,throws,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(caught&&s.restored(),"native C++ exception propagates after exact restoration");}
    {NativeShadowScope s;check(s.begin(f.base,left),"SEH unwind scope begins");
        check(invoke_seh(&s)&&s.restored(),"native SEH propagates after finally restoration");}
    for(unsigned mutation=0;mutation<5;++mutation){auto bad=left;
        if(mutation==0)++bad.frame;if(mutation==1)bad.buffer+=0x186300;if(mutation==2)bad.state.spot_count=5;
        if(mutation==3)bad.state.light_count=256;if(mutation==4)bad.state.spots[0x180]=255;
        NativeShadowScope s;check(!s.begin(f.base,bad),"wrong frame/buffer or malformed copied bounds refuses before writing");
        check(read<std::array<std::uint8_t,shadow_sun_bytes>>(f.buffer+0x183f40)==preserved,"rejected begin leaves native sun untouched");}
    {NativeShadowScope s;put(f.slot(2)-0x38+4,1u);check(!s.begin(f.base,left),"changed buffer-local light identity rejects replay");put(f.slot(2)-0x38+4,0u);}
    {NativeShadowScope s;check(s.begin(f.base,left),"foreign-thread restoration fixture begins");bool foreign=true;
        std::thread worker([&]{foreign=s.finish();});worker.join();check(!foreign,"foreign thread cannot write journal");
        check(s.finish()&&s.restored()&&!s.good(),"original owner restores after foreign refusal and marks pair invalid");}
    {NativeShadowScope s;check(s.begin(f.base,left),"changed-native-frame fixture begins");put(f.buffer+0x15d7a0,f.frame+1);
        check(!s.finish(),"new native frame cannot receive stale restoration");put(f.buffer+0x15d7a0,f.frame);
        check(s.finish()&&!s.good(),"restoration resumes only when exact original identity is restored");}
    {NativeShadowScope s;check(s.begin(f.base,left),"selected-index restoration fixture begins");
        put(f.buffer+0x16cc38,2u);
        check(s.finish()&&s.restored()&&!s.good(),"outer native selected-index restoration still restores same-frame shadow journal");
        put(f.buffer+0x16cc38,0u);}
    {NativeShadowScope s;check(s.begin(f.base,left),"unexpected owned mutation fixture begins");put(f.buffer+0x88500,0x11111111u);
        check(s.finish()&&!s.good()&&s.restored(),"unexpected native mutation invalidates pair but journal still restores");}
    {Fixture cache;const auto eye0=cache.build(0,2,3,4),eye1=cache.build(1,2,4,4);
        for(const auto* eye:{&eye0,&eye1}){put(cache.buffer+0x16cc38,eye->eye);NativeShadowScope scope;
            check(scope.begin(cache.base,*eye),"all four spot slots admit after previous eye's native cache update");
            const std::array<float,3> native_translation{12.5f+eye->eye,-31.25f,92.75f};
            for(unsigned i=2;i<6;++i)put(cache.slot(i)-0x38+0x110,native_translation);
            check(scope.finish()&&scope.good()&&scope.restored(),"native spot viewMatrix translations do not invalidate restored ownership");
            for(unsigned i=2;i<6;++i)check(read<std::array<float,3>>(cache.slot(i)-0x38+0x110)==native_translation,
                "native three-float cache writes are retained, never restored from frontend");}
        put(cache.buffer+0x16cc38,0u);
        for(const unsigned offset:{0x10fu,0x11cu}){NativeShadowScope scope;check(scope.begin(cache.base,eye0),"adjacent-field mutation fixture begins");
            const auto address=cache.slot(2)-0x38+offset;const auto original=read<std::uint8_t>(address);put(address,std::uint8_t{0x7e});
            check(!scope.finish()&&!scope.good()&&scope.pending(),"adjacent light identity byte remains a hard restoration failure");
            check(scope.failure().address==address&&!std::strcmp(scope.failure().field,"light_proof"),"failure diagnostic reports exact changed adjacent native byte");
            put(address,original);check(scope.finish()&&scope.restored()&&!scope.good(),"owner journal recovers only after exact adjacent identity returns");}
        auto omni=eye0;omni.state.light_slots[0].proof[0]=3;put(cache.slot(2)-0x38,std::uint8_t{3});
        // Type3 has no such retail cache writer: its same offset is immutable.
        omni.state.light_slots[0].proof[0x110]^=1;
        NativeShadowScope reject;check(!reject.begin(cache.base,omni),"type3 light cannot claim type2 native cache exclusion");}
    put(f.buffer+0x16cc38,0u);put(f.views+0x2980,0xffffffffu);EyeShadowState bad{};
    check(!capture_eye_shadow(f.base,f.buffer,f.frame,0,&bad),"sun constants must match exact captured native projection");
    check(!validate_shadow_native_guards(nullptr),"null executable rejected");
    {Fixture hook;const auto auxiliary_right=hook.build(1,2,2,4);
        const auto extra_state=hook.build(1,2,9,4);(void)extra_state;
        put(hook.views+2*0x38e0+0x184,1u);put(hook.buffer+0x16cc38,2u);
        put(hook.base+0x2eb7c8,std::uint8_t{0xe8});
        const auto original_call=static_cast<std::int32_t>(0x2d2ec0-0x2eb7c8-5);
        put(hook.base+0x2eb7c9,original_call);
        const auto module=reinterpret_cast<HMODULE>(hook.base);
        check(validate_shadow_native_guards(module),"complete authored guard ranges admit synthetic host");
        const auto call_byte=read<std::uint8_t>(hook.base+0x2eb7c9);put(hook.base+0x2eb7c9,static_cast<std::uint8_t>(call_byte^1));
        check(!install_shadow_inner_dispatch(module),"altered native call displacement refuses installation");
        put(hook.base+0x2eb7c9,call_byte);
        check(install_shadow_inner_dispatch(module)&&validate_shadow_native_guards(module),"exact owned call patch preserves full native guard validation");
        check(install_shadow_inner_dispatch(module),"same startup installation is idempotent");
        const auto owned=read<std::array<std::uint8_t,5>>(hook.base+0x2eb7c8);
        put(hook.base+0x2eb7c9,static_cast<std::uint8_t>(owned[1]^1));
        check(!validate_shadow_native_guards(module),"foreign or partial five-byte patch cannot borrow owned guard allowance");
        put(hook.base+0x2eb7c8,owned);const auto adjacent=read<std::uint8_t>(hook.base+0x2eb7cd);put(hook.base+0x2eb7cd,static_cast<std::uint8_t>(adjacent^1));
        check(!validate_shadow_native_guards(module),"adjacent loop instruction stays byte exact");put(hook.base+0x2eb7cd,adjacent);
        const auto sun_before=read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40);
        {NativeShadowScope s;check(s.begin(hook.base,auxiliary_right),"separately shadowed extra camera arms deferred right journal");
            check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==sun_before&&!s.pending(),"arming does not write extra camera shadows");
            check(prepare_shadow_inner_view(hook.views+2*0x38e0),"first original inner call admits native extra2");
            check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==sun_before,"extra2 runs with unchanged native shared shadow state");
            complete_shadow_inner_view();check(prepare_shadow_inner_view(auxiliary_right.view)&&s.pending(),"only actual ordinary right1 starts journal");
            check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==auxiliary_right.state.sun,"inner right draw receives its copied shadows");
            complete_shadow_inner_view();check(s.finish()&&s.good()&&s.restored()&&!s.pending(),"inner and outer return restore once and accept complete order");}
        check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==sun_before,"extra camera's original shared state survives right rendering");
        for(unsigned why=0;why<4;++why){NativeShadowScope s;check(s.begin(hook.base,auxiliary_right),"invalid deferred sequence fixture arms");
            if(why==0){prepare_shadow_inner_view(auxiliary_right.view);complete_shadow_inner_view();}
            if(why==1){prepare_shadow_inner_view(hook.views+2*0x38e0);complete_shadow_inner_view();}
            if(why==2){prepare_shadow_inner_view(hook.views+2*0x38e0);
                check(prepare_shadow_inner_view(hook.views+2*0x38e0),"nested extra with no writes retains native passthrough");
                complete_shadow_inner_view();complete_shadow_inner_view();prepare_shadow_inner_view(auxiliary_right.view);complete_shadow_inner_view();}
            if(why==3){prepare_shadow_inner_view(hook.views+2*0x38e0);complete_shadow_inner_view();prepare_shadow_inner_view(auxiliary_right.view);
                check(!prepare_shadow_inner_view(auxiliary_right.view),"nested call cannot run beneath an active foreign shadow journal");
                complete_shadow_inner_view();complete_shadow_inner_view();}
            check(s.finish()&&!s.good()&&s.restored()&&!s.pending(),"missing swapped or nested native callbacks reject and clear their scope");
            check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==sun_before,"rejected order leaves or restores native shadow state");}
        fixture_site=hook.base+0x2eb7c8;put(fixture_site+5,std::uint8_t{0xc3});DWORD ignored{};
        check(VirtualProtect(reinterpret_cast<void*>(fixture_site),6,PAGE_EXECUTE_READ,&ignored)!=0,"synthetic patched CALL plus return is executable");
        set_synthetic_native(hook.base,reinterpret_cast<void*>(&synthetic_inner));
        bridge_calls=0;call_fixture_site(auxiliary_right.view);
        check(bridge_calls==1&&native_entry_regs[0]==auxiliary_right.view,"real bridge preserves EAX view and calls native exactly once while unarmed");
        check(native_entry_regs[1]==0x01020304&&native_entry_regs[2]==0x11224488&&native_entry_regs[3]==0x88774422&&
            native_entry_regs[4]==0x10203040&&native_entry_regs[5]==0x40302010&&(native_entry_flags&1),"bridge callbacks preserve incoming GPRs and carry flag");
        check(bridge_return_regs[0]==0x13579bdf&&bridge_return_regs[2]==0x2468ace0&&bridge_return_regs[3]==0x11223344&&
            bridge_return_regs[1]==native_entry_regs[1]&&bridge_return_regs[4]==native_entry_regs[4]&&
            bridge_return_regs[5]==native_entry_regs[5]&&bridge_return_regs[6]==native_entry_regs[6]&&(bridge_return_flags&1),"bridge preserves native result registers flags and callee-saved registers");
        check(!std::memcmp(caller_fx,native_entry_fx,512)&&!std::memcmp(native_return_fx,bridge_return_fx,512),"bridge preserves incoming and outgoing x87 SSE and MXCSR state");
        {NativeShadowScope s;check(s.begin(hook.base,auxiliary_right),"real bridge auxiliary sequence arms");bridge_calls=0;
            call_fixture_site(hook.views+2*0x38e0);call_fixture_site(auxiliary_right.view);
            check(bridge_calls==2&&native_views[0]==hook.views+2*0x38e0&&native_views[1]==auxiliary_right.view,"real patched loop bridge keeps extra2 then right1 exactly once each");
            check(native_sun_words[0]==0x09090909&&native_sun_words[1]==0x02020202,"actual native bridge sees original extra shadows then copied right shadows");
            check(s.finish()&&s.good()&&s.restored(),"real bridge restores native journal after both calls");}
        for(bool native_seh:{false,true}){NativeShadowScope s;check(s.begin(hook.base,auxiliary_right),"real bridge native unwind fixture arms");
            prepare_shadow_inner_view(hook.views+2*0x38e0);complete_shadow_inner_view();
            set_synthetic_native(hook.base,native_seh?reinterpret_cast<void*>(&seh):reinterpret_cast<void*>(&throws));
            bool caught=false;if(native_seh)caught=invoke_seh_site(&s,auxiliary_right.view);
            else try{invoke_shadow_native(&s,call_fixture_body,reinterpret_cast<void*>(auxiliary_right.view));}catch(const std::runtime_error&){caught=true;}
            check(caught&&s.restored()&&!s.pending()&&!s.good(),"native exception crosses real bridge and outer finally restores disarms then propagates");
            check(read<std::array<std::uint8_t,shadow_sun_bytes>>(hook.buffer+0x183f40)==sun_before,"real native unwind preserves original auxiliary shadows");}
    }
    std::printf("native_shadow_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
