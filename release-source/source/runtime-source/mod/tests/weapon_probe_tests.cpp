// SPDX-License-Identifier: GPL-3.0-only
// White-box private seam test; the public installer still requires pinned SP.
#define install_weapon_probe install_weapon_probe_test_copy
#define set_weapon_callbacks set_weapon_callbacks_test_copy
#include "../src/t5/weapon_probe.cpp"
#undef install_weapon_probe
#undef set_weapon_callbacks
#include <array>
#include <cstdio>
#include <limits>
#include <vector>
#include "t5/weapon_animation_math.hpp"
#include "t5/native_clear_pair.hpp"

namespace {
void test_clear_after(const bovr::t5::NativeWeaponContext&,std::uint64_t);
bool animation_fake_enabled{},borrowed_fake_enabled{};
unsigned borrowed_before_count{},borrowed_fault{};std::uint64_t borrowed_token{};
bool borrowed_writer_fake{},borrowed_select_accept{};unsigned borrowed_select_fault{},borrowed_select_count{};
bool hidden_reservation_fake{},knife_continuity_fake{};unsigned hidden_reservation_count{},knife_continuity_count{};
std::vector<std::uint64_t> retired_borrowed_tokens;
bovr::input::ScaledWeaponPlacement borrowed_selected_value{};
unsigned animation_before_count{},animation_after_count{},animation_pair_failures{};
unsigned animation_fault{};
std::vector<std::uintptr_t> animation_storage_addresses;
const unsigned short observer_control=0x027f;
const std::uint32_t observer_xmm[]{0xFFFF,0xAAAA,0xBBBB,0xCCCC};
bool manual_grenade_visual_scope_fake{};
}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool manual_grenade_render_pending() noexcept {return false;}
bool manual_grenade_primary_visual_scope(const NativeWeaponContext&) noexcept {return manual_grenade_visual_scope_fake;}
bool submit_manual_grenade_model(const NativeWeaponContext&) noexcept {return false;}
std::wstring executable_path(){return {};}
Validation validate_path(const std::wstring&){return {};}
bool validate_loaded_headers(HMODULE,std::string&){return false;}
bool read_weapon_lifetime(std::uintptr_t,std::uintptr_t,WeaponLifetime* out) noexcept {if(out)*out={};return false;}
bool weapon_barrel_probe_enabled() noexcept {return false;}
void observe_weapon_barrel_after(const NativeWeaponContext&){}
bool weapon_profile_probe_enabled() noexcept {return false;}
void observe_weapon_profile_after(const NativeWeaponContext&) noexcept {}
bool weapon_animation_probe_enabled() noexcept {return animation_fake_enabled;}
bool weapon_borrowed_probe_enabled() noexcept{return borrowed_fake_enabled;}
bool weapon_borrowed_placement_enabled() noexcept{return borrowed_writer_fake;}
bool select_borrowed_weapon_placement(const NativeWeaponContext&,input::ScaledWeaponPlacement* out){
    ++borrowed_select_count;const auto scope=read_weapon_native_clear_scope();
    SetLastError(9898);
    __asm {
        fldcw [observer_control]
        movdqu xmm0,[observer_xmm]
    }
    if(borrowed_select_fault==1)RaiseException(0xE1230555,0,0,nullptr);if(borrowed_select_fault==2)throw 90;
    if(!borrowed_select_accept||!scope.valid||scope.depth!=1)return false;
    *out=borrowed_selected_value;return true;
}
bool reserve_hidden_primary_weapon_scene_frame(const NativeWeaponContext&) noexcept {++hidden_reservation_count;return hidden_reservation_fake;}
bool select_borrowed_knife_visual_continuity(const NativeWeaponContext&,input::ScaledWeaponPlacement* out) noexcept {
    ++knife_continuity_count;if(!knife_continuity_fake)return false;*out=borrowed_selected_value;return true;
}
void retire_borrowed_weapon_invocation(std::uint64_t token) noexcept {
    if(borrowed_writer_fake)retired_borrowed_tokens.push_back(token);
}
void observe_borrowed_weapon_before(const NativeWeaponContext&){
    ++borrowed_before_count;const auto scope=read_weapon_native_clear_scope();borrowed_token=scope.valid&&scope.depth==1?scope.token:0;
    SetLastError(8888);
    __asm {
        fldcw [observer_control]
        movdqu xmm0,[observer_xmm]
    }
    if(borrowed_fault==1)RaiseException(0xE1230555,0,0,nullptr);if(borrowed_fault==2)throw 89;
}
bool weapon_native_clear_probe_enabled() noexcept {return weapon_clear_scope_enabled!=0;}
void observe_native_clear_weapon_return(const NativeWeaponContext& c,std::uint64_t token){test_clear_after(c,token);}
void observe_weapon_animation_before(const NativeWeaponContext&,WeaponAnimationStorage* p){
    ++animation_before_count;animation_storage_addresses.push_back(reinterpret_cast<std::uintptr_t>(p));
    memset(p,0x5A,sizeof(*p));SetLastError(123);
    __asm {
        fldcw [observer_control]
        movdqu xmm0,[observer_xmm]
    }
    if(animation_fault==1)RaiseException(0xE1230555,0,0,nullptr);if(animation_fault==3)throw 73;
}
void observe_weapon_animation_after(const NativeWeaponContext&,WeaponAnimationStorage* p){
    ++animation_after_count;
    for(auto b:p->bytes)if(b!=std::byte{0x5A}){++animation_pair_failures;break;}
    SetLastError(321);if(animation_fault==2)RaiseException(0xE1230555,0,0,nullptr);if(animation_fault==4)throw 74;
    __asm {
        fldcw [observer_control]
        movdqu xmm0,[observer_xmm]
    }
}
}

