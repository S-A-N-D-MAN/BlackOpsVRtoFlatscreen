// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_scene_transaction.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <cstdio>
using namespace bovr;
namespace {
unsigned checks{},failures{};void check(bool v,const char* s){++checks;if(!v){++failures;printf("FAIL %s\n",s);}}
std::uint64_t now()noexcept{return 1000;}
struct Transport final:vr::FrameTransport {
 unsigned begins{},submits{},ends{};
 bool initialize(const vr::xr::RuntimeConfig&,const vr::xr::HostCallbacks&)override{return true;}
 vr::BeginStatus begin(vr::xr::FrameState* f)override{*f={};f->frame_id=++begins;f->views_valid=f->should_render=true;return vr::BeginStatus::started;}
 vr::TransportResult discard(const vr::xr::FrameState&)override{++ends;return {vr::TransportStatus::zero_layer,0,0,true};}
 vr::TransportResult submit(IDirect3DDevice9*,const vr::xr::FrameState&,const vr::xr::StereoSourceLayout&)override{++submits;++ends;return {vr::TransportStatus::projection,1,3,true};}
 bool before_reset(bool)override{return true;}bool after_reset(bool,bool ok)override{return ok;}bool shutdown()override{return true;}
};
}
int main(){
 t5::NativeWeaponContext p{};p.valid=p.body_valid=p.normal_context=p.server_normal=p.primary_viewmodel=true;p.native_thread=7;
 p.cg_identity=0x10000;p.refdef_identity=0x20000;p.native_time=p.refdef_time=42;p.native_frame_time=16;
 p.stock_view.width=1280;p.stock_view.height=720;
 t5::NativeSceneSample s{7,p.cg_identity,p.refdef_identity,42,16,42,p.stock_view,true};
 for(unsigned variant=0;variant<3;++variant)for(unsigned v=0;v<24;++v){
    Transport t;vr::FrameCoordinator coordinator(t,now);coordinator.initialize({});const auto original=coordinator.begin_frame();
    const vr::NativeBuildIdentity native{0x3b40100,4,5};vr::SceneReservation reserved{};
    check(coordinator.reserve_scene_frame(native,&reserved),"native weapon borrows exactly the current canonical pending frame");
    t5::WeaponSceneTransaction transaction{true,reserved,p,{}};
    t5::BorrowedWeaponClaim c{};c.wrapper_token=6;c.camera_serial=20;c.camera_published_ms=1000;
    c.key.command_epoch=2;c.key.command_sequence=9;c.key.thread=7;
    const auto row=synthetic_profile_fixture::row(variant);const auto& shape=*t5::borrowed_profile::shape(row);
    c.profile_revision=shape.revision;c.key.profile_row=row;c.key.profile_revision=shape.revision;c.key.weapon=shape.weapon;c.key.gun_bone=shape.gun;c.key.flash_bone=shape.flash;c.key.bone_count=shape.total;
    transaction.borrowed={true,true,c,21};
    auto stamp=1050ULL,clear=21ULL,token=6ULL,epoch=2ULL,seq=9ULL;bool idle=true;auto scene=s;auto n=native;bool anchor=true;
    switch(v){case 1:transaction.borrowed.completed=false;break;case 2:++clear;break;case 3:++token;break;
    case 4:++epoch;break;case 5:++seq;break;case 6:idle=false;break;case 7:stamp=1101;break;
    case 8:++scene.stock_view.origin.x;break;case 9:++scene.native_frame_time;break;case 10:anchor=false;break;
    case 11:++n.native_frame;break;case 12:++n.build_serial;break;
    case 13:coordinator.cancel_scene_reservation(reserved.ticket.key,reserved.serial);break;
    case 14:coordinator.expire_scene_reservation(1101,100);break;
    case 15:coordinator.before_reset(false);coordinator.after_reset(true);break;
    case 16:stamp=999;break;
    case 17:transaction.borrowed.claim.profile_revision=2;break;
    case 18:transaction.borrowed.selected=true;break;
    case 19:transaction.borrowed.selected=true;break;
    case 20:transaction.borrowed.selected=transaction.borrowed.world_verified=true;break;
    case 21:transaction.borrowed.claim.key.profile_row=3;break;
    case 22:++transaction.borrowed.claim.key.gun_bone;break;
    case 23:++transaction.borrowed.claim.key.bone_count;break;}
    const bool completed=v==19||t5::borrowed_scene_completed(transaction.borrowed,stamp,clear,token,epoch,seq,idle);
    vr::FrameTicket output{};const auto result=t5::claim_weapon_scene_prediction(transaction,coordinator,n,scene,16,anchor,&output,completed);
    if(!v||v==20){
        check(result==t5::WeaponSceneClaim::reserved&&output.key==original.ticket.key,"sealed borrowed call joins original scene prediction");
        check(!output.frame.actions.focused,"fixture keeps hand input unfocused while head/eyes remain valid");
        const vr::BackendFrameIdentity backend{native.buffer_identity,native.build_serial};
        check(coordinator.stage_render_frame(output.key,backend,{})&&coordinator.mark_eye_complete(output.key,backend,0)&&coordinator.mark_eye_complete(output.key,backend,1),"same reserved prediction carries both native eye completion stamps");
        check(coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted&&t.submits==1,"hand-unfocused stock fallback submits canonical stereo once");
    }else{
        check(result==t5::WeaponSceneClaim::rejected&&!transaction.active&&!output.key.frame_id,"missing/intervening/stale completion cannot claim a scene");
        coordinator.present(nullptr);check(t.submits==0,"failed borrowed claim never submits invalid stereo");
    }
    coordinator.shutdown();
 }
 // Preserve a tombstone when its canonical ticket is cancelled and a newer one
 // is already available. The old native scene must never steal the successor.
 Transport t;vr::FrameCoordinator c(t,now);c.initialize({});c.begin_frame();vr::SceneReservation r{};
 const vr::NativeBuildIdentity n{0x10000,1,1};c.reserve_scene_frame(n,&r);t5::WeaponSceneTransaction old{true,r,p,{}};
 old.borrowed.active=true;c.cancel_scene_reservation(r.ticket.key,r.serial);c.present(nullptr);const auto newer=c.begin_frame();
 vr::FrameTicket out{};check(t5::claim_weapon_scene_prediction(old,c,n,s,16,true,&out,false)==t5::WeaponSceneClaim::rejected,"uncompleted old borrowed scene consumes only its tombstone");
 check(c.claim_render_frame(&out)&&out.key==newer.ticket.key,"newer canonical prediction is still untouched");c.discard_pending();c.shutdown();
 printf("borrowed_scene_checks=%u failures=%u native_calls=0 game_writes=0\n",checks,failures);return failures?1:0;
}
