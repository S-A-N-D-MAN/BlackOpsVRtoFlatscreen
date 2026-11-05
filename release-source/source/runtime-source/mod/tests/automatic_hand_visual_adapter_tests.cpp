// SPDX-License-Identifier: GPL-3.0-only
// Production prepare/publication gates against copied, authored fixtures only.
// No engine function, native ammo owner, XR provider or game is invoked.
#include "synthetic_runtime_profile_fixture.hpp"
#include "../src/t5/weapon_barrel_probe.cpp"
#include "t5/weapon_render_admission.hpp"
#include "t5/crossbow_adapter.hpp"
#include "t5/crossbow_script_aim.hpp"
#include <cstdio>

namespace fixture {
bool manual{},offhand{},publication{};
bovr::input::manual::Output reload{};
unsigned reads{},mesh_clears{},ammo_commands{},input_updates{};
}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept{return false;}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&) noexcept{return false;}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&,std::uint64_t,bool) noexcept{return false;}
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity&) noexcept{}
bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept{return false;}
std::uint64_t pose_clear_serial() noexcept{return 0;}
bool enable_weapon_native_clear_scope() noexcept{return false;}
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept{return {};}
bool install_pose_clear_probe(HMODULE,PoseClearCallback) noexcept{return false;}
bool copy_pose_clear_weapon_context(std::uintptr_t,int,std::uint64_t,NativeWeaponContext*) noexcept{return false;}
std::wstring executable_path(){return {};}
Validation validate_path(const std::wstring&){return {};}
bool validate_loaded_headers(HMODULE,std::string&){return false;}
std::string sha256(std::span<const std::uint8_t>){return {};}
namespace profile_detail {
bool protected_profile_copy(void*,std::uintptr_t,void*,std::size_t) noexcept{return false;}
ReadStatus read_profile(const ReadHost&,const NativeWeaponContext&,DWORD,std::uint64_t,ProfileRecord*,ReadWorkCounts* counts) noexcept{if(counts)*counts={};return ReadStatus::context;}
}
bool update_crossbow_input(const CommandContext&,const input::CommandRigSample&,bool) noexcept{return false;}
bool configure_crossbow_projectile_hook(HMODULE) noexcept{return false;}
bool configure_crossbow_script_aim(HMODULE) noexcept{return false;}
bool manual_reload_enabled() noexcept{return fixture::manual;}
bool manual_reload_uses_offhand() noexcept{return fixture::offhand;}
bool cancel_manual_reload_for_selection(const CommandContext&) noexcept{return true;}
void update_manual_reload_input(const CommandContext&,const input::CommandRigSample&,bool) noexcept{++fixture::input_updates;}
void apply_manual_reload_command(const CommandContext&,unsigned char*) noexcept{++fixture::ammo_commands;}
bool read_manual_reload_visual(input::manual::Output* out) noexcept{++fixture::reads;if(!fixture::publication)return false;*out=fixture::reload;return true;}
void publish_manual_reload_pose_visual(std::uint64_t,std::uint64_t,bool) noexcept{}
bool publish_hand_mesh(std::uintptr_t model,unsigned,std::uint64_t,std::uint64_t) noexcept{if(!model)++fixture::mesh_clears;return true;}
bool publish_left_hand_mesh(std::uintptr_t,const borrowed_profile::Profile&,unsigned,std::uint64_t,std::uint64_t) noexcept{return false;}
bool manual_reload_authored_device_visible(std::uint64_t,std::uint64_t) noexcept{return false;}
}
namespace {
using namespace bovr::t5;
namespace a=animation_detail;
namespace p=reload_pose;
namespace m=bovr::input::manual;
unsigned checks{},failures{};
void check(bool pass,const char* label){++checks;if(!pass){++failures;std::printf("FAIL %s\n",label);}}
NativeWeaponContext context(){
    NativeWeaponContext c{};c.valid=c.body_valid=c.normal_context=c.server_normal=c.primary_viewmodel=c.weapon_definition_valid=true;
    c.native_thread=17;c.draw_gun=1;c.weapon_index=4;c.command_number=10;c.command_clock=3000;c.native_time=c.refdef_time=3500;
    c.cg_identity=1000;c.refdef_identity=c.cg_identity+0x8c100;c.viewmodel_identity=2000;c.dobj_identity=3000;
    c.variant_identity=4000;c.definition_identity=5000;c.placement.scale=1;c.stock_view.origin={100,200,300};
    c.call_serial=1;c.weapon_class=2;return c;
}
struct Input {
    bovr::input::ReservedWeaponFrame reserved{};
    bovr::input::BorrowedPlacementValues proposal{};
    std::uint64_t epoch=7;
    Input(){
        const auto now=GetTickCount64();reserved.identity={2,9,3,4};reserved.anchor_generation=2;
        reserved.published_ms=now;reserved.exclusively_reserved_for_scene=true;reserved.prediction.frame_id=9;
        reserved.prediction.should_render=reserved.prediction.views_valid=reserved.prediction.actions.focused=true;
        auto& grip=reserved.prediction.actions.hands[0].grip;
        grip.active=grip.position_valid=grip.position_tracked=grip.orientation_valid=grip.orientation_tracked=true;
        grip.pose.position={-.25f,1.25f,-.35f};
        auto& aim=reserved.prediction.actions.hands[0].aim;
        aim.active=aim.position_valid=aim.position_tracked=aim.orientation_valid=aim.orientation_tracked=true;
        aim.pose.position=grip.pose.position;
        proposal.identity=reserved.identity;proposal.valid=true;proposal.body={{100,200,300},{{1,0,0},{0,1,0},{0,0,1}}};
    }
    void prepare(bool dual=false){prepare_borrowed_visual_pose(reserved,proposal,4,epoch,dual);}
};
bool admits(const p::Frame& frame,const NativeWeaponContext& c,bool manual,std::uint64_t now){
    return borrowed_visual_pose_current(frame,frame.identity,frame.epoch,true,c,manual,now);
}
void automatic_preparation(){
    fixture::manual=false;fixture::offhand=false;fixture::publication=true;
    fixture::reload.transaction=99;fixture::reload.stage=m::Stage::held;
    const auto initial_reads=fixture::reads;
    Input input;input.prepare();const auto automatic=reload_visual_frame;
    check(automatic.valid&&automatic.free_hand&&!automatic.dual_ready&&automatic.stage==m::Stage::ready&&automatic.transaction==0,
        "actual ordinary preparation creates automatic ready free-glove ticket");
    check(fixture::reads==initial_reads,"automatic visual ignores manual publication and its previous transaction");
    check(admits(automatic,context(),false,automatic.published_ms),"actual automatic ordinary publication gate admits current ticket");
    auto paired=input;paired.proposal.support.paired=true;paired.prepare();const auto support=reload_visual_frame;
    check(support.valid&&!support.free_hand&&!support.pimax_grip_fallback,
        "automatic support capture retains authored gripping glove");
    input.reserved.pimax_free_glove_grip_basis=true;input.prepare();
    check(reload_visual_frame.valid&&reload_visual_frame.free_hand&&reload_visual_frame.pimax_grip_fallback,
        "automatic released support retains accepted Pimax neutral basis");
    auto& palm=input.reserved.prediction.actions.hands[0].palm;palm=input.reserved.prediction.actions.hands[0].grip;
    input.prepare();check(reload_visual_frame.valid&&reload_visual_frame.palm_surface&&!reload_visual_frame.pimax_grip_fallback,
        "real tracked palm wins over Pimax automatic fallback");
    palm={};input.reserved.prediction.actions.hands[0].grip.orientation_tracked=false;input.prepare();
    check(!reload_visual_frame.valid,"lost support tracking cannot manufacture automatic hand ticket");
    input.reserved.prediction.actions.hands[0].grip.orientation_tracked=true;
    --input.reserved.anchor_generation;input.prepare();check(!reload_visual_frame.valid,"changed anchor rejects actual automatic preparation");
    ++input.reserved.anchor_generation;input.proposal.valid=false;input.prepare();
    check(!reload_visual_frame.valid,"unproposed native placement rejects actual automatic preparation");
    check(!fixture::ammo_commands&&!fixture::input_updates,"visual preparation never invokes manual ammo or gesture commands");

    synthetic_profile_fixture::Fixture skeleton;const auto row=skeleton.enroll();const auto* profile=borrowed_profile::get(row);
    const auto* shape=borrowed_profile::shape(row);check(profile&&shape,"authored ordinary fixture enrolls");
    if(!profile||!shape)return;
    std::array<a::NativeMat32,borrowed_profile::max_profile_bones> raw{};
    for(unsigned i=0;i<shape->total;++i){a::NativeMat32 bind{};std::memcpy(&bind,profile->bone[i].bind.data(),sizeof(bind));
        a::Transform local{};check(a::convert_native(bind,&local),"fixture transform is rigid");
        raw[i]=p::matrix(a::compose({{250,-100,50},{{0,1,0},{-1,0,0},{0,0,1}}},local));}
    const auto matrices=std::span<const a::NativeMat32>{raw.data(),shape->total};p::Result free{},gripped{};
    check(p::pose(row,automatic,matrices,&free)&&free.hand,"automatic ready ticket moves released glove");
    check(p::pose(row,support,matrices,&gripped)&&!gripped.hand&&!gripped.count&&
        !std::memcmp(raw.data(),gripped.matrices.data(),shape->total*sizeof(a::NativeMat32)),
        "automatic captured support leaves complete authored assembly unchanged");
    for(unsigned i=shape->arms;i<shape->total;++i)check(!free.changed[i]&&
        !std::memcmp(&raw[i],&free.matrices[i],sizeof(a::NativeMat32)),"automatic free glove preserves every gun, feed and muzzle matrix");
}
void publication_and_mode_guards(){
    Input input;fixture::manual=false;input.prepare();const auto good=reload_visual_frame;const auto c=context();
    auto f=good;f.valid=false;check(!admits(f,c,false,f.published_ms),"invalid visual rejected");
    f=good;++f.weapon;check(!admits(f,c,false,f.published_ms),"changed weapon rejected");
    check(!borrowed_visual_pose_current(good,{3,9,3,4},good.epoch,true,c,false,good.published_ms),"changed frame identity rejected");
    check(!borrowed_visual_pose_current(good,good.identity,good.epoch+1,true,c,false,good.published_ms),"changed native epoch rejected");
    check(!borrowed_visual_pose_current(good,good.identity,good.epoch,false,c,false,good.published_ms),"unverified native normal placement rejected");
    check(!admits(good,c,false,good.published_ms-1)&&!admits(good,c,false,good.published_ms+101),"future and stale publications rejected");
    check(admits(good,c,false,good.published_ms+100),"existing100ms freshness boundary retained");
    f=good;f.transaction=1;check(!admits(f,c,false,f.published_ms),"automatic mode cannot replay manual ammo transaction");
    for(const auto stage:{m::Stage::available,m::Stage::held,m::Stage::insertion,m::Stage::charge,m::Stage::charging,m::Stage::committing}){
        f=good;f.stage=stage;check(!admits(f,c,false,f.published_ms),"automatic mode cannot claim physical reload stage");
    }
    fixture::manual=true;fixture::publication=true;fixture::offhand=false;
    fixture::reload={};fixture::reload.supported=true;fixture::reload.weapon=4;fixture::reload.epoch=input.epoch;
    fixture::reload.generation=input.reserved.identity.generation;fixture::reload.updated_ms=GetTickCount64();
    fixture::reload.transaction=17;fixture::reload.stage=m::Stage::held;fixture::reload.block_attack=true;
    input.prepare();f=reload_visual_frame;
    check(f.valid&&f.stage==m::Stage::held&&f.transaction==17&&admits(f,c,true,f.published_ms),"manual live transaction still admits its authored reload glove");
    input.reserved.pimax_free_glove_grip_basis=true;input.prepare();
    check(reload_visual_frame.valid&&reload_visual_frame.pimax_grip_fallback&&reload_visual_frame.stage==m::Stage::held,
        "manual animated reload retains accepted Pimax compatibility path");
    fixture::reload.kind=m::ReloadKind::internal_rounds;fixture::reload.stage=m::Stage::charging;
    fixture::reload.pump_gripped=true;fixture::reload.charge_fraction=.5f;
    auto& squeeze=input.reserved.prediction.actions.hands[0].squeeze;squeeze.active=true;squeeze.current=1.f;
    input.prepare();check(reload_visual_frame.valid&&reload_visual_frame.pump_gripped&&!reload_visual_frame.pimax_grip_fallback,
        "actual physically held pump retains authored hand basis");
    fixture::reload.kind=m::ReloadKind::magazine;fixture::reload.stage=m::Stage::held;fixture::reload.pump_gripped=false;
    fixture::reload.charge_fraction=0.f;
    ++fixture::reload.transaction;check(!admits(f,c,true,f.published_ms),"changed manual transaction rejected");--fixture::reload.transaction;
    fixture::reload.hand=0;check(!admits(f,c,true,f.published_ms),"wrong manual hand owner rejected");fixture::reload.hand=1;
    fixture::publication=false;check(!admits(f,c,true,f.published_ms),"missing current manual publication cannot replay held glove");
    fixture::offhand=true;const auto cleared=fixture::mesh_clears;input.prepare(true);
    check(!reload_visual_frame.valid&&fixture::mesh_clears==cleared+1,"stale paired manual owner still retires ready dual hand mesh");
    fixture::manual=false;fixture::offhand=false;input.prepare(true);f=reload_visual_frame;auto dual=c;
    dual.dual_wield=true;dual.weapon_class=4;
    check(f.valid&&f.dual_ready&&!f.free_hand&&admits(f,dual,false,f.published_ms),"automatic ordinary dual pistols keep independent ready path");
    for(int state=0;state<18;++state){dual.weapon_state=state;
        check(admits(f,dual,false,f.published_ms)==bovr::input::paired_reload::ordinary_state(state),"automatic dual reload-state restriction retained");}
    dual.weapon_state=0;dual.other_weapon_state=12;check(!admits(f,dual,false,f.published_ms),"partner automatic reload cannot claim dual ready pose");
    dual.other_weapon_state=0;dual.weapon_class=3;check(!admits(f,dual,false,f.published_ms),"dual shotgun cannot enter independent-pistol path");
}
void unchanged_native_scope(){
    auto c=context();check(supported(c,17)&&expects_tracked_weapon_render(c,true),"ordinary automatic native weapon remains admitted");
    for(int state=0;state<18;++state){c.weapon_state=state;
        check(supported(c,17)&&expects_tracked_weapon_render(c,true),"native stock ammo reload states retain existing tracked render eligibility");}
    c.weapon_state=0;c.primary_viewmodel=false;
    check(!supported(c,17)&&!expects_tracked_weapon_render(c,true),"mounted/non-primary model remains outside primary tracking");
    c=context();c.normal_context=false;c.server_normal=false;
    check(!supported(c,17)&&!expects_tracked_weapon_render(c,true),"rejected primary context cannot gain tracked visuals");
    c=context();c.manual_grenade_primary_visual=true;
    check(!expects_tracked_weapon_render(c,true),"grenade-owned primary render remains outside ordinary tracked weapon claim");
    c=context();c.weapon_state=19;check(!supported(c,17)&&!expects_tracked_weapon_render(c,true),"native offhand/switch model remains outside ordinary claim");
}
}
int main(){automatic_preparation();publication_and_mode_guards();unchanged_native_scope();
    std::printf("%u automatic hand visual checks, %u failures\n",checks,failures);return failures?1:0;}
