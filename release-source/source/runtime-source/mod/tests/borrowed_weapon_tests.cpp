// SPDX-License-Identifier: GPL-3.0-only
#include "t5/borrowed_weapon_claim.hpp"
#include "t5/borrowed_matrix_copy.hpp"
#include "input/tracked_gun_pose.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace bovr;
namespace a=t5::animation_detail;
namespace {
unsigned checks{},failures{};
void check(bool v,const char* s){++checks;if(!v){++failures;std::printf("FAIL %s\n",s);}}
t5::BorrowedWeaponClaim claim(){const auto row=synthetic_profile_fixture::row(2);const auto& shape=*t5::borrowed_profile::shape(row);
    t5::BorrowedWeaponClaim c{};c.wrapper_token=7;c.camera_serial=13;c.camera_published_ms=1000;c.profile_revision=shape.revision;
    c.key.profile_row=row;c.key.profile_revision=shape.revision;c.key.weapon=shape.weapon;c.key.gun_bone=shape.gun;
    c.key.flash_bone=shape.flash;c.key.bone_count=shape.total;c.key.command_epoch=2;c.key.command_sequence=55;c.key.thread=3;return c;}
struct Host {
    unsigned locks{},unlocks{},states{},copies{},bad_state{},bad_copy{},change_raw{},change_pose{},change_clock{},raise_at{};
    a::LockedCopy expected{};
    static void lock(void* p,std::uintptr_t){auto& h=*static_cast<Host*>(p);if(h.raise_at==1)throw std::runtime_error("native lock");++h.locks;}
    static void unlock(void* p,std::uintptr_t){auto& h=*static_cast<Host*>(p);if(h.raise_at==4)throw std::runtime_error("native unlock");++h.unlocks;}
    static bool state(void* p,const a::LockedRequest&,a::CacheCopy* c){auto& h=*static_cast<Host*>(p);++h.states;
        if(h.raise_at==2)throw std::runtime_error("provider/native fault");
        if(h.bad_state==h.states)return false;
        *c={0x20000,9,9,h.expected.pose,true,true,{}};
        if(h.change_pose==h.states)++c->pose.origin.x;if(h.change_clock==h.states)++c->clock;return true;}
    static bool copy(void* p,const a::LockedRequest&,std::uintptr_t,a::NativeMat32* g,a::NativeMat32* f){auto& h=*static_cast<Host*>(p);++h.copies;
        if(h.raise_at==3)throw std::runtime_error("native copy fault");if(h.bad_copy==h.copies)return false;
        *g=h.expected.gun;*f=h.expected.flash;if(h.change_raw==h.copies)++g->origin.x;return true;}
    a::LockedHost host(){return {this,lock,unlock,state,nullptr,nullptr,nullptr,nullptr,copy};}
};
}
int main(){
    {
        auto paired=claim();t5::NativeWeaponContext context{};
        context.valid=context.normal_context=context.server_normal=context.body_valid=context.primary_viewmodel=true;
        context.weapon_index=paired.key.weapon;context.native_thread=paired.key.thread;
        check(t5::borrowed_claim_matches_context(paired,context),"ordinary borrowed context remains accepted");
        auto linked=context;linked.normal_context=linked.server_normal=false;
        linked.linked_weapon_context=linked.linked_server_context=true;linked.server_link_identity=0x23450000;
        check(t5::borrowed_claim_matches_context(paired,linked),"verified linked primary claim preserves the same native weapon identity");
        input::profile_admission::ObservedIdentity id{};id.tracking_mode=2;id.server_link=linked.server_link_identity;
        check(t5::borrowed_admission_matches_context(id,linked),"linked command admission matches the copied render link and mode");
        id.server_link+=4;check(!t5::borrowed_admission_matches_context(id,linked),"another linked parent cannot reuse command admission");
        id.server_link=linked.server_link_identity;id.tracking_mode=1;
        check(!t5::borrowed_admission_matches_context(id,linked),"ordinary command admission cannot authorize a linked render");
        context.dual_wield=true;
        check(!t5::borrowed_claim_matches_context(paired,context),"dual scene cannot enter legacy unadmitted path");
        paired.command_admission={10,0};
        check(t5::borrowed_claim_matches_context(paired,context),"dual first-resume claim accepts the ledger's valid initial revision zero");
        paired.command_admission={0,2};
        check(!t5::borrowed_claim_matches_context(paired,context),"dual scene rejects a missing admission serial even at a later revision");
        paired.command_admission={10,UINT64_MAX};
        check(!t5::borrowed_claim_matches_context(paired,context),"dual scene rejects an exhausted admission revision");
        paired.command_admission={10,2};
        check(t5::borrowed_claim_matches_context(paired,context),"dual copied context permits separately ledger-validated exact claim");
        ++context.weapon_index;
        check(!t5::borrowed_claim_matches_context(paired,context),"dual token cannot cover a changed weapon context");
    }
    for(unsigned variant=0;variant<3;++variant){const auto row=synthetic_profile_fixture::row(variant);const auto& shape=*t5::borrowed_profile::shape(row);auto c=claim();
        c.profile_revision=shape.revision;c.key.profile_row=row;c.key.profile_revision=shape.revision;c.key.gun_bone=shape.gun;c.key.flash_bone=shape.flash;c.key.bone_count=shape.total;
        check(t5::valid_borrowed_claim(c,1050),"exact row-specific copied shape admits bounded claim, still needs current payload");
        const t5::borrowed_profile::Selection selected{row,shape.gun,shape.flash,shape.total};
        check(t5::borrowed_profile::same_selection(selected,selected),"matching known copied profile key permits current-cache proof");
        for(unsigned v=0;v<5;++v){auto bad=c;switch(v){case 0:++bad.key.gun_bone;break;case 1:++bad.key.flash_bone;break;
            case 2:++bad.key.bone_count;break;case 3:bad.key.profile_row=0;break;case 4:bad.key.profile_row=UINT32_MAX;break;}
            check(!t5::valid_borrowed_claim(bad,1050),"inconsistent or unknown row shape cannot reserve native scene");}
        a::ClearValue source{};source.valid=true;source.serial=c.camera_serial;source.key=c.key;source.published_ms=1000;
        for(unsigned other=0;other<3;++other)if(other!=variant){t5::BorrowedCameraClaim gate;gate.publish(source);auto foreign=c.key;foreign.profile_row=synthetic_profile_fixture::row(other);
            t5::BorrowedWeaponClaim out{};a::ClearValue copy{};
            check(!gate.take(foreign,c.camera_serial,c.wrapper_token,1050,&out,&copy),"one-use camera cannot switch exact profile rows even with identical counts");}
        for(unsigned other=0;other<3;++other)if(other!=variant){const auto other_row=synthetic_profile_fixture::row(other);const auto& s=*t5::borrowed_profile::shape(other_row);
            a::LockedCopy before{},after{};before.gun.trans_weight=after.gun.trans_weight=2;
            check(a::same_raw(before,after),"regression matrices are intentionally identical across rows");
            check(!t5::borrowed_profile::same_selection({other_row,s.gun,s.flash,s.total},selected),"fresh runtime other-row payload cannot recheck prior camera or normal even with identical raw matrices");}
    }
    auto c=claim();a::ClearValue camera{};camera.valid=true;camera.serial=c.camera_serial;camera.published_ms=1000;camera.key=c.key;
    t5::BorrowedCameraClaim gate;gate.publish(camera);t5::BorrowedWeaponClaim claimed{};a::ClearValue copied{};
    check(gate.take(c.key,13,7,1100,&claimed,&copied)&&claimed==c,"camera binds exactly once to real wrapper token at age100");
    check(!gate.take(c.key,13,7,1100,&claimed,&copied),"same invocation cannot claim camera twice");
    for(unsigned variant=0;variant<11;++variant){auto key=c.key;std::uint64_t serial=13,token=7,now=1001;
        switch(variant){case 0:++key.command_sequence;break;case 1:++key.command_epoch;break;case 2:++key.thread;break;
        case 3:++key.opaque_identity[2];break;case 4:++key.command_number;break;case 5:++key.skeleton_clock;break;
        case 6:serial=14;break;case 7:token=0;break;case 8:now=1101;break;case 9:now=999;break;case 10:key.profile_row=0;break;}
        gate.publish(camera);check(!gate.take(key,serial,token,now,&claimed,&copied),"changed native claim or age fails closed");
        check(!gate.available(13),"failed claim cannot retry a different context");}
    gate.publish(camera);gate.clear_entry(14);check(!gate.available(13),"intervening clear retires unclaimed camera");
    t5::BorrowedSceneRecord completed{true,true,c,14};
    check(t5::borrowed_scene_completed(completed,1050,14,7,2,55,true),"sealed exact normal clear joins later scene");
    for(unsigned v=0;v<9;++v){auto r=completed;auto now=1050ULL,serial=14ULL,token=7ULL,epoch=2ULL,sequence=55ULL;bool idle=true;
        switch(v){case 0:r.active=false;break;case 1:r.completed=false;break;case 2:++serial;break;case 3:++token;break;
        case 4:++epoch;break;case 5:++sequence;break;case 6:idle=false;break;case 7:now=1101;break;case 8:++r.normal_serial;break;}
        check(!t5::borrowed_scene_completed(r,now,serial,token,epoch,sequence,idle),"unsealed/intervening/expired scene is rejected");}
    for(unsigned variant=0;variant<3;++variant)for(unsigned v=0;v<15;++v){const auto& shape=*t5::borrowed_profile::shape(synthetic_profile_fixture::row(variant));Host h;h.expected.clock=9;h.expected.gun.quaternion[3]=h.expected.flash.quaternion[3]=1;
        h.expected.gun.trans_weight=h.expected.flash.trans_weight=2;
        switch(v){case 1:case 2:h.bad_state=v;break;case 3:case 4:h.bad_copy=v-2;break;case 5:case 6:h.change_raw=v-4;break;
        case 7:case 8:h.change_pose=v-6;break;case 9:case 10:h.change_clock=v-8;break;
        case 11:case 12:case 13:case 14:h.raise_at=v-10;break;}
        a::LockedCopy out{};bool fault=false;a::LockedStatus s{};
        try{s=a::copy_existing_locked(h.host(),{0x10000,shape.gun,shape.flash,shape.total,9,false,false},h.expected,&out);}catch(const std::runtime_error&){fault=true;}
        if(!v)check(s==a::LockedStatus::accepted&&h.states==2&&h.copies==2&&h.locks==1&&h.unlocks==1,"existing raw matrix proof uses no native calculation and unlocks once");
        else if(v<11)check(s!=a::LockedStatus::accepted&&!fault&&h.locks==1&&h.unlocks==1,"every normal copy/identity failure unlocks exactly once");
        else check(fault&&h.unlocks==0&&h.locks==(v==11?0U:1U),"native failure propagates without invented unlock");
    }
    for(unsigned variant=0;variant<3;++variant)for(unsigned field=0;field<3;++field){const auto& shape=*t5::borrowed_profile::shape(synthetic_profile_fixture::row(variant));Host h;a::LockedCopy out{};
        a::LockedRequest request{0x10000,shape.gun,shape.flash,shape.total,9,false,false};
        if(field==0)++request.gun_bone;else if(field==1)++request.flash_bone;else ++request.bone_count;
        check(a::copy_existing_locked(h.host(),request,h.expected,&out)==a::LockedStatus::invalid&&!h.locks&&!h.unlocks&&!h.states&&!h.copies,"unknown or mixed active matrix shape rejects before any native lock or copy");}
    input::ReservedWeaponFrame r{};r.identity={2,3,4,5};r.anchor_generation=2;r.published_ms=1000;r.exclusively_reserved_for_scene=true;
    auto& f=r.prediction;f.frame_id=3;f.should_render=f.views_valid=true;f.predicted_display_time=200;f.predicted_display_period=10;f.actions.sequence=9;f.actions.focused=true;
    auto& hand=f.actions.hands[static_cast<unsigned>(input::xr::Hand::Right)];
    hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;hand.aim.active=hand.aim.orientation_valid=hand.aim.orientation_tracked=true;
    hand.grip.pose.position={.2F,-.1F,-.3F};
    input::WeaponBodySample body{r.identity,{{10,20,30},{{1,0,0},{0,1,0},{0,0,1}}},input::BodyCameraSource::completed_native_refdef};
    a::LocalGeometry geometry{};geometry.gun_in_dobj={{8,2,-3},{{1,0,0},{0,1,0},{0,0,1}}};
    geometry.flash_in_gun={{5.4F,.07F,1.17F},{{1,0,0},{0,1,0},{0,0,1}}};
    input::BorrowedPlacementValues out{};
    check(input::propose_borrowed_placement(r,body,geometry,1050,&out)==input::BorrowedPlacementStatus::proposed&&out.valid,"canonical ticket yields value-only authored gun placement");
    const auto straight=out;
    constexpr float cos_angle=.999792212F,sin_angle=.020385497F;
    geometry.flash_in_gun.axis={{cos_angle,0,sin_angle},{0,1,0},{-sin_angle,0,cos_angle}};
    check(input::propose_borrowed_placement(r,body,geometry,1050,&out)==input::BorrowedPlacementStatus::proposed&&
        a::axis_error(out.barrel.axis,straight.barrel.axis)<.00001F&&a::axis_error(out.gun.axis,straight.gun.axis)>.02F,
        "fixed angled muzzle is solved out of j_gun while visible barrel keeps controller aim");
    check(a::axis_error(a::compose(out.gun,geometry.flash_in_gun).axis,out.barrel.axis)<.00001F,
        "angled muzzle placement remains the exact native j_gun to tag_flash composition");
    geometry.flash_in_gun.axis={{1,0,0},{0,1,0},{0,0,1}};
    const auto initial=out;f.head_center.position.x=.1F;
    check(input::propose_borrowed_placement(r,body,geometry,1050,&out)==input::BorrowedPlacementStatus::proposed&&
        a::distance(initial.gun.origin,out.gun.origin)==0,"head motion alone does not attach hand to head");
    for(unsigned i=0;i<180;++i){const float t=float(i)*.035F;hand.aim.pose.orientation={0,std::sin(t/2),0,std::cos(t/2)};
        geometry.gun_in_dobj.axis={{std::cos(t),std::sin(t),0},{-std::sin(t),std::cos(t),0},{0,0,1}};
        check(input::propose_borrowed_placement(r,body,geometry,1050,&out)==input::BorrowedPlacementStatus::proposed,"finite rotated animation and tracked aim compose");
        const auto recomposed=a::compose(out.dobj,geometry.gun_in_dobj);
        check(a::distance(recomposed.origin,out.gun.origin)<.002F&&a::axis_error(recomposed.axis,out.gun.axis)<.002F,"proposal cancels full current animated gun transform");
        input::BorrowedPlacementValues kicked{},settled{};
        check(input::propose_borrowed_placement(r,body,geometry,1050,&kicked,false,.018f)==input::BorrowedPlacementStatus::proposed,"shot kick uses the same admitted placement");
        const auto delta=a::sub(kicked.gun.origin,out.gun.origin);
        check(std::fabs(a::dot(delta,out.gun.axis.forward)+.018f*input::xr::kIwUnitsPerMeter)<.002f&&
            a::distance(delta,a::sub(kicked.barrel.origin,out.barrel.origin))<.002f&&
            a::axis_error(kicked.gun.axis,out.gun.axis)==0.f&&a::axis_error(kicked.barrel.axis,out.barrel.axis)==0.f&&
            !std::memcmp(&kicked.body,&out.body,sizeof(out.body)),"kick translates gun and barrel backward together without aiming or camera rotation");
        const auto kicked_native=a::compose(kicked.dobj,geometry.gun_in_dobj);
        check(a::distance(kicked_native.origin,kicked.gun.origin)<.002f,"native animated assembly reaches kicked gun position");
        check(input::propose_borrowed_placement(r,body,geometry,1050,&settled,false,0.f)==input::BorrowedPlacementStatus::proposed&&
            a::distance(settled.gun.origin,out.gun.origin)==0.f,"decayed kick does not accumulate position drift");}
    hand.aim.pose.orientation={};
    for(float bad_kick:{-.001f,.031f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        check(input::propose_borrowed_placement(r,body,geometry,1050,&out,false,bad_kick)==input::BorrowedPlacementStatus::geometry&&!out.valid,"invalid kick rejects before native pose selection");
    for(unsigned v=0;v<11;++v){auto bad=r;auto b=body;auto g=geometry;auto now=1050ULL;
        switch(v){case 0:++bad.identity.generation;break;case 1:bad.exclusively_reserved_for_scene=false;break;case 2:now=1101;break;
        case 3:bad.prediction.views_valid=false;break;case 4:bad.prediction.actions.focused=false;break;
        case 5:bad.prediction.actions.hands[1].grip.position_tracked=false;break;case 6:bad.prediction.actions.hands[1].aim.orientation_tracked=false;break;
        case 7:b.source=input::BodyCameraSource::unknown;break;case 8:g.gun_in_dobj.axis.forward={};break;case 9:bad.tracking_anchor.orientation={0,0,0,2};break;case 10:now=999;break;}
        check(input::propose_borrowed_placement(bad,b,g,now,&out)!=input::BorrowedPlacementStatus::proposed&&!out.valid,"invalid prediction/tracking/body/geometry rejects only owned proposal");}
    check(t5::borrowed_scene_completed(completed,1050,14,7,2,55,true),"hand loss does not poison completed native head scene");
    std::printf("borrowed_weapon_value_checks=%u failures=%u native_calls=0 game_writes=0\n",checks,failures);return failures?1:0;
}
