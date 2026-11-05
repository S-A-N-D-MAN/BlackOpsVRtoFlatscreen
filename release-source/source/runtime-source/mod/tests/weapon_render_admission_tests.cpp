// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_render_admission.hpp"
#include "t5/rebirth_native_prop_context.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
using namespace bovr;
void run_native_melee_scene_tests();
namespace {
unsigned checks{};
void check(bool pass,const char* why){++checks;if(!pass){std::printf("FAIL %s\n",why);std::exit(1);}}
struct HandsMemory {
    std::map<std::uintptr_t,std::vector<unsigned char>> parts;
    static constexpr std::uintptr_t cg=0x200000,server=0x300000,variant=0x400000,definition=0x500000,name=0x600000;
    unsigned root_reads{},name_reads{},variant_reads{},drift{};
    template<class T>void put(std::uintptr_t at,const T& value){
        auto& bytes=parts[at];bytes.resize(sizeof value);std::memcpy(bytes.data(),&value,sizeof value);
    }
    HandsMemory(){
        put(0x2ff5354,std::uint32_t(cg));put(0x1a796f8+0x13c,std::uint32_t(server));put(0x1c03140,std::uint32_t(server));
        put(cg+0x8a364+0x130,static_cast<unsigned char>(0));put(server+0x130,static_cast<unsigned char>(0));
        put(cg+0x8a364+0x144,static_cast<unsigned char>(46));put(server+0x144,static_cast<unsigned char>(46));
        put(0xbe19a8+4*46,std::uint32_t(variant));put(variant+8,std::uint32_t(definition));put(variant,std::uint32_t(name));
        put(name,"rebirth_hands_sp");put(definition+0x1c,0);put(definition+0x20,0);put(variant+0x20,0);
        put(definition+0x56a,static_cast<unsigned char>(0));
    }
    static bool copy(void* context,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<HandsMemory*>(context);auto found=m.parts.upper_bound(at);
        if(found==m.parts.begin())return false;--found;
        const auto offset=at-found->first;
        if(offset>found->second.size()||size>found->second.size()-offset)return false;
        std::memcpy(out,found->second.data()+offset,size);
        if(at==0x2ff5354&&++m.root_reads==2&&m.drift==1)*static_cast<std::uint32_t*>(out)+=4;
        if(at==name&&++m.name_reads==2&&m.drift==2)static_cast<char*>(out)[0]='x';
        if(at==0xbe19a8+4*46&&++m.variant_reads==2&&m.drift==3)*static_cast<std::uint32_t*>(out)+=4;
        return true;
    }
    t5::rebirth_prop::Current current() const {return {46,cg,server,variant,definition};}
    bool read(){return t5::rebirth_prop::read_current(copy,this,current());}
};
void native_hands_identity(){
    HandsMemory m;check(m.read(),"exact current Rebirth empty hands pairs both native players and weapon descriptor");
    m=HandsMemory{};m.put(HandsMemory::name,"rebirth_hatchet_sp");
    check(m.read(),"independently verified exact Rebirth scripted hatchet retains its native visual");
    for(const char* name:{"rebirth_hands","rebirth_hands_sp_alt","rebirth_hatchet","rebirth_hatchet_sp_alt","hatchet_rebirth_sp","ak47_sp","hands_sp"}){
        m=HandsMemory{};std::array<char,32> text{};std::memcpy(text.data(),name,std::strlen(name));m.put(HandsMemory::name,text);
        check(!m.read(),"near-name hands or hatchet, separate throwable and normal firearm cannot gain native prop admission");
    }
    for(unsigned fault=0;fault<16;++fault){
        m=HandsMemory{};auto c=m.current();
        switch(fault){
        case 0:c.weapon=0;break;case 1:c.weapon=256;break;
        case 2:c.cg+=4;break;case 3:c.server+=4;break;case 4:c.variant+=4;break;case 5:c.definition+=4;break;
        case 6:m.put(0x1c03140,std::uint32_t(HandsMemory::server+4));break;
        case 7:m.put(HandsMemory::cg+0x8a364+0x130,static_cast<unsigned char>(1));break;
        case 8:m.put(HandsMemory::server+0x130,static_cast<unsigned char>(1));break;
        case 9:m.put(HandsMemory::cg+0x8a364+0x144,static_cast<unsigned char>(41));break;
        case 10:m.put(HandsMemory::server+0x144,static_cast<unsigned char>(41));break;
        case 11:m.put(HandsMemory::definition+0x1c,2);break;case 12:m.put(HandsMemory::definition+0x20,1);break;
        case 13:m.put(HandsMemory::variant+0x20,1);break;
        case 14:m.put(HandsMemory::definition+0x56a,static_cast<unsigned char>(1));break;
        case 15:m.put(HandsMemory::variant,std::uint32_t(0xfffffff0));break;
        }
        check(!t5::rebirth_prop::read_current(HandsMemory::copy,&m,c),"changed owner, selected weapon or exact descriptor rejects native hands admission");
    }
    for(unsigned drift=1;drift<=3;++drift){m=HandsMemory{};m.drift=drift;
        check(!m.read(),"owner, asset name or table identity changing between independent reads rejects native hands admission");}
    for(unsigned drift=1;drift<=3;++drift){m=HandsMemory{};m.put(HandsMemory::name,"rebirth_hatchet_sp");m.drift=drift;
        check(!m.read(),"native hatchet retains the same owner, name and table recheck requirements");}
    m=HandsMemory{};m.put(HandsMemory::name,"rebirth_hatchet_sp");m.put(HandsMemory::variant+0x20,1);
    check(!m.read(),"ammunition-bearing item cannot gain scripted hatchet exemption");
    m=HandsMemory{};m.parts.erase(HandsMemory::definition+0x20);check(!m.read(),"unreadable native descriptor rejects");
    check(!t5::rebirth_prop::read_current(nullptr,nullptr,m.current()),"missing protected reader rejects");
    m=HandsMemory{};auto alternate=m.current();alternate.weapon=47;
    m.put(0xbe19a8+4*47,std::uint32_t(HandsMemory::variant));
    m.put(HandsMemory::cg+0x8a364+0x144,static_cast<unsigned char>(47));m.put(HandsMemory::server+0x144,static_cast<unsigned char>(47));
    check(t5::rebirth_prop::read_current(HandsMemory::copy,&m,alternate),"exact paired asset is not tied to a map load's weapon ordinal");
}
std::uint64_t publication_now=1000;
std::uint64_t clock_value() noexcept{return publication_now;}
struct Transport final:vr::FrameTransport {
    unsigned ends{},submits{},begins{};
    bool initialize(const vr::xr::RuntimeConfig&,const vr::xr::HostCallbacks&) override{return true;}
    vr::BeginStatus begin(vr::xr::FrameState* f) override{*f={};f->frame_id=++begins;f->views_valid=f->should_render=true;
        f->head_center.position={static_cast<float>(begins),2,3};f->actions.sequence=100+begins;return vr::BeginStatus::started;}
    vr::TransportResult discard(const vr::xr::FrameState&) override{++ends;return {vr::TransportStatus::zero_layer,0,0,true};}
    vr::TransportResult submit(IDirect3DDevice9*,const vr::xr::FrameState&,const vr::xr::StereoSourceLayout&) override{++ends;++submits;return {vr::TransportStatus::projection,1,3,true};}
    bool before_reset(bool) override{return true;}
    bool after_reset(bool,bool ok) override{return ok;}
    bool shutdown() override{return true;}
};
t5::NativeWeaponContext producer() {
    t5::NativeWeaponContext c{};
    c.valid=c.body_valid=c.normal_context=c.server_normal=c.primary_viewmodel=c.weapon_definition_valid=true;
    c.native_thread=123;c.call_serial=42;c.draw_gun=1;c.weapon_index=9;
    c.cg_identity=0x100000;c.refdef_identity=0x18c100;c.viewmodel_identity=0x200000;c.dobj_identity=0x300000;
    c.variant_identity=0x400000;c.definition_identity=0x500000;c.model_generation=3;c.model_lifetime_verified=true;
    c.native_time=c.refdef_time=4321;c.native_frame_time=16;
    c.stock_view.width=1280;c.stock_view.height=720;c.stock_view.origin={10,20,30};return c;
}
void unowned_rejection_lifecycle() {
    using Result=t5::WeaponRenderRejectionAction;
    using Status=t5::WeaponRenderAdmission;
    using Ownership=t5::WeaponRenderRejectionOwnership;
    publication_now=1000;
    Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);
    check(coordinator.initialize({}),"fresh-unowned race fixture initializes");
    const auto first=coordinator.begin_frame().ticket;vr::FrameTicket claimed{};
    check(coordinator.claim_render_frame(&claimed),"earlier tracked scene claims frame A");
    const vr::BackendFrameIdentity backend_a{0x2000,40};
    check(coordinator.stage_render_frame(first.key,backend_a,{})&&coordinator.mark_eye_complete(first.key,backend_a,0),
        "frame A has the same one-eye-complete state as the observed Zombies refusal");
    vr::SceneReservation unavailable{};vr::SceneReservationDiagnostic refusal{};
    check(!coordinator.reserve_scene_frame({0x1000,31,41},&unavailable,&refusal)&&refusal.claimed&&refusal.staged&&refusal.eye_mask==1,
        "next native weapon fails to reserve the still-claimed older frame A");
    check(coordinator.mark_eye_complete(first.key,backend_a,1)&&coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted,
        "XR owner completes and submits the older admitted pair");
    publication_now=1016;const auto next=coordinator.begin_frame().ticket;
    check(next.key!=first.key&&next.published_ms==1016&&next.frame.head_center.position.x==2&&next.frame.actions.sequence==102,
        "XR owner publishes distinct prediction B before failed native RenderScene arrives");
    const std::array<t5::WeaponRenderProductionIdentity,1> old_production{{{true,first.key,backend_a,30}}};
    const auto ownership=t5::classify_weapon_render_rejection(next.key,0x1000,31,old_production);
    check(ownership==Ownership::reject_unbound,"no earlier production owns the newly pending XR key");
    const auto p=producer();const t5::NativeSceneSample scene{p.native_thread,p.cg_identity,p.refdef_identity,p.native_time,p.native_frame_time,p.refdef_time,p.stock_view,true};
    const t5::WeaponRenderWitness missed{p,true,true,false,false};
    const auto status=t5::assess_weapon_render(missed,scene,{},16);
    check(status==Status::no_transaction&&!t5::weapon_render_admitted(status),"failed native stock scene remains rejected before any stereo claim");
    check(t5::resolve_weapon_render_rejection(coordinator,status,ownership,next.key,publication_now,100)==Result::preserve_unowned,
        "old reservation failure preserves the exact new unowned prediction instead of poisoning it");
    vr::FrameTicket retained{};
    check(coordinator.read_pending(&retained)&&retained.key==next.key&&retained.published_ms==next.published_ms&&
        retained.frame.head_center.position.x==next.frame.head_center.position.x&&retained.frame.actions.sequence==next.frame.actions.sequence,
        "preservation cannot refresh timestamp or replace predicted head/action samples");
    vr::FrameTicket stock{};
    check(!coordinator.claim_render_frame(&stock),"preserved prediction blocks generic stock-scene and menu claims");
    check(!coordinator.stage_render_frame(next.key,{0x1000,41},{})&&!coordinator.mark_eye_complete(next.key,{0x1000,41},0),
        "rejected native scene cannot invent a matching staged eye through preservation");
    check(!coordinator.stage_menu_frame(next.key,{0x1000,41},{})&&!coordinator.mark_menu_complete(next.key,{0x1000,41}),
        "rejected native scene cannot acquire a quad fallback");
    check(transport.begins==2&&transport.ends==1&&transport.submits==1,"preserving B adds no XR begin/end/submit calls");
    publication_now=1032;
    check(t5::resolve_weapon_render_rejection(coordinator,status,ownership,next.key,publication_now,100)==Result::preserve_unowned&&
        coordinator.read_pending(&retained)&&retained.published_ms==1016,"repeated missed scene cannot renew prediction age");
    const vr::NativeBuildIdentity native_b{0x2000,32,42};vr::SceneReservation reserved{};
    check(coordinator.reserve_scene_frame(native_b,&reserved)&&reserved.ticket.key==next.key&&reserved.ticket.published_ms==1016,
        "next independently tracked native weapon reserves the preserved original prediction");
    check(!coordinator.claim_render_frame(&stock),"real reservation remains exclusive against generic fallback claims");
    check(coordinator.consume_scene_reservation(reserved,native_b,&claimed)&&claimed.key==next.key&&claimed.frame.actions.sequence==102,
        "only the exact next native reservation consumes original prediction B");
    const vr::BackendFrameIdentity backend_b{native_b.buffer_identity,native_b.build_serial};
    check(coordinator.stage_render_frame(next.key,backend_b,{})&&coordinator.mark_eye_complete(next.key,backend_b,0)&&
        coordinator.mark_eye_complete(next.key,backend_b,1),"independent tracked frame B supplies its own two exact backend eyes");
    check(coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted&&transport.begins==2&&transport.ends==2&&transport.submits==2,
        "A and B each submit once with no intervening fabricated zero-layer frame");
    check(t5::resolve_weapon_render_rejection(coordinator,status,ownership,next.key,publication_now,100)==Result::stale_key,
        "retired prediction cannot be revived by late preservation");
    check(coordinator.shutdown(),"fresh-unowned lifecycle shuts down");publication_now=1000;
}
void unowned_rejection_limits() {
    using Action=t5::WeaponRenderRejectionAction;
    using Status=t5::WeaponRenderAdmission;
    using Ownership=t5::WeaponRenderRejectionOwnership;
    for(unsigned variant=0;variant<9;++variant){
        publication_now=1000;Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);
        check(coordinator.initialize({}),"unowned rejection limit fixture initializes");
        const auto ticket=coordinator.begin_frame().ticket;
        auto status=Status::no_transaction;auto ownership=Ownership::reject_unbound;
        vr::FrameTicket claimed{};vr::SceneReservation reserved{};
        const vr::BackendFrameIdentity backend{0x1000,77};
        switch(variant){
        case 0:status=Status::original_placement;break;
        case 1:status=Status::incomplete;break;
        case 2:status=Status::unverified;break;
        case 3:ownership=Ownership::reject_current;break;
        case 4:check(coordinator.claim_render_frame(&claimed),"claimed frame can temporarily lack a produced record");break;
        case 5:check(coordinator.reserve_scene_frame({0x1000,30,77},&reserved),"active native reservation is already ownership");break;
        case 6:check(coordinator.claim_render_frame(&claimed)&&coordinator.stage_render_frame(ticket.key,backend,{})&&
            coordinator.mark_eye_complete(ticket.key,backend,0)&&coordinator.mark_eye_complete(ticket.key,backend,1),
            "current exact completed pair remains rejectable");break;
        case 7:publication_now=1101;break;
        case 8:publication_now=999;break;
        }
        check(t5::resolve_weapon_render_rejection(coordinator,status,ownership,ticket.key,publication_now,100)==Action::invalidate,
            "preservation never broadens placement failures, ownership, or original-age limits");
        vr::SceneReservation denied{};
        check(!coordinator.claim_render_frame(&claimed)&&!coordinator.reserve_scene_frame({0x2000,31,78},&denied),
            "invalidated current frame cannot revive as ordinary or tracked production");
        check(coordinator.present(nullptr).status==vr::CoordinatorStatus::discarded_incomplete&&transport.ends==1&&transport.submits==0,
            "rejected owned/stale frame retains exactly one zero-layer retirement");
        check(coordinator.shutdown(),"rejection limit fixture shuts down");
    }
    for(bool delayed_reservation:{false,true}){
        publication_now=1000;Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);coordinator.initialize({});
        const auto ticket=coordinator.begin_frame().ticket;
        check(t5::resolve_weapon_render_rejection(coordinator,Status::no_transaction,Ownership::reject_unbound,ticket.key,1100,100)==Action::preserve_unowned,
            "original 100ms prediction-age boundary remains inclusive without refresh");
        vr::FrameTicket pending{};
        check(coordinator.read_pending(&pending)&&pending.published_ms==1000&&!t5::usable_weapon_scene_publication(pending,1101,100),
            "next native producer still rejects an over-age retained prediction");
        if(delayed_reservation){vr::SceneReservation reserved{};
            check(coordinator.reserve_scene_frame({0x1000,30,77},&reserved),"late reservation exposes canonical original timestamp to unchanged adapter age check");
            check(!t5::usable_weapon_scene_publication(reserved.ticket,1101,100)&&coordinator.cancel_scene_reservation(reserved.ticket.key,reserved.serial),
                "unchanged adapter age rejection cancels the late native reservation before placement");
            check(!coordinator.consume_scene_reservation(reserved,reserved.native,&pending),"expired preserved prediction cannot be consumed later");
        }
        check(coordinator.discard_pending().status==vr::CoordinatorStatus::discarded_cancelled&&transport.ends==1&&transport.submits==0,
            "existing explicit owner timeout can retire deferred prediction without any fallback image");
        publication_now=1120;const auto fresh=coordinator.begin_frame().ticket;
        check(t5::resolve_weapon_render_rejection(coordinator,Status::no_transaction,Ownership::reject_unbound,ticket.key,publication_now,100)==Action::stale_key,
            "late failed invocation cannot preserve or poison a replacement XR key");
        check(coordinator.claim_render_frame(&pending)&&pending.key==fresh.key,
            "awaiting-tracked restriction retires with its own key and cannot leak into a later ordinary frame");
        coordinator.discard_pending();check(coordinator.shutdown(),"deferred-age fixture shuts down");
    }
    publication_now=1000;
}
}
int main() {
    native_hands_identity();
    unowned_rejection_lifecycle();
    unowned_rejection_limits();
    const auto p=producer();
    const t5::NativeSceneSample scene{p.native_thread,p.cg_identity,p.refdef_identity,p.native_time,p.native_frame_time,p.refdef_time,p.stock_view,true};
    const t5::WeaponRenderWitness good{p,true,true,true,false};
    check(t5::expects_tracked_weapon_render(p,true),"normal primary rifle expects tracked rendering before any profile/reservation attempt");
    check(!t5::expects_tracked_weapon_render(p,false),"observer-only configuration preserves native rendering");
    {
        auto hands=p;hands.rebirth_native_prop_visual=true;
        check(t5::native_rebirth_prop_visual(hands)&&!t5::expects_tracked_weapon_render(hands,true),
              "exact current empty-hands prop does not require absent firearm profile tags");
        const t5::WeaponRenderWitness native_hands{hands,true,false,false,false};
        check(t5::assess_weapon_render(native_hands,scene,{},16)==t5::WeaponRenderAdmission::ordinary,
              "exact Rebirth native prop admits ordinary stereo without a fabricated weapon transaction");
        for(unsigned fault=0;fault<4;++fault){auto other=hands;
            if(fault==0)other.rebirth_native_prop_visual=false;
            if(fault==1)other.weapon_type=2;
            if(fault==2)other.weapon_class=1;
            if(fault==3)other.dual_wield=true;
            check(!t5::native_rebirth_prop_visual(other)&&t5::expects_tracked_weapon_render(other,true),
                  "changed hands identity or firearm metadata retains fail-closed tracked rendering");
            const t5::WeaponRenderWitness missing_gun{other,true,t5::expects_tracked_weapon_render(other,true),false,false};
            check(t5::assess_weapon_render(missing_gun,scene,{},16)==t5::WeaponRenderAdmission::no_transaction,
                  "normal weapon profile failure cannot silently acquire ordinary stereo admission");
        }
        hands.normal_context=hands.server_normal=false;hands.linked_weapon_context=hands.linked_server_context=true;
        hands.server_link_identity=0x23450000;
        check(!t5::native_rebirth_prop_visual(hands)&&t5::expects_tracked_weapon_render(hands,true),
              "ordinary native prop witness cannot grant a linked weapon exemption");
    }
    {
        auto marker=p;marker.weapon_type=2;marker.weapon_class=4;
        check(t5::expects_tracked_weapon_render(marker,true),"ordinary launcher class still requires tracked weapon rendering");
        marker.support_marker_native_visual=true;
        check(t5::native_support_marker_visual(marker)&&!t5::expects_tracked_weapon_render(marker,true),
              "exact descriptor-verified mission radio does not require missing firearm profile tags");
        const t5::WeaponRenderWitness native_radio{marker,true,false,false,false};
        check(t5::assess_weapon_render(native_radio,scene,{},16)==t5::WeaponRenderAdmission::ordinary,
              "native radio permits ordinary world stereo without a borrowed gun transaction");
        for(unsigned mismatch=0;mismatch<4;++mismatch){
            auto other=marker;
            if(mismatch==0)other.support_marker_native_visual=false;
            if(mismatch==1)other.weapon_type=0;
            if(mismatch==2)other.weapon_class=0;
            if(mismatch==3)other.weapon_type=4;
            check(!t5::native_support_marker_visual(other)&&t5::expects_tracked_weapon_render(other,true),
                  "missing exact marker identity or changed weapon metadata retains tracked render requirement");
        }
    }
    auto linked=p;linked.normal_context=linked.server_normal=false;
    linked.linked_weapon_context=linked.linked_server_context=true;linked.server_link_identity=0x23450000;
    check(t5::expects_tracked_weapon_render(linked,true),"verified linked primary weapon requires tracked rendering without ordinary gameplay admission");
    for(unsigned fault=0;fault<6;++fault) {
        auto bad=linked;
        switch(fault){case 0:bad.linked_weapon_context=false;break;case 1:bad.linked_server_context=false;break;
        case 2:bad.server_link_identity=0;break;case 3:bad.server_link_identity=1;break;
        case 4:bad.normal_context=true;break;case 5:bad.weapon_state=21;break;}
        check(!t5::expects_tracked_weapon_render(bad,true),"linked mode retains paired identity and offhand exclusions");
    }
    for(const bool other:{false,true})for(int state=-1;state<53;++state) {
        auto animation_state=p;(other?animation_state.other_weapon_state:animation_state.weapon_state)=state;
        const bool accepted_state=(state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
        check(t5::expects_tracked_weapon_render(animation_state,true)==accepted_state,
              "render witness expects primary tracking during pinned sprint/continuous/low-ready states and preserves redirected/unknown states");
    }
    for(int state=21;state<=26;++state) {
        auto grenade_visual=p;grenade_visual.weapon_state=state;grenade_visual.manual_grenade_primary_visual=true;
        check(!t5::expects_tracked_weapon_render(grenade_visual,true),
              "exact manual grenade primary visual keeps each native offhand transition on the ordinary stereo route");
        const t5::WeaponRenderWitness ordinary_grenade{grenade_visual,true,false,false,false};
        check(t5::assess_weapon_render(ordinary_grenade,scene,{},16)==t5::WeaponRenderAdmission::ordinary,
              "manual grenade primary visual does not require an unavailable offhand command transaction");
        grenade_visual.manual_grenade_primary_visual=false;
        check(!t5::expects_tracked_weapon_render(grenade_visual,true),
              "offhand transition state alone cannot claim tracked primary rendering");
        auto secondary_transition=p;secondary_transition.other_weapon_state=state;secondary_transition.manual_grenade_primary_visual=true;
        check(!t5::expects_tracked_weapon_render(secondary_transition,true),
              "manual grenade primary visual never exempts the secondary weapon state");
    }
    for(int count:{0,1,2,4,7,8,-1,(-2147483647-1)}) {auto stacked=p;stacked.stack_count=count;
        check(t5::expects_tracked_weapon_render(stacked,true)==(count>=0&&count<=7),"valid stacked-fire counts keep handheld tracking required while out-of-range values are excluded");}
    for(unsigned variant=0;variant<21;++variant) {
        auto excluded=p;
        switch(variant) {
        case 0:excluded.normal_context=false;break;case 1:excluded.server_normal=false;break;
        case 2:excluded.primary_viewmodel=false;break;case 3:excluded.draw_gun=0;break;
        case 4:excluded.stack_count=8;break;case 5:excluded.weapon_state=18;break;
        case 6:excluded.weapon_state=21;break;case 7:excluded.other_weapon_state=18;break;
        case 8:excluded.weapon_type=1;break;case 9:excluded.weapon_type=3;break;
        case 10:excluded.weapon_class=9;break;case 11:excluded.weapon_definition_valid=false;break;
        case 12:excluded.weapon_index=0;break;case 13:excluded.local_client=1;break;
        case 14:excluded.body_valid=false;break;case 15:excluded.valid=false;break;
        case 16:excluded.call_serial=0;break;case 17:excluded.weapon_state=-1;break;
        case 18:excluded.native_time++;break;case 19:excluded.dobj_identity=0;break;
        case 20:excluded.refdef_identity++;break;
        }
        check(!t5::expects_tracked_weapon_render(excluded,true),"native non-handheld/scripted/transition scope is not made a tracked requirement");
        const t5::WeaponRenderWitness native{excluded,true,false,false,false};
        check(t5::assess_weapon_render(native,scene,{},16)==t5::WeaponRenderAdmission::ordinary,"excluded scope still admits its ordinary scene route");
    }
    for(int type:{0,2,4}){auto supported=p;supported.weapon_type=type;supported.weapon_class=type==4?9:0;
        check(t5::expects_tracked_weapon_render(supported,true),"hitscan/projectile/flamethrower handhelds all require tracked render");}
    auto dual=p;dual.dual_wield=true;check(t5::expects_tracked_weapon_render(dual,true),"paired hitscan duals require same current tracked render");
    dual.weapon_type=2;check(!t5::expects_tracked_weapon_render(dual,true),"non-pistol projectile dual remains excluded");
    dual.weapon_class=4;check(t5::expects_tracked_weapon_render(dual,true),"native projectile pistol dual requires exact tracked render");
    for(int weapon_class:{0,2,3,5,9}){dual.weapon_class=weapon_class;
        check(!t5::expects_tracked_weapon_render(dual,true),"projectile dual eligibility cannot expand to other native classes");}
    dual.weapon_type=0;for(int weapon_class:{2,3,4}){dual.weapon_class=weapon_class;
        check(t5::expects_tracked_weapon_render(dual,true),"existing hitscan dual tracking remains unchanged for SMGs shotguns and pistols");}

    for(unsigned reason=0;reason<9;++reason) {
        Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);coordinator.initialize({});coordinator.begin_frame();
        auto witness=good;t5::WeaponSceneTransaction transaction{};transaction.producer=p;
        transaction.active=transaction.borrowed.active=transaction.borrowed.selected=transaction.borrowed.completed=transaction.borrowed.world_verified=true;
        auto actual_scene=scene;
        switch(reason) {
        case 0:transaction={};break; // Publication arrived only after native stock placement.
        case 1:witness.substituted=false;break; // Accepted head/eye ticket, failed hand tracking.
        case 2:transaction.borrowed.selected=false;break;
        case 3:transaction.borrowed.completed=false;break;
        case 4:transaction.borrowed.world_verified=false;break;
        case 5:++transaction.producer.call_serial;break; // Old verified gun cannot authorize new stock draw.
        case 6:++transaction.producer.dobj_identity;break;
        case 7:witness.nested=true;break;
        case 8:actual_scene.stock_view.origin.x+=1;break;
        }
        const auto admitted=t5::assess_weapon_render(witness,actual_scene,transaction,16);
        check(!t5::weapon_render_admitted(admitted),"expected tracked frame rejects original/missing/stale/unverified placement before claiming eyes");
        // Mirror the frontend integration: the original native scene can run,
        // but rejection never claims/stages an ordinary XR pair or quad.
        check(coordinator.present(nullptr).status==vr::CoordinatorStatus::discarded_incomplete&&transport.ends==1&&transport.submits==0,
              "rejected native frame ends with zero layers and no stock-gun pair publication");
        coordinator.shutdown();
    }
    t5::WeaponSceneTransaction verified{};verified.producer=p;
    verified.active=verified.borrowed.active=verified.borrowed.selected=verified.borrowed.completed=verified.borrowed.world_verified=true;
    check(t5::assess_weapon_render(good,scene,verified,16)==t5::WeaponRenderAdmission::tracked,"same-call verified native tracked model reaches the existing reservation consumer");
    auto knife=p;knife.primary_knife_variant=true;
    check(t5::suppress_untracked_primary_knife_for_world(knife,true),
        "Vorkuta fallback selects only a currently tracked primary-knife render invocation");
    auto rifle=p;
    check(!t5::suppress_untracked_primary_knife_for_world(rifle,true)&&
        !t5::suppress_untracked_primary_knife_for_world(knife,false),
        "non-knife or disabled writer cannot suppress a stock weapon call");
    const t5::WeaponRenderWitness hidden_knife{knife,true,true,false,false,true,true};
    t5::WeaponSceneTransaction hidden_transaction{};hidden_transaction.active=true;
    hidden_transaction.producer=knife;hidden_transaction.hidden_primary_weapon=true;
    check(t5::assess_weapon_render(hidden_knife,scene,hidden_transaction,16)==t5::WeaponRenderAdmission::hidden_primary_weapon&&
        t5::weapon_render_admitted(t5::WeaponRenderAdmission::hidden_primary_weapon),
        "native-suppressed primary knife admits world-only stereo only with its exact hidden reservation");
    check(t5::assess_weapon_render(hidden_knife,scene,{},16)==t5::WeaponRenderAdmission::no_transaction,
        "suppression without an exact hidden reservation cannot claim a headset frame");
    auto visible_knife=hidden_knife;visible_knife.hidden_primary_weapon=false;
    check(t5::assess_weapon_render(visible_knife,scene,{},16)==t5::WeaponRenderAdmission::no_transaction,
        "visible untracked knife still cannot enter either stereo eye");
    auto wrong_hidden=hidden_knife;wrong_hidden.producer.primary_knife_variant=false;
    check(t5::assess_weapon_render(wrong_hidden,scene,{},16)==t5::WeaponRenderAdmission::original_placement,
        "world fallback cannot broaden to a non-knife primary weapon");
    auto unsuppressed=hidden_knife;unsuppressed.native_call_suppressed=false;
    check(t5::assess_weapon_render(unsuppressed,scene,{},16)==t5::WeaponRenderAdmission::original_placement,
        "world fallback requires synchronous suppression of the complete stock knife render call");
    auto argument_changed=hidden_knife;argument_changed.substituted=true;
    check(t5::assess_weapon_render(argument_changed,scene,{},16)==t5::WeaponRenderAdmission::original_placement,
        "world fallback cannot be authorized by another placement argument substitution");
    check(t5::assess_weapon_render({},scene,{},16)==t5::WeaponRenderAdmission::ordinary,"no weapon call leaves menu/mounted/scripted scene routes available");
    for(unsigned variant=0;variant<4;++variant) {
        auto later=scene;
        switch(variant){case 0:++later.native_time;break;case 1:++later.cg_identity;break;case 2:++later.refdef_identity;break;case 3:++later.native_thread;break;}
        check(t5::assess_weapon_render(good,later,{},16)==t5::WeaponRenderAdmission::ordinary,"earlier or unrelated view witness never blocks another native scene");
    }
    auto invalid=scene;invalid.valid=false;
    check(t5::assess_weapon_render(good,invalid,verified,16)==t5::WeaponRenderAdmission::native_scene,"unreadable current native scene cannot publish a tracked expectation");
    check(t5::assess_weapon_render(good,scene,verified,17)==t5::WeaponRenderAdmission::native_scene,"changed current frame-time fails closed");
    using Ownership=t5::WeaponRenderRejectionOwnership;
    const vr::FrameKey pending_key{7,9};
    std::array<t5::WeaponRenderProductionIdentity,3> production{};
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::reject_unbound,
          "prediction published between weapon and scene cannot acquire an ordinary stock-gun pair");
    production[0]={true,pending_key,{0x2000,99},29};
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::preserve_other,
          "different prior native buffer/frame keeps its own pending XR production");
    production[0].stamp.buffer_identity=0x1000;
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::preserve_other,
          "same native buffer reused for a later frame cannot cancel its older pending production");
    production[0].native_frame=30;
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::reject_current,
          "failed current native frame invalidates its own already staged production");
    production[0].valid=false;
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::reject_unbound,
          "invalid earlier metadata cannot preserve a new unbound prediction");
    production[0]={true,{7,8},{0x2000,99},29};
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::reject_unbound,
          "another XR key is not ownership for the current prediction");
    production[0]={true,pending_key,{0x2000,0},29};
    check(t5::classify_weapon_render_rejection(pending_key,0x1000,30,production)==Ownership::reject_unbound,
          "unproven producer serial cannot exempt an unbound stock frame");
    for(const bool already_complete:{false,true}) {
        Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);coordinator.initialize({});
        const auto begun=coordinator.begin_frame();vr::FrameTicket old_ticket{};
        check(coordinator.claim_render_frame(&old_ticket),"earlier independently admitted tracked frame owns prediction");
        const vr::BackendFrameIdentity old_backend{0x2000,40};
        check(coordinator.stage_render_frame(old_ticket.key,old_backend,{}),"earlier tracked frame stages its native source");
        if(already_complete)check(coordinator.mark_eye_complete(old_ticket.key,old_backend,0)&&
            coordinator.mark_eye_complete(old_ticket.key,old_backend,1),"earlier eye pair may already be complete");
        vr::SceneReservation unavailable{};
        check(!coordinator.reserve_scene_frame({0x1000,31,41},&unavailable),"next native weapon cannot reserve prior prediction twice");
        const std::array<t5::WeaponRenderProductionIdentity,1> old_production{{{true,old_ticket.key,old_backend,30}}};
        const auto ownership=t5::classify_weapon_render_rejection(begun.ticket.key,0x1000,31,old_production);
        check(ownership==Ownership::preserve_other,"next stock-native scene preserves the prior in-progress or completed tracked pair");
        check(t5::resolve_weapon_render_rejection(coordinator,t5::WeaponRenderAdmission::no_transaction,ownership,old_ticket.key,publication_now,100)==t5::WeaponRenderRejectionAction::preserve_other,
              "actual frontend helper preserves independently owned older production without invalidation");
        if(!already_complete)check(coordinator.mark_eye_complete(old_ticket.key,old_backend,0)&&
            coordinator.mark_eye_complete(old_ticket.key,old_backend,1),"earlier backend can finish after the next native stock scene was refused");
        check(coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted&&transport.submits==1&&transport.ends==1,
              "frontend-ahead rejection cannot starve the prior independently tracked projection");
        coordinator.shutdown();
    }
    {
        publication_now=1000;Transport transport;vr::FrameCoordinator coordinator(transport,clock_value);
        check(coordinator.initialize({}),"hidden-world coordinator fixture initializes");
        const auto ticket=coordinator.begin_frame().ticket;
        check(t5::resolve_weapon_render_rejection(coordinator,t5::WeaponRenderAdmission::no_transaction,
            Ownership::reject_unbound,ticket.key,publication_now,100)==t5::WeaponRenderRejectionAction::preserve_unowned,
            "earlier missed placement preserves the current prediction for a later hidden-world scene");
        vr::SceneReservation reservation{};const vr::NativeBuildIdentity native{0x3000,31,81};
        vr::FrameTicket ordinary{};
        check(!coordinator.claim_render_frame(&ordinary)&&coordinator.reserve_scene_frame(native,&reservation),
            "hidden-weapon wrapper must reserve the exact native build before suppression");
        t5::WeaponSceneTransaction hidden{true,reservation,knife,{}};hidden.hidden_primary_weapon=true;
        vr::FrameTicket claimed{};
        check(t5::claim_weapon_scene_prediction(hidden,coordinator,native,scene,knife.native_frame_time,true,&claimed)==t5::WeaponSceneClaim::reserved&&
            claimed.key==ticket.key,"exact hidden-weapon native scene consumes its preserved reservation");
        const vr::BackendFrameIdentity backend{0x3000,81};
        check(coordinator.stage_render_frame(ticket.key,backend,{})&&coordinator.mark_eye_complete(ticket.key,backend,0)&&
            coordinator.mark_eye_complete(ticket.key,backend,1)&&coordinator.present(nullptr).status==vr::CoordinatorStatus::submitted,
            "hidden-weapon fallback still requires and submits two current stereo eyes");
        check(coordinator.shutdown(),"hidden-world coordinator fixture shuts down");
    }
    run_native_melee_scene_tests();
    std::printf("weapon_render_admission_checks=%u failures=0 native_writes=0\n",checks);
}
