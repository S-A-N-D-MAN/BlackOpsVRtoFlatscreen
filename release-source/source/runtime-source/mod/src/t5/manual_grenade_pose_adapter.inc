// SPDX-License-Identifier: GPL-3.0-only
// Included inside bovr::t5 after the normal evaluated-skeleton writer. No new
// instruction hook, DObj allocation, persistent native pointer or asset write.
namespace {
struct GrenadePoseCopy {
    std::uintptr_t owner{},object{},models_table{},arena{},gun_table{};
    int pool{},clock{},skeleton_context{};
    unsigned count{},arms{};
    std::uint16_t duplicate_handle{};
    std::uint8_t duplicate_size{};
    std::array<std::uintptr_t,2> model{};
    std::array<std::uint32_t,16> gun_models{};
    std::array<std::array<std::uint32_t,8>,2> header{};
    std::array<std::array<char,128>,2> model_names{};
    std::array<std::array<char,96>,borrowed_profile::max_profile_bones> bone_names{};
    std::array<std::uint16_t,borrowed_profile::max_profile_bones> ids{};
    std::array<std::uint8_t,borrowed_profile::max_profile_bones> parents{};
    std::array<std::array<std::uint32_t,8>,borrowed_profile::max_profile_bones> bind{};
    std::array<std::uint8_t,2> model_parents{};
    std::array<std::uint8_t,255> duplicates{};
    anim::PoseCopy pose{};
};
bool grenade_read(std::uintptr_t address,void* output,std::size_t bytes) noexcept {
    return address>=0x10000&&bytes&&bytes<=UINT32_MAX&&address<=UINT32_MAX-bytes&&
        borrowed_protected_copy(nullptr,address,output,bytes);
}
template<class T> bool grenade_value(std::uintptr_t address,T* output) noexcept {
    return output&&grenade_read(address,output,sizeof(*output));
}
template<std::size_t N> bool grenade_text(std::uintptr_t address,std::array<char,N>* output) noexcept {
    if(!output)return false;*output={};
    for(unsigned i=0;i<N;++i){if(address>UINT32_MAX-i||!grenade_read(address+i,&(*output)[i],1))return false;
        if(!(*output)[i])return i!=0;if(static_cast<unsigned char>((*output)[i])<32)return false;}
    return false;
}
bool grenade_owner(const ManualGrenadePose& ticket,const PoseClearPoint& point,std::uintptr_t* owner,
    int* pool) noexcept {
    std::uintptr_t cg{},object{};short index{};std::uint8_t allocated{};
    if(!manual_grenade_pose_current(ticket)||!grenade_value(0x2ff5354,&cg)||cg!=ticket.cg||
       !grenade_value(0xc1c6d8,owner)||!grenade_value(*owner,&object)||object!=point.object||
       !grenade_value(0x24e6150+1536*2,&index)||index<=0||index>=0xc00||
       object!=0x2489148+static_cast<unsigned>(index)*0x7c||
       !grenade_value(0x2488548+index,&allocated)||!allocated)return false;
    *pool=index;return true;
}
bool grenade_copy(const ManualGrenadePose& ticket,const PoseClearPoint& point,GrenadePoseCopy* output) noexcept {
    if(!output)return false;*output={};auto& c=*output;c.object=point.object;
    if(!grenade_owner(ticket,point,&c.owner,&c.pool)||
       !grenade_value(0x3b1f794,&c.skeleton_context)||c.skeleton_context<0||c.skeleton_context>1||
       !grenade_value(0x2911e40+static_cast<unsigned>(c.skeleton_context)*0x2008e0,&c.clock)||c.clock<=0)return false;
    std::uint8_t models{},bones{},type{},flag{},destructible{};
    if(!grenade_value(c.object+9,&models)||models!=2||!grenade_value(c.object+0xa,&bones)||!bones||bones>borrowed_profile::max_profile_bones||
       !grenade_value(c.object+0x78,&c.models_table)||!grenade_read(c.models_table,c.model.data(),8)||
       !grenade_read(c.models_table+8,c.model_parents.data(),2)||c.model_parents[0]!=255||
       !grenade_value(0x3067c00,&c.arena)||!grenade_value(c.object+4,&c.duplicate_handle)||!c.duplicate_handle||
       !grenade_value(c.object+8,&c.duplicate_size)||c.duplicate_size<21||!(c.duplicate_size&1)||
       !grenade_value(ticket.cg+0xc5e06,&type)||type!=20||!grenade_value(ticket.cg+0xc5e09,&flag)||flag||
       !grenade_value(ticket.cg+0xc5e18,&destructible)||destructible||
       !grenade_read(ticket.cg+0xc5e28,&c.pose.origin,12)||!grenade_read(ticket.cg+0xc5e34,&c.pose.angles,12)||
       !anim::finite(c.pose.origin)||!anim::finite(c.pose.angles))return false;
    const auto duplicate=std::uint64_t(c.arena)+16ULL*c.duplicate_handle;std::uint16_t refs{};
    if(duplicate>UINT32_MAX-259||!grenade_value(static_cast<std::uintptr_t>(duplicate),&refs)||!refs||
       !grenade_read(static_cast<std::uintptr_t>(duplicate)+4,c.duplicates.data(),c.duplicate_size)||c.duplicates[c.duplicate_size-1])return false;
    // The second model must be one of this exact active offhand definition's
    // native viewmodel variants, not an unrelated primary assembly at the seam.
    if(!grenade_value(ticket.definition+4,&c.gun_table)||!grenade_read(c.gun_table,c.gun_models.data(),sizeof(c.gun_models))||
       std::find(c.gun_models.begin(),c.gun_models.end(),c.model[1])==c.gun_models.end())return false;
    unsigned total=0;
    for(unsigned m=0;m<2;++m){
        if(!grenade_read(c.model[m],c.header[m].data(),32)||!grenade_text(c.header[m][0],&c.model_names[m]))return false;
        const auto* header=reinterpret_cast<const std::uint8_t*>(c.header[m].data());
        const unsigned count=header[4],roots=header[5];
        if(!count||count>borrowed_profile::max_profile_bones-total||!roots||roots>count)return false;
        if(!m)c.arms=count;
        for(unsigned j=0;j<count;++j){const auto i=total+j;
            if(!grenade_value(std::uintptr_t(c.header[m][2])+2*j,&c.ids[i])||!c.ids[i])return false;
            const auto entry=std::uint64_t(c.arena)+16ULL*c.ids[i];
            if(entry>UINT32_MAX-100||!grenade_value(static_cast<std::uintptr_t>(entry),&refs)||!refs||
               !grenade_text(static_cast<std::uintptr_t>(entry)+4,&c.bone_names[i])||
               !grenade_read(std::uintptr_t(c.header[m][7])+32*j,c.bind[i].data(),32))return false;
            if(j>=roots&&(!grenade_value(std::uintptr_t(c.header[m][3])+j-roots,&c.parents[i])||!c.parents[i]||c.parents[i]>j))return false;
            anim::NativeMat32 bind{};anim::Transform checked{};memcpy(&bind,c.bind[i].data(),32);
            if(!anim::convert_native(bind,&checked))return false;
        }
        total+=count;
    }
    if(total!=bones||c.model_parents[1]>=c.arms)return false;c.count=total;
    std::array<std::uint32_t,5> expected{};unsigned previous=0;
    for(unsigned i=20;i+1<c.duplicate_size;i+=2){const unsigned child=c.duplicates[i],parent=c.duplicates[i+1];
        if(!child||!parent||child>total||parent>=child||child<=previous)return false;
        previous=child;expected[(child-1)/32]|=0x80000000u>>((child-1)%32);}
    if(memcmp(expected.data(),c.duplicates.data(),20))return false;
    return true;
}
bool grenade_same(const GrenadePoseCopy& a,const GrenadePoseCopy& b) noexcept {
    // No skeleton stamp/matrix address belongs to this immutable identity copy:
    // the guarded native cache creator is allowed to allocate those below.
    return a.owner==b.owner&&a.object==b.object&&a.models_table==b.models_table&&a.arena==b.arena&&a.gun_table==b.gun_table&&
        a.pool==b.pool&&a.clock==b.clock&&a.skeleton_context==b.skeleton_context&&a.count==b.count&&a.arms==b.arms&&
        a.duplicate_handle==b.duplicate_handle&&a.duplicate_size==b.duplicate_size&&a.model==b.model&&a.gun_models==b.gun_models&&
        a.header==b.header&&a.model_names==b.model_names&&a.bone_names==b.bone_names&&a.ids==b.ids&&a.parents==b.parents&&a.bind==b.bind&&
        a.model_parents==b.model_parents&&a.duplicates==b.duplicates&&anim::same_pose(a.pose,b.pose);
}
unsigned grenade_pose_locked(const ManualGrenadePose& ticket,const PoseClearPoint& point,std::uint64_t token,
    std::uint64_t epoch,GrenadePoseCopy* captured,reload_pose::Result* edited) {
    auto& before=*captured;GrenadePoseCopy after{};
    if(!grenade_copy(ticket,point,&before))return 1;
    std::array<std::uint32_t,15> clear{};
    if(!grenade_read(point.object+0x14,clear.data(),60)||clear!=std::array<std::uint32_t,15>{})return 2;
    std::uintptr_t matrices{};
    if(animation_cache(nullptr,point.object,before.arms,&matrices)!=0||!matrices)return 3;
    int stamp{};std::uintptr_t current_matrices{};
    if(!grenade_copy(ticket,point,&after)||!grenade_same(before,after)||
       !grenade_value(point.object+0x50,&stamp)||stamp!=before.clock||
       !grenade_value(point.object+0x54,&current_matrices)||current_matrices!=matrices)return 4;
    NativeWeaponContext context{};context.cg_identity=ticket.cg;AnimationNative native{};native.c=&context;
    std::uint32_t bits[5]{};for(unsigned i=0;i<before.count;++i)bits[i>>5]|=0x80000000u>>(i&31);
    animation_pose(&native,point.object,bits);animation_skeleton(nullptr,point.object,bits);
    std::array<anim::NativeMat32,borrowed_profile::max_profile_bones> original{};
    if(!reload_computed(point.object,before.count)||!grenade_read(matrices,original.data(),32*before.count))return 5;
    std::array<borrowed_profile::Model,2> model{};
    std::array<borrowed_profile::Bone,borrowed_profile::max_profile_bones> bone{};
    for(unsigned m=0,start=0;m<2;++m){const auto* h=reinterpret_cast<const std::uint8_t*>(before.header[m].data());
        model[m]={before.model_names[m].data(),h[4],h[5],start};start+=h[4];}
    for(unsigned i=0;i<before.count;++i)bone[i]={before.bone_names[i].data(),before.parents[i],before.bind[i]};
    const borrowed_profile::Profile profile{model,{bone.data(),before.count},before.model_parents,{before.duplicates.data(),before.duplicate_size}};
    if(!grenade_pose::pose(profile,before.arms,ticket.hand,ticket.holding,{original.data(),before.count},edited))return 6;
    const auto scope=read_weapon_native_clear_scope();
    if(!scope.valid||scope.depth!=1||scope.token!=token||pose_clear_serial()!=point.serial||
       !grenade_copy(ticket,point,&after)||!grenade_same(before,after)||
       !grenade_value(point.object+0x50,&stamp)||stamp!=before.clock||
       !grenade_value(point.object+0x54,&current_matrices)||current_matrices!=matrices||!reload_computed(point.object,before.count))return 7;
    // Validate/publish the copied arms filter before changing the skeleton. A
    // missing glove mask must never turn this path into a detached whole arm.
    if(!publish_left_hand_mesh(before.model[0],profile,before.arms,epoch,ticket.generation))return 8;
    if(!manual_grenade_pose_current(ticket)||pose_clear_serial()!=point.serial||
       !reload_write_matrices(matrices,before.count,*edited)){publish_hand_mesh(0,0,0,0);return 9;}
    return 0;
}
std::array<std::uint64_t,10> grenade_primary_hand_counts{};
unsigned grenade_primary_hand_locked(AnimationNative& native,const anim::LockedRequest& request,
    const anim::LockedCopy& expected,const ManualGrenadePose& ticket,const PreCommandSnapshot& command,
    const PoseClearPoint& point,reload_pose::Result* edited) {
    anim::CacheCopy before{},computed{},last{};anim::LockedCopy copied{};
    if(!borrowed_state(&native,request,&before)||before.clock!=request.clock||before.stamp!=request.clock||
       !before.matrices||!before.gun_computed||!before.flash_computed||
       !animation_copy(nullptr,request,before.matrices,&copied.gun,&copied.flash))return 1;
    copied.pose=before.pose;copied.clock=before.clock;if(!anim::same_raw(copied,expected))return 1;
    std::uint32_t bits[5]{};for(unsigned i=0;i<request.bone_count;++i)bits[i>>5]|=0x80000000u>>(i&31);
    animation_pose(&native,request.object,bits);animation_skeleton(&native,request.object,bits);
    if(!borrowed_state(&native,request,&computed)||computed.clock!=before.clock||computed.stamp!=before.stamp||
       computed.matrices!=before.matrices||!anim::same_pose(before.pose,computed.pose)||
       !reload_computed(request.object,request.bone_count))return 2;
    std::array<anim::NativeMat32,borrowed_profile::max_profile_bones> originals{};
    if(request.bone_count>originals.size()||!borrowed_protected_copy(nullptr,computed.matrices,originals.data(),
       request.bone_count*sizeof(anim::NativeMat32)))return 3;
    if(std::memcmp(&originals[request.gun_bone],&expected.gun,32)||
       std::memcmp(&originals[request.flash_bone],&expected.flash,32))return 3;
    const auto* profile=borrowed_profile::get(native.profile->profile_row);
    const auto* shape=borrowed_profile::shape(native.profile->profile_row);
    if(!profile||!shape||shape->weapon!=native.c->weapon_index||shape->total!=request.bone_count||
       !grenade_pose::pose_primary_hand(*profile,shape->arms,ticket.hand,
           {originals.data(),request.bone_count},edited))return 4;
    if(!borrowed_state(&native,request,&last)||last.clock!=computed.clock||last.stamp!=computed.stamp||
       last.matrices!=computed.matrices||!anim::same_pose(last.pose,computed.pose)||
       !reload_computed(request.object,request.bone_count))return 5;
    const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot current{};
    if(!scope.valid||scope.depth!=1||scope.token!=native.wrapper_observation||pose_clear_serial()!=point.serial||
       !native_precommand(nullptr,&current)||!fresh_command(*native.c,current,GetTickCount64())||
       current.sequence!=command.sequence||current.gameplay_epoch!=command.gameplay_epoch||
       !manual_grenade_pose_current(ticket)||!manual_grenade_primary_visual_scope(*native.c))return 6;
    // Validate and publish the existing split before moving the evaluated hand.
    // It retains the native right arm while removing the detached left sleeve.
    if(!publish_hand_mesh(native.expected.models[0],native.profile->profile_row,
        command.gameplay_epoch,ticket.generation))return 7;
    if(!manual_grenade_pose_current(ticket)||pose_clear_serial()!=point.serial||
       read_weapon_native_clear_scope().token!=native.wrapper_observation||
       !reload_write_matrices(last.matrices,request.bone_count,*edited)){
        publish_hand_mesh(0,0,0,0);return 8;
    }
    return 0;
}
bool apply_manual_grenade_primary_hand_pose(const PoseClearPoint& point,const NativeWeaponContext& c,
    const NativeBorrow& borrow,const AnimationProfile& profile,const PreCommandSnapshot& command,
    const anim::ClearValue& current) {
    if(!manual_grenade_standalone_renderer_enabled()||!c.manual_grenade_primary_visual||
       point.caller!=PoseClearCaller::normal_weapon||point.local_client||point.object!=c.dobj_identity||
       !manual_grenade_primary_visual_scope(c))return false;
    ManualGrenadePose ticket{};
    if(!read_manual_grenade_pose(&ticket)||c.cg_identity!=ticket.cg||c.viewmodel_identity!=ticket.viewmodel||
       c.dobj_identity!=ticket.dobj||c.variant_identity!=ticket.primary_variant||
       c.definition_identity!=ticket.primary_definition||c.weapon_index!=ticket.primary_weapon||
       c.offhand!=static_cast<int>(ticket.weapon)||c.weapon_state<21||c.weapon_state>26)return false;
    const auto scope=read_weapon_native_clear_scope();
    if(!scope.valid||scope.depth!=1||!scope.token||scope.token!=current.invocation_token||
       current.serial!=point.serial||current.key!=native_clear_key(c,borrow,profile,command))return false;
    AnimationNative native{&c,borrow,&profile,{},point.serial,scope.token};
    if(!animation_assembly_copy(borrow,profile,&native.assembly))return false;
    const anim::LockedRequest request{borrow.dobj,profile.gun_bone,borrow.tag_bone,borrow.bone_count,
        borrow.skeleton_clock,false,false};reload_pose::Result edited{};
    animation_lock(nullptr,borrow.dobj);
    const unsigned status=grenade_primary_hand_locked(native,request,current.copy,ticket,command,point,&edited);
    animation_unlock(nullptr,borrow.dobj);
    if(!status)publish_manual_grenade_pose(ticket.transaction,ticket.generation);
    const auto count=++grenade_primary_hand_counts[status];
    if(count<=6||count%120==0)trace("t5_manual_grenade_primary_hand",
        "status=%u count=%llu serial=%llu token=%llu transaction=%llu generation=%llu row=%u weapon=%u grenade=%u state=%d holding=%u changed=%u hand=%.5f,%.5f,%.5f primary_gun_unchanged=1 primary_flash_unchanged=1 right_arm_native=1 left_sleeve_filtered=1 current_locked_skeleton=1 asset_writes=0 player_state_writes=0",
        status,count,point.serial,scope.token,ticket.transaction,ticket.generation,profile.profile_row,
        c.weapon_index,ticket.weapon,c.weapon_state,ticket.holding,edited.count,
        ticket.hand.origin.x,ticket.hand.origin.y,ticket.hand.origin.z);
    return !status;
}
void grenade_pose_early_report(const ManualGrenadePose& ticket,const PoseClearPoint& point,unsigned status) noexcept {
    static std::array<std::uint64_t,2> counts{};const auto count=++counts[status-10];
    if(count<=6||count%120==0)trace("t5_manual_grenade_pose",
        "status=%u count=%llu serial=%llu transaction=%llu generation=%llu weapon=%u holding=%u early_rejected=1 native_pose_writes=0",
        status,count,point.serial,ticket.transaction,ticket.generation,ticket.weapon,ticket.holding);
}
bool apply_manual_grenade_pose(const PoseClearPoint& point) {
    if(manual_grenade_standalone_renderer_enabled())return false;
    if(point.caller!=PoseClearCaller::normal_weapon||point.local_client||!point.object)return false;
    ManualGrenadePose ticket{};if(!read_manual_grenade_pose(&ticket))return false;
    const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot command{};
    if(!scope.valid||scope.depth!=1||!scope.token||pose_clear_serial()!=point.serial||
       !read_pre_command_snapshot(&command)||!command.gameplay_active||!command.gameplay_epoch||
       command.native_cg_identity!=ticket.cg){grenade_pose_early_report(ticket,point,10);return false;}
    std::uintptr_t owner{};int pool{};
    if(!grenade_owner(ticket,point,&owner,&pool)){grenade_pose_early_report(ticket,point,11);return false;}
    GrenadePoseCopy captured{};reload_pose::Result edited{};
    animation_lock(nullptr,point.object);
    // Match existing native exception behavior. Normal rejects unlock once;
    // a lower native exception retains the original native unwinding semantics.
    const auto status=grenade_pose_locked(ticket,point,scope.token,command.gameplay_epoch,&captured,&edited);
    animation_unlock(nullptr,point.object);
    if(!status)publish_manual_grenade_pose(ticket.transaction,ticket.generation);
    static std::array<std::uint64_t,10> counts{};const auto count=++counts[status];
    if(count<=6||count%120==0)trace("t5_manual_grenade_pose",
        "status=%u count=%llu serial=%llu transaction=%llu generation=%llu weapon=%u holding=%u release_hidden=%u arms=%u bones=%u changed=%u model0=%s model1=%s root0=%s root1=%s hand=%.5f,%.5f,%.5f current_locked_skeleton=1 asset_writes=0 player_state_writes=0 new_instruction_hooks=0",
        status,count,point.serial,ticket.transaction,ticket.generation,ticket.weapon,ticket.holding,!ticket.holding&&!status,
        captured.arms,captured.count,edited.count,captured.model_names[0].data(),captured.model_names[1].data(),captured.bone_names[0].data(),
        captured.arms<captured.bone_names.size()?captured.bone_names[captured.arms].data():"",ticket.hand.origin.x,ticket.hand.origin.y,ticket.hand.origin.z);
    return true;
}
}
