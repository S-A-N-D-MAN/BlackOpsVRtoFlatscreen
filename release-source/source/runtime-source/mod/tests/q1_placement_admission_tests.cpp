// SPDX-License-Identifier: GPL-3.0-only
// Actual borrowed-adapter lifecycle, with copied providers and reservation/token
// stubs. This is not a protected native-reader or renderer-coordinator test.
// Matrix-copy, geometry, proposal, claim and writer predicates are production.
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include "t5/borrowed_weapon_writer.hpp"
#include "t5/borrowed_profile_copy.hpp"
#include "t5/borrowed_matrix_copy.hpp"
#include "t5/borrowed_barrel_geometry.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/pose_clear_probe.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace bovr { void trace(const char*,const char*,...) noexcept {} }
namespace bovr::t5 {
float predicted_shot_kickback(std::uint64_t,std::uint64_t,unsigned,std::uint64_t) noexcept {return 0.f;}
namespace {
namespace anim=animation_detail;
unsigned checks{},failures{};
void check(bool condition,const char* label){++checks;if(!condition){++failures;std::printf("FAIL %s\n",label);}}
// Only the copied fields consumed by the included adapter are represented.
// These are test-provider values, not a claim about the native structure ABI.
struct NativeBorrow {
    std::uintptr_t dobj{},models_table{},server{};
    std::array<std::uintptr_t,2> models{};
    std::array<unsigned short,160> bone_names{};
    unsigned tag{},tag_bone{},bone_count{},model_count{2};
    int skeleton_clock{};
    friend bool operator==(const NativeBorrow&,const NativeBorrow&)=default;
};
struct AnimationProfile {
    unsigned profile_row{},profile_revision{},weapon_index{},gun_bone{};
};
struct AnimationAssembly {
    std::uintptr_t arena{};
    unsigned short duplicate_handle{};
    unsigned char duplicate_size{};
    std::array<std::uintptr_t,2> names{},parent_data{},bind_data{};
};
struct AnimationNative {
    const NativeWeaponContext* context{};NativeBorrow borrow{};
    const AnimationProfile* profile{};AnimationAssembly assembly{};
    std::uint64_t clear{},wrapper{};
};
struct Provider {
    NativeWeaponContext context{};PreCommandSnapshot pre{};
    NativeBorrow borrow{};AnimationProfile profile{};AnimationAssembly assembly{};
    anim::ClearComparisonKey key{};anim::LockedCopy copied{};
    WeaponNativeClearScope scope{17,1,true};std::uint64_t clear{40};
    input::profile_admission::Token token{31,2};command_profile::Copy command{};
    input::ReservedWeaponFrame reserved{};input::WeaponBodySample body{};
    BorrowedWeaponClaim reserved_claim{};
    bool enabled{true},current{true},profile_ok{true},borrow_ok{true},assembly_ok{true},state_ok{true},copy_ok{true};
    bool admission_ok{true},witness_ok{true},reserve_ok{true},select_ok{true},complete_ok{true},throw_lock{},revoke_copy{};
    unsigned reads{},witnesses{},locks{},unlocks{},states{},copies{},reserves{},selected{},completed{},cancelled{},revoked{};
    std::uint32_t server{0x712000},clients{0x712000};unsigned fail_copy{};unsigned protected_copies{};
} provider;
std::atomic_flag native_clear_busy=ATOMIC_FLAG_INIT;
std::atomic<bool> olympia_enabled{},animation_enabled{};
bool supported(const NativeWeaponContext& c,DWORD thread) noexcept {return provider.current&&c.valid&&c.native_thread==thread;}
bool animation_extra_context(const NativeWeaponContext&) noexcept {return provider.current;}
bool native_precommand(void*,PreCommandSnapshot* out) noexcept {*out=provider.pre;return provider.current;}
bool fresh_command(const NativeWeaponContext&,const PreCommandSnapshot&,std::uint64_t) noexcept {return provider.current;}
bool animation_profile(const NativeWeaponContext&,std::uint64_t,AnimationProfile* out) noexcept {*out=provider.profile;return provider.profile_ok;}
bool native_borrow(void*,const NativeWeaponContext&,NativeBorrow* out) noexcept {*out=provider.borrow;return provider.borrow_ok;}
anim::ClearComparisonKey native_clear_key(const NativeWeaponContext&,const NativeBorrow&,const AnimationProfile&,const PreCommandSnapshot&) noexcept {return provider.key;}
bool animation_assembly_copy(const NativeBorrow&,const AnimationProfile&,AnimationAssembly* out) noexcept {*out=provider.assembly;return provider.assembly_ok;}
void animation_lock(void*,std::uintptr_t){++provider.locks;if(provider.throw_lock)throw std::runtime_error("owned lock fixture");}
void animation_unlock(void*,std::uintptr_t){++provider.unlocks;}
bool animation_current_state(void*,const anim::LockedRequest&,anim::CacheCopy* out,bool full) noexcept {
    ++provider.states;check(full,"adapter requests full current copied provider");
    *out={0x714000,provider.copied.clock,provider.copied.final_stamp,provider.copied.pose,true,true,{}};
    return provider.state_ok;
}
bool animation_copy(void*,const anim::LockedRequest&,std::uintptr_t,anim::NativeMat32* gun,anim::NativeMat32* flash) noexcept {
    ++provider.copies;*gun=provider.copied.gun;*flash=provider.copied.flash;
    if(provider.revoke_copy)provider.witness_ok=false;
    return provider.copy_ok;
}
}
bool weapon_native_clear_probe_enabled() noexcept {return true;}
bool weapon_two_hand_support_enabled() noexcept {return false;}
bool read_pre_command_snapshot(PreCommandSnapshot* out) noexcept {
    if(!out)return false;
    *out=provider.pre;return provider.current;
}
std::uint64_t pose_clear_serial() noexcept {return provider.clear;}
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept {return provider.scope;}
bool exact_profile_aim_enabled() noexcept {return provider.enabled;}
void invalidate_native_command_aim() noexcept {++provider.revoked;provider.witness_ok=false;}
bool command_profile_admission_witness(input::profile_admission::Token token) noexcept {
    ++provider.witnesses;return provider.witness_ok&&token==provider.token;
}
bool read_command_profile_admission(const anim::ClearComparisonKey& key,input::profile_admission::Token* token,command_profile::Copy* copy) noexcept {
    ++provider.reads;*token={};*copy={};
    if(!provider.admission_ok||!provider.witness_ok||key!=provider.key)return false;
    *token=provider.token;*copy=provider.command;return true;
}
namespace command_profile {
bool protected_copy(void*,std::uintptr_t address,void* out,std::size_t bytes) noexcept {
    if(++provider.protected_copies==provider.fail_copy||bytes!=4)return false;
    std::uint32_t value{};
    if(address==0x1a796f8+0x13c)value=provider.server;
    else if(address==0x1c03140)value=provider.clients;
    else if(address==provider.borrow.models[0])value=0x720000;
    else if(address==provider.borrow.models[1])value=0x721000;
    else return false;
    std::memcpy(out,&value,4);return true;
}
}
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim& claim,input::ReservedWeaponFrame* out,input::WeaponBodySample* body) noexcept {
    ++provider.reserves;provider.reserved_claim=claim;
    check(claim.command_admission==(provider.enabled?provider.token:input::profile_admission::Token{}),"command token latched before reservation, or absent on old route");
    *out=provider.reserved;*body=provider.body;return provider.reserve_ok;
}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity& id,const BorrowedWeaponClaim& claim) noexcept {
    ++provider.selected;check(id==provider.reserved.identity&&claim==provider.reserved_claim,"selection uses same reservation and complete claim");
    return provider.select_ok&&command_profile_admission_witness(claim.command_admission);
}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity& id,const BorrowedWeaponClaim& claim,std::uint64_t serial,bool world) noexcept {
    ++provider.completed;check(id==provider.reserved.identity&&claim==provider.reserved_claim&&serial==41&&world,"completion retains selected normal world witness and claim");
    return provider.complete_ok&&command_profile_admission_witness(claim.command_admission);
}
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity& id) noexcept {
    ++provider.cancelled;check(id==provider.reserved.identity,"cancellation names the reserved frame");
}
namespace {
void prepare_borrowed_visual_pose(const input::ReservedWeaponFrame&,const input::BorrowedPlacementValues&,unsigned,std::uint64_t,bool) noexcept {}
void apply_borrowed_visual_pose(const NativeWeaponContext&,const anim::ClearValue&) {}
unsigned client_angle_writes{};
bool write_client_barrel_angles(const NativeWeaponContext&,anim::Vec forward) noexcept {
    ++client_angle_writes;check(provider.completed==1&&provider.complete_ok,"client aim changes only after accepted native completion");
    check(anim::finite(forward)&&std::fabs(anim::dot(forward,forward)-1)<.003f,"client aim gets a unit barrel direction");return true;
}
}
#include "t5/weapon_borrowed_adapter.inc"
namespace {
anim::NativeMat32 matrix(const anim::Transform& value){const auto q=borrowed_writer::quaternion(value.axis);return {{q.x,q.y,q.z,q.w},value.origin,2};}
anim::ClearValue camera;
void setup(unsigned variant=3){
    client_angle_writes=0;provider={};borrowed_pending={};borrowed_camera.invalidate();native_clear_busy.clear();borrowed_counts={};
    borrowed_enabled=true;borrowed_writer_enabled=true;borrowed_exact_required=true;
    const auto now=GetTickCount64();const auto row=synthetic_profile_fixture::row(variant);const auto& shape=*borrowed_profile::shape(row);
    auto& c=provider.context;c.valid=c.normal_context=c.server_normal=c.body_valid=c.primary_viewmodel=true;c.native_thread=GetCurrentThreadId();
    c.weapon_index=shape.weapon;c.command_number=11;c.command_clock=100;c.native_time=100;c.native_frame_time=16;
    c.cg_identity=0x701000;c.viewmodel_identity=0x702000;c.dobj_identity=0x703000;c.variant_identity=0x704000;c.definition_identity=0x705000;
    provider.profile={row,shape.revision,shape.weapon,shape.gun};
    auto& b=provider.borrow;b.dobj=c.dobj_identity;b.models_table=0x706000;b.server=provider.server;b.models={0x710000,0x711000};b.tag=3;b.tag_bone=shape.flash;b.bone_count=shape.total;b.skeleton_clock=7;
    for(unsigned n=0;n<b.bone_count;++n)b.bone_names[n]=static_cast<unsigned short>(8*(n+1));b.tag=b.bone_names[shape.flash];
    provider.assembly={0x730000,1,21,{0x740000,0x741000},{0x750000,0x751000},{0x760000,0x761000}};
    auto& key=provider.key;key.command_epoch=5;key.command_sequence=13;key.thread=c.native_thread;key.weapon=c.weapon_index;key.profile_row=row;key.profile_revision=shape.revision;
    key.native_time=c.native_time;key.frame_time=c.native_frame_time;key.command_number=c.command_number;key.command_clock=c.command_clock;key.skeleton_clock=7;
    key.gun_bone=shape.gun;key.flash_bone=shape.flash;key.bone_count=shape.total;key.pool_index=1;
    key.opaque_identity={0x701000,0x702000,0x703000,0x704000,0x705000,0x706000};
    provider.pre.sequence=13;provider.pre.gameplay_epoch=5;
    auto& cmd=provider.command;cmd.profile={row,shape.revision,shape.weapon,shape.gun,shape.flash,shape.total,1};
    auto& id=cmd.identity;id.server=provider.server;id.tracking_mode=1;id.arena=0x730000;id.duplicates=1;id.duplicate_size=21;id.tag=static_cast<unsigned short>(b.tag);
    for(unsigned i=0;i<2;++i)id.model_storage[i]={static_cast<std::uint32_t>(b.models[i]),0x720000+0x1000*i,static_cast<std::uint32_t>(provider.assembly.names[i]),static_cast<std::uint32_t>(provider.assembly.parent_data[i]),static_cast<std::uint32_t>(provider.assembly.bind_data[i])};
    for(unsigned n=0;n<b.bone_count;++n)id.ids[n]=b.bone_names[n];
    provider.copied.gun={{0,0,0,1},{},2};std::memcpy(&provider.copied.flash,borrowed_profile::get(row)->bone[shape.flash].bind.data(),32);
    provider.copied.clock=provider.copied.old_stamp=provider.copied.final_stamp=7;
    camera={key,provider.copied,40,now,true,0};borrowed_camera.publish(camera);
    auto& r=provider.reserved;r.identity={9,20,30,40};r.anchor_generation=9;r.published_ms=now;r.exclusively_reserved_for_scene=true;
    auto& f=r.prediction;f.frame_id=20;f.should_render=f.views_valid=true;f.predicted_display_time=90000;f.predicted_display_period=11111111;
    f.actions.sequence=50;f.actions.focused=true;auto& hand=f.actions.hands[1];
    hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;hand.grip.pose.position={.1F,.2F,-.3F};
    hand.aim.active=hand.aim.orientation_valid=hand.aim.orientation_tracked=true;
    provider.body={r.identity,{{},{{1,0,0},{0,1,0},{0,0,1}}},input::BodyCameraSource::completed_native_refdef};
}
void before(){SetLastError(0x41234567);observe_borrowed_weapon_before(provider.context);check(GetLastError()==0x41234567,"before preserves LastError");}
bool select(){input::ScaledWeaponPlacement out{};out.origin={321,654,987};const auto original=out;SetLastError(0x41234568);
    const bool value=select_borrowed_weapon_placement(provider.context,&out);check(GetLastError()==0x41234568,"select preserves LastError");
    if(!value)check(!std::memcmp(&out,&original,sizeof(out)),"refused selection leaves owned output unchanged");return value;}
anim::ClearValue normal(){
    const auto& p=borrowed_pending.proposal;provider.copied.gun=matrix(p.gun);provider.copied.flash=matrix(p.barrel);
    constexpr float degrees=57.29577951308232F;const auto& axis=p.dobj.axis;
    provider.copied.pose={p.dobj.origin,{std::asin(-axis.forward.z)*degrees,std::atan2(axis.forward.y,axis.forward.x)*degrees,std::atan2(axis.left.z,axis.up.z)*degrees}};
    provider.clear=41;return {provider.key,provider.copied,41,GetTickCount64(),true,provider.scope.token};
}
void positive(unsigned row){
    setup(row);const auto native=provider.context;before();check(borrowed_pending.active&&provider.reserves==1&&borrowed_pending.proposal.valid,"actual before publishes valid reserved proposal");
    check(select()&&provider.selected==1,"actual selector admits current matching token");
    const auto n=normal();borrowed_normal_work(provider.context,n);check(borrowed_pending.active&&borrowed_pending.write.normal_verified,"actual normal callback records world witness");
    borrowed_complete_work(provider.context,n);check(!borrowed_pending.active&&provider.completed==1&&!provider.cancelled,"actual after completes exactly once");
    check(client_angle_writes==1,"one client aim update per accepted placement");
    const auto reads=provider.reads;retire_borrowed_weapon_invocation(17);
    check(!provider.revoked&&provider.witness_ok&&provider.reads==reads,"normal wrapper retirement keeps completed command admission alive");
    check(!std::memcmp(&native,&provider.context,sizeof(native)),"no native context or original placement write");
    check(provider.locks==provider.unlocks&&provider.locks==4&&provider.states==8&&provider.copies==8,"four real locked current-copy barriers remain balanced");
    before();check(!borrowed_pending.active&&provider.reserves==1,"consumed camera cannot reserve again");
}
void changed(unsigned phase,unsigned kind){
    setup();if(phase){before();check(borrowed_pending.active,"negative starts with published before");}
    if(phase>1)check(select(),"negative starts after actual selection");
    anim::ClearValue n{};if(phase>1)n=normal();if(phase>2)borrowed_normal_work(provider.context,n);
    switch(kind){
    case 0:provider.witness_ok=false;break;
    case 1:++provider.token.serial;break;
    case 2:++provider.token.revision;break;
    case 3:++provider.command.identity.arena;break;
    case 4:++provider.command.identity.duplicates;break;
    case 5:++provider.command.identity.duplicate_size;break;
    case 6:++provider.command.identity.tag;break;
    case 7:++provider.command.identity.model_storage[1][0];break;
    case 8:++provider.command.identity.model_storage[0][1];break;
    case 9:++provider.command.identity.model_storage[0][2];break;
    case 10:++provider.command.identity.model_storage[1][3];break;
    case 11:++provider.command.identity.model_storage[1][4];break;
    case 12:++provider.command.identity.ids[provider.key.flash_bone];break;
    case 13:++provider.server;break;
    case 14:++provider.clients;break;
    case 15:provider.admission_ok=false;break;
    case 16:provider.assembly_ok=false;break;
    case 17:provider.fail_copy=provider.protected_copies+4;break;
    case 18:provider.command.identity.tracking_mode=2;break;
    case 19:provider.command.identity.server_link=0x23450000;break;
    case 20:++provider.borrow.server;break;
    }
    if(!phase){before();check(!borrowed_pending.active&&!provider.reserves,"changed admission before cannot publish reservation");}
    else if(phase==1){check(!select()&&!provider.selected,"changed admission at selector cannot publish scratch");retire_borrowed_weapon_invocation(17);}
    else if(phase==2){borrowed_normal_work(provider.context,n);check(!borrowed_pending.active&&provider.cancelled==1&&!provider.completed,"changed normal admission cancels selected reservation");}
    else{borrowed_complete_work(provider.context,n);check(!borrowed_pending.active&&provider.cancelled==1&&!provider.completed,"changed after admission cancels selected reservation");}
    check(!client_angle_writes,"refused placement never changes client aim");
    check(provider.locks==provider.unlocks,"all normal refusal paths preserve lock balance");
}
// The actual protected provider's model-name parser is covered separately.
// Here its refusal must flow through the actual placement lifecycle, and a
// changed already-copied name pointer must not reuse the command admission.
void prisoner_profile_refusal(){
    for(unsigned row:{4U,5U}){
        setup(row);provider.profile_ok=false;before();
        check(!borrowed_pending.active&&!provider.reserves,"new-row foreign-profile provider refusal cannot reserve placement");
        provider.profile_ok=true;borrowed_camera.publish(camera);before();
        check(borrowed_pending.active&&select(),"fresh copied known profile recovers after provider refusal");
        const auto n=normal();borrowed_normal_work(provider.context,n);borrowed_complete_work(provider.context,n);
        check(provider.completed==1&&!borrowed_pending.active,"recovered prisoner profile completes actual placement");
        setup(row);before();++provider.command.identity.model_storage[0][1];
        check(!select()&&!provider.selected,"changed current model-name pointer rejects prisoner placement");
        retire_borrowed_weapon_invocation(17);check(provider.cancelled==1&&provider.revoked==1,"mismatched new-row name identity cancels and revokes pending scope");
        positive(row);
    }
}
void lifecycle(){
    setup();provider.enabled=false;before();check(!borrowed_pending.active&&!provider.reads&&!provider.reserves,"partial startup never publishes active borrowed proposal");
    setup();before();retire_borrowed_weapon_invocation(18);check(borrowed_pending.active&&!provider.cancelled&&!provider.revoked,"foreign wrapper token cannot retire owner claim");
    retire_borrowed_weapon_invocation(17);check(!borrowed_pending.active&&provider.cancelled==1&&provider.revoked==1&&!provider.witness_ok,"matching unfinished retirement cancels and revokes");
    setup();before();check(select(),"fault path begins selected");provider.throw_lock=true;bool caught=false;
    try{const auto n=normal();borrowed_normal_work(provider.context,n);}catch(const std::runtime_error&){caught=true;}
    check(caught&&borrowed_pending.active,"native lower fault propagates with pending claim retained");
    retire_borrowed_weapon_invocation(17);check(!borrowed_pending.active&&provider.cancelled==1&&provider.revoked==1,"wrapper fault retirement cancels retained pending without retry");
    check(provider.locks==provider.unlocks+1,"native lower fault does not synthesize original unlock");
    setup();before();provider.revoke_copy=true;check(!select()&&!provider.selected,"revocation during current matrix copy refuses final selection");retire_borrowed_weapon_invocation(17);
    setup();before();native_clear_busy.test_and_set();const auto state=borrowed_pending;const auto reads=provider.reads;
    check(!select(),"nested selector refuses");retire_borrowed_weapon_invocation(17);observe_borrowed_weapon_before(provider.context);
    check(provider.reads==reads&&borrowed_pending.claim==state.claim&&!provider.revoked&&!provider.cancelled,"busy nested callbacks do not mutate owner pending");native_clear_busy.clear();retire_borrowed_weapon_invocation(17);
    setup();provider.reserve_ok=false;before();check(!borrowed_pending.active&&provider.reserves==1&&!provider.cancelled,"reservation refusal does not create pending metadata");
    setup();before();provider.select_ok=false;check(!select(),"renderer selection rejection leaves original input");retire_borrowed_weapon_invocation(17);
    setup();before();check(select(),"completion rejection begins selected");const auto n=normal();borrowed_normal_work(provider.context,n);provider.complete_ok=false;borrowed_complete_work(provider.context,n);
    check(!borrowed_pending.active&&provider.completed==1&&provider.cancelled==1,"renderer completion rejection cancels rather than credits frame");
    setup();provider.state_ok=false;before();check(!borrowed_pending.active&&!provider.reserves&&provider.locks==provider.unlocks,"copied current-state failure refuses before reserve");
    setup();provider.copy_ok=false;before();check(!borrowed_pending.active&&!provider.reserves&&provider.locks==provider.unlocks,"copied matrix failure refuses before reserve");
    setup();borrowed_exact_required=false;provider.enabled=false;before();check(borrowed_pending.active&&!provider.reads,"unchanged non-exact route does not consult command admission");retire_borrowed_weapon_invocation(17);
    check(!provider.revoked,"non-exact retirement preserves old command behavior");
    positive(3); // Fresh camera/reservation recovers after all refusal/fault cases.
}
}
}
int main(){using namespace bovr::t5;for(unsigned row=0;row<6;++row)positive(row);
    for(unsigned phase=0;phase<4;++phase)for(unsigned kind=0;kind<21;++kind){if(!phase&&(kind==1||kind==2))continue;changed(phase,kind);}
    prisoner_profile_refusal();lifecycle();std::printf("Q1 placement admission: checks=%u failures=%u native_gpu_calls=0 copied_provider=1\n",checks,failures);return failures?1:0;}
