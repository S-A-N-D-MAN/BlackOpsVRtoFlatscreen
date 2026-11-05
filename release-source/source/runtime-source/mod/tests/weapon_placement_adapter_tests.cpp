// SPDX-License-Identifier: GPL-3.0-only
#define configure_weapon_placement_adapter configure_weapon_placement_adapter_test_copy
#include "../src/t5/weapon_placement_adapter.cpp"
#undef configure_weapon_placement_adapter
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace bovr::t5;
unsigned failures{},checks{};
void check(bool yes,const char* name) {++checks;printf("%s %s\n",yes?"PASS":"FAIL",name);if(!yes) ++failures;}
struct Fake {
    std::uint64_t now{1000},generation{2},frames{},profiles{},reservations{},cancellations{};
    bool can_reserve{true},focused{true},tracked{true},valid_reservation{true},allowed_command{true};
    ip::WeaponFrameIdentity cancelled{};
    static bool reserve(void* p,const NativeWeaponContext& c,ip::ReservedWeaponFrame* f,ip::WeaponBodySample* b) {
        auto& h=*static_cast<Fake*>(p);if(!h.can_reserve) return false;
        ++h.reservations;f->identity={h.generation,++h.frames,100+h.frames,200+h.frames};f->exclusively_reserved_for_scene=true;
        f->published_ms=h.now;f->anchor_generation=h.generation;
        auto& xr=f->prediction;xr.frame_id=f->identity.frame_id;xr.predicted_display_time=12345;xr.predicted_display_period=11111111;
        xr.should_render=xr.views_valid=true;xr.head_center.position={0,1.6F,0};f->tracking_anchor=xr.head_center;
        xr.actions.focused=h.focused;xr.actions.sequence=h.frames;
        for(auto& hand:xr.actions.hands) {
            hand.grip.active=hand.grip.position_valid=true;hand.grip.position_tracked=h.tracked;hand.grip.pose.position={0.2F,1.4F,-0.5F};
            hand.aim.active=hand.aim.orientation_valid=hand.aim.orientation_tracked=true;
        }
        b->identity=f->identity;b->stock_camera={c.stock_view.origin,c.stock_view.axis};b->source=ip::BodyCameraSource::completed_native_refdef;return true;
    }
    static void cancel(void* p,const ip::WeaponFrameIdentity& key) {auto& h=*static_cast<Fake*>(p);++h.cancellations;h.cancelled=key;}
    static std::uint64_t clock(void* p) {return static_cast<Fake*>(p)->now;}
    static void profile(void* p,const NativeWeaponContext&,const ip::ReservedWeaponFrame&,const ip::WeaponCalibration&) {++static_cast<Fake*>(p)->profiles;}
    static bool validate(void* p,const NativeWeaponContext&,const ip::WeaponFrameIdentity&) {return static_cast<Fake*>(p)->valid_reservation;}
    static bool command(void* p,const NativeWeaponContext&,std::uint64_t) {return static_cast<Fake*>(p)->allowed_command;}
    ProposalHost host() {return {this,reserve,cancel,clock,profile,nullptr,validate,command};}
};
NativeWeaponContext native() {
    NativeWeaponContext c{};c.native_thread=17;c.call_serial=1;c.cg_identity=0x1000;c.refdef_identity=0x8d100;c.viewmodel_identity=0x2000;
    c.dobj_identity=0x3000;c.variant_identity=0x4000;c.definition_identity=0x5000;c.draw_gun=1;c.native_time=c.refdef_time=3000;
    c.native_frame_time=16;c.weapon_index=4;c.valid=c.body_valid=c.normal_context=c.server_normal=c.primary_viewmodel=c.weapon_definition_valid=true;
    c.stock_view.origin={100,200,300};c.stock_view.width=1280;c.stock_view.height=720;c.stock_view.tan_half_fov_x=c.stock_view.tan_half_fov_y=1;
    c.placement.origin=c.stock_view.origin;return c;
}
}
int main() {
    {Fake host;PassivePlacementAdapter adapter{};adapter.host=host.host();auto linked=native();
        linked.normal_context=linked.server_normal=false;linked.linked_weapon_context=linked.linked_server_context=true;
        linked.server_link_identity=0x23450000;adapter.before(linked);
        check(adapter.last_proposal.valid&&adapter.calibration.confirmed,"linked primary scope can form a current passive placement proposal");
        const auto reserved=adapter.reserved_key;linked.server_link_identity+=4;adapter.after(linked);
        check(host.cancelled==reserved&&!adapter.last_proposal.valid&&!adapter.calibration.confirmed,
            "linked parent change after native placement cancels the exact proposal");}
    {Fake host;PassivePlacementAdapter adapter{};adapter.host=host.host();auto current=native();current.stack_count=1;
        adapter.before(current);const auto key=adapter.reserved_key;current.stack_count=4;adapter.after(current);
        check(host.cancelled==key&&!adapter.last_proposal.valid,"changed stacked-fire count retires passive invocation without reusing its reservation");}
    Fake fake;PassivePlacementAdapter a{};a.host=fake.host();auto c=native();const auto unchanged=c.placement;
    a.before(c);const auto first=a.reserved_key;
    check(a.last_proposal.valid&&a.last_status==Status::accepted&&fake.profiles==1&&a.calibration.confirmed,"native-ready same-ticket sample derives explicit experimental profile and passive proposal");
    check(a.last_proposal.identity==first&&first.generation&&first.frame_id&&first.reservation_serial&&first.native_build_serial,"proposal carries all four actual reservation fields");
    check(std::fabs(a.last_proposal.placement.origin.x-c.placement.origin.x)<0.002F&&std::fabs(a.last_proposal.placement.origin.y-c.placement.origin.y)<0.002F&&
        std::fabs(a.last_proposal.placement.origin.z-c.placement.origin.z)<0.002F&&!memcmp(&unchanged,&c.placement,32),"initial alignment matches native placement without changing any input byte");
    a.after(c);check(fake.cancellations==0,"unchanged native post preserves reservation for scene consumption");
    ++c.call_serial;c.weapon_state=6;a.before(c);check(a.last_proposal.valid&&fake.profiles==1,"later primary firing state reuses explicit profile rather than recalibrating");
    const auto reserved=a.reserved_key;auto changed=c;changed.dobj_identity+=4;a.after(changed);
    check(fake.cancellations==1&&fake.cancelled==reserved&&!a.calibration.confirmed&&!a.last_proposal.valid,"post-native DObj change cancels exact four-field ticket and invalidates observed profile");
    c=native();c.weapon_state=6;a.before(c);check(a.last_status==Status::calibration_mismatch&&!a.calibration.confirmed,"non-ready native state cannot mint initial experimental alignment");
    c.weapon_state=0;a.before(c);check(a.last_proposal.valid&&fake.profiles==2,"ready state can establish a new explicit observed profile");
    auto foreign=c;foreign.native_thread=18;const auto preserved=a.last_proposal;a.before(foreign);
    check(a.last_proposal.identity==preserved.identity&&a.last_proposal.valid,"foreign frontend thread cannot replace current proposal state");
    c.normal_context=false;a.before(c);check(!a.calibration.confirmed&&!a.last_proposal.valid&&!a.identity_valid,"pause/death/freeze context loss invalidates observed continuity");
    c.normal_context=true;a.before(c);const auto epoch=a.epoch;
    fake.now+=251;a.before(c);check(a.epoch>epoch&&fake.profiles==4,"callback gap invalidates profile even when native pointers are reused");
    const auto epoch2=a.epoch;c.native_time=c.refdef_time=100;a.before(c);check(a.epoch>epoch2&&fake.profiles==5,"native-time rollback invalidates same-address observed profile");
    ++fake.generation;a.before(c);check(a.last_proposal.identity.generation==fake.generation&&fake.profiles==6,"XR generation reset derives a separately stamped profile");
    const auto before_loss=fake.cancellations;fake.focused=false;a.before(c);
    check(!a.last_proposal.valid&&a.last_status==Status::invalid_tracking&&fake.cancellations==before_loss&&a.reserved_key.reservation_serial,"unfocused weapon input preserves canonical head scene and native weapon");
    fake.focused=true;fake.can_reserve=false;const auto reservations=fake.reservations;a.before(c);
    check(!a.last_proposal.valid&&a.last_status==Status::reservation_mismatch&&fake.reservations==reservations,"no pending reservation cannot fall back to cached actions or proposal");
    fake.can_reserve=true;c.dual_wield=true;a.before(c);check(!a.last_proposal.valid&&a.last_status==Status::context_rejected,"dual wield remains outside initial passive eligibility");
    c.dual_wield=false;c.stack_count=1;a.before(c);check(a.last_proposal.valid,"retained single-shot count preserves passive primary placement");
    c.stack_count=8;a.before(c);check(!a.last_proposal.valid,"out-of-range stack remains outside passive eligibility");
    c.stack_count=0;c.weapon_type=1;a.before(c);check(!a.last_proposal.valid,"projectile type remains outside initial passive eligibility");
    c.weapon_type=0;c.primary_viewmodel=false;a.before(c);check(!a.last_proposal.valid,"offhand or prior-model transition cannot reuse primary profile");
    c.primary_viewmodel=true;a.before(c);const auto key=a.reserved_key;++c.call_serial;a.after(c);
    check(fake.cancelled==key&&!a.last_proposal.valid,"post-call serial mismatch cancels its exact reservation");
    Fake active_host;PassivePlacementAdapter active{};active.host=active_host.host();c=native();c.model_generation=77;c.model_lifetime_verified=true;
    ip::ScaledWeaponPlacement scratch{};active.before(c);
    check(!active.select(c,&scratch),"native placement remains disabled despite a valid proposal and verified lifetime");
    active.placement_enabled=true;
    check(active.select(c,&scratch)&&!memcmp(&scratch,&active.last_proposal.placement,32),"explicit activation returns only current same-ticket owned scratch");
    auto unverified=c;unverified.model_lifetime_verified=false;
    check(!active.select(unverified,&scratch),"observed pointer epoch cannot authorize native placement");
    ++c.model_generation;active.before(c);
    check(active.calibration.model_generation==c.model_generation&&active_host.profiles==2,"same-address verified allocation change recalibrates separate lifetime");
    active_host.allowed_command=false;check(!active.select(c,&scratch),"retained pre-native scripted override blocks scratch substitution");active_host.allowed_command=true;
    active_host.valid_reservation=false;check(!active.select(c,&scratch),"point-in-time reservation validation must still pass immediately before native call");active_host.valid_reservation=true;
    active_host.tracked=false;const auto cancels=active_host.cancellations;active.before(c);active.after(c);
    check(!active.last_proposal.valid&&!active.select(c,&scratch)&&active_host.cancellations==cancels&&active.reserved_key.reservation_serial,"hand tracking loss writes no placement and leaves same canonical scene available");
    active_host.tracked=true;active.before(c);check(active.select(c,&scratch),"fresh tracked recovery can use current reservation without stale pose reuse");
    SetEnvironmentVariableW(L"BOVR_WEAPON_RESERVATION_PROBE",L"0");
    check(!configure_weapon_placement_adapter_test_copy(GetModuleHandleW(nullptr),true),"passive adapter remains independently opt-in");
    printf("checks=%u failures=%u native_writes=0 shot_writes=0 real_reservations=0\n",checks,failures);return failures?1:0;
}
