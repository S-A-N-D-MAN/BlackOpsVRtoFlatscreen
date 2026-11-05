// SPDX-License-Identifier: GPL-3.0-only
// Actual protected native reader, actual input callbacks and native bridge.
#define main baseline_input_main
#include "native_command_aim_adapter_tests.cpp"
#undef main
#include <bit>
#include <limits>
namespace {
void pair_fixture(Fixture& f) {
 f.aim.two_hand_support=true;
 for(auto& h:actions.hands){h.squeeze.active=true;h.squeeze.current=.8F;
  h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;}
 actions.hands[0].grip.pose.position={.15F,.08F,-.4F};
 rig.frame.actions=actions;
}
void copy_delta(xr::HandActionState& h,unsigned mode) {
 switch(mode){
 case 0:h.grip.active=!h.grip.active;break;
 case 1:h.grip.position_valid=!h.grip.position_valid;break;
 case 2:h.grip.position_tracked=!h.grip.position_tracked;break;
 case 3:h.grip.orientation_valid=!h.grip.orientation_valid;break;
 case 4:h.grip.orientation_tracked=!h.grip.orientation_tracked;break;
 case 5:h.grip.pose.position.x+=.01F;break;
 case 6:h.grip.pose.position.y+=.01F;break;
 case 7:h.grip.pose.position.z+=.01F;break;
 case 8:h.grip.pose.orientation.x+=.01F;break;
 case 9:h.grip.pose.orientation.y+=.01F;break;
 case 10:h.grip.pose.orientation.z+=.01F;break;
 case 11:h.grip.pose.orientation.w+=.01F;break;
 case 12:h.squeeze.active=!h.squeeze.active;break;
 case 13:h.squeeze.current+=.01F;break;
 case 14:h.squeeze.changed=!h.squeeze.changed;break;
 }
}
}
int main() {
 SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",nullptr);
 if(baseline_input_main())return 1;
 const auto before=checks;
 const std::array<const wchar_t*,9> required{L"BOVR_COMMAND_AIM_PROBE",L"BOVR_COMMAND_AIM_PLACEMENT_PROBE",
  L"BOVR_XR_STEREO_PROBE",L"BOVR_INPUT_PROBE",L"BOVR_COMMAND_PROBE",L"BOVR_WEAPON_PROBE",
  L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",L"BOVR_BORROWED_WEAPON_PROBE",L"BOVR_BORROWED_WEAPON_PLACEMENT"};
 const std::array<const wchar_t*,15> forbidden{L"BOVR_BULLET_PROBE",L"BOVR_BULLET_ENTRY_PROBE",L"BOVR_BULLET_TAIL_PROBE",
  L"BOVR_SHOT_COMMAND_PROBE",L"BOVR_CONSUMED_COMMAND_PROBE",L"BOVR_CONSUMED_TAIL_PROBE",L"BOVR_COMMAND_RIG_PROBE",
  L"BOVR_BULLET_SELECTION",L"BOVR_SHOT_HAPTICS",L"BOVR_WEAPON_PLACEMENT",L"BOVR_WEAPON_RESERVATION_PROBE",
  L"BOVR_WEAPON_LIFECYCLE_PROBE",L"BOVR_WEAPON_ANIMATION_PROBE",L"BOVR_WEAPON_BARREL_PROBE",L"BOVR_WEAPON_PROFILE_PROBE"};
 for(auto key:required)SetEnvironmentVariableW(key,L"1");
 for(auto key:forbidden)SetEnvironmentVariableW(key,L"0");
 SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",L"right");SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",L"1");
 clear_ready=borrow_ready=writer_ready=true;xr::Hand hand{};
 check(t5::aim_startup_mode(true,&hand)==t5::AimMode::two_hand_support&&hand==xr::Hand::Right,"exact T2 chain admits distinct mode3");
 for(auto key:required){for(auto value:{static_cast<const wchar_t*>(nullptr),L"0",L"true",L"2",L"1-extra-long"}){
  SetEnvironmentVariableW(key,value);check(t5::aim_startup_mode(true,&hand)==t5::AimMode::refused,"T2 cannot bypass any exact A2 prerequisite");}
  SetEnvironmentVariableW(key,L"1");}
 for(auto key:forbidden){for(auto value:{L"1",L"pure",L"observe",L"malformed"}){
  SetEnvironmentVariableW(key,value);check(t5::aim_startup_mode(true,&hand)==t5::AimMode::refused,"T2 cannot enable an excluded native or profile route");}
  SetEnvironmentVariableW(key,L"0");}
 for(auto value:{static_cast<const wchar_t*>(nullptr),L"",L"left",L"invalid",L"right-long"}){
  SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",value);check(t5::aim_startup_mode(true,&hand)==t5::AimMode::refused,"T2 requires explicit exact right hand");}
 SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",L"right");
 for(auto value:{L"true",L"2",L"pure",L"1-extra-long"}){
  SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",value);check(t5::aim_startup_mode(true,&hand)==t5::AimMode::refused,"unknown support request cannot silently become legacy mode");}
 for(auto value:{static_cast<const wchar_t*>(nullptr),L"0"}){
  SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",value);check(t5::aim_startup_mode(true,&hand)==t5::AimMode::borrowed_placement,"off support retains old A2 mode exactly");}
 SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",L"1");
 for(unsigned mask=0;mask<8;++mask){clear_ready=(mask&1)!=0;borrow_ready=(mask&2)!=0;writer_ready=(mask&4)!=0;
  check((t5::aim_startup_mode(true,&hand)==t5::AimMode::two_hand_support)==(mask==7),"T2 requires all actual writer readiness, not only requested env");}
 check(t5::aim_startup_mode(false,&hand)==t5::AimMode::refused,"T2 refuses missing existing native command hook");
 for(auto key:required)SetEnvironmentVariableW(key,nullptr);for(auto key:forbidden)SetEnvironmentVariableW(key,nullptr);
 SetEnvironmentVariableW(L"BOVR_TWO_HAND_SUPPORT_PROBE",nullptr);SetEnvironmentVariableW(L"BOVR_WEAPON_HAND",nullptr);
 {
  Fixture f;pair_fixture(f);f.aim.two_hand_support=false;auto b=f.bytes();
  rig.frame.actions.hands[0].grip.pose.position={5,6,7};f.begin();f.finish(b);
  check(f.aim.serial==1&&*reinterpret_cast<unsigned*>(b.data()+0x20)==0,"disabled support keeps exact original selected-aim-only join despite irrelevant support difference");
 }
 {
  Fixture f;check(!t5::weapon_two_hand_support_enabled(),"default immutable visual policy is false");pair_fixture(f);
  check(t5::weapon_two_hand_support_enabled(),"accepted shared policy reaches actual visual adapter getter");
  auto b=f.bytes();t5::original_create_cmd=reinterpret_cast<void*>(synthetic_native);
  check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"actual paired input binds existing callback bridge");
  const auto reads=rig_reads,old_actions=action_reads;ReturnState captured{};native_count=0;
  SetLastError(0x84726351);invoke_command(reinterpret_cast<void*>(t5::command_detour),b.data(),&captured);
  t5::CommandAimPublication p{};
  check(native_count==1&&native_client==0&&f.aim.serial==1&&t5::read_native_command_aim_publication(1,&p)&&p.aim.support.paired,
   "actual native command executes once and final paired DWORD publishes with original association");
  check(rig_reads==reads+1&&action_reads==old_actions&&p.command.rig.published_ms==990&&
   !memcmp(&p.command.rig.frame.actions,&rig.frame.actions,sizeof(actions)),"one atomic pre snapshot retains exact both grip/squeeze values and original age");
  check(captured.eax==reinterpret_cast<std::uint32_t>(b.data())&&(captured.flags&0x8d5)==0x44&&!memcmp(captured.xmm,pattern,16)&&captured.x87==1&&GetLastError()==0x84726351,
   "native return flags XMM x87 LastError survive actual pair math");
  auto expected=f.bytes();*reinterpret_cast<int*>(expected.data())=12345;
  for(unsigned i=0;i<52;++i)if(i<0x20||i>=0x24)check(b[i]==expected[i],"all48 other completed native command bytes preserved with paired steering");
 }
 for(unsigned h=0;h<2;++h)for(unsigned mode=0;mode<15;++mode){
  Fixture f;pair_fixture(f);auto b=f.bytes(),old=b;copy_delta(rig.frame.actions.hands[h],mode);f.begin();f.finish(b);
  check(b==old&&!f.aim.serial,"mismatched owned grip/squeeze copy rejects instead of manufacturing pair or fallback");
  f.next();rig.frame.actions=actions;f.begin();f.finish(b);check(f.aim.serial==1,"fresh original copy restores after identity rejection");
 }
 {
  Fixture f;pair_fixture(f);auto b=f.bytes();f.begin();const auto original=actions;
  actions.hands[0].grip.pose.position={9,8,7};actions.hands[1].squeeze.current=0;rig.frame.actions=actions;
  const auto reads=rig_reads;f.finish(b);t5::CommandAimPublication p{};
  check(f.aim.serial==1&&t5::read_native_command_aim_publication(1,&p)&&p.aim.support.paired&&rig_reads==reads&&
   !memcmp(&p.command.rig.frame.actions,&original,sizeof(actions)),"post uses its original owned pair despite newer global action values");
 }
 for(unsigned mode=0;mode<8;++mode){Fixture f;pair_fixture(f);auto b=f.bytes(),old=b;
  if(mode==0)actions.hands[0].grip.position_tracked=false;
  if(mode==1)actions.hands[0].squeeze.current=.2F;
  if(mode==2)actions.hands[1].squeeze.current=.2F;
  if(mode==3)actions.hands[0].grip.pose.position.x=std::numeric_limits<float>::quiet_NaN();
  if(mode==4)actions.hands[1].aim.orientation_tracked=false;
  if(mode==5)actions.focused=false;
  rig.frame.actions=actions;f.begin();if(mode==6)generation_current=false;if(mode==7)aim_now=1091;f.finish(b);
  check(mode==2?(f.aim.serial==1&&*reinterpret_cast<unsigned*>(b.data()+0x20)!=0):mode<4?(f.aim.serial==1&&*reinterpret_cast<unsigned*>(b.data()+0x20)==0):(b==old&&!f.aim.serial),
   "support-only loss uses exact right fallback while right/focus/stale/generation preserve native result");
  f.next();aim_now=1000;generation_current=true;actions.focused=true;actions.hands[1].aim.orientation_tracked=true;pair_fixture(f);f.begin();f.finish(b);
  t5::CommandAimPublication p{};check(t5::read_native_command_aim_publication(f.aim.serial,&p)&&p.aim.support.paired,"fresh held current pair recovers without latch or newer post read");
 }
 for(unsigned mode=0;mode<6;++mode){Fixture f;pair_fixture(f);auto b=f.bytes(),old=b;f.begin();
  switch(mode){case 0:f.begin();break;
   case 1:{std::thread foreign([&]{t5::finish_native_command_aim(f.context(),b.data());});foreign.join();break;}
   case 2:++t5::pre_snapshot.gameplay_epoch;break;
   case 3:b[0x18]=5;old=b;break;
   case 4:invalidate_at_check=generation_checks+2;break;
   case 5:*reinterpret_cast<int*>(f.def+0x1c)=1;break;}
  f.finish(b);check(b==old&&!f.aim.serial,"paired path keeps native weapon generation foreign nested and epoch refusals");
 }
 {
  Fixture f;pair_fixture(f);auto b=f.bytes(),old=b;f.begin();DWORD protection{};
  VirtualProtect(f.dvar,4096,PAGE_NOACCESS,&protection);f.finish(b);
  check(b==old&&!f.aim.serial,"protected observer recopy failure does not write paired or fallback command");
  DWORD unused{};VirtualProtect(f.dvar,4096,protection,&unused);f.next();f.begin();f.finish(b);
  check(f.aim.serial==1,"fresh original snapshot recovers after contained read fault");
 }
 {
  Fixture f;pair_fixture(f);auto b=f.bytes(),old=b;t5::original_create_cmd=reinterpret_cast<void*>(synthetic_fault);
  check(t5::set_command_callbacks({&f.controls,t5::before_native,t5::after_native}),"paired native fault fixture binds actual callbacks");
  native_count=0;check(invoke_fault(reinterpret_cast<void*>(t5::command_detour),b.data())==0xe0421973&&native_count==1&&!f.aim.serial&&b==old,
   "native exception remains unchanged with no paired command write");
 }
 std::printf("two_hand_input_checks=%u cumulative=%u failures=%u new_hooks=0 native_retail_calls=0\n",checks-before,checks,failures);
 return failures?1:0;
}
