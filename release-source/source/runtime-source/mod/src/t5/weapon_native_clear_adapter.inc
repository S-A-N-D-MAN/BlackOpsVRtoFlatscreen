// SPDX-License-Identifier: GPL-3.0-only
// Included once by weapon_barrel_probe.cpp to reuse the reviewed current-borrow
// provider and exact profile rows. No native pointer leaves a callback.
#include "t5/manual_grenade_adapter.hpp"
#include "t5/manual_grenade_renderer.hpp"
#include "t5/manual_grenade_pose.hpp"
namespace bovr::t5 {
namespace {
anim::NativeClearPair native_clear_pair;
std::atomic_flag native_clear_busy=ATOMIC_FLAG_INIT;
std::uint64_t native_clear_last_sample{},native_clear_samples{},native_clear_reports{},native_clear_attempt_serial{};
std::array<std::uint64_t,16> native_clear_status_counts{};
// Value-only diagnostic receipts, guarded by native_clear_busy. These never
// authorize a borrow, reservation, native calculation, or frame submission.
std::uint64_t normal_gate_skips{},normal_gate_return_skips{},normal_gate_last_serial{},normal_gate_last_token{};
enum class NativeClearStatus:unsigned {accepted,context,command,profile,borrow,not_empty,calculation,changed,geometry,unpaired,post_unpaired,post_accepted,post_failed,throttled};
void prepare_borrowed_visual_pose(const input::ReservedWeaponFrame&,const input::BorrowedPlacementValues&,unsigned,std::uint64_t,bool) noexcept;
void apply_borrowed_visual_pose(const NativeWeaponContext&,const anim::ClearValue&);
anim::ClearComparisonKey native_clear_key(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,const PreCommandSnapshot& command) noexcept {
    anim::ClearComparisonKey key{};
    key.command_epoch=command.gameplay_epoch;key.command_sequence=command.sequence;key.thread=c.native_thread;
    key.weapon=c.weapon_index;key.profile_row=profile.profile_row;key.profile_revision=profile.profile_revision;key.native_time=c.native_time;key.frame_time=c.native_frame_time;
    key.command_number=c.command_number;key.command_clock=c.command_clock;key.skeleton_clock=b.skeleton_clock;
    key.weapon_state=c.weapon_state;key.other_weapon_state=c.other_weapon_state;key.gun_bone=profile.gun_bone;key.flash_bone=b.tag_bone;
    key.bone_count=b.bone_count;key.pool_index=static_cast<unsigned>(b.pool_index);
    key.opaque_identity={static_cast<std::uint32_t>(c.cg_identity),static_cast<std::uint32_t>(c.viewmodel_identity),static_cast<std::uint32_t>(c.dobj_identity),
                         static_cast<std::uint32_t>(c.variant_identity),static_cast<std::uint32_t>(c.definition_identity),static_cast<std::uint32_t>(b.models_table)};
    return key;
}
}
#include "t5/client_gun_angles_adapter.inc"
#include "t5/weapon_borrowed_adapter.inc"
#include "t5/manual_reload_pose_adapter.inc"
#include "t5/manual_grenade_pose_adapter.inc"
namespace {
void native_clear_report(NativeClearStatus status,const PoseClearPoint& point,const NativeWeaponContext& c,
        const anim::ClearValue* value=nullptr,const anim::ClearValue* before=nullptr,const anim::Difference* difference=nullptr,unsigned native_status=0) noexcept {
    const auto index=static_cast<unsigned>(status);const auto count=++native_clear_status_counts[index];
    if(native_clear_reports>=512||(count>6&&count%120))return;++native_clear_reports;
    const anim::ClearValue empty{};const anim::Difference zero{};if(!value)value=&empty;if(!before)before=&empty;if(!difference)difference=&zero;
    trace("t5_weapon_native_clear","report=%llu serial=%llu invocation=%llu caller=%u status=%u count=%llu native_status=%u camera_serial=%llu thread=%lu time=%d frame_time=%d command=%d clock=%d epoch=%llu command_sequence=%llu skeleton_clock=%d row=%u profile_revision=%u weapon=%u state=%d gun=%u flash=%u old_stamp=%d final_stamp=%d calculated=%u gun_position_delta=%.9g gun_angle_rad=%.9g flash_position_delta=%.9g flash_angle_rad=%.9g native_clear_witness=completed_original all60_bits_required_zero=1 placement_source=persistent_pose_copy observer_placement_writes=0 scratch_writer_enabled=%u shot_writes=0 forced_cache_clears=0 xr_authority=0",
        native_clear_reports,point.serial,value->invocation_token,static_cast<unsigned>(point.caller),index,count,native_status,before->serial,c.native_thread,c.native_time,c.native_frame_time,
        c.command_number,c.command_clock,value->key.command_epoch,value->key.command_sequence,value->key.skeleton_clock,value->key.profile_row,value->key.profile_revision,c.weapon_index,c.weapon_state,
        value->key.gun_bone,value->key.flash_bone,value->copy.old_stamp,value->copy.final_stamp,value->copy.calculated,
        difference->gun_position,difference->gun_angle_radians,difference->flash_position,difference->flash_angle_radians,borrowed_writer_enabled.load());
    if(value->valid&&(status==NativeClearStatus::accepted||status==NativeClearStatus::post_accepted||status==NativeClearStatus::unpaired)){
        const auto raw=[&](const char* phase,const anim::ClearValue& sample){std::array<std::uint32_t,8> gun{},flash{};memcpy(gun.data(),&sample.copy.gun,32);memcpy(flash.data(),&sample.copy.flash,32);
            trace("t5_weapon_native_clear_raw","serial=%llu phase=%s gun_u32=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X flash_u32=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X pose_origin=%.9g,%.9g,%.9g pose_angles=%.9g,%.9g,%.9g",
                sample.serial,phase,gun[0],gun[1],gun[2],gun[3],gun[4],gun[5],gun[6],gun[7],flash[0],flash[1],flash[2],flash[3],flash[4],flash[5],flash[6],flash[7],
                sample.copy.pose.origin.x,sample.copy.pose.origin.y,sample.copy.pose.origin.z,sample.copy.pose.angles.x,sample.copy.pose.angles.y,sample.copy.pose.angles.z);};
        if(before->valid)raw(status==NativeClearStatus::post_accepted?"normal_before_scene":"camera",*before);
        raw(status==NativeClearStatus::post_accepted?"after_weapon":"normal_or_camera",*value);
    }
}
anim::LockedStatus capture_native_clear(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,
        std::uint64_t serial,bool require_clear,anim::LockedCopy* output) {
    const auto scope=read_weapon_native_clear_scope();
    AnimationNative provider{&c,b,&profile,{},serial,scope.valid?scope.token:0};
    if(!animation_assembly_copy(b,profile,&provider.assembly))return anim::LockedStatus::identity;
    const anim::LockedHost host{&provider,animation_lock,animation_unlock,profile.profile_row>=borrowed_profile::runtime_row_base?borrowed_state:animation_state,animation_cache,animation_hierarchy,animation_pose,animation_skeleton,animation_copy};
    return anim::capture_locked(host,{b.dobj,profile.gun_bone,b.tag_bone,b.bone_count,b.skeleton_clock,false,require_clear},output);
}
bool native_clear_current(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,const PreCommandSnapshot& command,
                          const PoseClearPoint& point,std::uint64_t started,std::uint64_t invocation_token) noexcept {
    const auto now=GetTickCount64();PreCommandSnapshot last{};NativeWeaponContext current{};NativeBorrow borrow{};AnimationProfile current_profile{};
    const auto scope=read_weapon_native_clear_scope();
    const bool scope_matches=point.caller==PoseClearCaller::camera?!scope.depth:
        scope.valid&&scope.depth==1&&invocation_token&&scope.token==invocation_token;
    return scope_matches&&pose_clear_serial()==point.serial&&now>=started&&now-started<=100&&
        native_precommand(nullptr,&last)&&fresh_command(c,last,now)&&last.sequence==command.sequence&&last.gameplay_epoch==command.gameplay_epoch&&
        copy_pose_clear_weapon_context(point.object,point.local_client,point.serial,&current)&&supported(current,point.thread)&&animation_extra_context(current)&&
        animation_profile(current,now,&current_profile)&&current_profile==profile&&native_borrow(nullptr,current,&borrow)&&borrow==b&&
        native_clear_key(current,borrow,current_profile,last)==native_clear_key(c,b,profile,command);
}
void native_clear_work(const PoseClearPoint& point) {
    if(apply_manual_grenade_pose(point)){native_clear_pair.invalidate();return;}
    const auto now=GetTickCount64();const bool camera=point.caller==PoseClearCaller::camera;
    normal_gate_last_serial=normal_gate_last_token=0;
    native_clear_pair.begin(point.serial,camera);
    if(borrowed_enabled)borrowed_camera.clear_entry(point.serial);
    NativeWeaponContext c{};
    const auto scope=read_weapon_native_clear_scope();
    if((camera&&scope.depth)||(!camera&&(!scope.valid||scope.depth!=1||!scope.token))){native_clear_pair.invalidate();return;}
    if(point.caller==PoseClearCaller::other||point.local_client||!point.object){native_clear_pair.invalidate();return;}
    if(camera){
        if(!borrowed_writer::prepare_camera(borrowed_writer_enabled.load(),native_clear_samples,native_clear_last_sample,now))return;
        native_clear_last_sample=now;native_clear_attempt_serial=point.serial;++native_clear_samples;
    }else if(point.serial!=native_clear_attempt_serial+1||!native_clear_last_sample||now<native_clear_last_sample||now-native_clear_last_sample>100){native_clear_pair.invalidate();return;}
    if(!copy_pose_clear_weapon_context(point.object,point.local_client,point.serial,&c)||!supported(c,point.thread)||!animation_extra_context(c)){
        native_clear_report(NativeClearStatus::context,point,c);native_clear_pair.invalidate();return;}
    PreCommandSnapshot command{};AnimationProfile profile{};NativeBorrow borrow{};
    if(!native_precommand(nullptr,&command)||!fresh_command(c,command,now)){native_clear_report(NativeClearStatus::command,point,c);native_clear_pair.invalidate();return;}
    // The wrapper already attempted its reservation before this normal clear.
    // An inactive claim cannot feed borrowed normal/complete placement. Keep
    // camera preparation, current primary-grenade visuals, passive mode and
    // every active claim (including tracking-loss stock placement) unchanged.
    if(!camera&&!anim::borrowed_normal_preparation_required(borrowed_normal_gate_enabled.load(),
            borrowed_writer_enabled.load(),c.manual_grenade_primary_visual,borrowed_pending.active)) {
        native_clear_pair.invalidate();borrowed_camera.invalidate();native_clear_attempt_serial=0;
        normal_gate_last_serial=point.serial;normal_gate_last_token=scope.token;
        const auto skipped=++normal_gate_skips;
        if(skipped<=6||skipped%120==0)trace("t5_borrowed_normal_gate",
            "normal_skips=%llu return_skips=%llu camera_skips=0 serial=%llu token=%llu weapon=%u primary_grenade=0 active_claim=0 phase=normal profile_reads=0 native_original_calls=unchanged admission_mutated=0",
            skipped,normal_gate_return_skips,point.serial,scope.token,c.weapon_index);
        return;
    }
    if(!animation_profile(c,now,&profile)){native_clear_report(NativeClearStatus::profile,point,c);native_clear_pair.invalidate();return;}
    if(!native_borrow(nullptr,c,&borrow)||borrow.tag_model!=1||borrow.tag_local_bone!=borrowed_profile::shape(profile.profile_row)->flash_local||borrow.tag_bone!=borrowed_profile::shape(profile.profile_row)->flash){native_clear_report(NativeClearStatus::borrow,point,c);native_clear_pair.invalidate();return;}
    anim::ClearValue value{};value.key=native_clear_key(c,borrow,profile,command);value.serial=point.serial;value.published_ms=now;value.invocation_token=camera?0:scope.token;
    const auto result=capture_native_clear(c,borrow,profile,point.serial,true,&value.copy);
    if(result!=anim::LockedStatus::accepted){native_clear_report(result==anim::LockedStatus::native_clear_not_empty?NativeClearStatus::not_empty:NativeClearStatus::calculation,point,c,&value,nullptr,nullptr,static_cast<unsigned>(result));native_clear_pair.invalidate();return;}
    if(!native_clear_current(c,borrow,profile,command,point,now,value.invocation_token)){native_clear_report(NativeClearStatus::changed,point,c,&value);native_clear_pair.invalidate();return;}
    // The grenade's exact primary visual has no paired camera transaction.  Its
    // completed native clear still owns a current primary DObj and profile, so
    // pose only that DObj's support hand and leave ordinary scene submission in
    // place.  A rejection falls through to the untouched native pose.
    if(apply_manual_grenade_primary_hand_pose(point,c,borrow,profile,command,value)){
        native_clear_pair.invalidate();return;
    }
    anim::LocalGeometry geometry{};
    if(!anim::derive_local(value.copy.gun,value.copy.flash,value.copy.pose,&geometry)){native_clear_report(NativeClearStatus::geometry,point,c,&value);native_clear_pair.invalidate();return;}
    value.valid=true;
    if(camera){native_clear_pair.camera(value);if(borrowed_enabled)borrowed_camera.publish(value);native_clear_report(NativeClearStatus::accepted,point,c,&value);return;}
    anim::ClearValue previous{};anim::LocalGeometry pre_geometry{};anim::Difference difference{};
    const bool pair=native_clear_pair.normal(value,&previous)&&anim::derive_local(previous.copy.gun,previous.copy.flash,previous.copy.pose,&pre_geometry)&&anim::difference(pre_geometry,geometry,&difference);
    native_clear_report(pair?NativeClearStatus::accepted:NativeClearStatus::unpaired,point,c,&value,&previous,&difference);
    if(borrowed_writer_enabled)borrowed_normal_work(c,value);
}
void native_clear_callback(const PoseClearPoint& point) {
    const auto error=GetLastError();if(!native_clear_enabled){SetLastError(error);return;}
    if(native_clear_busy.test_and_set()){SetLastError(error);return;} // Serial still advanced in the hook.
    __try {native_clear_work(point);}
    __finally {native_clear_busy.clear();SetLastError(error);}
}
void native_clear_return_work(const NativeWeaponContext& c,std::uint64_t invocation_token) {
    const auto now=GetTickCount64(),serial=pose_clear_serial();PreCommandSnapshot command{};AnimationProfile profile{};NativeBorrow borrow{};
    const auto scope=read_weapon_native_clear_scope();
    if(!invocation_token||!scope.valid||scope.depth!=1||scope.token!=invocation_token){native_clear_pair.invalidate();return;}
    if(!native_clear_pair.has_normal(serial)) {
        if(borrowed_normal_gate_enabled.load()&&normal_gate_last_serial==serial&&normal_gate_last_token==invocation_token) {
            normal_gate_last_serial=normal_gate_last_token=0; // Diagnostic receipt is single-use.
            const auto skipped=++normal_gate_return_skips;
            if(skipped<=6||skipped%120==0)trace("t5_borrowed_normal_gate",
                "normal_skips=%llu return_skips=%llu camera_skips=0 serial=%llu token=%llu weapon=%u phase=return profile_reads=0 native_original_calls=unchanged admission_mutated=0",
                normal_gate_skips,skipped,serial,invocation_token,c.weapon_index);
        }
        return;
    }
    if(!supported(c,GetCurrentThreadId())||!animation_extra_context(c)||!native_precommand(nullptr,&command)||!fresh_command(c,command,now)||
       !animation_profile(c,now,&profile)||!native_borrow(nullptr,c,&borrow)){native_clear_pair.invalidate();return;}
    const auto key=native_clear_key(c,borrow,profile,command);anim::ClearValue previous{};
    if(!native_clear_pair.take_normal(key,serial,invocation_token,now,&previous))return;
    PoseClearPoint point{0x400000,0x50b83d,c.dobj_identity,0,GetCurrentThreadId(),serial,PoseClearCaller::normal_weapon};
    anim::ClearValue value{key,{},serial,previous.published_ms,false,invocation_token};
    const auto result=capture_native_clear(c,borrow,profile,serial,false,&value.copy);
    if(result!=anim::LockedStatus::accepted||!native_clear_current(c,borrow,profile,command,point,previous.published_ms,invocation_token)){
        native_clear_report(NativeClearStatus::post_failed,point,c,&value,&previous,nullptr,static_cast<unsigned>(result));return;}
    anim::LocalGeometry before{},after{};anim::Difference difference{};
    if(!anim::derive_local(previous.copy.gun,previous.copy.flash,previous.copy.pose,&before)||!anim::derive_local(value.copy.gun,value.copy.flash,value.copy.pose,&after)||!anim::difference(before,after,&difference)){
        native_clear_report(NativeClearStatus::geometry,point,c,&value,&previous);return;}
    value.valid=true;native_clear_report(NativeClearStatus::post_accepted,point,c,&value,&previous,&difference);
    if(borrowed_enabled)borrowed_complete_work(c,value);
}
}
bool configure_weapon_native_clear_probe(HMODULE image,bool prerequisites) noexcept {
    const auto error=GetLastError();bool ok{};
    try{wchar_t flag[8]{},writer[8]{},lifetime[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",flag,8);
        GetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",writer,8);GetEnvironmentVariableW(L"BOVR_WEAPON_LIFECYCLE_PROBE",lifetime,8);
        if(prerequisites&&!wcscmp(flag,L"1")&&wcscmp(writer,L"1")&&wcscmp(lifetime,L"1")&&animation_native_guards()&&install_pose_clear_probe(image,native_clear_callback)&&enable_weapon_native_clear_scope()){
            native_clear_enabled=true;ok=true;trace("t5_weapon_native_clear_configured","mode=passive exact_native_clear=1 all60_bits_empty=1 old_clock_gate_unchanged=1 max_camera_samples=512 interval_ms=100 placement_writes=0 shot_writes=0 forced_cache_clears=0");}}
    catch(...){}SetLastError(error);return ok;
}
bool weapon_native_clear_probe_enabled() noexcept{return native_clear_enabled.load();}
void observe_native_clear_weapon_return(const NativeWeaponContext& c,std::uint64_t invocation_token) {
    const auto error=GetLastError();if(!native_clear_enabled||native_clear_busy.test_and_set()){SetLastError(error);return;}
    __try{native_clear_return_work(c,invocation_token);}
    __finally{native_clear_busy.clear();SetLastError(error);}
}
}
