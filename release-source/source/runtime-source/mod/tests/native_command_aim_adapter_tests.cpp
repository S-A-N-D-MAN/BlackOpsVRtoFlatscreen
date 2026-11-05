// SPDX-License-Identifier: GPL-3.0-only
// Actual copied reader, input callbacks AND original command preservation bridge.
// All memory/code is owned fixture storage; no retail/XR/graphics calls.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <string>
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/vr_scene_adapter.hpp"
static std::uint64_t aim_now=1000;
static ULONGLONG aim_clock(){return aim_now;}
#define GetTickCount64 aim_clock
#include "../src/t5/command_probe.cpp"
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/native_command_aim_adapter.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#include "../src/t5/input_adapter.cpp"
#undef GetTickCount64
namespace {
using namespace bovr;namespace xr=wawvr::xr;
unsigned checks{},failures{},rig_reads{},action_reads{},native_count{},generation_checks{};
void (*on_rig_read)(){};
bool generation_current=true,mutate_generation_check=false,nest_dispatch=false,rig_live=true;
unsigned invalidate_at_check{};void* recursive_controls{};t5::CommandContext recursive_context{};
bool clear_ready{},borrow_ready{},writer_ready{},renderer_ready=true;
unsigned readiness_reads{};
xr::ActionSnapshot actions{};input::CommandRigSample rig{};
xr::Posef current_anchor{};
bool body_commit_enabled{};unsigned body_commit_calls{};void (*after_body_commit)(){};
std::vector<std::string> dispatches;
void check(bool ok,const char* text){++checks;if(!ok){++failures;printf("FAIL %s\n",text);}}
void __cdecl sink(int,int,const char* text){dispatches.emplace_back(text);if(nest_dispatch){
 nest_dispatch=false;unsigned char scratch[52]{};t5::before_native(recursive_controls,recursive_context);
 t5::after_native(recursive_controls,recursive_context,scratch);
}}
template<class T>void store(std::uintptr_t p,const T& value){memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
struct Fixture {
 unsigned char* image{};unsigned char* cg{};unsigned char* dvar{};unsigned char* def{};unsigned char* variant{};
 t5::AimAdapter aim; t5::Adapter controls{0,false};
 Fixture(bool cycle=false,bool combined=false):controls(0,false,cycle,combined){
  image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  cg=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xb0000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  dvar=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  def=dvar+0x100;variant=dvar+0x700;check(image&&cg&&dvar,"owned native fixture allocations");
  aim.base=reinterpret_cast<std::uintptr_t>(image);controls.base=aim.base;controls.execute=sink;
  store(aim.base+0x2bf5354,reinterpret_cast<std::uintptr_t>(cg));store(aim.base+0x7cd150,reinterpret_cast<std::uintptr_t>(dvar));
  const char name[]="player_topDownCamMode";memcpy(dvar+0x800,name,sizeof(name));store(reinterpret_cast<std::uintptr_t>(dvar),reinterpret_cast<std::uintptr_t>(dvar+0x800));
  *reinterpret_cast<int*>(dvar+0x10)=5;*reinterpret_cast<int*>(def+0x20)=1;store(aim.base+0x7e19a8+16,reinterpret_cast<std::uintptr_t>(variant));store(reinterpret_cast<std::uintptr_t>(variant+8),reinterpret_cast<std::uintptr_t>(def));
  const auto ps=reinterpret_cast<std::uintptr_t>(cg+0x8a364);store(ps+0x1c4,100);store(ps+0x144,(unsigned char)4);
  store(reinterpret_cast<std::uintptr_t>(cg+0x8a348),5000);store(reinterpret_cast<std::uintptr_t>(cg+0x8c158),5000);store(reinterpret_cast<std::uintptr_t>(cg+0x8c134),xr::Basis3f{});
  store(aim.base+0x2510164,10);store(aim.base+0x2593850,100);store(aim.base+0x2081764,4000);
  t5::image_base=aim.base;t5::original_create_cmd=reinterpret_cast<void*>(1);t5::pre_snapshot={};t5::before_command(0);
  aim_now=1000;actions={};actions.focused=true;actions.sequence=1;
  auto& h=actions.hands[1];h.aim.active=h.aim.orientation_valid=h.aim.orientation_tracked=true;h.trigger.active=h.stick.active=true;
  rig={};rig.frame.actions=actions;rig.frame.frame_id=7;rig.xr_generation=rig.anchor_generation=3;rig.published_ms=990;rig.frame.should_render=rig.frame.views_valid=true;rig.frame.predicted_display_time=1000000;rig.frame.predicted_display_period=11111111;
  generation_current=rig_live=true;mutate_generation_check=false;invalidate_at_check=0;nest_dispatch=false;dispatches.clear();t5::aim_adapter.store(&aim);
  current_anchor=rig.tracking_anchor;body_commit_enabled=false;body_commit_calls=0;after_body_commit=nullptr;
 }
 ~Fixture(){t5::registered_callbacks={};t5::aim_adapter.store(nullptr);t5::original_create_cmd=nullptr;for(void* p:{image,cg,dvar})if(p)VirtualFree(p,0,MEM_RELEASE);}
 t5::CommandContext context(){return t5::pre_snapshot.context;}
 void next(){++t5::pre_snapshot.context.command_number;++t5::pre_snapshot.sequence;store(aim.base+0x2593850,t5::pre_snapshot.context.command_number);}
 void begin(){t5::begin_native_command_aim(context(),actions,3,rig,true);}
 input::NativeCommandBytes bytes(){input::NativeCommandBytes b{};for(unsigned i=0;i<b.size();++i)b[i]=static_cast<unsigned char>(i*7+3);b[0x18]=4;return b;}
 void finish(input::NativeCommandBytes& b){t5::finish_native_command_aim(context(),b.data());}
};
Fixture* mutation_fixture{};
unsigned native_client{};
alignas(16) const std::uint32_t pattern[4]{0x12345678,0x99abcdef,0x76543210,0x44442222};
__declspec(naked) void synthetic_native(){__asm {
 inc dword ptr [native_count]
 mov edx,[esp+4]
 mov [native_client],edx
 mov dword ptr [eax],12345
 mov dword ptr [eax+20h],0x11223344
 movdqu xmm0,[pattern]
 fld1
 cmp eax,eax
 ret
}}
__declspec(naked) void synthetic_fault(){__asm {
 inc dword ptr [native_count]
 push 0
 push 0
 push 0
 push 0xe0421973
 call RaiseException
 ret
}}
DWORD invoke_fault(void* fn,unsigned char* command) {
 __try {__asm {
  mov ecx,fn
  mov eax,command
  push 0
  call ecx
  add esp,4
 }}
 __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}return 0;
}
struct ReturnState{std::uint32_t eax,flags,xmm[4];double x87;};
__declspec(naked) void invoke_command(void*,unsigned char*,ReturnState*){__asm {
 push esi
 mov esi,[esp+16]
 mov ecx,[esp+8]
 mov eax,[esp+12]
 push 0
 call ecx
 lea esp,[esp+4]
 mov [esi],eax
 pushfd
 pop dword ptr [esi+4]
 movdqu [esi+8],xmm0
 fstp qword ptr [esi+24]
 pop esi
 ret
}}
}
namespace bovr::t5 {
unsigned dual_hook_ensure_calls{};
bool ensure_dual_pistol_hooks(const CommandContext&) noexcept {++dual_hook_ensure_calls;return true;}
void observe_predicted_shot_feedback(const PreCommandSnapshot&) noexcept {}
bool command_rig_enabled() noexcept{return false;}
void begin_command_rig(const CommandContext&,const xr::ActionSnapshot&,std::uint64_t,const input::CommandRigSample&,bool) noexcept{}
void finish_command_rig(const CommandContext&,const unsigned char*) noexcept{}
bool weapon_integration_renderer_ready() noexcept{return renderer_ready;}
bool weapon_olympia_profile_enabled() noexcept{return borrow_ready&&writer_ready;}
bool weapon_native_clear_probe_enabled() noexcept{++readiness_reads;return clear_ready;}
bool weapon_borrowed_probe_enabled() noexcept{++readiness_reads;return borrow_ready;}
bool weapon_borrowed_placement_enabled() noexcept{++readiness_reads;return writer_ready;}
bool read_vr_input_snapshot(xr::ActionSnapshot* a,std::uint64_t* g) noexcept{++action_reads;*a=actions;*g=3;return true;}
bool read_vr_command_input_snapshot(xr::ActionSnapshot* a,std::uint64_t* g,input::CommandRigSample* r) noexcept{++rig_reads;if(on_rig_read)on_rig_read();*a=actions;*g=3;*r=rig;return rig_live;}
bool command_input_generation_current(const input::CommandRigSample& r) noexcept{
 ++generation_checks;if(mutate_generation_check&&mutation_fixture){mutate_generation_check=false;*reinterpret_cast<float*>(mutation_fixture->cg+0x8c134)=.5F;}
 return generation_current&&r.xr_generation==3&&r.anchor_generation==3&&!memcmp(&r.tracking_anchor,&current_anchor,sizeof(current_anchor))&&
     (!invalidate_at_check||generation_checks<invalidate_at_check);
}
bool consumed_command_input_generation_current(const input::CommandRigSample& r) noexcept{
 return generation_current&&r.xr_generation==3&&r.anchor_generation==3;
}
bool commit_vr_body_yaw(const input::CommandRigSample& original,const xr::Posef& desired,float,float) noexcept{
 ++body_commit_calls;
 if(!body_commit_enabled||!command_input_generation_current(original))return false;
 current_anchor=desired;if(after_body_commit)after_body_commit();return true;
}
bool commit_vr_room_scale(const input::CommandRigSample& original,const xr::Posef& desired,const RoomScaleView&) noexcept {
 return commit_vr_body_yaw(original,desired,0,0);
}
void revoke_room_scale_view() noexcept {}
}
int main(){
 // Exercise the production startup policy, which the actual configurator calls
 // before identity/byte guards. All environment changes are local to this CPU
 // fixture process; no graphics factory or retail image is called.
 const std::array<const wchar_t*,9> required{L"BOVR_COMMAND_AIM_PROBE",L"BOVR_COMMAND_AIM_PLACEMENT_PROBE",
  L"BOVR_XR_STEREO_PROBE",L"BOVR_INPUT_PROBE",L"BOVR_COMMAND_PROBE",L"BOVR_WEAPON_PROBE",
  L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",L"BOVR_BORROWED_WEAPON_PROBE",L"BOVR_BORROWED_WEAPON_PLACEMENT"};
 const std::array<const wchar_t*,15> forbidden{L"BOVR_BULLET_PROBE",L"BOVR_BULLET_ENTRY_PROBE",L"BOVR_BULLET_TAIL_PROBE",
  L"BOVR_SHOT_COMMAND_PROBE",L"BOVR_CONSUMED_COMMAND_PROBE",L"BOVR_CONSUMED_TAIL_PROBE",L"BOVR_COMMAND_RIG_PROBE",
  L"BOVR_BULLET_SELECTION",L"BOVR_SHOT_HAPTICS",L"BOVR_WEAPON_PLACEMENT",L"BOVR_WEAPON_RESERVATION_PROBE",
  L"BOVR_WEAPON_LIFECYCLE_PROBE",L"BOVR_WEAPON_ANIMATION_PROBE",L"BOVR_WEAPON_BARREL_PROBE",L"BOVR_WEAPON_PROFILE_PROBE"};
 auto environment=[&](bool combined){for(auto key:required)SetEnvironmentVariableW(key,combined?L"1":nullptr);
  for(auto key:forbidden)SetEnvironmentVariableW(key,nullptr);SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",nullptr);};
 environment(false);xr::Hand configured_hand{};const auto before_readiness=readiness_reads;
 check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::disabled&&readiness_reads==before_readiness,"default off avoids readiness reads");
 environment(true);clear_ready=borrow_ready=writer_ready=true;
 check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::borrowed_placement&&configured_hand==xr::Hand::Right,"exact combined request and actual readiness admit right-hand policy");
 for(unsigned mask=0;mask<8;++mask){clear_ready=(mask&1)!=0;borrow_ready=(mask&2)!=0;writer_ready=(mask&4)!=0;
  check((t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::borrowed_placement)==(mask==7),"each missing actual native-clear borrowed or writer readiness refuses");}
 clear_ready=borrow_ready=writer_ready=true;
 for(auto key:required){for(auto value:{static_cast<const wchar_t*>(nullptr),L"0",L"2",L"true",L"1-extra-long-invalid-value"}){
  SetEnvironmentVariableW(key,value);check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::refused,"missing or malformed explicit prerequisite cannot enable combined mode");
  SetEnvironmentVariableW(key,L"1");}}
 for(auto key:forbidden){for(auto value:{L"1",L"pure",L"observe",L"malformed"}){
  SetEnvironmentVariableW(key,value);check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::refused,"every conflicting route including unknown values refuses");}
  SetEnvironmentVariableW(key,L"0");check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::borrowed_placement,"exact disabled conflict permits combined mode");}
 check(t5::aim_startup_mode(false,&configured_hand)==t5::AimMode::refused,"failed native command hook cannot enable mode");
 for(auto value:{L"left",L"invalid",L"right-extra-long-invalid-value"}){SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",value);
  check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::refused,"combined mode refuses left or malformed hand without changing visual writer");}
 SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",L"right");
 SetLastError(0x35192847);check(!t5::configure_native_command_aim(nullptr,true)&&!t5::native_command_aim_enabled()&&GetLastError()==0x35192847,"full configurator retains identity refusal and LastError after valid combined policy");
 environment(false);for(auto key:{L"BOVR_COMMAND_AIM_PROBE",L"BOVR_XR_STEREO_PROBE",L"BOVR_INPUT_PROBE"})SetEnvironmentVariableW(key,L"1");
 clear_ready=borrow_ready=writer_ready=false;
 for(auto hand:{L"left",L"right"}){SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",hand);const auto reads=readiness_reads;
  check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::standalone&&readiness_reads==reads,"standalone A1 keeps both hands and does not query weapon readiness");}
 for(auto key:{L"BOVR_WEAPON_PROBE",L"BOVR_BORROWED_WEAPON_PLACEMENT"}){SetEnvironmentVariableW(key,L"1");
  check(t5::aim_startup_mode(true,&configured_hand)==t5::AimMode::refused,"standalone aim still excludes weapon route without explicit combined mode");SetEnvironmentVariableW(key,nullptr);}
 environment(false);
 SetEnvironmentVariableW(L"BOVR_COMMAND_AIM_PROBE",nullptr);
 check(!t5::configure_native_command_aim(GetModuleHandleW(nullptr),true)&&!t5::native_command_aim_enabled(),"default disabled");
 for(const auto* value:{L"left",L"right",L"invalid",L"left-extra-long-value",L""}){
  SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",value);xr::Hand hand{};const bool ok=t5::aim_hand(&hand);
  check((!wcscmp(value,L"invalid")||!wcscmp(value,L"left-extra-long-value"))?!ok:
        (ok&&hand==(!wcscmp(value,L"left")?xr::Hand::Left:xr::Hand::Right)),"established weapon-hand selection and malformed refusal");
 }
 SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",nullptr);
 for(const bool other:{false,true})for(int state=-1;state<53;++state){Fixture f;t5::AimNative copied{};
  store(reinterpret_cast<std::uintptr_t>(f.cg+0x8a364+(other?0x15c:0x158)),state);
  const bool accepted_state=(state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
  check(t5::copy_aim_native(f.aim.base,f.context(),&copied)==accepted_state,
        "actual native aim reader preserves primary model through pinned sprint/continuous/low-ready states only");
 }
 {
  Fixture f;t5::AimNative copied{};check(t5::copy_aim_native(f.aim.base,f.context(),&copied),"actual protected native-body/dvar/weapon reader accepts owned normal fields");
  auto b=f.bytes(),original=b;f.begin();f.finish(b);check(f.aim.serial==1,"accepted stream publishes once");
  for(unsigned i=0;i<52;++i)check(b[i]==(i>=0x20&&i<0x24?0:original[i]),"only four native gun angle bytes change");
  t5::CommandAimPublication p{};check(t5::read_native_command_aim_publication(1,&p)&&p.command.bytes==b&&p.command.rig.published_ms==990&&p.native_time==5000,"exact producer52, original rig, body retained without consumption authority");
  f.finish(b);check(f.aim.serial==1,"post cannot replay pending aim");
  b=original;f.begin();f.finish(b);check(b==original&&f.aim.serial==1,"duplicate whole producer callback rejected");
  ++t5::pre_snapshot.sequence;--t5::pre_snapshot.context.command_number;f.begin();f.finish(b);
  check(b==original&&f.aim.serial==1,"regressed command without native epoch rejected");
  ++t5::pre_snapshot.gameplay_epoch;f.next();f.begin();f.finish(b);
  check(f.aim.serial==2&&!t5::read_native_command_aim_publication(1,&p),"new native epoch permits command reset and retires old serial");
 }
 {
  Fixture f;auto b=f.bytes();b[0x18]=5;const auto original=b;f.begin();f.finish(b);
  check(b==original&&!f.aim.serial,"native requested weapon transition cannot inherit current predicted weapon admission");
  f.next();b[0x18]=4;f.begin();f.finish(b);check(f.aim.serial==1,"steady matching requested/current weapon recovers on fresh command");
 }
 for(unsigned count:{0u,1u,2u,4u,7u,8u,0x80000000u,0xffffffffu}) {Fixture f;store(reinterpret_cast<std::uintptr_t>(f.cg+0x8a364+0x1b0),count);
  auto b=f.bytes(),original=b;f.begin();f.finish(b);
  check((f.aim.serial==1)==(count<=7),"native aim admits valid retained native counters before and after the command");
  if(count>7)check(b==original,"unsupported counter preserves command bytes");
 }
 for(unsigned mode=0;mode<19;++mode){Fixture f;auto b=f.bytes(),original=b;f.begin();switch(mode){
  case 0:aim_now=1091;break;case 1:generation_current=false;break;case 2:++t5::pre_snapshot.gameplay_epoch;break;
  case 3:*reinterpret_cast<int*>(f.cg+0x8a348)=5001;break;case 4:*reinterpret_cast<int*>(f.cg+0x8c158)=5001;break;
  case 5:*reinterpret_cast<float*>(f.cg+0x8c134)=.5F;break;case 6:t5::pre_snapshot.context.native_override=true;break;
  case 7:*reinterpret_cast<int*>(f.dvar+0x18)=1;break;case 8:*reinterpret_cast<unsigned*>(f.cg+0x8a364+0xe0)=0x4000;break;
  case 9:*reinterpret_cast<int*>(f.def+0x1c)=1;break;case 10:*reinterpret_cast<int*>(f.cg+0x8a364+4)=5;break;
  case 11:mutation_fixture=&f;mutate_generation_check=true;break;case 12:store(f.aim.base+0x2bf5354,std::uintptr_t{});break;
  case 13:t5::pre_snapshot.context.viewangles[1]=30;break;case 14:t5::invalidate_native_command_aim();break;
  case 15:*reinterpret_cast<unsigned*>(f.cg+0x8a364+0x1b0)=0xffffffff;break;
  case 16:*reinterpret_cast<unsigned*>(f.cg+0x8a364+0x1b0)=0x80000000;break;
  case 17:*reinterpret_cast<int*>(f.def+0x30)=5;break;
  case 18:*reinterpret_cast<unsigned*>(f.cg+0x8a364+0x1b0)=1;break;}
  f.finish(b);check(b==original&&f.aim.serial==0,"post recopy/generation/epoch/body/special-path rejection preserves52bytes");mutation_fixture=nullptr;
 }
 for(unsigned mode=0;mode<8;++mode){Fixture f;auto b=f.bytes(),original=b;
  switch(mode){case 0:rig.xr_generation=4;break;case 1:rig.anchor_generation=4;break;
   case 2:actions.hands[1].aim.orientation_tracked=false;rig.frame.actions=actions;break;
   case 3:rig.published_ms=899;break;case 4:rig.tracking_anchor.orientation.w=-1;break;
   case 5:invalidate_at_check=generation_checks+2;break;case 6:generation_current=false;break;
   case 7:rig.frame.actions.sequence++;break;}
  f.begin();f.finish(b);check(b==original&&!f.aim.serial,"pre mismatch or final metadata validation refuses unchanged bytes");
 }
 {
  Fixture f;auto b=f.bytes(),original=b;actions.hands[1].aim.orientation_tracked=false;rig.frame.actions=actions;f.begin();f.finish(b);
  check(b==original&&!f.aim.serial,"tracking loss preserves native fields");
  f.next();actions.hands[1].aim.orientation_tracked=true;rig.frame.actions=actions;f.begin();f.finish(b);
  check(f.aim.serial==1,"fresh tracked command resumes stream");
  t5::CommandAimPublication p{};aim_now=1091;check(!t5::read_native_command_aim_publication(1,&p),"retained publication expiry never refreshed");
 }
 {
  Fixture f;auto b=f.bytes();for(unsigned i=0;i<130;++i){if(i)f.next();f.begin();f.finish(b);}
  t5::CommandAimPublication p{};check(f.aim.serial==130&&!t5::read_native_command_aim_publication(1,&p)&&
   !t5::read_native_command_aim_publication(2,&p)&&t5::read_native_command_aim_publication(3,&p)&&
   t5::read_native_command_aim_publication(130,&p),"fixed128-slot ring refuses evicted exact serials");
  check(t5::aim_guards(f.aim.base),"all13 pinned guard records admitted");
  for(const auto& g:t5::aim_code_guards)for(unsigned i=0;i<g.size;++i){f.image[g.address+i]^=1;
    check(!t5::aim_guards(f.aim.base),"each guarded instruction byte mutation refuses");f.image[g.address+i]^=1;}
  check(!t5::aim_guards(0),"guard protected read fault refuses");
 }
 {
  Fixture f;auto b=f.bytes(),original=b;f.begin();std::thread foreign([&]{t5::finish_native_command_aim(f.context(),b.data());});foreign.join();f.finish(b);
  check(b==original&&!f.aim.serial,"foreign-thread entry invalidates pending, never writes");
  f.next();f.begin();f.begin();f.finish(b);check(b==original&&!f.aim.serial,"nested pre invalidates both pending uses");
  f.next();f.begin();f.finish(b);check(f.aim.serial==1,"fresh command recovers after rejection");
 }
 {
  Fixture f;auto b=f.bytes(),original=b;DWORD old{};VirtualProtect(f.dvar,4096,PAGE_NOACCESS,&old);
  f.begin();f.finish(b);check(b==original&&!f.aim.serial,"SEH-contained protected reader fault preserves native command");VirtualProtect(f.dvar,4096,old,&old);
  auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));memcpy(page,b.data(),52);VirtualProtect(page,4096,PAGE_READONLY,&old);
  f.next();f.begin();t5::finish_native_command_aim(f.context(),page);check(!memcmp(page,b.data(),52)&&!f.aim.serial,"failed aligned native write leaves readonly bytes unchanged");VirtualFree(page,0,MEM_RELEASE);
 }
 {
  Fixture f;auto b=f.bytes();t5::original_create_cmd=reinterpret_cast<void*>(synthetic_native);
  check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"real input callbacks register once with existing bridge");
  const auto reads=rig_reads,old_actions=action_reads;ReturnState captured{};native_count=0;
  SetLastError(0x87654321);invoke_command(reinterpret_cast<void*>(t5::command_detour),b.data(),&captured);
  check(native_count==1&&native_client==0&&f.aim.serial==1,"actual original bridge executes native once with exact client argument and final aim delivery");
  check(rig_reads==reads+1&&action_reads==old_actions,"one atomic original rig/action read, no post pose read");
  check(captured.eax==reinterpret_cast<std::uint32_t>(b.data())&&(captured.flags&0x8d5)==0x44&&!memcmp(captured.xmm,pattern,16)&&captured.x87==1&&GetLastError()==0x87654321,"actual bridge preserves native result flags x87 XMM LastError around real aim trigonometry");
  check(*reinterpret_cast<int*>(b.data())==12345&&*reinterpret_cast<unsigned*>(b.data()+0x20)==0,"native time preserved and only post gun field selected");
  auto expected=f.bytes();*reinterpret_cast<int*>(expected.data())=12345;
  for(unsigned i=0;i<52;++i)if(i<0x20||i>=0x24)check(b[i]==expected[i],"real native forwarding preserves all48 unrelated result bytes");
 }
 {
  Fixture f;auto b=f.bytes();t5::original_create_cmd=reinterpret_cast<void*>(synthetic_fault);
  check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"fault fixture binds existing input callbacks");
  native_count=0;check(invoke_fault(reinterpret_cast<void*>(t5::command_detour),b.data())==0xe0421973&&native_count==1&&!f.aim.serial,
       "original native SEH propagates unchanged with no aim write");
  t5::original_create_cmd=reinterpret_cast<void*>(synthetic_native);f.next();ReturnState captured{};
  invoke_command(reinterpret_cast<void*>(t5::command_detour),b.data(),&captured);
  check(!f.aim.serial,"aborted native pending association cannot be revived by next return");
  f.next();invoke_command(reinterpret_cast<void*>(t5::command_detour),b.data(),&captured);
  check(f.aim.serial==1,"fresh native command recovers after aborted pending retirement");
 }
 {
  Fixture f;auto b=f.bytes();for(auto& hand:actions.hands){hand.grip.active=hand.grip.orientation_valid=hand.grip.position_valid=
   hand.grip.orientation_tracked=hand.grip.position_tracked=hand.stick.active=true;}
  rig.frame.actions=actions;t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),b.data());
  f.next();++actions.sequence;actions.hands[0].stick.current={0,1};actions.hands[1].trigger.current=1;rig.frame.actions=actions;
  recursive_controls=&f.controls;recursive_context=f.context();nest_dispatch=true;const auto reads=rig_reads;
  b=f.bytes();const auto original=b;t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),b.data());
  check(!nest_dispatch&&rig_reads==reads+1&&f.aim.serial==1,"nested native dispatch gets no newer pose and invalidates outer aim");
  check(b[0x1b]==127&&b[0x1c]==0&&!memcmp(b.data()+0x20,original.data()+0x20,4)&&f.controls.controls.forward==1,
   "reentrant refusal preserves outer movement controls and native gun bytes");
  f.next();++actions.sequence;actions.hands[1].trigger.current=0;actions.hands[1].stick.current={1,0};rig.frame.actions=actions;
  t5::before_native(&f.controls,f.context());
  check(*reinterpret_cast<float*>(f.image+t5::yaw_rva)!=0,"existing snap turn still updates native ordinary camera yaw");
  auto changed=f.context();changed.viewangles[1]=*reinterpret_cast<float*>(f.image+t5::yaw_rva);
  t5::after_native(&f.controls,changed,b.data());check(f.aim.serial==1,"same-command view change conservatively refuses aim");
 }
 {
  Fixture f;auto b=f.bytes(),original=b;rig_live=false;const auto reads=rig_reads;
  t5::before_native(&f.controls,f.context());t5::after_native(&f.controls,f.context(),b.data());
  check(b==original&&!f.aim.serial&&rig_reads==reads+1,"missing shared rig preserves all fields without newer fallback");
 }
 {
  Fixture f;auto command=f.bytes();f.begin();f.finish(command);
  f.aim.exact_profile=true;auto& stored=f.aim.publications[0];stored.dual_wield=true;stored.weapon=4;stored.dual_weapon=5;
  stored.invalidation_revision=f.aim.invalidation.load();
  t5::CommandAimPublication p{};const auto cg=reinterpret_cast<std::uintptr_t>(f.cg);int time{};memcpy(&time,command.data(),4);
  check(t5::read_native_command_aim_for_shot(command,cg,&p)&&p.command.publication_serial==1,
        "dual shot binds the exact final52 bytes to their original command rig");
  bool server_worker_read{};std::thread server_worker([&]{server_worker_read=t5::read_native_command_aim_for_shot(command,cg,&p);});server_worker.join();
  check(server_worker_read&&p.command.native_thread==GetCurrentThreadId(),
        "owned server worker can read exact retained input-producer command without changing its producer identity");
  ++stored.command.native_thread;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p),"foreign producer cannot claim retained shot publication");
  --stored.command.native_thread;
  const auto original_anchor=current_anchor;auto rebased_anchor=current_anchor;rebased_anchor.position.x+=1;
  body_commit_enabled=true;
  check(t5::commit_vr_room_scale(stored.command.rig,rebased_anchor,t5::RoomScaleView{}),
        "owned room-scale commit changes only the current anchor in the same session");
  check(!t5::command_input_generation_current(stored.command.rig)&&
        t5::read_native_command_aim_for_shot(command,cg,&p)&&
        !memcmp(&p.command.rig.tracking_anchor,&original_anchor,sizeof(original_anchor)),
        "exact consumed command retains authored body anchor after a later command changes current anchor");
  check(t5::read_native_command_aim_for_effect(time,cg,4,&p)&&p.command.bytes==command&&
        !memcmp(&p.command.rig.tracking_anchor,&original_anchor,sizeof(original_anchor)),
        "exact event effects retain the authored command and body after an owned room-scale rebase");
  check(!t5::read_native_command_aim_publication(1,&p),
        "generic current publication lookup remains strict after an owned room-scale rebase");
  stored.dual_wield=false;stored.dual_weapon=0;
  check(t5::read_native_command_aim_for_effect(time,cg,4,&p)&&p.command.bytes==command&&
        !memcmp(&p.command.rig.tracking_anchor,&original_anchor,sizeof(original_anchor)),
        "ordinary event effects retain immutable packed world aim across the same owned rebase");
  stored.dual_wield=true;stored.dual_weapon=5;
  ++stored.command.rig.anchor_generation;
  check(!t5::read_native_command_aim_for_effect(time,cg,4,&p),
        "recenter anchor generation still retires retained event effects");
  --stored.command.rig.anchor_generation;
  ++stored.command.rig.xr_generation;
  check(!t5::read_native_command_aim_for_effect(time,cg,4,&p),
        "replacement XR session generation still retires retained event effects");
  --stored.command.rig.xr_generation;
  generation_current=false;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p)&&!t5::read_native_command_aim_for_effect(time,cg,4,&p),
        "historical command and exact event still reject retired session generation");
  generation_current=true;current_anchor=original_anchor;
  check(t5::read_native_command_aim_for_effect(time,cg,4,&p),"predicted dual effect binds unique commandTime and primary weapon");
  stored.dual_wield=false;stored.dual_weapon=0;t5::EffectAimDiagnostic diagnostic{};
  check(t5::read_native_command_aim_for_effect(time,cg,4,&p,&diagnostic)&&!p.dual_wield&&
        diagnostic.matches==1&&!diagnostic.rejection&&diagnostic.publication==1,
        "ordinary hitscan effect can bind the same unique exact command without manufacturing a dual partner");
  check(!t5::read_native_command_aim_for_shot(command,cg,&p)&&
        t5::read_native_command_aim_for_shot(command,cg,&p,true)&&p.command.bytes==command&&!p.dual_wield,
        "ordinary server geometry needs explicit caller permission and binds the full retained command");
  bool ordinary_worker{};std::thread ordinary_server([&]{ordinary_worker=t5::read_native_command_aim_for_shot(command,cg,&p,true);});ordinary_server.join();
  check(ordinary_worker&&p.command.native_thread==GetCurrentThreadId(),
        "ordinary exact event can consume its input producer command on the native server worker");
  for(unsigned i=0;i<command.size();++i){auto changed=command;changed[i]^=1;
    check(!t5::read_native_command_aim_for_shot(changed,cg,&p,true),
        "ordinary shot refuses any changed consumed command byte including same-time gun words");}
  check(!t5::read_native_command_aim_for_shot(command,cg+4,&p,true),"ordinary shot refuses another cg owner");
  stored.dual_weapon=5;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary shot cannot inherit a dual partner");stored.dual_weapon=0;
  ++f.aim.invalidation;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary exact shot rejects retired revision");--f.aim.invalidation;
  generation_current=false;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary exact shot rejects ended tracking lifecycle");generation_current=true;
  ++stored.command.rig.anchor_generation;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary exact shot rejects a recentered anchor generation");--stored.command.rig.anchor_generation;
  f.aim.publications[1]=stored;f.aim.publications[1].command.publication_serial=2;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary exact shot rejects ambiguous retained command");f.aim.publications[1]={};
  const auto ordinary_clock=aim_now;aim_now=2001;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p,true),"ordinary exact shot cannot extend the1000ms consumed-command lifetime");aim_now=ordinary_clock;
  check(!t5::read_native_command_aim_for_effect(time+1,cg,4,&p,&diagnostic)&&diagnostic.matches==0&&
        (diagnostic.rejection&1)&&diagnostic.latest_command_time==time,
        "event mismatch diagnostics expose the latest retained time while refusing newer-pose substitution");
  stored.dual_wield=true;stored.dual_weapon=5;
  for(unsigned i=0;i<command.size();++i){auto changed=command;changed[i]^=1;
    check(!t5::read_native_command_aim_for_shot(changed,cg,&p),"any consumed command byte mismatch refuses shot pose");}
  check(!t5::read_native_command_aim_for_effect(time,cg,5,&p)&&!t5::read_native_command_aim_for_effect(time+1,cg,4,&p),
        "effect cannot select another weapon or another commandTime");
  f.aim.publications[1]=stored;f.aim.publications[1].command.publication_serial=2;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p)&&!t5::read_native_command_aim_for_effect(time,cg,4,&p),
        "duplicate retained fingerprints are ambiguous even when the newer pose agrees");
  f.aim.publications[1].command.rig.published_ms=1;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p),"stale duplicate still prevents picking a preferred pose");
  f.aim.publications[1]={};++f.aim.invalidation;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p)&&!t5::read_native_command_aim_for_effect(time,cg,4,&p),
        "selection pause or ownership retirement invalidates old shot and effect poses");
  --f.aim.invalidation;f.aim.shot_forgotten_time=time;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p),"evicted commandTime cutoff prevents an incomplete uniqueness claim");
  f.aim.shot_forgotten_time=-1;aim_now=1101;
  check(!t5::read_native_command_aim_for_effect(time,cg,4,&p)&&t5::read_native_command_aim_for_shot(command,cg,&p)&&
        p.command.rig.published_ms==990,"delayed server shot retains original producer pose while effects require fresh pose");
  aim_now=2000;check(t5::read_native_command_aim_for_shot(command,cg,&p),"exact server consumption admits native-bounded1000ms delay");
  aim_now=2001;check(!t5::read_native_command_aim_for_shot(command,cg,&p),"server consumption over1000ms cannot revive old command pose");
  aim_now=1050;stored.command.rig.published_ms=899;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p),"pose older than100ms when authored remains ineligible even for prompt server consumption");
  stored.command.rig.published_ms=1001;
  check(!t5::read_native_command_aim_for_shot(command,cg,&p),"future producer pose timestamp rejected without rewriting stored time");
 }
 check(t5::dual_hook_ensure_calls==0,"startup and ordinary-weapon command fixtures never request dual instruction writes");
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
