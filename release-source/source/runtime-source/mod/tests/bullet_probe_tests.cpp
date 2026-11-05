// SPDX-License-Identifier: GPL-3.0-only
#define install_bullet_probe install_bullet_probe_test_copy
#define set_local_shot_callbacks set_local_shot_callbacks_test_copy
#define set_bullet_selector set_bullet_selector_test_copy
#include "../src/t5/bullet_probe.cpp"
#undef install_bullet_probe
#undef set_local_shot_callbacks
#undef set_bullet_selector
#include <array>
#include <cstdio>
#include <limits>
#include <vector>
namespace {bool body_probe_fixture_enabled{};unsigned body_probe_calls{};bovr::t5::NativeBulletSelectionContext body_probe_context{};}
namespace {bool script_selection{};unsigned script_selection_calls{};std::array<unsigned char,68> script_geometry{};}
namespace {bool dual_selection{};unsigned dual_event{};const std::uint32_t* dual_stack{};std::array<unsigned char,68> dual_geometry{};}
namespace {bovr::t5::NativeConsumedCommand linked_scope_fixture{};bool linked_scope_visible{};unsigned linked_scope_reads{};
    std::uint32_t* linked_mutation{};}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool install_dual_pistol_effects(HMODULE) noexcept {return false;}
bool prepare_dual_pistol_effects(HMODULE) noexcept {return false;}
bool commit_dual_pistol_effects() noexcept {return false;}
bool prepare_dual_pistol_projectile(HMODULE) noexcept {return false;}
bool commit_dual_pistol_projectile() noexcept {return false;}
bool native_command_dual_current(const CommandContext&) noexcept {return false;}
bool native_command_hitscan_current(const CommandContext&) noexcept {return false;}
bool native_command_ordinary_attack_inhibited(const CommandContext&) noexcept {return false;}
bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept {return false;}
bool dual_pistol_shot_guards(std::uintptr_t) noexcept {return false;}
bool select_dual_pistol_shot(std::uintptr_t,const std::uint32_t* stack,unsigned event,unsigned char* scratch) noexcept {
 dual_stack=stack;dual_event=event;if(!dual_selection)return false;memcpy(scratch,dual_geometry.data(),68);return true;
}
bool select_motorcycle_script_shot(std::uintptr_t,const std::uint32_t*,unsigned char* scratch) noexcept {
 ++script_selection_calls;if(!script_selection)return false;memcpy(scratch,script_geometry.data(),68);return true;
}
bool command_rig_enabled() noexcept {return body_probe_fixture_enabled;}
void trace_native_shot_body(std::uintptr_t,const NativeBulletSelectionContext& context) noexcept {++body_probe_calls;body_probe_context=context;SetLastError(43210);}
bool trace_native_shot_command_rig(const NativeLocalShot&) noexcept {return false;}
bool read_consumed_command_scope(NativeConsumedCommand* out) noexcept {
    ++linked_scope_reads;
    if(linked_mutation&&linked_scope_reads==1)*linked_mutation+=4;
    if(!linked_scope_visible)return false;*out=linked_scope_fixture;return true;
}
Validation validate_path(const std::wstring&) {return {};}
bool validate_loaded_headers(HMODULE,std::string&) {return false;}
std::wstring executable_path() {return {};}
}
namespace {
volatile unsigned executed{};
std::uint32_t received[5]{},capture_placement{};
std::array<unsigned char,68> received_placement{};
std::uint32_t entry_eax{},entry_edx{},entry_flags{},entry_xmm[4]{};
double entry_x87{};
DWORD native_seen_error{};
const std::uint32_t xmm_pattern[]{0x12345678,0x89abcdef,0x31415926,0x27182818};
unsigned failures{},checks{},callbacks{};
bool unbind_rejected{};
bovr::t5::NativeLocalShot last{};
void check(bool ok,const char* text) {++checks;printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok) ++failures;}
void callback(void*,const bovr::t5::NativeLocalShot& s) {++callbacks;last=s;unbind_rejected=!bovr::t5::set_local_shot_callbacks_test_copy({});SetLastError(9999);}
void throwing(void*,const bovr::t5::NativeLocalShot&) {throw 7;}
void record_native_error(){native_seen_error=GetLastError();}
bool select_result=true,select_cpp_throw{},select_seh_throw{},select_nonfinite{};
unsigned selections{};bool selector_unbind_rejected{};
bovr::t5::NativeBulletSelectionContext selection_context{};
bovr::t5::NativeBulletGeometry selected_geometry{};
bool selector(void*,const bovr::t5::NativeBulletSelectionContext& context,bovr::t5::NativeBulletGeometry* output) {
    ++selections;selection_context=context;
    selector_unbind_rejected=!bovr::t5::set_bullet_selector_test_copy({});
    SetLastError(98765);
    if(select_cpp_throw)throw 12;
    if(select_seh_throw)RaiseException(0xe043ba18,0,0,nullptr);
    *output=selected_geometry;
    if(select_nonfinite)output->muzzle[1]=std::numeric_limits<float>::infinity();
    return select_result;
}
#pragma pack(push,4)
struct Capture {
    std::uint32_t eax{},ecx{},edx{},ebx{},esi{},edi{},ebp{},flags{};
    std::int32_t stack_delta{};
    std::uint32_t xmm[4]{};double x87{};
};
#pragma pack(pop)
static_assert(offsetof(Capture,xmm)==36 && offsetof(Capture,x87)==52);
__declspec(naked) void native_body() {
    __asm {
        pushfd
        pop dword ptr [entry_flags]
        mov [entry_eax],eax
        mov [entry_edx],edx
        movdqu [entry_xmm],xmm5
        fstp qword ptr [entry_x87]
        inc dword ptr [executed]
        mov eax,[esp+4]
        mov [received],eax
        mov eax,[esp+8]
        mov [received+4],eax
        mov eax,[esp+12]
        mov [received+8],eax
        mov eax,[esp+16]
        mov [received+12],eax
        mov eax,[esp+20]
        mov [received+16],eax
        cmp dword ptr [capture_placement],0
        je skip_placement
        mov edx,[esp+12]
        movdqu xmm0,[edx]
        movdqu [received_placement],xmm0
        movdqu xmm0,[edx+16]
        movdqu [received_placement+16],xmm0
        movdqu xmm0,[edx+32]
        movdqu [received_placement+32],xmm0
        movdqu xmm0,[edx+48]
        movdqu [received_placement+48],xmm0
        mov eax,[edx+64]
        mov [received_placement+64],eax
    skip_placement:
        call record_native_error
        mov eax,0x13572468
        mov ecx,0x76543210
        mov edx,0x24681357
        movdqu xmm0,[xmm_pattern]
        fld1
        cmp eax,eax
        ret
    }
}
__declspec(naked) void __cdecl invoke(void*,int,void*,void*,void*,int,Capture*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        push esp
        push dword ptr [esp+44]
        push dword ptr [esp+44]
        push dword ptr [esp+44]
        push dword ptr [esp+44]
        push dword ptr [esp+44]
        mov ecx,[esp+44]
        mov ebx,0x11223344
        mov esi,0x55667788
        mov edi,0x99aabbcc
        mov ebp,0x1234abcd
        mov eax,0xabcd0123
        mov edx,0x4567abcd
        movdqu xmm5,[xmm_pattern]
        fldpi
        stc
        call ecx
        lea esp,[esp+20]
        // Store all volatile outputs before using a scratch register.
        pushfd
        push edx
        mov edx,[esp+56]
        mov [edx],eax
        mov [edx+4],ecx
        pop dword ptr [edx+8]
        mov [edx+12],ebx
        mov [edx+16],esi
        mov [edx+20],edi
        mov [edx+24],ebp
        pop dword ptr [edx+28]
        lea ecx,[esp+4]
        sub ecx,[esp]
        mov [edx+32],ecx
        movdqu [edx+36],xmm0
        fstp qword ptr [edx+52]
        lea esp,[esp+4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void verify_capture(const Capture& s) {
    check(s.eax==0x13572468 && s.ecx==0x76543210 && s.edx==0x24681357,"native volatile result registers survive observation");
    check(s.ebx==0x11223344 && s.esi==0x55667788 && s.edi==0x99aabbcc && s.ebp==0x1234abcd,"all native nonvolatile registers survive");
    check(s.stack_delta==0,"five copied cdecl arguments and caller stack remain balanced");
    check((s.flags&0x8d5)==0x44,"native arithmetic flags survive");
    check(!memcmp(s.xmm,xmm_pattern,sizeof(xmm_pattern)) && s.x87==1.0,"native SSE and x87 outputs survive");
    check(entry_eax==0xabcd0123&&entry_edx==0x4567abcd&&(entry_flags&1),"preselection preserves incoming volatile registers and carry flag");
    check(!memcmp(entry_xmm,xmm_pattern,sizeof(xmm_pattern))&&std::abs(entry_x87-3.141592653589793)<1e-14,"preselection preserves native x87 and SSE input");
}

volatile unsigned native_raised{};
std::uintptr_t exception_wp{};
std::array<unsigned char,68> exception_bytes{};
void __cdecl native_raise(void*,float,const void* wp,void*,int) {
    ++native_raised;exception_wp=reinterpret_cast<std::uintptr_t>(wp);memcpy(exception_bytes.data(),wp,68);
    RaiseException(0xe043ba19,0,0,nullptr);
}
void __cdecl native_cpp_raise(void*,float,const void* wp,void*,int) {
    ++native_raised;exception_wp=reinterpret_cast<std::uintptr_t>(wp);memcpy(exception_bytes.data(),wp,68);
    throw 0x4512;
}
bool invoke_native_cpp_exception(void* attacker,void* wp) {
    try {reinterpret_cast<void(__cdecl*)(void*,float,void*,void*,int)>(&bovr::t5::bullet_detour)(attacker,1.25F,wp,attacker,7000);}
    catch(int code) {return code==0x4512;}
    return false;
}
bool invoke_native_exception(void* attacker,void* wp) {
    __try {
        reinterpret_cast<void(__cdecl*)(void*,float,void*,void*,int)>(&bovr::t5::bullet_detour)(attacker,1.25F,wp,attacker,7000);
    } __except(GetExceptionCode()==0xe043ba19?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
unsigned nested_calls{};bool nested_scratch_intact{},nested_scratch_distinct{};
void* nested_parameters{};
void __cdecl native_nested(void* attacker,float,const void* wp,void*,int) {
    ++nested_calls;std::array<unsigned char,68> outer{};memcpy(outer.data(),wp,68);
    auto geometry=selected_geometry;selected_geometry.muzzle[0]=654;
    auto* original=bovr::t5::bullet_original;bovr::t5::bullet_original=reinterpret_cast<void*>(&native_body);
    Capture capture{};
    invoke(reinterpret_cast<void*>(&bovr::t5::bullet_detour),static_cast<int>(reinterpret_cast<std::uintptr_t>(attacker)),reinterpret_cast<void*>(0x3fa00000),nested_parameters,attacker,8000,&capture);
    nested_scratch_intact=!memcmp(outer.data(),wp,68);
    nested_scratch_distinct=received[2]!=reinterpret_cast<std::uintptr_t>(wp)&&received[2]!=reinterpret_cast<std::uintptr_t>(nested_parameters);
    verify_capture(capture);
    bovr::t5::bullet_original=original;selected_geometry=geometry;
}
template<class T> void put(std::uintptr_t p,T value) {memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
}
int main() {
    using namespace bovr::t5;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE,PAGE_NOACCESS));if(!image) return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    for(const auto rva:{bullet_call_rva,bullet_native_rva,std::uintptr_t{0x16796f8},std::uintptr_t{0x1803140},std::uintptr_t{0x2bf5354},std::uintptr_t{0x7e19a8},std::uintptr_t{0x2510164},std::uintptr_t{0x2511e2c}})
        if(!VirtualAlloc(image+(rva&~std::uintptr_t{0xfff}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    check(!patch_bullet_call(base),"wrong native CALL guard refuses patch");
    check(!set_local_shot_callbacks_test_copy({nullptr,callback}),"shot callback cannot register before exact hook");
    SetEnvironmentVariableW(L"BOVR_BULLET_SELECTION",L"0");
    check(!set_bullet_selector_test_copy({nullptr,selector}),"selector defaults off even with a callback");
    SetEnvironmentVariableW(L"BOVR_BULLET_SELECTION",L"1");
    check(!set_bullet_selector_test_copy({nullptr,selector}),"selector cannot bind before exact hook installation");
    auto* site=image+bullet_call_rva;site[0]=0xe8;
    const auto native_displacement=static_cast<std::int32_t>(bullet_native_rva-bullet_call_rva-5);
    memcpy(site+1,&native_displacement,sizeof(native_displacement));
    check(patch_bullet_call(base)&&site[0]==0xe8,"exact selected call installs without touching native caller cleanup");
    check(patch_bullet_call(base)&&!patch_bullet_call(base+4096),"same-image hook is idempotent without duplicate native call");
    bullet_original=reinterpret_cast<void*>(&native_body);
    std::array<unsigned char,0x44> wp{};
    Capture capture{};invoke(reinterpret_cast<void*>(&bullet_detour),17,reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(0x23450000),6000,&capture);
    check(executed==1&&received[0]==17&&received[1]==0x3fa00000&&received[2]==reinterpret_cast<std::uint32_t>(wp.data())&&received[3]==0x23450000&&received[4]==6000,"all five native bullet argument words arrive unchanged exactly once");verify_capture(capture);
    std::vector<unsigned char> cg(0xd0000),client(0x1d28);std::array<unsigned char,16> variant{};std::array<unsigned char,0x570> definition{};
    const auto cgptr=reinterpret_cast<std::uintptr_t>(cg.data()),cl=reinterpret_cast<std::uintptr_t>(client.data()),ps=cgptr+0x8a364,entity=base+0x16796f8;
    const auto var=reinterpret_cast<std::uintptr_t>(variant.data()),def=reinterpret_cast<std::uintptr_t>(definition.data());
    put(base+0x2bf5354,cgptr);put(base+0x1803140,cl);put(entity+0x13c,cl);put(entity+0x184,100);put<unsigned char>(entity+0xd4,4);
    put(cl+0x1c4,100);put(cl+0x1a2c,2);put(ps+0x1c4,100);put<unsigned char>(cl+0x144,4);put<unsigned char>(ps+0x144,4);
    put(base+0x7e19a8+16,var);put(var+8,def);
    const auto w=reinterpret_cast<std::uintptr_t>(wp.data());put(w,1.F);put(w+16,1.F);put(w+32,1.F);put(w+36,100.F);put(w+40,200.F);put(w+44,300.F);put(w+48,1.F);put(w+60,var);put(w+64,def);
    const auto original_wp=wp;const auto original_cg=cg;const auto original_client=client;
    std::uint32_t args[]{static_cast<std::uint32_t>(entity),0x3fa00000,static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(entity),6000};
    auto s=read_local_shot(args,55);check(s.local_shot_confirmed&&s.serial==55&&s.weapon_index==4&&s.spread==1.25F&&s.muzzle[2]==300.F,"completed native local bullet event exposes exact copied weapon and muzzle values");
    check(wp==original_wp&&cg==original_cg&&client==original_client,"observer changes no weaponParms player or native client bytes");
    {
        put(cl+4,1);put(ps+4,1);put<std::uint32_t>(cl+0xc,0x1800000);put<std::uint32_t>(ps+0xc,0x1800000);
        put<std::uint32_t>(cl+0xe0,0x80002);put<std::uint32_t>(ps+0xe0,0x80002);
        put<std::uint32_t>(entity+0x298,0x6500000);put(base+0x2510164,10);
        linked_scope_fixture={};linked_scope_fixture.image_base=base;linked_scope_fixture.scope_serial=7;
        linked_scope_fixture.observed_ms=GetTickCount64();linked_scope_fixture.native_thread=GetCurrentThreadId();
        linked_scope_fixture.server_client_identity=0x7600000;linked_scope_fixture.entity_identity=entity;
        linked_scope_fixture.game_client_identity=cl;linked_scope_fixture.source_command_identity=0x7600100;
        linked_scope_fixture.server_state=4;linked_scope_fixture.command_time=6000;
        memcpy(linked_scope_fixture.command.data(),&linked_scope_fixture.command_time,4);linked_scope_fixture.command[0x18]=4;
        linked_scope_fixture.active=linked_scope_fixture.readable=linked_scope_fixture.local_identity=true;
        linked_scope_visible=true;linked_scope_reads=0;
        const auto confirmed=read_local_shot(args,63);
        check(confirmed.local_shot_confirmed&&confirmed.linked_context&&!confirmed.normal_context&&confirmed.server_link==0x6500000&&linked_scope_reads==2,
              "linked enabled local pistol requires two stable native consumed-command joins");
        put<std::uint32_t>(cl+0x10,0x100);put<std::uint32_t>(ps+0x10,0x100);
        check(read_local_shot(args,64).local_shot_confirmed,"linked primary bullet permits disabled offhand only");
        put<std::uint32_t>(cl+0x10,0);put<std::uint32_t>(ps+0x10,0);
        for(unsigned fault=0;fault<10;++fault) {
            const auto saved_scope=linked_scope_fixture;linked_scope_reads=0;
            if(fault==0)linked_scope_visible=false;
            if(fault==1)linked_scope_fixture.command[0x18]=5;
            if(fault==2)linked_scope_fixture.nesting_invalidated=true;
            if(fault==3)linked_scope_fixture.native_returned=true;
            if(fault==4)put<std::uint32_t>(entity+0x298,0);
            if(fault==5)put(ps+4,0);
            if(fault==6)put<std::uint32_t>(cl+0xc,0x1800800);
            if(fault==7)put<std::uint32_t>(ps+0x10,0x80);
            if(fault==8)linked_mutation=reinterpret_cast<std::uint32_t*>(entity+0x298);
            if(fault==9)put(base+0x2511e2c,1);
            check(!read_local_shot(args,65+fault).local_shot_confirmed,
                  "scripted bullet without real command scope or with changed mode link freeze weapon override stays rejected");
            linked_scope_fixture=saved_scope;linked_scope_visible=true;linked_mutation=nullptr;
            put<std::uint32_t>(entity+0x298,0x6500000);put(ps+4,1);put<std::uint32_t>(cl+0xc,0x1800000);
            put<std::uint32_t>(ps+0x10,0);put(base+0x2511e2c,0);
        }
        memcpy(cg.data(),original_cg.data(),cg.size());memcpy(client.data(),original_client.data(),client.size());
        put<std::uint32_t>(entity+0x298,0);put(base+0x2510164,0);linked_scope_visible=false;
    }
    args[3]+=0x34c;check(!read_local_shot(args,56).local_shot_confirmed,"foreign weapon entity cannot impersonate local attacker");args[3]-=0x34c;
    put(cl+4,9);check(!read_local_shot(args,57).local_shot_confirmed,"dead native client is not confirmed eligible local gameplay");put(cl+4,0);
    put(cl+0x1c0c,4);check(!read_local_shot(args,58).local_shot_confirmed,"script-frozen client cannot confirm gameplay shot");put(cl+0x1c0c,0);
    put(def+0x1c,1);check(!read_local_shot(args,59).local_shot_confirmed,"projectile definition is outside selected bullet contract");put(def+0x1c,0);
    put(w+36,std::numeric_limits<float>::infinity());check(!read_local_shot(args,60).local_shot_confirmed,"nonfinite shot parameters fail closed without write");put(w+36,100.F);
    put(w,std::numeric_limits<float>::quiet_NaN());check(!read_local_shot(args,61).local_shot_confirmed,"nonfinite consumed forward basis still rejects");put(w,1.F);
    put(w+48,std::numeric_limits<float>::quiet_NaN());put(w+52,std::numeric_limits<float>::infinity());
    const auto opaque=wp;
    check(read_local_shot(args,62).local_shot_confirmed,"unused uninitialized gunForward floats do not reject a legitimate local type0 shot");
    check(wp==opaque,"observer preserves opaque gunForward bytes exactly");wp=original_wp;
    check(set_local_shot_callbacks_test_copy({nullptr,callback}),"default-empty shot callback binds independently");SetLastError(5555);
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6000,&capture);const auto error=GetLastError();
    check(executed==2&&callbacks==1&&last.local_shot_confirmed&&unbind_rejected,"callback follows one completed native shot and cannot unbind while in flight");
    check(error==5555&&wp==original_wp,"shot callback preserves native last-error and all68 parameter bytes");verify_capture(capture);
    check(set_local_shot_callbacks_test_copy({nullptr,throwing}),"throwing test callback binds outside native dispatch");
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6001,&capture);
    check(executed==3,"callback exception cannot duplicate or skip native bullet call");verify_capture(capture);
    check(set_local_shot_callbacks_test_copy({}),"callbacks release after native dispatch without retained game pointers");

    capture_placement=1;
    selected_geometry={{0,1,0},{1,0,0},{0,0,1},{321,456,789}};
    SetEnvironmentVariableW(L"BOVR_BULLET_SELECTION",L"0");
    check(!set_bullet_selector_test_copy({nullptr,selector}),"installed native observer alone does not enable selection");
    SetEnvironmentVariableW(L"BOVR_BULLET_SELECTION",L"1");
    check(set_bullet_selector_test_copy({nullptr,selector})&&set_local_shot_callbacks_test_copy({nullptr,callback}),"independent selector and completed observer coexist");
    const auto before_calls=executed;SetLastError(5555);
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6002,&capture);
    check(executed==before_calls+1&&selections==1&&selector_unbind_rejected,"accepted preselector runs once before one original call and owns only its callback dispatch");
    check(selection_context.eligible&&!selection_context.native.local_shot_confirmed&&selection_context.native_parameters==original_wp,"preselection context is copied eligible input, never a completed-shot confirmation");
    check(received[0]==entity&&received[1]==0x3fa00000&&received[3]==entity&&received[4]==6002&&received[2]!=w,"only the third of five native argument words is substituted");
    check(!memcmp(received_placement.data(),&selected_geometry,48)&&!memcmp(received_placement.data()+48,original_wp.data()+48,20),"native consumes selected first48 bytes and exact opaque trailing20 bytes");
    check(wp==original_wp&&last.local_shot_confirmed&&last.muzzle[0]==321&&last.forward[1]==1,"original WP remains untouched and completed observer reads actual substituted parameters");
    check(GetLastError()==5555&&native_seen_error==5555,"both selector and completed callback preserve native LastError");verify_capture(capture);

    dual_pistol_selection_enabled=true;dual_selection=true;dual_geometry=original_wp;memcpy(dual_geometry.data(),&selected_geometry,48);
    const auto dual_before=executed;SetLastError(5555);
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6002,&capture);
    check(executed==dual_before+1&&dual_event==0x1234abcd&&dual_stack&&received_placement==dual_geometry&&wp==original_wp,
          "dual dispatch passes saved native event and original stack while using invocation-private scratch once");
    check(GetLastError()==5555,"dual dispatch preserves native LastError");verify_capture(capture);
    dual_selection=false;
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6002,&capture);
    check(received[2]==w&&received_placement==original_wp&&wp==original_wp,"rejected dual dispatch retains the original native pointer and all bytes");verify_capture(capture);
    dual_pistol_selection_enabled=false;

    for(unsigned mode=0;mode<4;++mode) {
        select_result=mode!=0;select_cpp_throw=mode==1;select_seh_throw=mode==2;select_nonfinite=mode==3;
        const auto n=executed;
        invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6003,&capture);
        check(executed==n+1&&received[2]==w&&received_placement==original_wp&&wp==original_wp,"false/C++/SEH/nonfinite selection leaves exact original pointer and68 bytes unchanged");
        check(bullet_selectors_in_flight==0,"rejected selector retains no callback lease");verify_capture(capture);
    }
    select_result=true;select_cpp_throw=select_seh_throw=select_nonfinite=false;
    const auto good_geometry=selected_geometry;
    for(unsigned mode=0;mode<5;++mode) {
        selected_geometry=good_geometry;
        switch(mode) {
        case 0:memset(selected_geometry.forward,0,12);break;
        case 1:memcpy(selected_geometry.right,selected_geometry.forward,12);break;
        case 2:selected_geometry.right[0]=-1;break;
        case 3:selected_geometry.up[2]=1.01F;break;
        case 4:selected_geometry.forward[0]=0.01F;break;
        }
        const auto n=executed;
        invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6004,&capture);
        check(executed==n+1&&received[2]==w&&received_placement==original_wp&&wp==original_wp,"zero/parallel/reflected/nonunit/nonorthogonal basis rejects to exact original pointer");
    }
    selected_geometry=good_geometry;
    for(unsigned degrees=0;degrees<360;degrees+=10) {
        const auto angle=static_cast<float>(degrees)*3.141592653589793F/180.F;
        const auto c=std::cos(angle),sine=std::sin(angle);
        NativeBulletGeometry rotated{{c,sine,0},{sine,-c,0},{0,0,1},{321,456,789}};
        check(valid_bullet_geometry(rotated),"proper rotated native F/R/U basis accepts without changing chirality");
    }
    const auto previous_selections=selections;put(cl+4,9);
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6004,&capture);
    check(selections==previous_selections&&received[2]==w,"ineligible native context never invokes policy or substitutes pointer");put(cl+4,0);
    auto tail_wp=wp;put(w+48,std::numeric_limits<float>::quiet_NaN());put(w+52,std::numeric_limits<float>::infinity());tail_wp=wp;
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6005,&capture);
    check(received[2]!=w&&!memcmp(received_placement.data()+48,tail_wp.data()+48,20)&&wp==tail_wp,"opaque unused nonfinite gunForward bits survive accepted selection exactly");wp=original_wp;

    auto* readonly_wp=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(readonly_wp!=nullptr,"isolated readonly parameter fixture allocates");if(!readonly_wp)return 1;
    auto* boundary_wp=readonly_wp+4096-68;memcpy(boundary_wp,original_wp.data(),68);DWORD old_protection{};
    check(VirtualProtect(readonly_wp,4096,PAGE_READONLY,&old_protection)&&VirtualProtect(readonly_wp+4096,4096,PAGE_NOACCESS,&old_protection),"input ends exactly before unreadable page and cannot be mutated");
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),boundary_wp,reinterpret_cast<void*>(entity),6005,&capture);
    check(received[2]!=reinterpret_cast<std::uintptr_t>(boundary_wp)&&!memcmp(boundary_wp,original_wp.data(),68),"selection reads exactly68 native bytes and never writes readonly original");verify_capture(capture);
    VirtualFree(readonly_wp,0,MEM_RELEASE);

    nested_parameters=wp.data();bullet_original=reinterpret_cast<void*>(&native_nested);
    const auto n_before=executed,s_before=selections;
    reinterpret_cast<void(__cdecl*)(void*,float,void*,void*,int)>(&bullet_detour)(reinterpret_cast<void*>(entity),1.25F,wp.data(),reinterpret_cast<void*>(entity),8001);
    check(nested_calls==1&&executed==n_before+1&&selections==s_before+2,"nested native firing selects once per actual original call without cross-call callback lease");
    check(nested_scratch_intact&&nested_scratch_distinct&&wp==original_wp&&last.muzzle[0]==321,"each nested invocation owns distinct scratch until its native return and outer post observes outer geometry");

    bullet_original=reinterpret_cast<void*>(&native_raise);
    const auto old_callbacks=callbacks;
    check(invoke_native_exception(reinterpret_cast<void*>(entity),wp.data()),"native SEH propagates through complete preselection/native wrapper to original outer handler");
    check(native_raised==1&&exception_wp!=w&&!memcmp(exception_bytes.data(),&selected_geometry,48)&&!memcmp(exception_bytes.data()+48,original_wp.data()+48,20),"accepted scratch remains live during native exception entry with opaque tail intact");
    check(callbacks==old_callbacks&&bullet_selectors_in_flight==0&&wp==original_wp,"native exception is never reported as completed and no mod callback lease or original write remains");
    bullet_original=reinterpret_cast<void*>(&native_cpp_raise);
    check(invoke_native_cpp_exception(reinterpret_cast<void*>(entity),wp.data())&&native_raised==2,"native C++ exception crosses complete wrapper unchanged without a noexcept termination boundary");
    check(callbacks==old_callbacks&&bullet_selectors_in_flight==0&&wp==original_wp,"native C++ unwind produces no false completion or retained selector dispatch");
    check(set_bullet_selector_test_copy({}),"selector can unregister after native exception without synthetic cleanup of native state");
    bullet_original=reinterpret_cast<void*>(&native_raise);
    check(invoke_native_exception(reinterpret_cast<void*>(entity),wp.data())&&native_raised==3&&exception_wp==w&&exception_bytes==original_wp,"default-empty native exception preserves exact original pointer and propagation");
    bullet_original=reinterpret_cast<void*>(&native_body);
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),6006,&capture);
    verify_capture(capture);
    check(set_local_shot_callbacks_test_copy({}),"completed observer survives both native exception paths and releases normally");
    body_probe_fixture_enabled=true;SetLastError(5555);
    const auto old_body_calls=body_probe_calls,native_before_body=executed;
    invoke(reinterpret_cast<void*>(&bullet_detour),static_cast<int>(entity),reinterpret_cast<void*>(0x3fa00000),wp.data(),reinterpret_cast<void*>(entity),9000,&capture);
    check(body_probe_calls==old_body_calls+1&&body_probe_context.eligible&&!body_probe_context.native.local_shot_confirmed&&body_probe_context.native_parameters==original_wp,"body-only mode receives current copied pre-native context without registering an aiming selector");
    check(executed==native_before_body+1&&received[2]==w&&wp==original_wp&&bullet_selectors_in_flight==0,"passive body-only mode preserves exact pointer and native once-call without selector ownership");
    check(GetLastError()==5555&&native_seen_error==5555,"body evidence cannot change incoming or outgoing native LastError");verify_capture(capture);
    body_probe_fixture_enabled=false;
    check(set_bullet_selector_test_copy({nullptr,selector}),"ordinary selector remains bound during scripted call isolation");
    std::array<std::uint32_t,33> scripted{};
    scripted[27]=static_cast<std::uint32_t>(bullet_base+0x16f818);scripted[28]=static_cast<std::uint32_t>(entity);
    scripted[30]=w;scripted[31]=static_cast<std::uint32_t>(entity);
    const auto ordinary_count=selections,callback_count=callbacks;
    SetLastError(5555);before_bullet(scripted.data());observe_bullet(scripted.data());
    check(script_selection_calls==1&&scripted[26]==w&&selections==ordinary_count&&callbacks==callback_count,
          "unmatched MagicBullet bypasses ordinary aim policy and ordinary completed callbacks");
    script_selection=true;script_geometry=original_wp;memcpy(script_geometry.data(),&selected_geometry,48);
    before_bullet(scripted.data());observe_bullet(scripted.data());
    check(script_selection_calls==2&&scripted[26]==reinterpret_cast<std::uint32_t>(scripted.data()+9)&&
          !memcmp(scripted.data()+9,script_geometry.data(),68)&&selections==ordinary_count&&callbacks==callback_count&&GetLastError()==5555,
          "accepted motorcycle shot owns full scratch without borrowing ordinary policy or changing LastError");
    check(wp==original_wp&&set_bullet_selector_test_copy({}),"script dispatch leaves original parameters and ordinary selector lifetime intact");
    SetEnvironmentVariableW(L"BOVR_BULLET_PROBE",L"0");check(!install_bullet_probe_test_copy(reinterpret_cast<HMODULE>(image)),"public bullet observer remains independently opt-in");
    printf("checks=%u failures=%u original_native_memory_writes=0 synthetic_argument_substitution=1 native_game_calls=0 game_launches=0\n",checks,failures);return failures?1:0;
}
