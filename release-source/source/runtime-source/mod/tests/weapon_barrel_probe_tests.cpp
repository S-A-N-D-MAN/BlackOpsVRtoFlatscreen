// SPDX-License-Identifier: GPL-3.0-only
#include "../src/t5/weapon_barrel_probe.cpp"
#include <cstdio>
#include <stdexcept>
// Native providers are never invoked by this CPU fixture.
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext&,const BorrowedWeaponClaim&,input::ReservedWeaponFrame*,input::WeaponBodySample*) noexcept{return false;}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&) noexcept{return false;}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext&,const input::WeaponFrameIdentity&,const BorrowedWeaponClaim&,std::uint64_t,bool) noexcept{return false;}
unsigned borrowed_cancelled{};input::WeaponFrameIdentity last_cancelled{};
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity& identity) noexcept{++borrowed_cancelled;last_cancelled=identity;}

bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept {return false;}
std::uint64_t pose_clear_serial() noexcept{return 0;}
bool enable_weapon_native_clear_scope() noexcept{return false;}
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept{return {};}

bool install_pose_clear_probe(HMODULE,PoseClearCallback) noexcept{return false;}
bool copy_pose_clear_weapon_context(std::uintptr_t,int,std::uint64_t,NativeWeaponContext*) noexcept{return false;}
Validation validate_path(const std::wstring&) {return {};}
bool validate_loaded_headers(HMODULE,std::string&) {return false;}
std::wstring executable_path() {return {};}
std::string sha256(std::span<const std::uint8_t>) {return {};}
namespace profile_detail {
bool protected_profile_copy(void*,std::uintptr_t,void*,std::size_t) noexcept {return false;}
ReadStatus read_profile(const ReadHost&,const NativeWeaponContext&,DWORD,std::uint64_t,ProfileRecord*,ReadWorkCounts* counts) noexcept {if(counts)*counts={};return ReadStatus::context;}
}
}
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool pass,const char* name){++checks;if(!pass){++failures;std::printf("FAIL %s\n",name);}}
NativeWeaponContext context() {
    NativeWeaponContext c{};c.valid=c.body_valid=c.normal_context=c.server_normal=c.primary_viewmodel=c.weapon_definition_valid=true;
    c.native_thread=17;c.draw_gun=1;c.weapon_index=4;c.command_number=10;c.command_clock=3000;c.native_time=c.refdef_time=3500;
    c.cg_identity=1000;c.refdef_identity=c.cg_identity+0x8c100;c.viewmodel_identity=2000;c.dobj_identity=3000;
    c.variant_identity=4000;c.definition_identity=5000;c.placement.scale=1;c.stock_view.origin={100,200,300};return c;
}
struct Fake {
    std::uint64_t clock=1000;
    PreCommandSnapshot pre{};
    NativeBorrow initial{},final{};
    Vec tag_origin{112,215,320};Axis tag_axis{};
    unsigned pre_calls{},borrow_calls{},tag_calls{},reports{};
    bool pre_ok=true,borrow_ok=true,tag_ok=true,throw_tag{},throw_seh{},native_lock{},throw_report{},change_pre{};
    BarrelStatus status{};BarrelSample sample{};
    explicit Fake(const NativeWeaponContext& c) {
        pre.context.valid=true;pre.context.native_thread=c.native_thread;pre.context.connection_state=10;
        pre.context.command_number=c.command_number;pre.context.native_frame_time=c.command_clock;
        pre.native_cg_identity=c.cg_identity;pre.native_gameplay_fields_valid=pre.gameplay_active=true;pre.gameplay_epoch=3;
        pre.sequence=5;pre.observed_ms=clock;
        initial.cg=c.cg_identity;initial.owner=c.viewmodel_identity;initial.dobj=c.dobj_identity;
        initial.variant=c.variant_identity;initial.definition=c.definition_identity;initial.pool_index=1;
        initial.model_count=2;initial.bone_count=3;initial.model_bones={2,1};initial.bone_names={7,8,42};
        initial.tag=42;initial.tag_model=1;initial.tag_bone=2;initial.tag_local_bone=0;initial.skeleton_clock=99;
        initial.model_origin=initial.pose_origin={110,210,310};final=initial;final.skeleton_stamp=99;
    }
    static std::uint64_t now(void* p){return static_cast<Fake*>(p)->clock;}
    static bool command(void* p,PreCommandSnapshot* s){auto& f=*static_cast<Fake*>(p);*s=f.pre;if(++f.pre_calls>1&&f.change_pre)++s->sequence;return f.pre_ok;}
    static bool borrow(void* p,const NativeWeaponContext&,NativeBorrow* b){auto& f=*static_cast<Fake*>(p);*b=(f.borrow_calls++%2)?f.final:f.initial;return f.borrow_ok;}
    static bool tag(void* p,const NativeWeaponContext&,unsigned id,Axis* a,Vec* v){auto& f=*static_cast<Fake*>(p);++f.tag_calls;check(id==42,"actual copied native tag identity is forwarded");if(f.throw_tag)throw std::runtime_error("tag fixture");if(f.throw_seh){f.native_lock=true;RaiseException(0xe043ba17,0,0,nullptr);}*a=f.tag_axis;*v=f.tag_origin;return f.tag_ok;}
    static void report(void* p,BarrelStatus s,const BarrelSample& sample){auto& f=*static_cast<Fake*>(p);if(f.throw_report)throw std::runtime_error("evidence fixture");++f.reports;f.status=s;f.sample=sample;}
    BarrelHost host(){return {this,now,command,borrow,tag,report};}
};
void run(Fake& f,const NativeWeaponContext& c){BarrelObserver o;o.host=f.host();o.observe(c,17,1000);}
bool observe_native_seh(BarrelObserver* o,const NativeWeaponContext* c) {
    __try {o->observe(*c,17,1000);}
    __except(GetExceptionCode()==0xe043ba17?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
}
int main(){
    auto c=context();Fake good(c);run(good,c);
    check(good.status==BarrelStatus::accepted&&good.tag_calls==1&&good.borrow_calls==2&&good.pre_calls==2,"one tag calculation with guards on both sides accepts a copied sample");
    check(good.sample.tag_model_origin.x==2&&good.sample.tag_model_origin.y==5&&good.sample.tag_model_origin.z==10,"model local barrel translation is derived from copied world values");
    check(good.sample.model_body_origin.x==10&&good.sample.model_body_origin.y==10&&good.sample.model_body_origin.z==10,"body local model offset comes from current body sample");
    check(good.sample.gameplay_epoch==3&&good.sample.command_sequence==5&&good.sample.observation==1,"copied gameplay metadata and independent observation serial retained");
    check(good.sample.position_roundtrip_error==0&&good.sample.axis_roundtrip_error==0,"identity geometry roundtrip is exact");
    auto linked=c;linked.normal_context=linked.server_normal=false;
    linked.linked_weapon_context=linked.linked_server_context=true;linked.server_link_identity=0x23450000;
    {Fake sample(linked);sample.pre.gameplay_active=false;sample.pre.linked_weapon_active=true;
        sample.initial.client_mode=sample.initial.server_mode=primary_weapon_tracking::Mode::linked;
        sample.initial.server_link=linked.server_link_identity;sample.final=sample.initial;sample.final.skeleton_stamp=99;
        run(sample,linked);check(sample.status==BarrelStatus::accepted&&sample.tag_calls==1,
            "verified linked primary command can observe the barrel without ordinary gameplay activation");}
    for(unsigned field=0;field<4;++field){Fake changed(linked);changed.pre.gameplay_active=false;changed.pre.linked_weapon_active=true;
        changed.initial.client_mode=changed.initial.server_mode=primary_weapon_tracking::Mode::linked;
        changed.initial.server_link=linked.server_link_identity;changed.final=changed.initial;changed.final.skeleton_stamp=99;
        switch(field){case 0:changed.final.client_mode=primary_weapon_tracking::Mode::ordinary;break;
        case 1:changed.final.server_mode=primary_weapon_tracking::Mode::ordinary;break;
        case 2:changed.final.server_link+=4;break;case 3:changed.final.server+=4;break;}
        run(changed,linked);check(changed.status==BarrelStatus::changed&&changed.tag_calls==1,
            "native barrel reread rejects changed client/server mode link or server owner");}
    for(const bool other:{false,true})for(int state=-1;state<53;++state) {
        auto animation_state=c;(other?animation_state.other_weapon_state:animation_state.weapon_state)=state;
        Fake sample_state(animation_state);run(sample_state,animation_state);
        const bool accepted_state=(state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
        check((sample_state.status==BarrelStatus::accepted)==accepted_state,
              "actual barrel/render support gate admits pinned sprint/continuous/low-ready states and rejects unknown or redirected viewmodels");
        check(sample_state.tag_calls==(accepted_state?1U:0U),"state permission changes no native helper count or ownership checks");
    }
    for(int state=21;state<=26;++state) {
        auto grenade_visual=c;grenade_visual.weapon_state=state;grenade_visual.manual_grenade_primary_visual=true;
        Fake accepted(grenade_visual);run(accepted,grenade_visual);
        check(accepted.status==BarrelStatus::accepted&&accepted.tag_calls==1,
              "exact manual grenade primary visual admits each offhand state to one guarded barrel sample");
        auto secondary_transition=c;secondary_transition.other_weapon_state=state;secondary_transition.manual_grenade_primary_visual=true;
        Fake rejected(secondary_transition);run(rejected,secondary_transition);
        check(rejected.status==BarrelStatus::context&&rejected.tag_calls==0&&rejected.borrow_calls==0,
              "manual grenade primary visual cannot authorize a secondary transition or native barrel work");
    }
    for(int count:{0,1,2,4,7,8,-1,(-2147483647-1)}) {auto stacked=c;stacked.stack_count=count;Fake sample(stacked);run(sample,stacked);
        check((sample.status==BarrelStatus::accepted)==(count>=0&&count<=7),"barrel observation admits only serialized native stack counts");}
    for(int weapon_class:{0,2,3,4,5,9}){auto projectile=c;projectile.dual_wield=true;projectile.weapon_type=2;projectile.weapon_class=weapon_class;
        Fake sample(projectile);run(sample,projectile);
        check((sample.status==BarrelStatus::accepted)==(weapon_class==4)&&sample.tag_calls==(weapon_class==4?1u:0u),
            "barrel adapter allows projectile pistol duals only and preserves one original tag call");}
    for(unsigned test=0;test<18;++test){
        auto bad=c;
        switch(test){case 0:bad.valid=false;break;case 1:bad.normal_context=false;break;case 2:bad.server_normal=false;break;
        case 3:bad.primary_viewmodel=false;break;case 4:bad.dual_wield=true;bad.weapon_type=2;break;case 5:bad.stack_count=8;break;
        case 6:bad.weapon_state=18;break;case 7:bad.other_weapon_state=18;break;case 8:bad.local_client=1;break;
        case 9:bad.native_thread=18;break;case 10:bad.draw_gun=0;break;case 11:bad.weapon_type=1;break;
        case 12:bad.placement.scale=2;break;case 13:bad.weapon_index=256;break;case 14:bad.refdef_time++;break;
        case 15:bad.stock_view.origin.x=std::numeric_limits<float>::quiet_NaN();break;case 16:bad.stock_view.axis.up.z=-1;break;
        case 17:bad.refdef_identity++;break;}
        Fake f(bad);run(f,bad);check(f.status==BarrelStatus::context&&f.tag_calls==0&&f.borrow_calls==0,"unsupported context cannot read native model or calculate a tag");
    }
    for(unsigned test=0;test<11;++test){Fake f(c);
        switch(test){case 0:f.pre.context.native_override=true;break;case 1:f.pre.context.key_catcher=1;break;
        case 2:f.pre.gameplay_active=false;break;case 3:f.pre.gameplay_epoch=0;break;case 4:f.pre.observed_ms=899;break;
        case 5:f.pre.observed_ms=1001;break;case 6:f.pre.context.command_number=8;break;
        case 7:f.pre.native_cg_identity++;break;case 8:f.pre.context.native_frame_time--;break;
        case 9:f.pre.native_gameplay_fields_valid=false;break;case 10:f.pre.context.native_thread++;break;}
        run(f,c);check(f.status==BarrelStatus::command&&f.tag_calls==0,"missing/stale/foreign pre-command authority suppresses all native tag work");
    }
    for(unsigned test=0;test<11;++test){Fake f(c);
        switch(test){case 0:f.final.owner++;break;case 1:f.final.dobj++;break;case 2:f.final.models[0]++;break;
        case 3:f.final.bone_names[0]++;break;case 4:f.final.model_origin.x++;break;case 5:f.final.skeleton_clock++;break;
        case 6:f.final.skeleton_stamp=0;break;case 7:f.change_pre=true;break;case 8:f.clock=1101;break;
        case 9:f.clock=999;break;case 10:f.final.pose_angles.y++;break;}
        run(f,c);check(f.status==BarrelStatus::changed&&f.tag_calls==1,"changed owner/model/pose/clock or post-command authority rejects copied result");
    }
    {Fake f(c);f.tag_ok=false;run(f,c);check(f.status==BarrelStatus::tag_missing&&f.borrow_calls==1,"missing tag produces no geometry or cached fallback");}
    {Fake f(c);f.borrow_ok=false;run(f,c);check(f.status==BarrelStatus::borrow&&f.tag_calls==0,"borrow failure cannot invoke native tag");}
    {Fake f(c);f.tag_axis.up.z=-1;run(f,c);check(f.status==BarrelStatus::geometry,"reflected native axis is rejected");}
    {Fake f(c);f.tag_origin.x=std::numeric_limits<float>::infinity();run(f,c);check(f.status==BarrelStatus::geometry,"nonfinite native output is rejected");}
    {Fake f(c);f.initial.model_origin.x+=1;f.final=f.initial;f.final.skeleton_stamp=99;run(f,c);check(f.status==BarrelStatus::geometry,"persistent pose and copied model disagreement is rejected");}
    {Fake f(c);f.throw_tag=true;BarrelObserver o;o.host=f.host();bool forwarded=false;
      try{o.observe(c,17,1000);}catch(const std::runtime_error&){forwarded=true;}
      check(forwarded&&f.reports==0,"native C++ exception propagates instead of being mislabeled as missing tag");
      f.throw_tag=false;f.clock=1100;f.pre.observed_ms=1100;f.borrow_calls=0;o.observe(c,17,1100);check(f.status==BarrelStatus::accepted,"observer scope is released after forwarded native exception");}
    {Fake f(c);f.throw_seh=true;BarrelObserver o;o.host=f.host();
      check(observe_native_seh(&o,&c)&&f.native_lock&&f.reports==0&&f.tag_calls==1,"native SEH propagates unchanged without inferred native unlock or fabricated tag status");
      check(!o.running.test_and_set(),"SEH unwind releases only the observer flag");o.running.clear();}
    {Fake f(c);f.throw_report=true;BarrelObserver o;o.host=f.host();o.observe(c,17,1000);check(!o.running.test_and_set(),"evidence failure cannot retain observer ownership");o.running.clear();}
    {Fake f(c);BarrelObserver o;o.host=f.host();o.observe(c,17,1000);o.observe(c,17,1099);check(f.tag_calls==1,"100ms native sampling cadence is bounded");
      f.clock=1100;f.pre.observed_ms=1100;o.observe(c,17,1100);check(f.tag_calls==2&&f.sample.observation==2,"later sample has a new serial without a model lifetime claim");}
    {Fake f(c);BarrelObserver o;o.host=f.host();o.attempts=max_attempts;o.observe(c,17,1000);check(f.status==BarrelStatus::budget&&f.tag_calls==0,"bounded diagnostic budget cannot keep calculating bones");}
    {Fake f(c);BarrelObserver o;o.host=f.host();o.running.test_and_set();o.observe(c,17,1000);check(f.reports==0&&f.tag_calls==0,"nested or concurrent observer cannot access the current borrow");o.running.clear();}
    for(unsigned i=0;i<180;++i){
        const float angle=static_cast<float>(i)*0.034906585F;const float x=std::cos(angle),y=std::sin(angle);
        BarrelSample s{};s.native_model_origin=s.native_pose_origin={1234,5678,90};s.body_origin={1200,5600,0};
        s.native_model_axis={{x,y,0},{-y,x,0},{0,0,1}};s.body_axis={{-y,x,0},{-x,-y,0},{0,0,1}};
        const Vec known{15,-2,3};const auto d=world(s.native_model_axis,known);s.tag_world_origin={1234+d.x,5678+d.y,90+d.z};s.tag_world_axis=s.native_model_axis;
        check(derive_geometry(s)&&distance(s.tag_model_origin,known)<0.002F,"rotated model/body barrel geometry roundtrips without mirroring");
    }
    SetLastError(9912);observe_weapon_barrel_after(c);check(GetLastError()==9912&&!weapon_barrel_probe_enabled(),"default-off public path preserves LastError and performs no native call");
    check(!configure_weapon_barrel_probe(nullptr,false)&&GetLastError()==9912,"unconfigured fixture cannot activate native entrypoints");
    borrowed_enabled=true;borrowed_writer_enabled=false;borrowed_pending.active=true;
    borrowed_pending.claim.wrapper_token=77;borrowed_pending.identity={2,3,4,5};SetLastError(9912);
    retire_borrowed_weapon_invocation(77);
    check(borrowed_pending.active&&!borrowed_cancelled&&GetLastError()==9912,"writer-off retirement preserves exact passive outstanding-claim behavior");
    borrowed_writer_enabled=true;retire_borrowed_weapon_invocation(78);
    check(borrowed_pending.active&&!borrowed_cancelled,"foreign invocation cannot cancel an active writer claim");
    native_clear_busy.test_and_set();retire_borrowed_weapon_invocation(77);
    check(borrowed_pending.active&&!borrowed_cancelled,"nested observer cannot steal active cleanup ownership");native_clear_busy.clear();
    retire_borrowed_weapon_invocation(77);
    check(!borrowed_pending.active&&borrowed_cancelled==1&&last_cancelled==bovr::input::WeaponFrameIdentity{2,3,4,5}&&GetLastError()==9912,"active actual retirement cancels only its full four-field reserved identity");
    retire_borrowed_weapon_invocation(77);
    check(borrowed_cancelled==1,"retired writer cannot cancel the scene twice");borrowed_writer_enabled=false;borrowed_enabled=false;
    std::printf("%u checks, %u failures, 0 game launches, 0 native game calls\n",checks,failures);return failures?1:0;
}
