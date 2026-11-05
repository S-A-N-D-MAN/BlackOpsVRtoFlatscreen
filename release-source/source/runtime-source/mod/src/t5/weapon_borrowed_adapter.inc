// SPDX-License-Identifier: GPL-3.0-only
// Included by the native-clear implementation; it shares ONLY the reviewed
// synchronous protected provider. No native pointer survives either callback.
namespace {
std::atomic<bool> borrowed_enabled{},borrowed_writer_enabled{},borrowed_exact_required{};
std::atomic<bool> borrowed_normal_gate_enabled{}; // Startup opt-in, default off.
BorrowedCameraClaim borrowed_camera;
struct BorrowedPending {
    BorrowedWeaponClaim claim{};anim::ClearValue camera{};
    input::WeaponFrameIdentity identity{};input::BorrowedPlacementValues proposal{};
    bool active{};borrowed_writer::Witness write{};
};
BorrowedPending borrowed_pending;
struct BorrowedKnifeContinuity {
    NativeWeaponContext producer{};
    input::ScaledWeaponPlacement placement{};
    std::uint64_t completed_ms{},command_epoch{},command_sequence{};
    bool valid{};
};
BorrowedKnifeContinuity borrowed_knife_continuity;
constexpr std::uint64_t borrowed_knife_continuity_ms=50;
std::array<std::uint64_t,16> borrowed_counts{};
enum class BorrowedStatus:unsigned {proposed,context,camera,profile,changed,reservation,geometry,tracking,completed,completion_rejected};
void borrowed_report(BorrowedStatus status,const BorrowedPending& p,unsigned detail=0) noexcept {
    const auto count=++borrowed_counts[static_cast<unsigned>(status)];if(count>6&&count%120)return;
    trace("t5_borrowed_weapon","status=%u count=%llu detail=%u token=%llu camera_serial=%llu normal_serial=%llu epoch=%llu command_sequence=%llu command=%d native_time=%d skeleton_clock=%d row=%u profile_revision=%u reservation=%llu build=%llu generation=%llu xr_frame=%llu original_published_ms=%llu predicted_time=%lld action_sequence=%llu gun=%.6f,%.6f,%.6f dobj=%.6f,%.6f,%.6f muzzle=%.6f,%.6f,%.6f proposal_valid=%u scratch_selected=%u native_normal_verified=%u placement_input_only=1 shot_writes=0 forced_cache_clears=0 opaque_identity_values=1 native_pointer_dereference_after_borrow=0 retained_native_lease=0 support_pair=%u support_reason=%u support_separation=%.6f admission=%llu admission_revision=%llu",
        static_cast<unsigned>(status),count,detail,p.claim.wrapper_token,p.claim.camera_serial,pose_clear_serial(),p.claim.key.command_epoch,p.claim.key.command_sequence,
        p.claim.key.command_number,p.claim.key.native_time,p.claim.key.skeleton_clock,p.claim.key.profile_row,p.claim.profile_revision,
        p.identity.reservation_serial,p.identity.native_build_serial,p.identity.generation,p.identity.frame_id,p.proposal.original_published_ms,
        p.proposal.predicted_time,p.proposal.action_sequence,p.proposal.gun.origin.x,p.proposal.gun.origin.y,p.proposal.gun.origin.z,
        p.proposal.dobj.origin.x,p.proposal.dobj.origin.y,p.proposal.dobj.origin.z,p.proposal.barrel.origin.x,p.proposal.barrel.origin.y,p.proposal.barrel.origin.z,p.proposal.valid,p.write.selected,p.write.normal_verified,
        p.proposal.support.paired,static_cast<unsigned>(p.proposal.support.reason),p.proposal.support.separation_meters,p.claim.command_admission.serial,p.claim.command_admission.revision);
}
bool borrowed_state(void* opaque,const anim::LockedRequest& r,anim::CacheCopy* out) noexcept {
    return animation_current_state(opaque,r,out,true);
}
anim::LockedStatus borrowed_current_matrix(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,
    const anim::ClearValue& expected,std::uint64_t token,anim::LockedCopy* copy) {
    // Equal matrices/counts do not imply the same canonical model row. Recheck
    // the fresh copied profile against this exact camera/normal key first.
    if(!expected.valid||expected.key.weapon!=profile.weapon_index||expected.key.profile_revision!=profile.profile_revision||!borrowed_profile::same_selection(
       {profile.profile_row,profile.gun_bone,b.tag_bone,b.bone_count},
       {expected.key.profile_row,expected.key.gun_bone,expected.key.flash_bone,expected.key.bone_count}))return anim::LockedStatus::identity;
    AnimationNative native{&c,b,&profile,{},pose_clear_serial(),token};
    if(!borrowed_profile::matches(profile.profile_row,profile.gun_bone,b.tag_bone,b.bone_count)||!animation_assembly_copy(b,profile,&native.assembly))return anim::LockedStatus::identity;
    const anim::LockedHost host{&native,animation_lock,animation_unlock,borrowed_state,nullptr,nullptr,nullptr,nullptr,animation_copy};
    return anim::copy_existing_locked(host,{b.dobj,profile.gun_bone,b.tag_bone,b.bone_count,b.skeleton_clock,false,false},expected.copy,copy);
}
bool borrowed_fixed_barrel(unsigned row,const anim::LocalGeometry& local) noexcept {
    return borrowed_profile::barrel_geometry(row,local)!=borrowed_profile::BarrelGeometryStatus::invalid;
}
// This comparison uses the existing fresh full-payload borrow/assembly. The
// command reader is never called with a fabricated command or render context.
bool borrowed_admission(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,
    BorrowedWeaponClaim& claim,bool latch) noexcept {
    if(!borrowed_exact_required)return true;
    if(!exact_profile_aim_enabled())return false;
    input::profile_admission::Token token{};command_profile::Copy command{};AnimationAssembly assembly{};
    if(!read_command_profile_admission(claim.key,&token,&command)||(!latch&&claim.command_admission!=token)||
       !animation_assembly_copy(b,profile,&assembly))return false;
    const auto& id=command.identity;
    if(!borrowed_admission_matches_context(id,c))return false;
    std::uint32_t server{},clients{};
    if(!command_profile::protected_copy(nullptr,0x1a796f8+0x13c,&server,sizeof(server))||
       !command_profile::protected_copy(nullptr,0x1c03140,&clients,sizeof(clients))||server!=id.server||clients!=server||server!=b.server)return false;
    if(id.arena!=assembly.arena||id.duplicates!=assembly.duplicate_handle||id.duplicate_size!=assembly.duplicate_size||id.tag!=b.tag)return false;
    for(unsigned i=0;i<b.model_count;++i) {
        // Name pointer is already part of the profile copy but not its public
        // result. Copy that header word inside the same protected read scope.
        std::uint32_t name{};
        if(!command_profile::protected_copy(nullptr,b.models[i],&name,sizeof(name))||
           id.model_storage[i]!=std::array<std::uint32_t,5>{static_cast<std::uint32_t>(b.models[i]),name,
             static_cast<std::uint32_t>(assembly.names[i]),static_cast<std::uint32_t>(assembly.parent_data[i]),static_cast<std::uint32_t>(assembly.bind_data[i])})return false;
    }
    for(unsigned i=0;i<command.profile.bones;++i)if(id.ids[i]!=b.bone_names[i])return false;
    if(!command_profile_admission_witness(token))return false;
    if(latch)claim.command_admission=token;
    return true;
}
void borrowed_before_work(const NativeWeaponContext& c) {
    if(borrowed_pending.active)cancel_weapon_scene_frame(borrowed_pending.identity);
    borrowed_pending={};BorrowedPending next{};
    if(borrowed_exact_required&&!exact_profile_aim_enabled())return;
    if(!borrowed_camera.available(pose_clear_serial()))return; // No per-frame asset hashing without a camera candidate.
    const auto now=GetTickCount64();const auto scope=read_weapon_native_clear_scope();
    PreCommandSnapshot command{};AnimationProfile profile{};NativeBorrow borrow{};
    if(!scope.valid||scope.depth!=1||!scope.token||!supported(c,GetCurrentThreadId())||!animation_extra_context(c)||
       !native_precommand(nullptr,&command)||!fresh_command(c,command,now)){
        borrowed_camera.invalidate();borrowed_report(BorrowedStatus::context,next);return;}
    if(!animation_profile(c,now,&profile)||!borrowed_profile::shape(profile.profile_row)||!native_borrow(nullptr,c,&borrow)){
        borrowed_camera.invalidate();borrowed_report(BorrowedStatus::profile,next);return;}
    const auto key=native_clear_key(c,borrow,profile,command);
    if(!borrowed_camera.take(key,pose_clear_serial(),scope.token,now,&next.claim,&next.camera)){
        borrowed_report(BorrowedStatus::camera,next);return;}
    anim::LockedCopy current{};const auto status=borrowed_current_matrix(c,borrow,profile,next.camera,scope.token,&current);
    if(status!=anim::LockedStatus::accepted){borrowed_report(BorrowedStatus::changed,next,static_cast<unsigned>(status));return;}
    // Exact recheck at the end of the lock was necessary, not a lifetime claim.
    // Recheck current native copied context/command immediately before reserve.
    PreCommandSnapshot last{};NativeBorrow check{};
    if(!native_precommand(nullptr,&last)||!fresh_command(c,last,GetTickCount64())||last.sequence!=command.sequence||last.gameplay_epoch!=command.gameplay_epoch||
       !native_borrow(nullptr,c,&check)||check!=borrow||pose_clear_serial()!=next.claim.camera_serial||
       read_weapon_native_clear_scope().token!=scope.token){borrowed_report(BorrowedStatus::changed,next);return;}
    if(!borrowed_admission(c,borrow,profile,next.claim,true)){borrowed_report(BorrowedStatus::context,next,20);return;}
    anim::LocalGeometry local{};
    if(!anim::derive_local(current.gun,current.flash,current.pose,&local)||!borrowed_fixed_barrel(profile.profile_row,local)){
        borrowed_report(BorrowedStatus::geometry,next);return;}
    input::ReservedWeaponFrame reserved{};input::WeaponBodySample body{};
    if(!reserve_borrowed_weapon_scene_frame(c,next.claim,&reserved,&body)){borrowed_report(BorrowedStatus::reservation,next);return;}
    next.identity=reserved.identity;next.active=true;
    const auto proposal_time=GetTickCount64();
    const float kickback=predicted_shot_kickback(reserved.identity.generation,next.claim.key.command_epoch,c.weapon_index,proposal_time);
    const auto proposed=input::propose_borrowed_placement(reserved,body,local,proposal_time,&next.proposal,weapon_two_hand_support_enabled()&&!c.dual_wield,kickback);
    if(proposed==input::BorrowedPlacementStatus::proposed&&kickback>0.f){
        static std::uint64_t kicks{};if(++kicks<=24||kicks%120==0)trace("t5_weapon_kickback",
            "count=%llu weapon=%u epoch=%llu generation=%llu meters=%.6f viewangle_writes=0 direction_changed=0",
            kicks,c.weapon_index,next.claim.key.command_epoch,reserved.identity.generation,kickback);
    }
    // Hand focus/tracking loss affects the proposal only. A completed stock
    // weapon can still join this canonical head/eye scene reservation.
    borrowed_pending=next;
    prepare_borrowed_visual_pose(reserved,next.proposal,c.weapon_index,next.claim.key.command_epoch,c.dual_wield);
    borrowed_report(proposed==input::BorrowedPlacementStatus::proposed?BorrowedStatus::proposed:BorrowedStatus::tracking,next,static_cast<unsigned>(proposed));
}
void borrowed_normal_work(const NativeWeaponContext& c,const anim::ClearValue& current) {
    if(!borrowed_pending.active||!borrowed_pending.write.selected)return;
    auto& p=borrowed_pending;const auto scope=read_weapon_native_clear_scope();AnimationProfile profile{};NativeBorrow b{};anim::LockedCopy copy{};anim::LocalGeometry geometry{},camera_geometry{};
    static std::uint64_t normal_failures{};
    bool valid=true;unsigned stage=0;bool derived=false;auto lock_status=static_cast<unsigned>(anim::LockedStatus::accepted);
    if(valid)valid=scope.valid&&scope.depth==1&&scope.token==p.claim.wrapper_token; if(!valid)stage=1;
    if(valid)valid=current.invocation_token==scope.token&&current.key==p.claim.key&&current.serial==p.claim.camera_serial+1&&valid_borrowed_claim(p.claim,GetTickCount64());if(!valid&&stage==0)stage=1;
    if(valid)valid=animation_profile(c,GetTickCount64(),&profile)&&borrowed_profile::shape(profile.profile_row);if(!valid&&stage==0)stage=2;
    if(valid)valid=native_borrow(nullptr,c,&b);if(!valid&&stage==0)stage=3;
    if(valid)valid=borrowed_admission(c,b,profile,p.claim,false);if(!valid&&stage==0)stage=4;
    if(valid){const auto status=borrowed_current_matrix(c,b,profile,current,scope.token,&copy);if(status!=anim::LockedStatus::accepted){valid=false;stage=5;lock_status=static_cast<unsigned>(status);} }
    if(valid)valid=anim::derive_local(copy.gun,copy.flash,copy.pose,&geometry);if(!valid&&stage==0)stage=6;
    if(valid)derived=true;
    if(valid)valid=borrowed_fixed_barrel(profile.profile_row,geometry);if(!valid&&stage==0)stage=7;
    // Propagate rounding from this invocation's owned camera copy. This is
    // value-only math; it adds no native read or durable pointer authority.
    if(valid)valid=anim::derive_local(p.camera.copy.gun,p.camera.copy.flash,p.camera.copy.pose,&camera_geometry);if(!valid&&stage==0)stage=9;
    if(valid)valid=borrowed_writer::observe_normal(p.write,p.proposal,geometry,camera_geometry);if(!valid&&stage==0)stage=8;
    if(!valid){
        const auto count=++normal_failures;if(count<=6||count%120==0){
            if(derived)trace("t5_borrowed_normal_failure",
                "stage=%u lock_status=%u token=%llu reservation=%llu gun_pos_error=%.9g gun_axis_error=%.9g muzzle_pos_error=%.9g muzzle_axis_error=%.9g pose_pos_error=%.9g pose_axis_error=%.9g",
                stage,lock_status,p.claim.wrapper_token,p.identity.reservation_serial,
                anim::distance(geometry.gun_world.origin,p.proposal.gun.origin),anim::axis_error(geometry.gun_world.axis,p.proposal.gun.axis),
                anim::distance(geometry.flash_world.origin,p.proposal.barrel.origin),anim::axis_error(geometry.flash_world.axis,p.proposal.barrel.axis),
                anim::distance(geometry.pose_world.origin,p.proposal.dobj.origin),anim::axis_error(geometry.pose_world.axis,p.proposal.dobj.axis));
            else trace("t5_borrowed_normal_failure","stage=%u lock_status=%u token=%llu reservation=%llu",
                stage,lock_status,p.claim.wrapper_token,p.identity.reservation_serial);
        }
        cancel_weapon_scene_frame(p.identity);borrowed_report(BorrowedStatus::completion_rejected,p,1);p={};return;
    }
    apply_borrowed_visual_pose(c,current);
    const auto n=++borrowed_counts[10];
    if(n<=6||n%120==0)trace("t5_borrowed_native_placed","phase=normal count=%llu token=%llu reservation=%llu gun=%.9g,%.9g,%.9g muzzle=%.9g,%.9g,%.9g gun_pos_error=%.9g gun_axis_error=%.9g muzzle_pos_error=%.9g muzzle_axis_error=%.9g persistent_pose_error=%.9g original_published_ms=%llu forced_cache_clears=0 pose_restore=0",
        n,p.claim.wrapper_token,p.identity.reservation_serial,geometry.gun_world.origin.x,geometry.gun_world.origin.y,geometry.gun_world.origin.z,
        geometry.flash_world.origin.x,geometry.flash_world.origin.y,geometry.flash_world.origin.z,
        anim::distance(geometry.gun_world.origin,p.proposal.gun.origin),anim::axis_error(geometry.gun_world.axis,p.proposal.gun.axis),
        anim::distance(geometry.flash_world.origin,p.proposal.barrel.origin),anim::axis_error(geometry.flash_world.axis,p.proposal.barrel.axis),
        anim::distance(geometry.pose_world.origin,p.proposal.dobj.origin),p.proposal.original_published_ms);
}
void borrowed_complete_work(const NativeWeaponContext& c,const anim::ClearValue& current) {
    if(!borrowed_pending.active)return;
    // Active selection keeps its claim through all native lock/copy calls so
    // wrapper retirement can cancel it on native faults. Preserve the original
    // passive exception path, which cleared this copied state before those calls.
    auto pending=borrowed_pending;
    if(!borrowed_writer_enabled)borrowed_pending={};
    const auto scope=read_weapon_native_clear_scope();const auto now=GetTickCount64();
    if(!scope.valid||scope.depth!=1||scope.token!=pending.claim.wrapper_token||current.invocation_token!=scope.token||
       current.serial!=pending.claim.camera_serial+1||current.key!=pending.claim.key||!valid_borrowed_claim(pending.claim,now)){
        if(c.primary_knife_variant)borrowed_knife_continuity={};
        cancel_weapon_scene_frame(pending.identity);borrowed_report(BorrowedStatus::completion_rejected,pending);borrowed_pending={};return;}
    AnimationProfile profile{};NativeBorrow borrow{};anim::LockedCopy reread{};anim::LocalGeometry camera{},normal{};
    bool valid=animation_profile(c,now,&profile)&&borrowed_profile::shape(profile.profile_row)&&native_borrow(nullptr,c,&borrow)&&
        borrowed_admission(c,borrow,profile,pending.claim,false);
    if(valid)valid=borrowed_current_matrix(c,borrow,profile,current,scope.token,&reread)==anim::LockedStatus::accepted;
    valid=valid&&anim::derive_local(pending.camera.copy.gun,pending.camera.copy.flash,pending.camera.copy.pose,&camera)&&
        anim::derive_local(reread.gun,reread.flash,reread.pose,&normal)&&borrowed_fixed_barrel(profile.profile_row,normal)&&
        native_precision::same_local_geometry(camera,normal);
    if(pending.write.selected)valid=valid&&borrowed_writer::observe_after(pending.write,pending.proposal,normal);
    if(!valid||!complete_borrowed_weapon_scene_frame(c,pending.identity,pending.claim,current.serial,pending.write.selected&&pending.write.normal_verified)){
        if(c.primary_knife_variant)borrowed_knife_continuity={};
        cancel_weapon_scene_frame(pending.identity);borrowed_report(BorrowedStatus::completion_rejected,pending);borrowed_pending={};return;}
    borrowed_pending={};
    if(pending.write.selected&&c.primary_knife_variant){
        input::ScaledWeaponPlacement completed{};
        if(borrowed_writer::make_value(pending.proposal,c.placement,now,&completed))
            borrowed_knife_continuity={c,completed,now,pending.claim.key.command_epoch,pending.claim.key.command_sequence,true};
        else borrowed_knife_continuity={};
    }else if(!c.primary_knife_variant)borrowed_knife_continuity={};
    if(pending.write.selected)write_client_barrel_angles(c,normal.flash_world.axis.forward);
    if(pending.write.selected){const auto n=++borrowed_counts[11];if(n<=6||n%120==0)
        trace("t5_borrowed_native_placed","phase=after count=%llu token=%llu reservation=%llu gun=%.9g,%.9g,%.9g gun_forward=%.9g,%.9g,%.9g muzzle=%.9g,%.9g,%.9g gun_pos_error=%.9g gun_axis_error=%.9g muzzle_pos_error=%.9g muzzle_axis_error=%.9g persistent_pose_error=%.9g original_published_ms=%llu forced_cache_clears=0 pose_restore=0",
            n,pending.claim.wrapper_token,pending.identity.reservation_serial,normal.gun_world.origin.x,normal.gun_world.origin.y,normal.gun_world.origin.z,
            normal.gun_world.axis.forward.x,normal.gun_world.axis.forward.y,normal.gun_world.axis.forward.z,
            normal.flash_world.origin.x,normal.flash_world.origin.y,normal.flash_world.origin.z,
            anim::distance(normal.gun_world.origin,pending.proposal.gun.origin),anim::axis_error(normal.gun_world.axis,pending.proposal.gun.axis),
            anim::distance(normal.flash_world.origin,pending.proposal.barrel.origin),anim::axis_error(normal.flash_world.axis,pending.proposal.barrel.axis),
            anim::distance(normal.pose_world.origin,pending.proposal.dobj.origin),pending.proposal.original_published_ms);}
    borrowed_report(BorrowedStatus::completed,pending);
}
}
bool configure_weapon_borrowed_probe(HMODULE image,bool prerequisites) noexcept {
    const auto error=GetLastError();bool result{};borrowed_knife_continuity={};
    wchar_t exact[16]{};const auto exact_length=GetEnvironmentVariableW(L"BOVR_EXACT_PROFILE_AIM_PROBE",exact,16);
    borrowed_exact_required.store(exact_length!=0&&(exact_length>=16||wcscmp(exact,L"0")));
    wchar_t flag[8]{},legacy[8]{},writer[8]{},lifetime[8]{};
    GetEnvironmentVariableW(L"BOVR_BORROWED_WEAPON_PROBE",flag,8);GetEnvironmentVariableW(L"BOVR_WEAPON_RESERVATION_PROBE",legacy,8);
    GetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",writer,8);GetEnvironmentVariableW(L"BOVR_WEAPON_LIFECYCLE_PROBE",lifetime,8);
    if(prerequisites&&weapon_native_clear_probe_enabled()&&image==GetModuleHandleW(nullptr)&&
       !wcscmp(flag,L"1")&&wcscmp(legacy,L"1")&&wcscmp(writer,L"1")&&wcscmp(lifetime,L"1")){
        borrowed_enabled=true;result=true;
        wchar_t active[8]{},animation[8]{};GetEnvironmentVariableW(L"BOVR_BORROWED_WEAPON_PLACEMENT",active,8);
        GetEnvironmentVariableW(L"BOVR_WEAPON_ANIMATION_PROBE",animation,8);
        borrowed_writer_enabled=!wcscmp(active,L"1")&&wcscmp(animation,L"1");
        if(borrowed_writer_enabled)trace("t5_borrowed_writer_configured","default_off=1 selected_argument_bytes=32 legacy_selector=0 fresh_per_native_camera=1 native_persistent_owner=1 expected_normal_post_required=1 pose_restore=0 shot_writes=0");
        trace("t5_borrowed_weapon_configured","profile_source=validated_current_runtime_catalog catalog_revision=%u full_current_payload=1 right_grip_aim_authored_pivot=1 scratch_writer_enabled=%u owned_argument_bytes=32 native_persistent_owner=1 shot_writes=0 new_instruction_hooks=0 legacy_callbacks_replaced=0",
            borrowed_profile::runtime_revision,borrowed_writer_enabled.load());
    }
    wchar_t normal_gate[8]{};
    const auto normal_gate_length=GetEnvironmentVariableW(L"BOVR_BORROWED_NORMAL_GATE",normal_gate,8);
    const bool normal_gate_requested=normal_gate_length==1&&normal_gate[0]==L'1'&&normal_gate[1]==0;
    borrowed_normal_gate_enabled=result&&borrowed_writer_enabled.load()&&normal_gate_requested;
    if(normal_gate_requested)trace("t5_borrowed_normal_gate_configured",
        "requested=1 accepted=%u default_off=1 camera_unchanged=1 reservation_unchanged=1 primary_grenade_exempt=1 passive_unchanged=1",
        borrowed_normal_gate_enabled.load());
    wchar_t olympia[8]{};const auto olympia_length=GetEnvironmentVariableW(L"BOVR_BORROWED_OLYMPIA_PROBE",olympia,8);
    const bool olympia_requested=olympia_length==1&&olympia[0]==L'1'&&olympia[1]==0;
    wchar_t animation[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_ANIMATION_PROBE",animation,8);
    olympia_enabled=result&&olympia_requested&&wcscmp(animation,L"1")&&!animation_enabled.load();
    if(olympia_requested)trace("t5_borrowed_olympia_configured","requested=1 accepted=%u runtime_catalog_revision=%u full_locked_payload=1 legacy_route=0 scratch_writer_enabled=%u new_instruction_hooks=0",olympia_enabled.load(),borrowed_profile::runtime_revision,borrowed_writer_enabled.load());
    SetLastError(error);return result;
}
bool weapon_olympia_profile_enabled() noexcept{return olympia_enabled.load();}
bool weapon_borrowed_probe_enabled() noexcept{return borrowed_enabled.load();}
bool weapon_borrowed_placement_enabled() noexcept{return borrowed_writer_enabled.load();}
bool select_borrowed_weapon_placement(const NativeWeaponContext& c,input::ScaledWeaponPlacement* out) {
    const auto error=GetLastError();bool selected=false;
    if(!borrowed_writer_enabled||!out||(borrowed_exact_required&&!exact_profile_aim_enabled())||native_clear_busy.test_and_set()){SetLastError(error);return false;}
    __try {
        auto& p=borrowed_pending;const auto scope=read_weapon_native_clear_scope();input::ScaledWeaponPlacement value{};
        if(p.active&&!p.write.selected&&scope.valid&&scope.depth==1&&scope.token==p.claim.wrapper_token&&
           valid_borrowed_claim(p.claim,GetTickCount64())&&borrowed_claim_matches_context(p.claim,c)&&
           borrowed_writer::make_value(p.proposal,c.placement,GetTickCount64(),&value)) {
            AnimationProfile profile{};NativeBorrow b{};anim::LockedCopy current{};
            bool valid=animation_profile(c,GetTickCount64(),&profile)&&borrowed_profile::shape(profile.profile_row)&&native_borrow(nullptr,c,&b);
            if(valid)valid=borrowed_current_matrix(c,b,profile,p.camera,scope.token,&current)==anim::LockedStatus::accepted;
            if(valid&&borrowed_admission(c,b,profile,p.claim,false)&&mark_borrowed_weapon_scene_selected(c,p.identity,p.claim)) {
                *out=value;p.write.selected=true;selected=true;
            }
        }
    }__finally {native_clear_busy.clear();SetLastError(error);}
    return selected;
}
bool select_borrowed_knife_visual_continuity(const NativeWeaponContext& c,input::ScaledWeaponPlacement* out) noexcept {
    const auto error=GetLastError();bool selected=false;
    if(!borrowed_writer_enabled||!out||!c.primary_knife_variant||native_clear_busy.test_and_set()){
        SetLastError(error);return false;
    }
    __try {
        const auto cached=borrowed_knife_continuity;const auto now=GetTickCount64();
        const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot command{};
        const auto& original=cached.producer;
        const auto native_time_delta=static_cast<std::int64_t>(c.native_time)-static_cast<std::int64_t>(original.native_time);
        const bool model_generation_current=!cached.valid?false:
            (!original.model_lifetime_verified&&!c.model_lifetime_verified)||
            (original.model_lifetime_verified&&c.model_lifetime_verified&&original.model_generation&&
             original.model_generation==c.model_generation);
        const bool identity_current=cached.valid&&c.valid&&c.body_valid&&primary_weapon_context(c)&&c.primary_viewmodel&&
            original.primary_knife_variant&&original.native_thread==c.native_thread&&original.cg_identity==c.cg_identity&&
            original.refdef_identity==c.refdef_identity&&original.viewmodel_identity==c.viewmodel_identity&&
            original.dobj_identity==c.dobj_identity&&original.variant_identity==c.variant_identity&&
            original.definition_identity==c.definition_identity&&original.weapon_index==c.weapon_index&&
            original.weapon_type==c.weapon_type&&original.weapon_class==c.weapon_class&&original.dual_wield==c.dual_wield&&
            model_generation_current&&cached.placement.scale==c.placement.scale&&native_time_delta>=0&&
            static_cast<std::uint64_t>(native_time_delta)<=borrowed_knife_continuity_ms;
        const bool time_current=identity_current&&cached.completed_ms&&now>=cached.completed_ms&&
            now-cached.completed_ms<=borrowed_knife_continuity_ms;
        const bool command_current=time_current&&scope.valid&&scope.depth==1&&scope.token&&
            read_pre_command_snapshot(&command)&&primary_weapon_active(command)&&fresh_command(c,command,now)&&
            command.gameplay_epoch==cached.command_epoch&&command.sequence>=cached.command_sequence&&
            command.sequence-cached.command_sequence<=16;
        if(!borrowed_pending.active&&command_current){*out=cached.placement;out->scale=c.placement.scale;selected=true;}
        else if(!identity_current||!time_current||
                (command.gameplay_epoch&&command.gameplay_epoch!=cached.command_epoch))borrowed_knife_continuity={};
    }__except(EXCEPTION_EXECUTE_HANDLER){borrowed_knife_continuity={};selected=false;}
    native_clear_busy.clear();SetLastError(error);return selected;
}
void retire_borrowed_weapon_invocation(std::uint64_t token) noexcept {
    const auto error=GetLastError();
    if(!borrowed_writer_enabled||!token||native_clear_busy.test_and_set()){SetLastError(error);return;}
    // Only mod metadata/canonical ticket is retired. Native persistent pose and
    // any lock disposition from a forwarded engine exception remain untouched.
    if(borrowed_pending.active&&borrowed_pending.claim.wrapper_token==token){
        if(borrowed_pending.proposal.valid&&borrowed_pending.claim.key.weapon&&borrowed_knife_continuity.valid&&
           borrowed_pending.claim.key.weapon==borrowed_knife_continuity.producer.weapon_index)borrowed_knife_continuity={};
        if(borrowed_exact_required)invalidate_native_command_aim();cancel_weapon_scene_frame(borrowed_pending.identity);borrowed_pending={};
    }
    native_clear_busy.clear();SetLastError(error);
}
void observe_borrowed_weapon_before(const NativeWeaponContext& c) {
    const auto error=GetLastError();if(!borrowed_enabled||native_clear_busy.test_and_set()){SetLastError(error);return;}
    __try{borrowed_before_work(c);}
    __finally{native_clear_busy.clear();SetLastError(error);}
}
