// SPDX-License-Identifier: GPL-3.0-only
#include "t5/crossbow_policy.hpp"
#include "t5/crossbow_optic.hpp"
#include "t5/magnified_optic_policy.hpp"
#include "t5/magnified_optic_fit.hpp"
#include "input/weapon_reload_mechanism.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
namespace c=bovr::t5::crossbow;
namespace p=bovr::t5::primary_weapon_tracking;
namespace m=bovr::input::manual;
namespace xr=wawvr::xr;
unsigned checks{};
void check(bool b,const char* s){++checks;if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
int main(){
    namespace optic=bovr::t5::magnified_optic;
    for(const char* material:{"mc/mtl_scope_lens","mc/mtl_acog_lens","mc/mtl_rus_scope_glass"})
        check(optic::lens_material(material),"authored scope glass recognized");
    for(const char* material:{"mc/mtl_rus_scope","mc/mtl_rus_scope_trans","mc/mtl_mosin_wood","mc/mtl_bottle_glass"})
        check(!optic::lens_material(material),"scope body and unrelated glass excluded");
    struct Inventory {int type,weapon_class,capacity;const char* name;const char* overlay;};
    constexpr Inventory inventory[]{
#include "magnified_optic_inventory.inc"
    };
    for(const auto& v:inventory){const bool equipment=std::string_view(v.name)=="berlin_listening_device";
        check((optic::classify(v.type,v.weapon_class,v.capacity,v.name,v.overlay)!=optic::Kind::none)!=equipment,v.name);}
    namespace anim=bovr::t5::animation_detail;
    const anim::Transform identity{{},{{1,0,0},{0,1,0},{0,0,1}}};
    c::LensFit drag_fit{};
    check(optic::fit_bounds({-1.68f,-.159f,4.332f},{-1.518f,1.077f,5.501f},identity,identity,false,&drag_fit)&&
        drag_fit.offset.x<0&&drag_fit.offset.y>0&&drag_fit.radius>.01f&&drag_fit.magnification==4,"native lens bounds keep offset Dragunov scope");
    check(!optic::fit_bounds({0,0,0},{20,1,1},identity,identity,false,&drag_fit),"long tube is not accepted as glass lens");
    c::LensFit mosin_fit{};
    check(optic::fit_bounds({-6.408906f,-.031416f,2.655019f},{-6.317401f,1.031194f,3.717631f},identity,identity,false,&mosin_fit)&&
        std::fabs(mosin_fit.offset.x+.163551f)<.0001f&&std::fabs(mosin_fit.radius-.013495f)<.0001f,
        "scoped Mosin rear glass produces model-specific lens fit");
    c::LensFit enfield_ir_fit{},enfield_ir_outer_fit{},too_small_fit{};
    check(optic::fit_bounds({-4.367423f,-.162050f,4.496316f},{-4.295509f,.168669f,4.827035f},identity,identity,false,&enfield_ir_fit)&&
        std::fabs(enfield_ir_fit.radius-.0042001f)<.000001f&&enfield_ir_fit.magnification==4,
        "observed Rebirth Enfield IR rear glass below five millimeters is valid");
    check(optic::fit_bounds({-4.346006f,-.174679f,4.492330f},{-4.289943f,.181359f,4.830942f},identity,identity,false,&enfield_ir_outer_fit)&&
        enfield_ir_outer_fit.radius>enfield_ir_fit.radius&&enfield_ir_outer_fit.radius<.005f,
        "observed visible Enfield IR outer lens is valid without changing its size");
    check(!optic::fit_bounds({0,0,0},{.01f,.30f,.30f},identity,identity,false,&too_small_fit),
        "sub-four-millimeter geometry remains rejected");
    for(const char* family:{"dragunov","l96a1","psg1","wa2000"})
        for(const char* suffix:{"_sp","_extclip_sp","_silencer_sp","_vzoom_sp","_zm","_upgraded_zm"})
            check(optic::classify(0,0,10,std::string(family)+suffix)==optic::Kind::telescopic,"sniper modes and upgrades use physical scope");
    for(const char* name:{"aug_acog_zm","aug_acog_mk_upgraded_zm","python_acog_sp","m14_acog_grip_sp","g11_lps_zm","g11_lps_upgraded_zm"})
        check(optic::classify(0,0,30,name)==optic::Kind::compact,"magnified attachment recognized without fullscreen overlay");
    check(optic::classify(0,0,30,"enfield_ir_sp")==optic::Kind::thermal,"thermal optic uses physical lens");
    check(optic::classify(0,0,30,"enfield_ir_mk_sp","scope_overlay_anpvs_ir")==optic::Kind::thermal,
        "Rebirth IR plus masterkey variant uses thermal optic policy");
    check(optic::classify(0,0,5,"mosin_scope_sp")==optic::Kind::telescopic,"Mosin scoped rifle recognized");
    check(optic::classify(0,0,10,"future_variant","scope_overlay_psg1")==optic::Kind::telescopic,"native scope material admits future variants");
    for(const char* name:{"m16_sp","m14_reflex_sp","g11Fake","dragunovFake","aug_elbit_sp"})
        check(optic::classify(0,0,30,name)==optic::Kind::none,"ordinary sights and prefix collisions unchanged");
    check(optic::classify(2,6,1,"tow_sp","scope_overlay_tow")==optic::Kind::none,"guided launcher excluded");
    check(optic::classify(0,7,100,"dragunov_sp")==optic::Kind::none&&optic::classify(0,0,0,"dragunov_sp")==optic::Kind::none,"native mounted or empty-capacity roles excluded");
    namespace admission=bovr::input::profile_admission;
    admission::NeutralSample neutral{};neutral.focused=neutral.dominant_tracking=neutral.menu_active=true;
    for(unsigned i=0;i<2;++i){neutral.squeeze_active[i]=true;neutral.trigger[i].analog_active=true;}
    neutral.squeeze[0]=1;
    check(admission::neutral(c::rearm_sample(neutral,true)),"holding crossbow support cannot latch tracking off after handoff");
    check(!admission::neutral(c::rearm_sample(neutral,false)),"other weapons keep existing neutral policy");
    for(unsigned i=0;i<2;++i){auto bad=neutral;bad.trigger[i].analog=1;
        check(!admission::neutral(c::rearm_sample(bad,true)),"held firing or grenade trigger cannot rearm");}
    auto invalid_grip=neutral;invalid_grip.squeeze[0]=std::numeric_limits<float>::quiet_NaN();
    check(!admission::neutral(c::rearm_sample(invalid_grip,true)),"invalid support input cannot rearm");
    for(const char* name:{"crossbow_explosive_alt_sp","crossbow_vzoom_alt_sp","crossbow_sp","crossbow_zm"})
        check(c::weapon(2,0,1,name),"native one-bolt crossbow variants accepted");
    check(!c::weapon(0,0,1,"crossbow_sp")&&!c::weapon(2,5,1,"crossbow_sp")&&
          !c::weapon(2,0,30,"crossbow_sp")&&!c::weapon(2,0,1,"crossbowFake"),"name never grants incompatible native weapon");
    p::Server s{{1,100,0x40000,0,0x80000,0},true,100,2,0,0,0x120000};
    check(c::reload_context(p::Mode::linked,s,true),"observed scripted crossbow admits reload");
    check(!c::reload_context(p::Mode::ordinary,s,true)&&!c::reload_context(p::Mode::linked,s,false),"paired linked mode and crossbow both required");
    check(c::reload_weapon_flags(0x3000300,true),"zipline offhand and fire restrictions do not disable primary bolt reload");
    check(!c::reload_weapon_flags(0x380,true)&&!c::reload_weapon_flags(0x100,false),"weapon disable and other weapon reload policy preserved");
    auto zip=s;zip.player.weapon_flags=0x3000300;
    auto zip_ok=[&](const p::Server& server,p::Mode mode=p::Mode::linked,unsigned flags=0x300u,
        const char* name="crossbow_vzoom_alt_sp"){return c::zipline_context(mode,server,flags,2,0,1,name);};
    check(zip_ok(zip),"scripted rope-bolt phase uses weapon aim for script look checks");
    auto can_fire=zip;can_fire.player.weapon_flags&=~0x1000000u;
    check(zip_ok(can_fire),"aim remains selected when native script enables fire");
    check(!zip_ok(zip,p::Mode::ordinary)&&!zip_ok(zip,p::Mode::linked,0x100)&&
        !zip_ok(zip,p::Mode::linked,0x300,"crossbow_explosive_alt_sp"),"ordinary play and explosive-bolt fight retain original script angles");
    for(unsigned field=0;field<6;++field){auto bad=zip;
        switch(field){case 0:bad.player.weapon_flags|=0x80;break;case 1:bad.local_identity=false;break;
        case 2:bad.link=0;break;case 3:bad.player.pm_flags|=0x800;break;case 4:bad.player.health=0;break;case 5:bad.player.weapon_flags&=~0x200u;break;}
        check(!zip_ok(bad),"invalid zipline identity or script context preserves native getter");}
    for(auto mask:{0x400u,0x800u}){auto bad=s;bad.player.pm_flags|=mask;check(!c::reload_context(p::Mode::linked,bad,true),"script freeze rejected");}
    for(unsigned field=0;field<7;++field){auto bad=s;
        switch(field){case 0:bad.link=0;break;case 1:bad.local_identity=false;break;case 2:bad.entity_health=0;break;
        case 3:bad.player.weapon_flags=0x80;break;case 4:bad.session=1;break;case 5:bad.connected=1;break;case 6:bad.flags=1;break;}
        check(!c::reload_context(p::Mode::linked,bad,true),"invalid server identity cannot grant manual reload");}
    xr::FrameState f{};f.views_valid=f.actions.focused=true;f.head_center.position={0,1.7f,0};
    for(unsigned i=0;i<2;++i){auto& h=f.actions.hands[i];h.grip.active=h.grip.position_valid=h.grip.position_tracked=h.grip.orientation_valid=h.grip.orientation_tracked=true;
        h.aim.active=h.aim.position_valid=h.aim.position_tracked=h.aim.orientation_valid=h.aim.orientation_tracked=true;h.squeeze.active=true;
        h.grip.pose.position={.032f,1.563f,i?-.20f:-.50f};
        f.eyes[i].pose.position={i?.032f:-.032f,1.7f,0};f.eyes[i].fov={-.8f,.8f,.8f,-.8f};}
    f.actions.hands[0].squeeze.current=1;
    xr::StereoSourceLayout layout{};const auto original=layout;
    check(c::scope(f,&layout)&&layout.physical_scope.active,"supported raised crossbow produces physical sight");
    const auto crossbow_lens=layout.physical_scope.lens_origin_reference_meters;
    c::LensFit compact{{.08f,.01f,.07f},.014f,.075f};
    auto compact_layout=original;
    check(c::scope(f,&compact_layout)&&compact_layout.physical_scope.lens_origin_reference_meters.y==crossbow_lens.y,"default fit preserves crossbow");
    check(c::scope(f,&compact_layout,compact)&&compact_layout.physical_scope.lens_radius_meters==.014f&&
        std::fabs(compact_layout.physical_scope.lens_origin_reference_meters.x-.022f)<.0001f&&
        compact_layout.physical_scope.source.width>layout.physical_scope.source.width,"optic fit sets model offset diameter and magnification");
    check(layout.eyes[0].width==original.eyes[0].width&&layout.eyes[1].x==original.eyes[1].x,"scope preserves base stereo layout");
    check(layout.physical_scope.source.x>=0&&layout.physical_scope.source.x+layout.physical_scope.source.width<=.5f,"zoom sampling cannot cross eye seam");
    auto enfield_frame=f;for(unsigned i=0;i<2;++i)
        enfield_frame.actions.hands[i].grip.pose.position={.032f+enfield_ir_fit.offset.y,1.7f-enfield_ir_fit.offset.z,i?-.30f:-.60f};
    auto enfield_layout=original;
    check(c::scope(enfield_frame,&enfield_layout,enfield_ir_fit)&&enfield_layout.physical_scope.active&&
        std::fabs(enfield_layout.physical_scope.lens_radius_meters-enfield_ir_fit.radius)<.000001f,
        "actual small Enfield IR lens reaches physical scope composition at its authored radius");
    check(enfield_layout.physical_scope.source.width>0&&enfield_layout.physical_scope.source.x>=0&&
        enfield_layout.physical_scope.source.x+enfield_layout.physical_scope.source.width<=.5f,
        "small Enfield IR lens samples within one eye");
    auto at_eye=[&](unsigned eye,float relief,float ipd=.064f){
        auto result=enfield_frame;
        result.eyes[0].pose.position.x=-ipd*.5f;result.eyes[1].pose.position.x=ipd*.5f;
        for(unsigned i=0;i<2;++i)result.actions.hands[i].grip.pose.position={
            result.eyes[eye].pose.position.x+enfield_ir_fit.offset.y,
            result.eyes[eye].pose.position.y-enfield_ir_fit.offset.z,
            -relief+enfield_ir_fit.offset.x-(i?0:.30f)};
        return result;
    };
    for(float ipd:{.055f,.064f,.074f})for(unsigned eye=0;eye<2;++eye)for(float relief:{.0101f,.03f,.08f,.20f}){
        auto near_frame=at_eye(eye,relief,ipd);auto near_layout=original;
        check(c::scope(near_frame,&near_layout,enfield_ir_fit)&&near_layout.physical_scope.active,
            "small IR optic remains active close to either eye across normal IPDs");
        const auto& crop=near_layout.physical_scope.source;
        check(crop.x>=0&&crop.x+crop.width<=1&&crop.y>=0&&crop.y+crop.height<=1&&
            (crop.x+crop.width<=.5f||crop.x>=.5f),"near-eye optic crop stays within one source eye");
    }
    auto near_layout=original,far_layout=original;
    check(c::scope(at_eye(1,.03f),&near_layout,enfield_ir_fit)&&
        c::scope(at_eye(1,.12f),&far_layout,enfield_ir_fit)&&
        std::fabs(near_layout.physical_scope.source.width/far_layout.physical_scope.source.width-4)<.001f,
        "scope magnification uses actual aligned-eye relief instead of head-midpoint distance");
    for(float relief:{-.03f,0.f,.005f,.009f,.71f}){
        auto invalid_layout=original;
        check(!c::scope(at_eye(1,relief),&invalid_layout,enfield_ir_fit)&&!invalid_layout.physical_scope.active,
            "behind-eye intersecting and out-of-reach optics remain inactive");
    }
    auto midpoint=at_eye(1,.012f);for(auto& h:midpoint.actions.hands)h.grip.pose.position.x-=.032f;
    auto invalid_eye_layout=original;
    check(!c::scope(midpoint,&invalid_eye_layout,enfield_ir_fit),"nearby optic between the eyes is not falsely eye-aligned");
    auto away=at_eye(1,.03f);for(auto& eye:away.eyes)eye.pose.orientation={0,1,0,0};
    check(!c::scope(away,&invalid_eye_layout,enfield_ir_fit),"looking away from the raised optic keeps scope inactive");
    auto invalid_eye=at_eye(1,.03f);invalid_eye.eyes[1].pose.position.x=std::numeric_limits<float>::quiet_NaN();
    check(!c::scope(invalid_eye,&invalid_eye_layout,enfield_ir_fit),"invalid aligned-eye pose cannot request a scope");
    auto too_small=enfield_ir_fit;too_small.radius=.00399f;auto rejected_layout=original;
    check(!c::scope(enfield_frame,&rejected_layout,too_small)&&!rejected_layout.physical_scope.active,
        "composition rejects a lens below the shared minimum");
    auto rolled=f;rolled.actions.hands[1].aim.pose.orientation={0,0,.38268343f,.92387953f};
    check(c::scope(rolled,&layout)&&std::fabs(layout.physical_scope.source_rotation_radians+.78539816f)<.001f,"optic roll samples current camera in the matching direction");
    check(layout.physical_scope.source.x>=.5f&&layout.physical_scope.source.x+layout.physical_scope.source.width<=1.f,"rolled lens switches to unobstructed right eye without crossing seam");
    auto left_lens=f;for(auto& h:left_lens.actions.hands)h.grip.pose.position.x=-.032f;
    check(c::scope(left_lens,&layout)&&layout.physical_scope.source.x>=.5f,"left-eye aiming samples opposite eye");
    auto bad=f;bad.actions.hands[0].squeeze.current=0;check(!c::scope(bad,&layout),"release support grip removes scope");
    bad=f;bad.actions.hands[1].aim.orientation_tracked=false;check(!c::scope(bad,&layout),"tracking loss removes scope");
    bad=f;bad.actions.focused=false;check(!c::scope(bad,&layout),"unfocused scope rejected");
    bad=f;for(auto& h:bad.actions.hands)h.grip.pose.position.z+=1;check(!c::scope(bad,&layout),"behind-eye scope rejected");
    bad=f;bad.actions.hands[1].grip.pose.position.x=std::numeric_limits<float>::quiet_NaN();check(!c::scope(bad,&layout),"invalid lens rejected");
    m::NativeWeaponReloadValues v{};v.weapon_type=2;v.weapon_class=0;v.fire_type=1;v.capacity=1;v.bolt_action=true;v.internal_name="crossbow_explosive_alt_sp";
    const auto mechanism=m::classify_native_reload(v);
    check(mechanism.status==m::ReloadClassificationStatus::supported&&mechanism.mechanism.kind==m::ReloadKind::single_round&&mechanism.mechanism.charge_on_empty,"retail crossbow needs bolt insertion and cocking");
    m::Controller controller;m::Context ctx{};ctx.valid=true;ctx.weapon=24;ctx.capacity=1;ctx.reserve=5;ctx.epoch=ctx.generation=1;ctx.now=1000;ctx.cod4_controls=true;ctx.mechanism=mechanism.mechanism;
    f.actions.hands[0].squeeze.current=0;f.actions.hands[1].primary.active=true;f.actions.hands[0].trigger.active=true;
    m::Output out{};auto tick=[&]{++f.actions.sequence;ctx.now+=16;out=controller.update(f,ctx);};tick();
    f.actions.hands[1].primary.current=true;tick();f.actions.hands[1].primary.current=false;tick();
    check(out.stage==m::Stage::available,"A makes one bolt available");
    auto& off=f.actions.hands[0];off.grip.pose.position=out.supply;off.squeeze.current=1;tick();
    check(out.held_device,"hip pickup holds bolt");
    off.grip.pose.position=out.receiver;tick();off.squeeze.current=0;tick();
    check(out.block_attack&&!out.request_commit,"insertion alone cannot fire uncocked crossbow");
    off.grip.pose.position=out.handle;off.trigger.current=1;tick();off.grip.pose.position.z+=.03f;tick();off.trigger.current=0;tick();
    check(!out.request_commit,"short string pull rejected");
    off.grip.pose.position=out.handle;off.trigger.current=1;tick();off.grip.pose.position.z+=.09f;tick();off.trigger.current=0;tick();
    check(out.request_commit&&out.commit_rounds==1,"full pull and release requests exactly one native bolt");
    ctx.clip=1;ctx.reserve=4;ctx.commit_completed=true;tick();ctx.commit_completed=false;tick();
    check(!out.block_attack&&!out.request_commit,"native acknowledgement returns ready without duplicate ammo");
    std::printf("%u crossbow checks passed\n",checks);
}
