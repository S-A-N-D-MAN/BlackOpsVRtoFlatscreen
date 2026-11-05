// SPDX-License-Identifier: GPL-3.0-only
// Exercise the production naked bridge and installer; only executable identity
// and native callee are injected into this private in-process fixture.
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/sun_shadow_lod_hook.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&){return {true,"fixture","fixture"};}
bool validate_loaded_headers(HMODULE,std::string&){return true;}
std::wstring executable_path(){return L"private fixture";}
}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void store(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof value);}
template<class T>T read(std::uintptr_t p){T value{};std::memcpy(&value,reinterpret_cast<void*>(p),sizeof value);return value;}
std::uintptr_t fixture_site{},fixture_spot_site{},fixture_base{};
std::uint32_t native_count{},seen_index[2]{},seen_partition[2]{},seen_max[2]{},seen_return[2]{},seen_scale[2]{};
std::uint32_t entry_regs[7]{},return_regs[7]{},entry_flags{},return_flags{};
std::uint32_t spot_native_count{},spot_seen_index[4]{},spot_seen_light[4]{},spot_seen_slot[4]{},spot_seen_return[4]{},spot_seen_scale[4]{},spot_caller_flags{};
__declspec(align(16)) unsigned char caller_fx[512]{},entry_fx[512]{},native_fx[512]{},returned_fx[512]{};
__declspec(align(16)) unsigned char first_entry_fx[512]{};
__declspec(naked) void synthetic_native(){
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
        cmp dword ptr [native_count],0
        jne later_entry
        fxsave [first_entry_fx]
    later_entry:
        mov edx,dword ptr [native_count]
        mov eax,dword ptr [esp+4]
        mov dword ptr [seen_index+edx*4],eax
        mov dword ptr [seen_partition+edx*4],ecx
        mov ecx,dword ptr [esp+8]
        mov dword ptr [seen_max+edx*4],ecx
        mov ecx,dword ptr [esp]
        mov dword ptr [seen_return+edx*4],ecx
        imul eax,eax,34h
        mov ecx,dword ptr [fixture_base]
        mov eax,dword ptr [ecx+eax+356ed04h]
        mov dword ptr [seen_scale+edx*4],eax
        inc dword ptr [native_count]
        mov eax,13579bdfh
        mov ecx,2468ace0h
        mov edx,11223344h
        fld1
        pcmpeqd xmm0,xmm0
        fxsave [native_fx]
        stc
        ret
    }
}
__declspec(naked) void call_sites(unsigned){
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        fxsave [caller_fx]
        mov eax,11224488h
        mov ebx,01020304h
        mov edx,88774422h
        mov esi,dword ptr [ebp+8]
        mov edi,40302010h
        call dword ptr [fixture_site]
        mov dword ptr [return_regs],eax
        mov dword ptr [return_regs+4],ebx
        mov dword ptr [return_regs+8],ecx
        mov dword ptr [return_regs+12],edx
        mov dword ptr [return_regs+16],esi
        mov dword ptr [return_regs+20],edi
        mov dword ptr [return_regs+24],ebp
        pushfd
        pop dword ptr [return_flags]
        fxsave [returned_fx]
        fxrstor [caller_fx]
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
__declspec(naked) void synthetic_spot_native(){
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
        cmp dword ptr [spot_native_count],0
        jne spot_later_entry
        fxsave [first_entry_fx]
    spot_later_entry:
        mov eax,dword ptr [spot_native_count]
        mov dword ptr [spot_seen_light+eax*4],ecx
        mov dword ptr [spot_seen_slot+eax*4],edx
        mov ecx,dword ptr [esp]
        mov dword ptr [spot_seen_return+eax*4],ecx
        mov ecx,dword ptr [esp+4]
        mov dword ptr [spot_seen_index+eax*4],ecx
        imul ecx,ecx,34h
        mov edx,dword ptr [fixture_base]
        mov ecx,dword ptr [edx+ecx+356ed04h]
        mov dword ptr [spot_seen_scale+eax*4],ecx
        inc dword ptr [spot_native_count]
        mov eax,13579bdfh
        mov ecx,2468ace0h
        mov edx,11223344h
        fld1
        pcmpeqd xmm0,xmm0
        fxsave [native_fx]
        stc
        ret
    }
}
// Execute the exact authored mov/push/CALL/add esp,4 native call sequence.
// The outer fixture's scratch word reproduces its real caller stack offset.
__declspec(naked) void call_spot_sites(unsigned,unsigned,unsigned){
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,28
        fxsave [caller_fx]
        mov eax,dword ptr [ebp+8]
        mov dword ptr [esp+24],eax
        mov ebx,01020304h
        mov esi,dword ptr [ebp+12]
        mov edi,dword ptr [ebp+16]
        stc
        pushfd
        pop dword ptr [spot_caller_flags]
        call dword ptr [fixture_spot_site]
        mov dword ptr [return_regs],eax
        mov dword ptr [return_regs+4],ebx
        mov dword ptr [return_regs+8],ecx
        mov dword ptr [return_regs+12],edx
        mov dword ptr [return_regs+16],esi
        mov dword ptr [return_regs+20],edi
        mov dword ptr [return_regs+24],ebp
        pushfd
        pop dword ptr [return_flags]
        fxsave [returned_fx]
        fxrstor [caller_fx]
        add esp,28
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
struct Fixture {
    std::uintptr_t base{},buffer{},views{},refdef{};std::uint32_t frame=45;
    std::array<std::uint32_t,3> origin{0xc5a5feefu,0x455fc123u,0x41c40a55u};
    Fixture(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4240000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!base)return;
        buffer=base+0x3740100;views=base+0x420f730;refdef=base+0x10000;
        std::array<std::uint8_t,calls_guard.size> authored{};
        authored[7]=0x31;authored[8]=0xc9;authored[9]=0x68;store(reinterpret_cast<std::uintptr_t>(authored.data()+10),0x1000u);authored[14]=0x56;
        for(const auto offset:{15u,31u}){authored[offset]=0xe8;const auto call=static_cast<std::int32_t>(target_rva-(calls_guard.address+offset+5));store(reinterpret_cast<std::uintptr_t>(authored.data()+offset+1),call);}
        authored[20]=0xb9;store(reinterpret_cast<std::uintptr_t>(authored.data()+21),1u);authored[25]=0x68;store(reinterpret_cast<std::uintptr_t>(authored.data()+26),0x2000u);authored[30]=0x56;
        authored[36]=0x83;authored[37]=0xc4;authored[38]=0x10;store(base+calls_guard.address,authored);
        std::array<std::uint8_t,spot_call_guard.size> spot_authored{0x8b,0x44,0x24,0x1c,0x50,0x8b,0xcf,0x8b,0xd6,0xe8,0,0,0,0,0x83,0xc4,0x04};
        store(reinterpret_cast<std::uintptr_t>(spot_authored.data()+10),static_cast<std::int32_t>(spot_target_rva-(spot_call+5)));
        store(base+spot_call_guard.address,spot_authored);
        store(base+0x373708c,static_cast<std::uint32_t>(buffer));store(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        store(base+0x356a24c,static_cast<std::uint32_t>(base+0x8000));store(base+0x80fc,3u);
        store(buffer+0x15d780,8u);store(buffer+0x183fe0,2u);
        for(unsigned slot:{1u,3u}){store(views+slot*0x38e0+0x3510,static_cast<std::uint32_t>(buffer));store(views+slot*0x38e0+0x184,1u);store(views+slot*0x38e0+0x100,origin);}
        store(refdef+0x20,origin);store(views+0x38e0+0x100,origin);
        store(base+0x356ecf8,origin);store(base+0x356ed04,0x403c7dcbu);
        before();
    }
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void before(unsigned slot=1){store(buffer+0x15d7a0,frame);store(buffer+0x16cc3c,slot);store(buffer+0x16cbe0,slot);store(buffer+0x16cc38,slot==3?1u:0u);}
    void built(unsigned slot=1){store(buffer+0x16cc3c,slot+1);store(buffer+0x16cbe0,slot+1);store(buffer+0x16cc38,slot);}
    bool arm(SunShadowLodScope& s,unsigned slot=1){before(slot);return s.begin(base,buffer,frame,refdef,12,slot);}
    void spots(unsigned count=2,bool sun=true,bool last_dynamic=false){
        store(buffer+0x184390,count);store(buffer+0x183db0,16u);store(base+0x203fb70,8u);
        std::array<std::uint8_t,4*0x1d0> records{};unsigned mask=sun?8u:0u;
        for(unsigned slot=0;slot<count;++slot){const auto light=last_dynamic&&slot+1==count?10u:5u+slot;
            records[slot*0x1d0+0x180]=static_cast<std::uint8_t>(light);
            store(reinterpret_cast<std::uintptr_t>(records.data())+slot*0x1d0+0x184,static_cast<std::uint32_t>(buffer+0x16cf20+light*0x170));
            store(buffer+0x16cf20+light*0x170,std::uint8_t{2});store(buffer+0x16cf20+light*0x170+0x38,slot);mask|=1u<<light;
        }
        store(buffer+0x1843a0,records);store(buffer+0x15d780,mask);store(buffer+0x183fe0,sun?2u:0u);
    }
};
void throw_cpp(void*){throw std::runtime_error("native C++");}
void throw_seh(void*){RaiseException(0xe0421234,0,0,nullptr);}
bool seh_scope(SunShadowLodScope* s){__try{invoke_sun_shadow_lod_native(s,throw_seh,nullptr);}__except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
void native_body(void*){call_sites(1);}
void native_thermal_body(void*){call_sites(3);}
void native_spot_body(void*){call_spot_sites(1,0,5);call_spot_sites(1,1,6);}
void native_spot_thermal_body(void*){call_spot_sites(3,0,5);call_spot_sites(3,1,6);}
void native_spot_one_body(void*){call_spot_sites(1,0,5);}
void check_entry_preservation(){
    check(entry_regs[0]==0x13579bdf&&entry_regs[1]==0x01020304&&entry_regs[2]==1&&entry_regs[3]==0x11223344&&
        entry_regs[4]==1&&entry_regs[5]==0x40302010&&entry_regs[6]==return_regs[6],"second native entry retains all incoming GPRs and partition register");
    check((entry_flags&0x8d5)==1,"second native entry retains native first-call arithmetic flags and carry");
}
void throwing_native(unsigned,unsigned){throw std::runtime_error("native via bridge");}
void seh_native(unsigned,unsigned){RaiseException(0xe0421234,0,0,nullptr);}
bool seh_bridge(SunShadowLodScope* s){__try{invoke_sun_shadow_lod_native(s,native_body,nullptr);}__except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
bool seh_thermal_bridge(SunShadowLodScope* s){__try{invoke_sun_shadow_lod_native(s,native_thermal_body,nullptr);}__except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
bool seh_spot_bridge(SunShadowLodScope* s){__try{invoke_sun_shadow_lod_native(s,native_spot_body,nullptr);}__except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
void run(){Fixture f;check(f.base!=0,"fixture allocation");if(!f.base)return;
    SetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",nullptr);
    SetEnvironmentVariableW(L"BOVR_SPOT_SHADOW_LOD_CORRECTION",nullptr);
    check(guards_match(f.base,false),"all complete pinned guards accepted");
    store(f.base+target_rva,std::uint8_t{1});check(!configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"callee mutation denies both hooks");store(f.base+target_rva,std::uint8_t{0});
    store(f.base+spot_target_rva,std::uint8_t{1});check(!configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"complete spot callee mutation denies installation");store(f.base+spot_target_rva,std::uint8_t{0});
    store(f.base+spot_call_guard.address,std::uint8_t{0x90});check(!configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"spot consumed-argument caller mutation denies installation");store(f.base+spot_call_guard.address,std::uint8_t{0x8b});
    check(configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"guarded installation patches both calls");
    check(configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"owned installation idempotent");
    check(guards_match(f.base,true),"owned calls retain full adjacent/callee guard proof");
    native_target=reinterpret_cast<std::uintptr_t>(&synthetic_native);fixture_base=f.base;fixture_site=f.base+0x2c743d;
    store(f.base+0x2c745d,std::uint8_t{0xc3});DWORD prior{};
    check(VirtualProtect(reinterpret_cast<void*>(fixture_site),0x21,PAGE_EXECUTE_READ,&prior)!=0,"copied real two-call path executable");
    fixture_spot_site=f.base+spot_call_guard.address;spot_native_target=reinterpret_cast<std::uintptr_t>(&synthetic_spot_native);
    store(f.base+spot_call+8,std::uint8_t{0xc3});
    check(VirtualProtect(reinterpret_cast<void*>(fixture_spot_site),18,PAGE_EXECUTE_READ,&prior)!=0,"copied real spot argument/call/discard path executable");
    native_count=0;SetLastError(0x12345678);call_sites(1);
    check(native_count==2&&seen_index[0]==1&&seen_index[1]==1,"unarmed actual bridges retain index1 and execute native once each");
    check(seen_partition[0]==0&&seen_partition[1]==1&&seen_max[0]==0x1000&&seen_max[1]==0x2000,"native partition and max counts preserved");
    check(seen_return[0]==f.base+call0+5&&seen_return[1]==f.base+call1+5,"tail bridge preserves exact native return addresses");
    check(GetLastError()==0x12345678,"actual bridge preserves LastError into and out of native");
    check(!std::memcmp(caller_fx,first_entry_fx,512),"bridge preserves complete incoming x87 SSE and MXCSR state");
    check(return_regs[0]==0x13579bdf&&return_regs[2]==0x2468ace0&&return_regs[3]==0x11223344&&
        return_regs[1]==0x01020304&&return_regs[4]==1&&return_regs[5]==0x40302010,"native outputs and nonvolatile registers preserved");
    check(!std::memcmp(native_fx,returned_fx,512),"native outgoing FX state survives tail return");
    check_entry_preservation();
    const auto before_lod=read<std::array<std::uint8_t,0x68>>(f.base+0x356ecf8);
    {SunShadowLodScope s;check(f.arm(s),"ordinary eye1 construction scope arms at count1");f.built();native_count=0;
        invoke_sun_shadow_lod_native(&s,native_body,nullptr);
        check(native_count==2&&seen_index[0]==0&&seen_index[1]==0,"actual patched calls both select freshly updated local slot0");
        check_entry_preservation();
        check(seen_scale[0]==0x403c7dcb&&seen_scale[1]==0x403c7dcb,"native sentinel actually reads current slot0 scale");
        check(s.safe()&&s.substitutions()==2&&!active_scope,"normal completion publishes complete substitution witness and clears TLS");
        check(read<std::array<std::uint8_t,0x68>>(f.base+0x356ecf8)==before_lod,"no native LOD slot bytes are written");
        check(read<std::uint32_t>(f.buffer+0x16cc38)==1&&read<std::uint32_t>(f.refdef+0x183a4)==0,"rendering view1 and localClient0 unchanged");}
    {SunShadowLodScope ordinary;f.before(3);
        check(!ordinary.begin(f.base,f.buffer,f.frame,f.refdef,12),
            "ordinary eye1 scope refuses the reserved count3 thermal construction state");}
    for(unsigned unsupported:{0u,2u,4u}){SunShadowLodScope s;f.before(unsupported);
        check(!s.begin(f.base,f.buffer,f.frame,f.refdef,12,unsupported),
            "LOD scope does not generalize thermal slot3 support to arbitrary native slots");}
    {SunShadowLodScope s;check(f.arm(s,3),"explicit thermal slot3 scope arms only at count3 index1");
        f.built(3);native_count=0;
        invoke_sun_shadow_lod_native(&s,native_thermal_body,nullptr);
        check(native_count==2&&seen_index[0]==0&&seen_index[1]==0,
            "actual patched thermal slot3 calls both redirect to the current local-client LOD record");
        check(seen_partition[0]==0&&seen_partition[1]==1&&seen_max[0]==0x1000&&seen_max[1]==0x2000,
            "thermal redirect preserves both native caster partitions and limits");
        check(seen_return[0]==f.base+call0+5&&seen_return[1]==f.base+call1+5,
            "thermal calls retain their original native return addresses");
        check(seen_scale[0]==0x403c7dcb&&seen_scale[1]==0x403c7dcb&&s.substitutions()==2&&s.safe()&&!active_scope,
            "thermal native sentinel consumes exact current LOD scale and disarms after both calls");
        check(read<std::uint32_t>(f.buffer+0x16cc38)==3&&read<std::uint32_t>(f.buffer+0x16cc3c)==4&&
              read<std::uint32_t>(f.buffer+0x16cbe0)==4&&read<std::array<std::uint8_t,0x68>>(f.base+0x356ecf8)==before_lod,
            "thermal LOD correction writes neither native slot selection/counts nor any LOD record");}
    for(unsigned why=0;why<9;++why){SunShadowLodScope s;check(f.arm(s,3),"thermal strict-identity fixture arms");f.built(3);
        unsigned original_index=3;
        if(why==0)store(f.buffer+0x16cc3c,3u);
        if(why==1)store(f.buffer+0x16cc38,2u);
        if(why==2)store(f.buffer+0x16cbe0,3u);
        if(why==3)store(f.buffer+0x15d7a0,f.frame+1);
        if(why==4)store(f.views+3*0x38e0+0x3510,static_cast<std::uint32_t>(f.buffer+0x186300));
        if(why==5)store(f.views+3*0x38e0+0x38c4,std::uint8_t{1});
        if(why==6)store(f.views+3*0x38e0+0x188,1u);
        if(why==7)store(f.views+3*0x38e0+0x100,0u);
        if(why==8)original_index=1;
        check(select_sun_shadow_lod_index(f.base+call0+5,0,original_index,0x1000)==original_index&&s.substitutions()==0,
            "wrong thermal count/index/parms/frame/buffer/dispatch/client/origin/argument preserves native fallback");
        s.finish();check(!active_scope&&s.safe(),"rejected thermal redirect disarms without partial substitutions");
        store(f.views+3*0x38e0+0x3510,static_cast<std::uint32_t>(f.buffer));
        store(f.views+3*0x38e0+0x38c4,std::uint8_t{});store(f.views+3*0x38e0+0x188,0u);store(f.views+3*0x38e0+0x100,f.origin);
    }
    {SunShadowLodScope s;check(f.arm(s,3),"thermal partial pair fixture arms");f.built(3);
        check(select_sun_shadow_lod_index(f.base+call0+5,0,3,0x1000)==0,"first thermal partition redirects");
        store(f.buffer+0x16cc38,1u);
        check(select_sun_shadow_lod_index(f.base+call1+5,1,3,0x2000)==3,"changed thermal selection refuses second redirect");
        s.finish();check(!s.safe()&&s.substitutions()==1&&!active_scope,
            "mixed thermal redirected/native pair is not publishable");}
    for(bool seh:{false,true}){SunShadowLodScope s;check(f.arm(s,3),"thermal bridge unwind fixture arms");f.built(3);
        native_target=seh?reinterpret_cast<std::uintptr_t>(&seh_native):reinterpret_cast<std::uintptr_t>(&throwing_native);
        bool caught=false;if(seh)caught=seh_thermal_bridge(&s);
        else try{invoke_sun_shadow_lod_native(&s,native_thermal_body,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(caught&&!active_scope&&s.substitutions()==1&&!s.safe(),
            "actual thermal bridge C++/SEH exception disarms and rejects a partial redirect");}
    native_target=reinterpret_cast<std::uintptr_t>(&synthetic_native);
    for(unsigned why=0;why<12;++why){SunShadowLodScope s;check(f.arm(s),"rejection fixture arms");f.built();
        auto ret=f.base+call0+5;unsigned part=0,index=1,max=0x1000;
        if(why==0)++ret;if(why==1)part=1;if(why==2)index=0;if(why==3)++max;
        if(why==4)store(f.buffer+0x15d7a0,f.frame+1);if(why==5)store(f.buffer+0x16cc38,0u);
        if(why==6)store(f.refdef+0x183a4,1u);if(why==7)store(f.views+0x38e0+0x38c4,std::uint8_t{1});
        if(why==8)store(f.base+0x356ed04,0u);if(why==9)store(f.base+0x356ecf8,0u);
        if(why==10)store(f.views+0x38e0+0x100,0u);if(why==11)store(f.buffer+0x15d780,0u);
        check(select_sun_shadow_lod_index(ret,part,index,max)==index&&s.substitutions()==0,"unsupported call returns original index without substitution");
        s.finish();check(s.safe()&&!active_scope,"zero substitution retains native fallback and clears TLS");
        store(f.refdef+0x183a4,0u);store(f.views+0x38e0+0x38c4,std::uint8_t{});store(f.base+0x356ecf8,before_lod);
        store(f.views+0x38e0+0x100,f.origin);store(f.buffer+0x15d780,8u);
    }
    {SunShadowLodScope s;check(f.arm(s),"partial redirect fixture arms");f.built();
        check(select_sun_shadow_lod_index(f.base+call0+5,0,1,0x1000)==0,"first partition redirects");
        store(f.base+0x356ed04,0u);check(select_sun_shadow_lod_index(f.base+call1+5,1,1,0x2000)==1,"second invalid partition uses native fallback");
        s.finish();check(!s.safe()&&s.substitutions()==1,"mixed redirected/unredirected pair cannot publish");store(f.base+0x356ecf8,before_lod);}
    {SunShadowLodScope s;check(f.arm(s),"foreign callback fixture arms");f.built();unsigned selected=99;
        std::thread t([&]{selected=select_sun_shadow_lod_index(f.base+call0+5,0,1,0x1000);});t.join();check(selected==1,"foreign thread cannot use active owner scope");s.finish();}
    {SunShadowLodScope s,nested;check(f.arm(s),"nested fixture arms");check(!f.arm(nested),"nested scope refuses and blocks outer redirect");f.built();
        check(select_sun_shadow_lod_index(f.base+call0+5,0,1,0x1000)==1,"nested construction cannot inherit outer redirection");s.finish();}
    {SunShadowLodScope s;check(f.arm(s),"C++ finally fixture arms");bool caught=false;try{invoke_sun_shadow_lod_native(&s,throw_cpp,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(caught&&!active_scope,"C++ exception propagates after TLS disarm");}
    {SunShadowLodScope s;check(f.arm(s),"SEH finally fixture arms");check(seh_scope(&s)&&!active_scope,"SEH propagates after TLS disarm");}
    for(bool seh:{false,true}){SunShadowLodScope s;check(f.arm(s),"actual native bridge exception fixture arms");f.built();native_target=seh?reinterpret_cast<std::uintptr_t>(&seh_native):reinterpret_cast<std::uintptr_t>(&throwing_native);
        bool caught=false;if(seh)caught=seh_bridge(&s);else try{invoke_sun_shadow_lod_native(&s,native_body,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(caught&&!active_scope&&s.substitutions()==1&&!s.safe(),"native bridge exception preserves original unwind and disarms partial scope");}
    native_target=reinterpret_cast<std::uintptr_t>(&synthetic_native);
    {SunShadowLodScope s;check(f.arm(s),"new scope is available after native exceptions");s.finish();}
    f.spots();spot_native_count=0;SetLastError(0x56781234);call_spot_sites(1,0,5);
    check(spot_native_count==1&&spot_seen_index[0]==1&&spot_seen_light[0]==5&&spot_seen_slot[0]==0,"unarmed production spot bridge passes native argument/light/slot once");
    check(spot_seen_return[0]==f.base+spot_call+5,"spot tail bridge retains exact original native return");
    check(GetLastError()==0x56781234&&entry_flags==spot_caller_flags,"spot bridge preserves LastError and original incoming EFLAGS");
    check(!std::memcmp(caller_fx,first_entry_fx,512)&&!std::memcmp(native_fx,returned_fx,512),"spot bridge preserves incoming and native outgoing complete FX state");
    check(entry_regs[0]==1&&entry_regs[1]==0x01020304&&entry_regs[2]==5&&entry_regs[3]==0&&entry_regs[4]==0&&entry_regs[5]==5&&entry_regs[6]==return_regs[6],"spot bridge preserves all incoming GPRs including distinct light and slot registers");
    check(return_regs[0]==0x13579bdf&&return_regs[2]==0x2468ace0&&return_regs[3]==0x11223344&&return_regs[1]==0x01020304&&return_regs[4]==0&&return_regs[5]==5,"spot bridge returns native outputs and preserved nonvolatile registers");
    for(unsigned view_slot:{1u,3u}){SunShadowLodScope s;f.spots(2,false);check(f.arm(s,view_slot),"spot-only ordinary/thermal frontend scope arms");f.built(view_slot);spot_native_count=0;
        invoke_sun_shadow_lod_native(&s,view_slot==3?native_spot_thermal_body:native_spot_body,nullptr);
        check(spot_native_count==2&&spot_seen_index[0]==0&&spot_seen_index[1]==0&&spot_seen_light[0]==5&&spot_seen_light[1]==6&&spot_seen_slot[0]==0&&spot_seen_slot[1]==1,"actual spot bridge corrects only LOD index and preserves both native light/slot pairs");
        check(spot_seen_scale[0]==0x403c7dcb&&spot_seen_scale[1]==0x403c7dcb&&s.spot_substitutions()==2&&s.safe()&&!active_scope,"native spotlight caster consumes current valid scale and complete primary-spot mask admits");
        check(read<std::array<std::uint8_t,0x68>>(f.base+0x356ecf8)==before_lod&&read<unsigned>(f.buffer+0x16cc38)==view_slot&&read<unsigned>(f.refdef+0x183a4)==0,"spot correction writes no global LOD/view/client state");
    }
    {SunShadowLodScope s;f.spots();check(f.arm(s),"combined sun/spot frontend scope arms");f.built();native_count=spot_native_count=0;
        call_sites(1);invoke_sun_shadow_lod_native(&s,native_spot_body,nullptr);
        check(s.substitutions()==2&&s.spot_substitutions()==2&&s.safe(),"combined native sun pair and complete spot sequence admit together");}
    {SunShadowLodScope s;f.spots(2,false,true);check(f.arm(s),"mixed primary/dynamic spot scope arms");f.built();spot_native_count=0;
        invoke_sun_shadow_lod_native(&s,native_spot_one_body,nullptr);
        check(spot_native_count==1&&s.spot_substitutions()==1&&s.safe(),"dynamic spotlight without primary static-caster call is excluded from expected mask");}
    SetEnvironmentVariableW(L"BOVR_SPOT_SHADOW_LOD_CORRECTION",L"0");check(configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"startup override zero preserves guarded installed baseline");
    {SunShadowLodScope s;f.spots();check(f.arm(s),"disabled spot baseline scope arms");f.built();spot_native_count=0;
        invoke_sun_shadow_lod_native(&s,native_spot_body,nullptr);
        check(spot_native_count==2&&spot_seen_index[0]==1&&spot_seen_index[1]==1&&s.spot_substitutions()==0&&s.safe(),"override zero performs native passthrough with no correction witness");}
    SetEnvironmentVariableW(L"BOVR_SPOT_SHADOW_LOD_CORRECTION",nullptr);check(configure_sun_shadow_lod_hook(reinterpret_cast<HMODULE>(f.base)),"absent startup override restores default enabled correction");
    for(unsigned why=0;why<21;++why){SunShadowLodScope s;f.spots();check(f.arm(s),"spot strict refusal fixture arms");f.built();auto ret=f.base+spot_call+5;unsigned light=5,index=1,slot=0;
        if(why==0)++ret;if(why==1)index=0;if(why==2)slot=1;if(why==3)slot=4;if(why==4)light=6;
        if(why==5)store(f.buffer+0x15d7a0,f.frame+1);if(why==6)store(f.buffer+0x16cc38,0u);if(why==7)store(f.buffer+0x16cc3c,1u);
        if(why==8)store(f.buffer+0x16cbe0,1u);if(why==9)store(f.refdef+0x183a4,1u);if(why==10)store(f.views+0x38e0+0x38c4,std::uint8_t{1});
        if(why==11)store(f.base+0x356ed04,0u);if(why==12)store(f.base+0x356ecf8,0u);if(why==13)store(f.views+0x38e0+0x100,0u);
        if(why==14)store(f.buffer+0x184390,5u);if(why==15)store(f.buffer+0x1843a0+0x180,std::uint8_t{6});if(why==16)store(f.buffer+0x1843a0+0x184,0u);
        if(why==17)store(f.buffer+0x16cf20+5*0x170+0x38,1u);if(why==18)store(f.buffer+0x15d780,8u);if(why==19)store(f.base+0x203fb70,5u);
        if(why==20)store(f.buffer+0x16cf20+5*0x170,std::uint8_t{1});
        check(select_spot_shadow_lod_index(ret,light,index,slot)==index&&s.spot_substitutions()==0,"wrong call/index/order/light/count/record/frame/view/client/LOD/mask/type retains native fallback");
        s.finish();check(s.safe()&&!active_scope,"zero spot correction refusal cannot publish partial correction and disarms");
        store(f.refdef+0x183a4,0u);store(f.views+0x38e0+0x38c4,std::uint8_t{});store(f.views+0x38e0+0x100,f.origin);store(f.base+0x356ecf8,before_lod);
    }
    {SunShadowLodScope s;f.spots();check(f.arm(s),"partial spot correction fixture arms");f.built();
        check(select_spot_shadow_lod_index(f.base+spot_call+5,5,1,0)==0,"first spotlight static-caster call redirects");
        store(f.buffer+0x16cf20+6*0x170+0x38,0u);
        check(select_spot_shadow_lod_index(f.base+spot_call+5,6,1,1)==1,"changed second spotlight ownership refuses correction");
        s.finish();check(s.spot_substitutions()==1&&!s.safe()&&!active_scope,"partial redirected/native spotlight sequence cannot publish");}
    {SunShadowLodScope s;f.spots();check(f.arm(s),"missing remaining primary spotlight fixture arms");f.built();
        check(select_spot_shadow_lod_index(f.base+spot_call+5,5,1,0)==0,"first spotlight redirects before native early completion");s.finish();
        check(!s.safe()&&!active_scope,"incomplete expected primary-spot mask refuses even without a second callback");}
    {SunShadowLodScope s;f.spots();check(f.arm(s),"duplicate spot fixture arms");f.built();
        check(select_spot_shadow_lod_index(f.base+spot_call+5,5,1,0)==0&&select_spot_shadow_lod_index(f.base+spot_call+5,5,1,0)==1,"duplicate native spotlight slot is refused");s.finish();check(!s.safe(),"duplicate after redirect cannot admit");}
    {SunShadowLodScope s;f.spots();check(f.arm(s),"foreign spot thread fixture arms");f.built();unsigned value=99;
        std::thread t([&]{value=select_spot_shadow_lod_index(f.base+spot_call+5,5,1,0);});t.join();check(value==1&&s.spot_substitutions()==0,"foreign thread does not inherit spotlight correction scope");s.finish();}
    for(bool seh:{false,true}){SunShadowLodScope s;f.spots();check(f.arm(s),"actual spot native exception fixture arms");f.built();
        spot_native_target=seh?reinterpret_cast<std::uintptr_t>(&seh_native):reinterpret_cast<std::uintptr_t>(&throwing_native);
        bool caught=false;if(seh)caught=seh_spot_bridge(&s);else try{invoke_sun_shadow_lod_native(&s,native_spot_body,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(caught&&!active_scope&&s.spot_substitutions()==1&&!s.safe(),"spot production bridge C++/SEH exception preserves native unwind and refuses partial correction");}
    spot_native_target=reinterpret_cast<std::uintptr_t>(&synthetic_spot_native);
    {SunShadowLodScope s;f.spots();check(f.arm(s),"new spotlight scope is usable after exceptions");s.finish();}
}
}
int main(){run();std::printf("sun shadow LOD hook: %u checks, %u failures\n",checks,failures);return failures?1:0;}