namespace {
unsigned scope_native_mode{},scope_after_fault{},scope_matches{},scope_invalid_nested{};
bool scope_recursing{},scope_reenter_after{};
std::uint64_t scope_expected_token{},scope_test_serial{};
bovr::t5::animation_detail::NativeClearPair scope_pair;
std::vector<std::uint64_t> scope_seen_tokens;
void __cdecl scope_native_work(int,void*,void*,void*,int);
volatile unsigned executed{};
std::uint32_t received[5]{};
std::uint32_t capture_placement{};
std::array<unsigned char,32> received_placement{};
const std::uint32_t xmm_pattern[]{0x12345678,0x89abcdef,0x31415926,0x27182818};
const std::uint32_t xmm_input[]{0x87654321,0xFEDCBA98,0x62531497,0x81828172};
const unsigned short input_control=0x037f,output_control=0x007f;
std::uint32_t native_input_eax{},native_input_edx{},native_input_flags{},native_input_xmm[4]{};
unsigned short native_input_control{};
unsigned failures{};
unsigned checks{};
unsigned before_count{},after_count{},before_native_count{},after_native_count{};
bool replacement_blocked{};
bovr::t5::NativeWeaponContext before_sample{},after_sample{};
void before_sample_callback(void*,const bovr::t5::NativeWeaponContext& context) {
    ++before_count;before_native_count=executed;before_sample=context;
    replacement_blocked=!bovr::t5::set_weapon_callbacks_test_copy({});SetLastError(1234);
}
void after_sample_callback(void*,const bovr::t5::NativeWeaponContext& context) {
    ++after_count;after_native_count=executed;after_sample=context;SetLastError(4321);
}
void throwing_callback(void*,const bovr::t5::NativeWeaponContext&) {throw 7;}
bool scratch_callback(void*,const bovr::t5::NativeWeaponContext& c,bovr::input::ScaledWeaponPlacement* out) {*out=c.placement;out->origin.x+=123.F;SetLastError(9999);return true;}
bool invalid_scratch_callback(void*,const bovr::t5::NativeWeaponContext& c,bovr::input::ScaledWeaponPlacement* out) {*out=c.placement;out->origin.x=std::numeric_limits<float>::infinity();return true;}
void check(bool ok,const char* text) {++checks;printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok) ++failures;}
#pragma pack(push,4)
struct Capture {
    std::uint32_t eax{},ecx{},edx{},ebx{},esi{},edi{},ebp{},flags{};
    std::int32_t stack_delta{};
    std::uint32_t xmm[4]{};double x87{};unsigned short control{};
};
#pragma pack(pop)
static_assert(offsetof(Capture,xmm)==36 && offsetof(Capture,x87)==52);
__declspec(naked) void native_body() {
    __asm {
        mov [native_input_eax],eax
        mov [native_input_edx],edx
        pushfd
        pop dword ptr [native_input_flags]
        movdqu [native_input_xmm],xmm0
        fnstcw [native_input_control]
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
        mov edx,[esp+8]
        movdqu xmm0,[edx]
        movdqu [received_placement],xmm0
        movdqu xmm0,[edx+16]
        movdqu [received_placement+16],xmm0
    skip_placement:
        cmp dword ptr [scope_native_mode],0
        je skip_scope_work
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        call scope_native_work
        lea esp,[esp+20]
    skip_scope_work:
        mov eax,0x13572468
        mov ecx,0x76543210
        mov edx,0x24681357
        movdqu xmm0,[xmm_pattern]
        fldcw [output_control]
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
        mov eax,0xABCDEF01
        mov edx,0x10293847
        movdqu xmm0,[xmm_input]
        fldcw [input_control]
        cmp eax,eax
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
        fnstcw [edx+60]
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
    check(native_input_eax==0xABCDEF01&&native_input_edx==0x10293847&&(native_input_flags&0x8d5)==0x44,"native incoming volatile GP and arithmetic flags survive pre observer");
    check(!memcmp(native_input_xmm,xmm_input,sizeof(xmm_input))&&native_input_control==input_control&&s.control==output_control,"incoming SSE/x87 control and distinct native output control survive both observers");
}
template<class T> void put(std::uintptr_t address,T value) {memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));}
bool invoke_seh(void* entry,void* placement,void* ps,void* cent,Capture* output) {
    __try {invoke(entry,0,placement,ps,cent,1,output);return false;}
    __except(GetExceptionCode()==0xE1230555?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
bool native_cpp_fault{};
void __cdecl native_fault_body(){++executed;SetLastError(7777);if(native_cpp_fault)throw 75;RaiseException(0xE1230555,0,0,nullptr);}
void test_clear_after(const bovr::t5::NativeWeaponContext&,std::uint64_t token){
    scope_seen_tokens.push_back(token);
    if(scope_after_fault==1)RaiseException(0xE1230555,0,0,nullptr);
    if(scope_after_fault==2)throw 81;
    bovr::t5::animation_detail::ClearValue value{};
    if(scope_pair.take_normal({},scope_test_serial,token,1010,&value))++scope_matches;
}
unsigned nested_depth{};
void* nested_entry{};
void __cdecl scope_native_work(int local,void* place,void* ps,void* cent,int draw){
    using namespace bovr::t5;
    const auto scope=read_weapon_native_clear_scope();
    if(!scope.valid){++scope_invalid_nested;return;}
    scope_expected_token=scope.token;
    animation_detail::ClearValue camera{},normal{},previous{};
    camera.valid=true;camera.serial=++scope_test_serial;camera.published_ms=1000;
    scope_pair.begin(camera.serial,true);scope_pair.camera(camera);
    normal=camera;normal.serial=++scope_test_serial;normal.invocation_token=scope.token;
    scope_pair.begin(normal.serial,false);scope_pair.normal(normal,&previous);
    if((scope_native_mode==2||scope_native_mode==3)&&!scope_recursing){
        scope_recursing=true;const auto mode=scope_native_mode;
        if(mode==2)scope_native_mode=0; // same native context, no simulated clear
        Capture inner{};invoke(nested_entry,local,place,ps,cent,draw,&inner);
        scope_native_mode=mode;scope_recursing=false;
    }
}
void scope_after_reentry(void*,const bovr::t5::NativeWeaponContext& c){
    if(scope_reenter_after)return;scope_reenter_after=true;
    const auto mode=scope_native_mode;scope_native_mode=0;
    Capture inner{};auto place=c.placement;
    invoke(nested_entry,c.local_client,&place,reinterpret_cast<void*>(c.cg_identity+0x8a364),reinterpret_cast<void*>(c.cg_identity+0x8bd7c),c.draw_gun,&inner);
    scope_native_mode=mode;scope_reenter_after=false;
}
void __cdecl native_nested_body(int local,void* place,void* ps,void* cent,int draw){
    if(!nested_depth){++nested_depth;Capture inner{};invoke(nested_entry,local,place,ps,cent,draw,&inner);--nested_depth;}
    reinterpret_cast<void(__cdecl*)(int,void*,void*,void*,int)>(&native_body)(local,place,ps,cent,draw);
}
}
int main() {
    using namespace bovr::t5;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE,PAGE_NOACCESS));
    if(!image) return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    for(const auto rva:{call_rva,native_rva,std::uintptr_t{0x2bf5354},std::uintptr_t{0x81c6d8},std::uintptr_t{0x16796f8},
            std::uintptr_t{0x1803140},std::uintptr_t{0x2593850},std::uintptr_t{0x2510160},std::uintptr_t{0x2511e2c},std::uintptr_t{0x7e19a8},std::uintptr_t{0x2081764},std::uintptr_t{0x2b67f28}})
        if(!VirtualAlloc(image+(rva&~std::uintptr_t{0xfff}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    auto* site=image+call_rva;
    SetEnvironmentVariableW(L"BOVR_WEAPON_PROBE",L"0");
    check(!install_weapon_probe_test_copy(reinterpret_cast<HMODULE>(image)),"public installation defaults off");
    SetEnvironmentVariableW(L"BOVR_WEAPON_PROBE",L"1");
    check(!install_weapon_probe_test_copy(reinterpret_cast<HMODULE>(image)),"public installation rejects foreign synthetic image");
    check(!patch_weapon_call(base),"private seam rejects wrong CALL bytes without writes");
    check(!set_weapon_callbacks_test_copy({nullptr,before_sample_callback,after_sample_callback}),"callbacks cannot register before exact native hook installation");
    const unsigned char push_arg[]{0xff,0x74,0x24,0x14};
    for(unsigned i=0;i<5;++i) memcpy(site-20+i*4,push_arg,4);
    site[0]=0xe8;const auto native_call=static_cast<std::int32_t>(native_rva-call_rva-5);
    memcpy(site+1,&native_call,sizeof(native_call));
    const unsigned char cleanup[]{0x8d,0x64,0x24,0x14,0xc3};memcpy(site+5,cleanup,5);
    auto* body=image+native_rva;body[0]=0xe9;
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&native_body)-reinterpret_cast<std::uintptr_t>(body+5));
    memcpy(body+1,&displacement,4);
    DWORD old{};
    if(!VirtualProtect(site-20,30,PAGE_EXECUTE_READ,&old) || !VirtualProtect(body,5,PAGE_EXECUTE_READ,&old)) return 1;
    FlushInstructionCache(GetCurrentProcess(),site-20,30);FlushInstructionCache(GetCurrentProcess(),body,5);
    std::array<unsigned char,32> placement{};placement.fill(0xa5);const auto untouched=placement;
    Capture capture{};
    invoke(site-20,17,placement.data(),reinterpret_cast<void*>(0x12345670),reinterpret_cast<void*>(0x23456780),3,&capture);
    check(executed==1,"baseline native function executes once");verify_capture(capture);
    check(patch_weapon_call(base),"exact normal viewmodel CALL is redirected");
    check(site[0]==0xe8 && !memcmp(site+5,cleanup,5),"patch preserves CALL instruction size and original caller cleanup");
    invoke(site-20,17,placement.data(),reinterpret_cast<void*>(0x12345670),reinterpret_cast<void*>(0x23456780),3,&capture);
    check(executed==2,"observer invokes native exactly once");verify_capture(capture);
    check(received[0]==17 && received[1]==reinterpret_cast<std::uintptr_t>(placement.data()) && received[2]==0x12345670 &&
        received[3]==0x23456780 && received[4]==3,"all five argument words reach native unchanged");
    check(placement==untouched,"observer preserves all32 placement bytes");
    check(patch_weapon_call(base) && !patch_weapon_call(base+4096),"install is idempotent only for same image");
    invoke(site-20,23,placement.data(),nullptr,nullptr,0,&capture);
    check(executed==3 && received[0]==23 && received[2]==0 && received[3]==0 && received[4]==0,"invalid observation context still passes original arguments exactly once");
    verify_capture(capture);

    // Exercise actual passive context reads without a game process or native API.
    std::vector<unsigned char> cg(0xd0000);
    std::vector<unsigned char> client(0x1d28);
    const auto cgptr=reinterpret_cast<std::uintptr_t>(cg.data());
    const auto clientptr=reinterpret_cast<std::uintptr_t>(client.data());
    const auto ps=cgptr+0x8a364,cent=cgptr+0x8bd7c,entity=base+0x16796f8;
    const std::uintptr_t dummy_dobj=0x12340000;
    put(base+0x2bf5354,cgptr);put(base+0x81c6d8,reinterpret_cast<std::uintptr_t>(&dummy_dobj));
    put(base+0x1803140,clientptr);put(entity+0x13c,clientptr);put(entity+0x184,100);
    put(clientptr+0x1c4,100);put(clientptr+0x1a2c,2);put(ps+0x1c4,100);put(ps+0x144,static_cast<unsigned char>(4));
    put(base+0x2510164,10);
    std::array<float,8> place{0,0,0,1,10,20,30,1};
    auto sample=[&] {return read_weapon(0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1);};
    const auto cg_before=cg;
    const auto client_before=client;
    auto s=sample();
    check(s.readable && s.local_identity && s.normal_context && s.server_identity && s.server_normal,"alive normal local synthetic context is recognized passively");
    put(ps+0x10,0x100u);put(clientptr+0x10,0x100u);s=sample();
    check(s.normal_context&&s.server_normal,
          "ordinary first-person primary reacquires when a script leaves only offhand disabled");
    put(ps+0x10,0u);put(clientptr+0x10,0u);
    check(cg==cg_before && client==client_before,"passive sampling preserves every byte of synthetic cg and server client");
    for(const unsigned flags:{8u,0u,0x100u}) {
        put(ps+4,1);put(clientptr+4,1);put(ps+0xc,0x1800000u);put(clientptr+0xc,0x1800000u);
        put(ps+0x10,flags);put(clientptr+0x10,flags);put(ps+0xe0,0x80002u);put(clientptr+0xe0,0x80002u);
        put(entity+0x298,std::uintptr_t{0x23450000});s=sample();
        check(s.linked_weapon_context&&s.linked_server_context&&!s.normal_context&&!s.server_normal,
              "actual passive reader recognizes enabled linked primary weapons without granting ordinary gameplay");
        const auto linked=read_weapon_context(s,99);
        check(primary_weapon_context(linked)&&linked.server_link_identity==0x23450000,
              "copied weapon context retains independently verified linked pair and exact parent identity");
    }
    put(ps+0x10,0x80u);check(!sample().linked_weapon_context,"linked weapon-disabled client remains excluded");put(ps+0x10,0u);
    put(clientptr+0xc,0x1800400u);check(!sample().linked_server_context,"linked server frozen controls remain excluded");put(clientptr+0xc,0x1800000u);
    put(entity+0x298,std::uintptr_t{1});check(!sample().linked_server_context,"unbounded server link cannot permit linked tracking");
    put(entity+0x298,std::uintptr_t{0x23450000});put(cgptr+0xa9d44,1);
    check(!sample().linked_weapon_context,"linked eligibility retains native draw suppression");put(cgptr+0xa9d44,0);
    put(ps+4,0);put(clientptr+4,0);put(ps+0xc,0u);put(clientptr+0xc,0u);
    put(ps+0x10,0u);put(clientptr+0x10,0u);put(ps+0xe0,0u);put(clientptr+0xe0,0u);put(entity+0x298,std::uintptr_t{});
    put(cgptr+0x8a360,2);s=sample();
    check(s.normal_context&&s.third_person==2,"actual weapon reader accepts shadow mode and retains its raw value");
    for(int mode:{1,-1,3}){put(cgptr+0x8a360,mode);check(!sample().normal_context,"actual weapon reader rejects model and unknown camera modes");}
    put(cgptr+0x8a360,2);put(cgptr+0xc,1);check(!sample().normal_context,"shadow mode cannot bypass cubemap");put(cgptr+0xc,0);
    put(cgptr+0xa9d44,1);check(!sample().normal_context,"shadow mode cannot bypass draw suppression");put(cgptr+0xa9d44,0);
    put(ps+4,4);check(!sample().normal_context,"shadow mode does not admit pm4");put(ps+4,0);
    put(cgptr+0x8a360,0);
    put(ps+4,9);check(!sample().normal_context,"dead predicted movement state is ineligible");put(ps+4,0);
    put(ps+0xc,0x800u);check(!sample().normal_context,"frozen predicted controls are ineligible");put(ps+0xc,0u);
    put(ps+0xe4,0x40000000u);check(!sample().normal_context,"freeze-allow-look context is ineligible");put(ps+0xe4,0u);
    put(ps+0x10,0x80u);check(!sample().normal_context,"script-disabled weapons are ineligible");put(ps+0x10,0u);
    put(clientptr+0x1c0c,0x10u);check(!sample().server_normal,"server freeze state is independently ineligible");put(clientptr+0x1c0c,0u);
    put(entity+0x298,std::uintptr_t{1});check(!sample().server_normal,"linked server player is ineligible");put(entity+0x298,std::uintptr_t{});
    place[4]=std::numeric_limits<float>::infinity();check(!sample().normal_context,"nonfinite placement remains ineligible");
    check(!read_weapon(0,nullptr,reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1).readable,"invalid borrowed pointer is rejected without dereference");
    place[4]=10;
    std::array<unsigned char,16> variant{};std::array<unsigned char,0x570> definition{};
    char knife_internal[]="vorkuta_knife_sp",knife_localized[]="WEAPON_KNIFE_PRISON";
    const auto variantptr=reinterpret_cast<std::uintptr_t>(variant.data()),defptr=reinterpret_cast<std::uintptr_t>(definition.data());
    put(base+0x7e19a8+4*4,variantptr);put(variantptr+8,defptr);
    put(cgptr+0x8a348,5000);put(cgptr+0x8c158,5000);put(cgptr+0x8a344,16);
    put(cgptr+0x8c108,1280);put(cgptr+0x8c10c,720);put(cgptr+0x8c110,1.F);put(cgptr+0x8c114,1.F);
    put(cgptr+0x8c134,1.F);put(cgptr+0x8c144,1.F);put(cgptr+0x8c154,1.F);
    auto context=read_weapon_context(sample(),10);
    {
        std::array<unsigned char,32> draw_option{};draw_option[0x18]=1;const auto optionptr=reinterpret_cast<std::uintptr_t>(draw_option.data());
        put(base+0x2b67f28,optionptr);
        const float pose_origin[]{31,42,53},pose_angles[]{25,-35,17};
        memcpy(reinterpret_cast<void*>(cgptr+0xc5e28),pose_origin,12);memcpy(reinterpret_cast<void*>(cgptr+0xc5e34),pose_angles,12);
        const auto original_cg=cg,original_client=client;NativeWeaponContext copied{};
        check(copy_pose_clear_weapon_context(dummy_dobj,0,999,&copied)&&copied.call_serial==999&&copied.placement.origin.x==31&&copied.placement.origin.z==53,
              "actual protected pose-context provider copies current local owner and persistent origin");
        animation_detail::Transform native_pose{},from_quaternion{};
        const auto& q=copied.placement.quaternion;
        animation_detail::NativeMat32 mat{{q.x,q.y,q.z,q.w},copied.placement.origin,2};
        check(animation_detail::pose_transform({{31,42,53},{25,-35,17}},&native_pose)&&animation_detail::convert_native(mat,&from_quaternion)&&
              animation_detail::axis_error(native_pose.axis,from_quaternion.axis)<0.00001F,
              "reconstructed metadata quaternion represents full native pitch/yaw/roll; no identity placeholder");
        check(cg==original_cg&&client==original_client,"pose-context copy changes no native-owned fixture bytes");
        check(!copy_pose_clear_weapon_context(dummy_dobj,1,999,&copied)&&!copied.valid&&!copy_pose_clear_weapon_context(dummy_dobj+4,0,999,&copied),
              "unsupported local or changed actual DObj is rejected before profile association");
        draw_option[0x18]=0;check(!copy_pose_clear_weapon_context(dummy_dobj,0,999,&copied),"actual native draw option suppresses camera sampling");draw_option[0x18]=1;
        put(base+0x2b67f28,std::uintptr_t{1});check(!copy_pose_clear_weapon_context(dummy_dobj,0,999,&copied),"protected invalid native option copy is contained");put(base+0x2b67f28,optionptr);
        put(cgptr+0xc5e34,std::numeric_limits<float>::infinity());check(!copy_pose_clear_weapon_context(dummy_dobj,0,999,&copied),"nonfinite persistent angles never reach native lower helpers");
        put(cgptr+0xc5e34,pose_angles[0]);put(cgptr+0xc5e28,std::numeric_limits<float>::quiet_NaN());check(!copy_pose_clear_weapon_context(dummy_dobj,0,999,&copied),"nonfinite persistent origin rejected");put(cgptr+0xc5e28,pose_origin[0]);
        // Restore fixture pose expected by the subsequent original wrapper tests.
        memset(reinterpret_cast<void*>(cgptr+0xc5e28),0,24);
    }
    check(context.valid&&context.body_valid&&context.primary_viewmodel&&context.weapon_definition_valid&&context.native_time==5000&&
        context.native_frame_time==16&&context.refdef_identity==cgptr+0x8c100&&context.stock_view.width==1280,
        "callback context copies current native body time viewport and exact primary definition");
    for(int state=21;state<=26;++state) {
        put(ps+0x158,state);manual_grenade_visual_scope_fake=false;
        const auto rejected=read_weapon_context(sample(),11);
        check(!rejected.manual_grenade_primary_visual&&!rejected.primary_viewmodel,
              "offhand transition state without exact grenade visual scope is not primary placement");
        manual_grenade_visual_scope_fake=true;const auto accepted=read_weapon_context(sample(),11);
        check(accepted.manual_grenade_primary_visual&&accepted.primary_viewmodel&&accepted.weapon_definition_valid,
              "exact grenade visual scope marks every offhand state and copies the preserved primary definition");
    }
    manual_grenade_visual_scope_fake=false;put(ps+0x158,0);
    check(set_weapon_callbacks_test_copy({nullptr,before_sample_callback,after_sample_callback}),"default-empty callbacks bind after guarded installation");
    const auto count_before=executed;SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    const auto error_after=GetLastError();
    check(before_count==1&&after_count==1&&before_native_count==count_before&&after_native_count==count_before+1,
        "pre and post callbacks straddle exactly one original native call");
    check(before_sample.valid&&after_sample.valid&&before_sample.cg_identity==cgptr&&after_sample.dobj_identity==dummy_dobj&&
        !memcmp(&before_sample.placement,place.data(),32),"callbacks receive independent copied values without an argument writer");
    check(replacement_blocked,"callback context cannot be unbound or replaced while in flight");
    check(error_after==5555,"callbacks preserve native thread last-error state");verify_capture(capture);
    check(set_weapon_callbacks_test_copy({nullptr,throwing_callback,throwing_callback}),"throwing test callbacks bind outside dispatch");
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(executed==count_before+2,"callback exceptions cannot skip or duplicate the original native call");verify_capture(capture);
    check(set_weapon_callbacks_test_copy({}),"callbacks unbind once both invocations retire");
    SetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",L"0");
    check(!set_weapon_callbacks_test_copy({nullptr,nullptr,nullptr,scratch_callback}),"scratch writer cannot register without separate explicit flag");
    SetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",L"1");
    check(set_weapon_callbacks_test_copy({nullptr,nullptr,nullptr,scratch_callback}),"explicit scratch-only callback registers after native hook");
    const auto original_placement=place;capture_placement=1;SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    const auto selected_error=GetLastError();bovr::input::ScaledWeaponPlacement actual{};memcpy(&actual,received_placement.data(),32);
    check(received[1]!=reinterpret_cast<std::uintptr_t>(place.data())&&actual.origin.x==place[4]+123.F&&place==original_placement,"native receives synchronous owned32-byte scratch while original placement remains byte-identical");
    check(received[0]==0&&received[2]==ps&&received[3]==cent&&received[4]==1,"scratch selection preserves the other four native arguments");
    check(selected_error==5555,"scratch selection preserves native last-error contract");verify_capture(capture);
    check(set_weapon_callbacks_test_copy({nullptr,nullptr,nullptr,invalid_scratch_callback}),"invalid proposal test callback registers explicitly");
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(received[1]==reinterpret_cast<std::uintptr_t>(place.data())&&!memcmp(received_placement.data(),place.data(),32),"nonfinite proposal falls back to exactly one untouched native argument");verify_capture(capture);
    check(set_weapon_callbacks_test_copy({}),"scratch callback releases without retaining stack placement");
    animation_fake_enabled=true;animation_fault=0;SetLastError(5555);const auto animation_native_before=executed;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(animation_before_count==1&&animation_after_count==1&&animation_pair_failures==0,"same wrapper-owned1536 bytes survive exactly one native call");
    check(executed==animation_native_before+1&&place==original_placement&&received[1]==reinterpret_cast<std::uintptr_t>(place.data()),"passive comparison never substitutes or writes original placement");
    check(GetLastError()==5555,"independent pre/post comparison preserves native LastError");verify_capture(capture);
    for(unsigned fault:{1U,2U,3U,4U}) {
        animation_fault=fault;const auto calls=executed,posts=animation_after_count;SetLastError(5555);
        bool caught{};
        if(fault<3)caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        else try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==static_cast<int>(70+fault);}
        check(caught,"pre/post comparison native SEH or C++ exception crosses complete wrapper unchanged");
        check(executed==calls+((fault==2||fault==4)?1U:0U)&&animation_after_count==posts+((fault==2||fault==4)?1U:0U),"fault path neither fabricates original execution nor a completed post callback");
        check(GetLastError()==5555,"full observer boundary restores own LastError on exception");
    }
    animation_fault=0;void* saved_original=original_weapon;original_weapon=reinterpret_cast<void*>(&native_fault_body);
    for(bool cpp:{false,true}){
        native_cpp_fault=cpp;const auto calls=executed,posts=animation_after_count;SetLastError(5555);bool caught{};
        if(cpp)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==75;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&executed==calls+1&&animation_after_count==posts,"native original fault crosses owned scratch frame without a false after sample");
        check(GetLastError()==7777,"original native fault LastError is not overwritten by unused post observer");
    }
    original_weapon=reinterpret_cast<void*>(&native_nested_body);nested_entry=site-20;
    const auto addresses=animation_storage_addresses.size(),posts=animation_after_count,calls=executed;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(executed==calls+2&&animation_after_count==posts+2&&animation_pair_failures==0,"nested native calls preserve each owned copied scratch independently");
    check(animation_storage_addresses.size()==addresses+2&&animation_storage_addresses[addresses]!=animation_storage_addresses[addresses+1],"nested wrapper cannot alias outer comparison storage");
    original_weapon=saved_original;
    animation_fake_enabled=false;
    check(enable_weapon_native_clear_scope(),"new scope tracking enables only after existing native hook");
    check(!read_weapon_native_clear_scope().valid&&!read_weapon_native_clear_scope().depth,"no synthetic wrapper scope exists outside invocation");
    SetLastError(5555);const auto clear_start_calls=executed;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(executed==clear_start_calls+1&&GetLastError()==5555,"guarded native bridge calls original once and preserves LastError");
    verify_capture(capture);
    check(received[0]==0&&received[1]==reinterpret_cast<std::uintptr_t>(place.data())&&received[2]==ps&&received[3]==cent&&received[4]==1,
          "guarded native bridge preserves all five exact argument words");
    check(!read_weapon_native_clear_scope().valid&&!read_weapon_native_clear_scope().depth,"normal wrapper retirement clears copied scope");
    scope_native_mode=1;scope_matches=0;scope_seen_tokens.clear();
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(scope_matches==1&&scope_seen_tokens.size()==1&&scope_seen_tokens.back()==scope_expected_token&&scope_expected_token,
          "actual normal observation and after callback join the same invocation-owned token");
    verify_capture(capture);
    for(unsigned mode:{2U,3U}){
        scope_native_mode=mode;scope_matches=0;scope_seen_tokens.clear();const auto bad=scope_invalid_nested;
        invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
        check(scope_matches==0&&scope_seen_tokens.size()==2&&scope_seen_tokens[0]==0&&scope_seen_tokens[1]==0,
              "nested same-context invocation cannot consume outer normal sample even without a clear");
        check(mode!=3||scope_invalid_nested==bad+1,"nested clear work has no valid borrowed wrapper authority");
        check(!read_weapon_native_clear_scope().valid&&!read_weapon_native_clear_scope().depth,"nested returns leave no parent scope resurrection");
    }
    scope_native_mode=1;scope_matches=0;scope_seen_tokens.clear();
    check(set_weapon_callbacks_test_copy({nullptr,nullptr,scope_after_reentry}),"after callback reentry fixture binds outside dispatch");
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(scope_matches==0&&scope_seen_tokens.size()==2&&scope_seen_tokens[0]==0&&scope_seen_tokens[1]==0,
          "after-return callback reentry invalidates both child and outer copied joins");
    check(set_weapon_callbacks_test_copy({}),"reentrant callback retires without replacement");
    scope_native_mode=0;original_weapon=reinterpret_cast<void*>(&native_fault_body);
    for(bool cpp:{false,true}){
        native_cpp_fault=cpp;SetLastError(5555);bool caught{};const auto pre=executed,post=scope_seen_tokens.size();
        if(cpp)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==75;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&executed==pre+1&&scope_seen_tokens.size()==post,"guarded original SEH/C++ fault propagates without a false after callback");
        check(GetLastError()==7777&&!read_weapon_native_clear_scope().valid&&!read_weapon_native_clear_scope().depth,
              "native fault preserves native LastError and retires only mod-owned invocation state");
    }
    original_weapon=saved_original;
    for(unsigned fault:{1U,2U}){
        scope_after_fault=fault;SetLastError(5555);bool caught{};
        if(fault==2)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==81;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&!read_weapon_native_clear_scope().valid&&!read_weapon_native_clear_scope().depth&&GetLastError()==5555,
              "post native observation SEH/C++ faults propagate while retiring copied invocation state");
    }
    scope_after_fault=0;scope_native_mode=0;borrowed_fake_enabled=true;borrowed_fault=0;
    const auto borrowed_native_before=executed;const auto token_before=weapon_native_clear_last_token();SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(borrowed_before_count==1&&borrowed_token>token_before&&weapon_native_clear_last_token()==borrowed_token,
          "passive borrowed pre observer sees the actual new wrapper token before original");
    check(executed==borrowed_native_before+1&&received[1]==reinterpret_cast<std::uintptr_t>(place.data())&&place==original_placement,
          "borrowed observer has no selector and leaves native argument/persistent owner contract unchanged");
    check(GetLastError()==5555&&!read_weapon_native_clear_scope().depth,"borrowed observer preserves LastError and retires own scope");verify_capture(capture);
    for(unsigned fault:{1U,2U}){borrowed_fault=fault;const auto previous_calls=executed;SetLastError(5555);bool caught{};
        if(fault==2)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==89;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&executed==previous_calls&&GetLastError()==5555&&!read_weapon_native_clear_scope().depth,
              "borrowed pre native SEH/C++ propagates without fake original call or leaked wrapper authority");
    }
    borrowed_fault=0;borrowed_writer_fake=true;borrowed_select_accept=true;weapon_placement_enabled=false;
    SetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",L"0");
    borrowed_selected_value={{0,0,.70710677F,.70710677F},{101,202,303},1};
    const auto selected_calls=executed,selected_count=borrowed_select_count;retired_borrowed_tokens.clear();SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(executed==selected_calls+1&&borrowed_select_count==selected_count+1,"independent borrowed selector runs once with legacy placement gate disabled");
    check(received[1]!=reinterpret_cast<std::uintptr_t>(place.data())&&!memcmp(received_placement.data(),&borrowed_selected_value,32)&&place==original_placement,"active borrowed path lends exact owned32 bytes without mutating original persistent input");
    check(received[0]==0&&received[2]==ps&&received[3]==cent&&received[4]==1,"independent writer preserves all other original argument words");
    check(retired_borrowed_tokens.size()==1&&retired_borrowed_tokens.back()==borrowed_token&&!read_weapon_native_clear_scope().depth,"normal selected call retires its actual copied token once");
    check(GetLastError()==5555,"independent selector preserves original LastError");verify_capture(capture);
    const auto selected_render_witness=take_weapon_render_witness();
    check(selected_render_witness.observed&&selected_render_witness.expected&&selected_render_witness.substituted&&!selected_render_witness.nested&&
          selected_render_witness.producer.weapon_index==4&&selected_render_witness.producer.native_time==5000,
          "actual wrapper publishes copied current handheld expectation and selected native argument");
    check(!take_weapon_render_witness().observed,"render witness is consumed once and retains no native lease");
    borrowed_select_accept=false;const auto rejected_calls=executed;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(executed==rejected_calls+1&&received[1]==reinterpret_cast<std::uintptr_t>(place.data())&&!memcmp(received_placement.data(),place.data(),32),"rejected independent selection leaves exact original native pointer and bytes");verify_capture(capture);
    const auto rejected_render_witness=take_weapon_render_witness();
    check(rejected_render_witness.observed&&rejected_render_witness.expected&&!rejected_render_witness.substituted,
          "failed selector retains current tracked expectation so an ordinary stock-gun pair cannot replace it");
    put(variantptr,reinterpret_cast<std::uintptr_t>(knife_internal));
    put(variantptr+0xc,reinterpret_cast<std::uintptr_t>(knife_localized));
    hidden_reservation_fake=false;knife_continuity_fake=true;
    const auto continuity_calls=executed,continuity_reservations=hidden_reservation_count,
        continuity_selections=knife_continuity_count;SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    const auto continuity_render_witness=take_weapon_render_witness();
    check(executed==continuity_calls+1&&hidden_reservation_count==continuity_reservations+1&&
          knife_continuity_count==continuity_selections+1&&!memcmp(received_placement.data(),&borrowed_selected_value,32),
          "claimed or busy knife history draw keeps a bounded completed controller placement and still calls native once");
    check(continuity_render_witness.observed&&continuity_render_witness.expected&&continuity_render_witness.substituted&&
          !continuity_render_witness.hidden_primary_weapon&&!continuity_render_witness.native_call_suppressed,
          "visual continuity placement has no authority to become a headset frame");
    hidden_reservation_fake=true;knife_continuity_fake=true;
    const auto hidden_calls=executed,hidden_retired=retired_borrowed_tokens.size(),hidden_continuity=knife_continuity_count;SetLastError(5555);
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    const auto hidden_render_witness=take_weapon_render_witness();
    check(executed==hidden_calls&&knife_continuity_count==hidden_continuity,
          "exact hidden reservation suppresses its stock knife call before considering non-headset continuity");
    check(hidden_render_witness.observed&&hidden_render_witness.expected&&!hidden_render_witness.substituted&&
          hidden_render_witness.hidden_primary_weapon&&hidden_render_witness.native_call_suppressed&&
          hidden_render_witness.producer.primary_knife_variant,
          "suppressed Vorkuta wrapper publishes an explicit world-only witness without a false placement substitution");
    check(retired_borrowed_tokens.size()==hidden_retired+1&&!read_weapon_native_clear_scope().depth&&GetLastError()==5555,
          "suppressed wrapper retires its owned scope and preserves LastError without a false post-native completion");
    hidden_reservation_fake=false;knife_continuity_fake=false;
    put(variantptr,std::uintptr_t{});put(variantptr+0xc,std::uintptr_t{});
    borrowed_select_accept=true;
    put(ps+0x158,22);manual_grenade_visual_scope_fake=true;
    const auto grenade_visual_calls=executed;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    const auto grenade_visual_witness=take_weapon_render_witness();
    check(executed==grenade_visual_calls+1&&grenade_visual_witness.observed&&!grenade_visual_witness.expected&&
          grenade_visual_witness.producer.manual_grenade_primary_visual&&!grenade_visual_witness.nested,
          "exact grenade visual wrapper calls native once and publishes an ordinary one-shot scene witness");
    check(!take_weapon_render_witness().observed,"ordinary grenade visual witness is consumed once");
    manual_grenade_visual_scope_fake=false;put(ps+0x158,0);
    for(unsigned fault:{1U,2U}){borrowed_select_fault=fault;const auto previous_calls=executed;const auto retired=retired_borrowed_tokens.size();SetLastError(5555);bool caught{};
        if(fault==2)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==90;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&executed==previous_calls&&GetLastError()==5555&&!read_weapon_native_clear_scope().depth&&retired_borrowed_tokens.size()==retired+1,
              "independent selector SEH/C++ propagates before native call and retires exact outstanding invocation");
    }
    borrowed_select_fault=0;original_weapon=reinterpret_cast<void*>(&native_fault_body);
    for(bool cpp:{false,true}){native_cpp_fault=cpp;const auto previous_calls=executed;const auto retired=retired_borrowed_tokens.size();SetLastError(5555);bool caught{};
        if(cpp)try{invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);}catch(int v){caught=v==75;}
        else caught=invoke_seh(site-20,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),&capture);
        check(caught&&executed==previous_calls+1&&GetLastError()==7777&&!read_weapon_native_clear_scope().depth&&retired_borrowed_tokens.size()==retired+1,
              "native fault after selection forwards native disposition/LastError and retires only mod claim");
    }
    original_weapon=saved_original;
    for(unsigned mode:{2U,3U}){scope_native_mode=mode;const auto retired=retired_borrowed_tokens.size();const auto seen=borrowed_select_count;
        invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
        check(borrowed_select_count==seen+2&&retired_borrowed_tokens.size()==retired+2&&!read_weapon_native_clear_scope().depth,
              "nested selected wrapper retires both values without restoring an outer native scope");
        const auto nested_render_witness=take_weapon_render_witness();
        check(nested_render_witness.observed&&nested_render_witness.expected&&nested_render_witness.nested,
              "nested weapon calls retain the outer render expectation but cannot authorize tracked publication");
        verify_capture(capture);
    }
    borrowed_writer_fake=false;borrowed_fake_enabled=false;borrowed_fault=0;
    scope_after_fault=0;scope_native_mode=1;scope_matches=0;
    invoke(site-20,0,place.data(),reinterpret_cast<void*>(ps),reinterpret_cast<void*>(cent),1,&capture);
    check(scope_matches==1&&!read_weapon_native_clear_scope().depth,"new real invocation recovers with a fresh token after forwarded faults");
    check(!take_weapon_render_witness().observed,"subsequent native-only invocation clears earlier tracked render witness");
    verify_capture(capture);
    VirtualFree(image,0,MEM_RELEASE);
    printf("checks=%u retail_processes=0 tag_calls=0 placement_writes=0 failures=%u\n",checks,failures);
    return failures?1:0;
}
