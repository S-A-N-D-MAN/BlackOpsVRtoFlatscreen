// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_scene_transaction.hpp"
#include "vr/frame_coordinator.hpp"
#include <cstdio>
#include <cstdlib>
using namespace bovr;
namespace {
unsigned checks{};void check(bool pass,const char* why){++checks;if(!pass){std::printf("FAIL %s\n",why);std::exit(1);}}
std::uint64_t clock_value() noexcept{return 1000;}
struct Transport final:vr::FrameTransport {
    unsigned ends{},submits{},begins{};
    bool initialize(const vr::xr::RuntimeConfig&,const vr::xr::HostCallbacks&) override{return true;}
    vr::BeginStatus begin(vr::xr::FrameState* f) override{*f={};f->frame_id=++begins;f->views_valid=f->should_render=true;return vr::BeginStatus::started;}
    vr::TransportResult discard(const vr::xr::FrameState&) override{++ends;return {vr::TransportStatus::zero_layer,0,0,true};}
    vr::TransportResult submit(IDirect3DDevice9*,const vr::xr::FrameState&,const vr::xr::StereoSourceLayout&) override{++ends;++submits;return {vr::TransportStatus::projection,1,3,true};}
    bool before_reset(bool) override{return true;}bool after_reset(bool,bool ok) override{return ok;}bool shutdown() override{return true;}
};
}
int main() {
    t5::NativeWeaponContext p{};p.valid=p.body_valid=p.normal_context=p.server_normal=true;
    p.native_thread=123;p.cg_identity=0x100000;p.refdef_identity=0x18c100;
    p.native_time=p.refdef_time=4321;p.native_frame_time=16;p.stock_view.width=1280;p.stock_view.height=720;
    p.stock_view.origin={123,456,789};
    t5::NativeSceneSample scene{p.native_thread,p.cg_identity,p.refdef_identity,p.native_time,p.native_frame_time,p.refdef_time,p.stock_view,true};
    check(t5::matches_weapon_scene(p,scene),"same current native body/time/thread matches");
    auto linked=p;linked.normal_context=linked.server_normal=false;
    linked.linked_weapon_context=linked.linked_server_context=true;linked.server_link_identity=0x23450000;
    check(t5::matches_weapon_scene(linked,scene),"verified linked primary weapon retains its exact current scene association");
    linked.linked_server_context=false;
    check(!t5::matches_weapon_scene(linked,scene),"client-only linked eligibility cannot claim the native scene");
    linked.linked_server_context=true;linked.server_link_identity=0;
    check(!t5::matches_weapon_scene(linked,scene),"missing linked identity cannot associate a tracked scene");
    for (unsigned variant=0;variant<13;++variant) {
        auto changed=scene;
        switch(variant) {
        case 0:++changed.native_thread;break;case 1:++changed.cg_identity;break;case 2:++changed.refdef_identity;break;
        case 3:++changed.native_time;break;case 4:++changed.native_frame_time;break;case 5:++changed.refdef_time;break;
        case 6:changed.stock_view.origin.x+=0.01f;break;case 7:changed.stock_view.axis.forward.x+=0.01f;break;
        case 8:++changed.stock_view.width;break;case 9:++changed.stock_view.x;break;
        case 10:changed.stock_view.tan_half_fov_x+=0.01f;break;case 11:changed.stock_view.near_clip+=0.01f;break;
        case 12:changed.valid=false;break;
        }
        Transport transport;vr::FrameCoordinator c(transport,clock_value);c.initialize({});c.begin_frame();
        vr::SceneReservation reserved{};const vr::NativeBuildIdentity native{0x3b40100,5,variant+1};
        check(c.reserve_scene_frame(native,&reserved),"weapon producer reserves canonical XR frame");
        check(!t5::matches_weapon_scene(p,changed),"changed native body/build context rejects reserved proposal");
        c.cancel_scene_reservation(reserved.ticket.key,reserved.serial);vr::FrameTicket ticket{};
        check(!c.consume_scene_reservation(reserved,native,&ticket),"mismatch never claims a newer or cancelled prediction");
        check(c.present(nullptr).status==vr::CoordinatorStatus::discarded_incomplete&&transport.submits==0&&transport.ends==1,
              "each native mismatch ends one zero-layer frame and never submits mismatched weapon/eyes");c.shutdown();
    }
    auto bad=p;bad.body_valid=false;check(!t5::matches_weapon_scene(bad,scene),"unproven current refdef rejects");
    bad=p;bad.normal_context=false;check(!t5::matches_weapon_scene(bad,scene),"non-gameplay producer rejects");
    {
        Transport transport;vr::FrameCoordinator c(transport,clock_value);c.initialize({});const auto begun=c.begin_frame();
        vr::SceneReservation reservation{};const vr::NativeBuildIdentity native{0x3b40100,0,98};
        check(c.reserve_scene_frame(native,&reservation),"visible unfocused prediction reserves its canonical scene");
        check(!reservation.ticket.frame.actions.focused,"regression fixture is explicitly unfocused");
        check(t5::usable_weapon_scene_publication(reservation.ticket,1000,100),
              "actual retail publication policy preserves unfocused head/eye ownership");
        auto focused=reservation.ticket;focused.frame.actions.focused=true;
        check(t5::usable_weapon_scene_publication(focused,1100,100),"focused ticket accepts maximum prediction age");
        check(!t5::usable_weapon_scene_publication(reservation.ticket,1101,100),"unfocused does not excuse stale prediction");
        check(!t5::usable_weapon_scene_publication(reservation.ticket,999,100),"clock regression still rejects");
        auto invalid=reservation.ticket;invalid.frame.views_valid=false;
        check(!t5::usable_weapon_scene_publication(invalid,1000,100),"invalid head/eyes still reject");
        invalid=reservation.ticket;invalid.frame.should_render=false;
        check(!t5::usable_weapon_scene_publication(invalid,1000,100),"nonrendering prediction still rejects");
        t5::WeaponSceneTransaction transaction{true,reservation,p,{}};vr::FrameTicket output{};
        check(t5::claim_weapon_scene_prediction(transaction,c,native,scene,p.native_frame_time,true,&output)==t5::WeaponSceneClaim::reserved,
              "actual scene consumer accepts the preserved unfocused reservation");
        check(output.key==begun.ticket.key&&!output.frame.actions.focused&&transport.begins==1,
              "consumer receives exactly the original unfocused prediction without a newer begin");
        const vr::BackendFrameIdentity backend{native.buffer_identity,native.build_serial};
        check(c.stage_render_frame(output.key,backend,{}),"unfocused canonical frame stages stereo");
        check(c.mark_eye_complete(output.key,backend,0)&&c.mark_eye_complete(output.key,backend,1),"both actual eye stamps complete");
        check(c.present(nullptr).status==vr::CoordinatorStatus::submitted&&transport.submits==1&&transport.ends==1,
              "unfocused visible world submits both eyes once instead of zero-layer cancellation");
        c.shutdown();
    }
    for(unsigned reason=0;reason<3;++reason) {
        Transport transport;vr::FrameCoordinator c(transport,clock_value);c.initialize({});c.begin_frame();
        vr::SceneReservation old{};const vr::NativeBuildIdentity native{0x3b40100,0,99};c.reserve_scene_frame(native,&old);
        t5::WeaponSceneTransaction transaction{true,old,p,{}};
        if(reason==0) {c.cancel_scene_reservation(old.ticket.key,old.serial);c.present(nullptr);}
        if(reason==1) {c.expire_scene_reservation(1101,100);c.present(nullptr);}
        if(reason==2) {c.before_reset(false);c.after_reset(true);}
        const auto newer=c.begin_frame();vr::FrameTicket output{};
        check(t5::claim_weapon_scene_prediction(transaction,c,native,scene,p.native_frame_time,true,&output)==t5::WeaponSceneClaim::rejected,
              "actual scene claim policy retains old native tombstone across cancel/expiry/reset");
        check(output.key.frame_id==0&&!transaction.active,"late native scene consumes tombstone and receives no newer ticket");
        check(c.claim_render_frame(&output)&&output.key==newer.ticket.key,
              "late scene rejection left newer canonical prediction unclaimed");c.discard_pending();c.shutdown();
    }
    std::printf("weapon_scene_transaction_checks=%u failures=0 game_writes=0\n",checks);
}
