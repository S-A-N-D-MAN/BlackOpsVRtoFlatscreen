// SPDX-License-Identifier: GPL-3.0-only
// Existing normal-clear callback only. No additional instruction hook and no
// asset/bind-pose changes: only the current, locked, evaluated skeleton is posed.
namespace {
reload_pose::Frame reload_visual_frame;
std::array<std::uint64_t,10> reload_visual_counts{};
std::array<std::uint64_t,7> pimax_glove_stage_counts{};
void prepare_borrowed_visual_pose(const input::ReservedWeaponFrame& reserved,const input::BorrowedPlacementValues& proposal,
    unsigned weapon,std::uint64_t epoch,bool dual) noexcept {
    reload_visual_frame={};input::manual::Output reload{};
    const bool manual=manual_reload_enabled();
    const auto now=GetTickCount64();
    if(!manual||!read_manual_reload_visual(&reload)){
        // A stale visual publication cannot turn an active paired reload back
        // into a ready gun grip. The interaction owner keeps the support hand.
        if(manual&&dual&&manual_reload_uses_offhand()){publish_hand_mesh(0,0,0,0);return;}
        // Automatic ammo reload and reloadless/empty-reserve weapons still
        // render the tracked support glove. This ready-only visual ticket does
        // not configure the manual interaction owner or change native ammo.
        reload.supported=true;reload.weapon=weapon;reload.epoch=epoch;
        reload.generation=reserved.identity.generation;reload.updated_ms=now;
    }
    // Every new ready dual frame must earn its current controller pose before
    // retaining the split-hand mesh. Active paired reload keeps its old path.
    if(!reload_pose::make_frame(reserved,proposal,reload,weapon,epoch,now,&reload_visual_frame,dual)&&
       dual&&reload.stage==input::manual::Stage::ready)publish_hand_mesh(0,0,0,0);
}
bool borrowed_visual_pose_current(const reload_pose::Frame& f,const input::WeaponFrameIdentity& identity,
    std::uint64_t epoch,bool normal_verified,const NativeWeaponContext& c,bool manual,std::uint64_t now) noexcept {
    if(!f.valid||f.identity!=identity||f.weapon!=c.weapon_index||f.epoch!=epoch||
       !f.published_ms||now<f.published_ms||now-f.published_ms>100||!normal_verified||
       (f.dual_ready&&(!c.dual_wield||!reload_pose::independent_dual_pistol(c.weapon_type,c.weapon_class)||
        (manual&&manual_reload_uses_offhand())||(!manual&&(!input::paired_reload::ordinary_state(c.weapon_state)||
        !input::paired_reload::ordinary_state(c.other_weapon_state))))))return false;
    input::manual::Output reload{};
    if(manual&&read_manual_reload_visual(&reload))return reload.weapon==f.weapon&&reload.hand==1&&reload.epoch==f.epoch&&
        reload.generation==f.identity.generation&&reload.transaction==f.transaction&&reload.stage==f.stage;
    // Without a current physical reload publication only the synthetic ready
    // ticket is authority. Automatic mode cannot inherit a manual transaction.
    return f.transaction==0&&f.stage==input::manual::Stage::ready;
}
bool reload_computed(std::uintptr_t object,unsigned count) noexcept {
    __try {for(unsigned i=0;i<count;++i)if(!(load<std::uint32_t>(object+0x3c+4*(i>>5))&(0x80000000u>>(i&31))))return false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool reload_write_matrices(std::uintptr_t address,unsigned count,const reload_pose::Result& value) noexcept {
    const auto bytes=count*sizeof(anim::NativeMat32);if(address<0x10000||address>UINT32_MAX-bytes||count>value.matrices.size())return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||info.State!=MEM_COMMIT||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
       address+bytes>reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize)return false;
    __try {for(unsigned i=0;i<count;++i)if(value.changed[i])
        memcpy(reinterpret_cast<void*>(address+i*sizeof(anim::NativeMat32)),&value.matrices[i],sizeof(anim::NativeMat32));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
unsigned reload_visual_locked(AnimationNative& native,const anim::LockedRequest& request,const anim::LockedCopy& expected,
    const reload_pose::Frame& frame,reload_pose::Result* edited) {
    anim::CacheCopy before{},computed{},last{};anim::LockedCopy copied{};
    if(!borrowed_state(&native,request,&before)||before.clock!=request.clock||before.stamp!=request.clock||!before.matrices||
       !before.gun_computed||!before.flash_computed||!animation_copy(nullptr,request,before.matrices,&copied.gun,&copied.flash))return 1;
    copied.pose=before.pose;copied.clock=before.clock;if(!anim::same_raw(copied,expected))return 1;
    // Request every real bone of this exact admitted object. The stock evaluator
    // preserves already-computed gun/muzzle ancestors and fills missing limbs.
    std::uint32_t bits[5]{};for(unsigned i=0;i<request.bone_count;++i)bits[i>>5]|=0x80000000u>>(i&31);
    animation_pose(&native,request.object,bits);
    animation_skeleton(&native,request.object,bits);
    if(!borrowed_state(&native,request,&computed)||computed.clock!=before.clock||computed.stamp!=before.stamp||
       computed.matrices!=before.matrices||!anim::same_pose(before.pose,computed.pose)||!reload_computed(request.object,request.bone_count))return 2;
    std::array<anim::NativeMat32,borrowed_profile::max_profile_bones> originals{};
    if(request.bone_count>originals.size()||!borrowed_protected_copy(nullptr,computed.matrices,originals.data(),request.bone_count*sizeof(anim::NativeMat32)))return 3;
    if(memcmp(&originals[request.gun_bone],&expected.gun,32)||memcmp(&originals[request.flash_bone],&expected.flash,32))return 3;
    if(!reload_pose::pose(native.profile->profile_row,frame,{originals.data(),request.bone_count},edited))return 4;
    if(!borrowed_state(&native,request,&last)||last.clock!=computed.clock||last.stamp!=computed.stamp||last.matrices!=computed.matrices||
       !anim::same_pose(last.pose,computed.pose)||!reload_computed(request.object,request.bone_count))return 5;
    // Native lock has never been released. No pointer is published or kept for
    // restoration; the next original camera/normal clear regenerates stock pose.
    if(!reload_write_matrices(last.matrices,request.bone_count,*edited))return 6;
    return 0;
}
void apply_borrowed_visual_pose(const NativeWeaponContext& c,const anim::ClearValue& current) {
    const auto f=reload_visual_frame;reload_visual_frame={};auto& p=borrowed_pending;const auto now=GetTickCount64();
    const bool manual=manual_reload_enabled();
    if(!borrowed_visual_pose_current(f,p.identity,p.claim.key.command_epoch,p.write.normal_verified,c,manual,now)){
        if(f.dual_ready)publish_hand_mesh(0,0,0,0);return;}
    AnimationProfile profile{};NativeBorrow b{};
    if(!animation_profile(c,now,&profile)||!native_borrow(nullptr,c,&b)||!borrowed_admission(c,b,profile,p.claim,false))return;
    AnimationNative native{&c,b,&profile,{},current.serial,p.claim.wrapper_token};
    if(!animation_assembly_copy(b,profile,&native.assembly))return;
    const anim::LockedRequest request{b.dobj,profile.gun_bone,b.tag_bone,b.bone_count,b.skeleton_clock,false,false};
    reload_pose::Result edited{};
    animation_lock(nullptr,b.dobj);
    // Preserve the existing native helper fault semantics (no synthetic unlock
    // after a lower native exception). Normal rejection always unlocks once.
    const unsigned status=reload_visual_locked(native,request,current.copy,f,&edited);
    animation_unlock(nullptr,b.dobj);
    if(!status){publish_manual_reload_pose_visual(f.identity.frame_id,f.transaction,edited.device);
        publish_hand_mesh(b.models[0],profile.profile_row,f.epoch,f.identity.generation);}
    else if(f.dual_ready)publish_hand_mesh(0,0,0,0);
    if(!status&&edited.companion){
        static std::uint64_t dual_poses{};const auto dual_count=++dual_poses;
        if(dual_count<=8||dual_count%120==0){
            const auto* runtime=borrowed_profile::get(profile.profile_row);
            const int left_flash=runtime?reload_pose::bone(*runtime,"tag_flash1"):-1;
            anim::Transform left{},right{};
            if(left_flash>=0&&anim::convert_native(edited.matrices[left_flash],&left)&&
               anim::convert_native(edited.matrices[b.tag_bone],&right))trace("t5_dual_pistol_pose",
                "count=%llu xr_frame=%llu generation=%llu reservation=%llu epoch=%llu row=%u weapon=%u manual_reload=%u left_grip=%.6f,%.6f,%.6f right_grip=%.6f,%.6f,%.6f left_flash=%.6f,%.6f,%.6f left_forward=%.6f,%.6f,%.6f right_flash=%.6f,%.6f,%.6f right_forward=%.6f,%.6f,%.6f changed=%u current_locked_skeleton=1 committed_matrix_copy=1 primary_unchanged=1",
                dual_count,f.identity.frame_id,f.identity.generation,f.identity.reservation_serial,f.epoch,profile.profile_row,c.weapon_index,manual,
                f.companion_aim.origin.x,f.companion_aim.origin.y,f.companion_aim.origin.z,
                p.proposal.gun.origin.x,p.proposal.gun.origin.y,p.proposal.gun.origin.z,
                left.origin.x,left.origin.y,left.origin.z,left.axis.forward.x,left.axis.forward.y,left.axis.forward.z,
                right.origin.x,right.origin.y,right.origin.z,right.axis.forward.x,right.axis.forward.y,right.axis.forward.z,edited.count);
        }
    }
    const auto count=++reload_visual_counts[status];
    const auto stage=static_cast<unsigned>(f.stage);
    bool pimax_stage_sample=false;
    if(f.pimax_grip_fallback&&stage<pimax_glove_stage_counts.size()){
        const auto observed=++pimax_glove_stage_counts[stage];
        pimax_stage_sample=observed<=3||observed%120==0;
    }
    if(count<=6||count%120==0||pimax_stage_sample)trace("t5_manual_reload_pose",
        "status=%u count=%llu xr_frame=%llu row=%u stage=%u transaction=%llu changed=%u free_hand=%u authored_device=%u charging_handle=%u hand=%.5f,%.5f,%.5f palm_surface=%u independent_dual=%u companion=%.5f,%.5f,%.5f pimax_grip_fallback=%u pimax_neutral_fallback=%u pimax_reload_fallback=%u manual_reload=%u automatic_ready_visual=%u current_locked_skeleton=1 asset_writes=0 new_instruction_hooks=0",
        status,count,f.identity.frame_id,profile.profile_row,static_cast<unsigned>(f.stage),f.transaction,edited.count,edited.hand,edited.device,edited.handle,f.hand.origin.x,f.hand.origin.y,f.hand.origin.z,f.palm_surface,
        edited.companion,f.companion_aim.origin.x,f.companion_aim.origin.y,f.companion_aim.origin.z,
        f.pimax_grip_fallback,f.pimax_grip_fallback&&f.stage==input::manual::Stage::ready,
        f.pimax_grip_fallback&&f.stage!=input::manual::Stage::ready,manual,!manual&&!f.dual_ready);
}
}
