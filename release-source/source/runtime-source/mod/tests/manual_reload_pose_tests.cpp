// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_pose.hpp"
#include "input/cod4_controls.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
#include "t5/free_glove_runtime_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace p=bovr::t5::reload_pose;
namespace b=bovr::t5::borrowed_profile;
namespace a=bovr::t5::animation_detail;
namespace m=bovr::input::manual;
unsigned checks{};
void check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
bool same_result(const p::Result& x,const p::Result& y,unsigned count){
    return x.changed==y.changed&&x.count==y.count&&x.hand==y.hand&&x.device==y.device&&x.handle==y.handle&&
        x.companion==y.companion&&!std::memcmp(x.matrices.data(),y.matrices.data(),count*sizeof(a::NativeMat32));
}

void dual_pose_tests(){
    namespace sf=synthetic_profile_fixture;
    check(p::independent_dual_pistol(0,4)&&p::independent_dual_pistol(2,4),
        "native bullet pistols and projectile pistols permit independent dual posing");
    check(!p::independent_dual_pistol(0,2)&&!p::independent_dual_pistol(0,3)&&
          !p::independent_dual_pistol(2,2)&&!p::independent_dual_pistol(2,3),
        "dual SMGs and shotguns preserve their existing paired presentation");
    check(!p::independent_dual_pistol(-1,4)&&!p::independent_dual_pistol(1,4)&&
          !p::independent_dual_pistol(3,4)&&!p::independent_dual_pistol(4,4)&&!p::independent_dual_pistol(2,-1),
        "unknown or unsupported native weapon classes and types reject independent posing");
    sf::Fixture fixture;
    auto& arms=fixture.copies.before[0];
    // This fixture has distinct arm branches, unlike the intentionally simple
    // single-chain skeleton used by the ordinary reload cases below.
    arms.bones[5].parent_delta=5;
    arms.bones[9].parent_delta=9;
    arms.bones[10].parent_delta=5;
    arms.bones[22].parent_delta=22;
    arms.bones[23].parent_delta=2;
    auto companion=sf::gun(14);companion.name.bytes="fixture_companion";
    for(unsigned i=0;i<companion.bones.size();++i){companion.bones[i].name.bytes="companion_"+std::to_string(i);companion.bones[i].script_id=static_cast<std::uint16_t>(100+i);}
    companion.bones[0].name.bytes="j_gun1";companion.bones[6].name.bytes="tag_flash1";
    const a::Axis identity{{1,0,0},{0,1,0},{0,0,1}},quarter{{0,1,0},{-1,0,0},{0,0,1}};
    auto turned_flash=p::matrix({{12,2,1},quarter});std::memcpy(companion.bones[6].bind_bits.data(),&turned_flash,32);
    fixture.copies.before.push_back(companion);fixture.parents.push_back(10);
    bovr::input::ProfileModelCopy sight{{"fixture_left_sight",true,true},1,{sf::bone("fixture_left_sight_root",120,0,3,1,1)}};
    fixture.copies.before.push_back(sight);fixture.parents.push_back(89);
    sight.name.bytes="fixture_right_sight";sight.bones[0].name.bytes="fixture_right_sight_root";sight.bones[0].script_id=121;
    fixture.copies.before.push_back(sight);fixture.parents.push_back(79);
    fixture.recanonical();const auto row=fixture.enroll();
    check(row!=b::invalid_runtime_row,"independently authored dual fixture registers");
    const auto& profile=*b::get(row);const auto& shape=*b::shape(row);
    std::array<a::NativeMat32,b::max_profile_bones> raw{};
    const a::Transform world{{-1200,750,87},quarter};
    for(unsigned i=0;i<shape.total;++i){a::NativeMat32 bind{};std::memcpy(&bind,profile.bone[i].bind.data(),32);
        a::Transform local{};check(a::convert_native(bind,&local),"dual fixture bind is rigid");raw[i]=p::matrix(a::compose(world,local));}
    const auto native=std::span<const a::NativeMat32>{raw.data(),shape.total};
    p::Frame frame{};frame.valid=frame.dual_ready=true;frame.weapon=shape.weapon;
    frame.hand=frame.supply={{-1150,700,100},identity};frame.companion_aim={{-1180,690,105},identity};
    p::Result posed{};check(p::pose(row,frame,native,&posed)&&posed.companion&&posed.hand&&!posed.device&&!posed.handle,
        "ready dual poses companion and its authored gripping glove");
    auto pimax_dual=frame;pimax_dual.pimax_grip_fallback=true;p::Result pimax_dual_pose{};
    check(p::pose(row,pimax_dual,native,&pimax_dual_pose)&&same_result(posed,pimax_dual_pose,shape.total),
        "Pimax free-glove basis cannot change ready dual pistol, muzzle or authored gripping hand");
    a::Transform source{},target{},old_wrist{},new_wrist{},flash{};
    check(a::convert_native(raw[82],&source)&&a::convert_native(posed.matrices[82],&target)&&
          a::convert_native(raw[21],&old_wrist)&&a::convert_native(posed.matrices[21],&new_wrist)&&
          a::convert_native(posed.matrices[88],&flash),"dual output transforms are valid");
    check(a::distance(target.origin,frame.companion_aim.origin)<.005f&&a::axis_error(flash.axis,frame.companion_aim.axis)<.002f,
        "left grip places companion root and left aim owns corrected muzzle direction");
    const auto old_grip=a::relative(source,old_wrist),new_grip=a::relative(target,new_wrist);
    check(a::distance(old_grip.origin,new_grip.origin)<.005f&&a::axis_error(old_grip.axis,new_grip.axis)<.002f,
        "closed authored glove stays fixed to companion through rotation and translation");
    for(unsigned i=shape.arms;i<shape.arms+profile.model[1].count;++i)
        check(!posed.changed[i]&&!std::memcmp(&posed.matrices[i],&raw[i],32),"all primary pistol bones stay byte-identical");
    check(!posed.changed[22]&&!std::memcmp(&posed.matrices[22],&raw[22],32)&&!posed.changed[9],
        "right wrist and primary attachment stay byte-identical");
    check(posed.changed[92]&&!posed.changed[93],"only accessory attached to companion follows left controller");
    auto moved=frame;moved.companion_aim={{-1080,590,125},quarter};p::Result moved_pose{};
    check(p::pose(row,moved,native,&moved_pose)&&a::distance(moved_pose.matrices[82].origin,moved.companion_aim.origin)<.005f&&
        !std::memcmp(&moved_pose.matrices[shape.gun],&posed.matrices[shape.gun],32),
        "moving left controller independently never moves dominant pistol");
    auto invalid=frame;invalid.stage=m::Stage::held;
    check(!p::pose(row,invalid,native,&posed)&&!posed.count,"ready-only ticket cannot cross into a reload stage");
    invalid=frame;invalid.companion_aim.origin.x=std::numeric_limits<float>::quiet_NaN();
    check(!p::pose(row,invalid,native,&posed)&&!posed.count,"invalid companion pose rejects all writes");
    invalid=frame;++invalid.weapon;check(!p::pose(row,invalid,native,&posed)&&!posed.count,"dual weapon identity mismatch rejects all writes");
    check(!p::pose(sf::row(0),frame,{raw.data(),82},&posed)&&!posed.count,"single pistol cannot use a companion ticket");
    auto malformed=fixture;malformed.copies.before[0].bones[22].parent_delta=1;malformed.recanonical();
    check(!p::pose(malformed.enroll(),frame,native,&posed)&&!posed.count,"overlapping right arm hierarchy rejects entire dual edit");
    malformed=fixture;malformed.parents[2]=9;malformed.recanonical();
    check(!p::pose(malformed.enroll(),frame,native,&posed)&&!posed.count,"wrong companion attachment rejects entire dual edit");

    bovr::input::ReservedWeaponFrame reserved{};bovr::input::BorrowedPlacementValues proposal{};m::Output reload{};
    reserved.identity={2,9,3,4};reserved.anchor_generation=2;reserved.published_ms=1000;reserved.exclusively_reserved_for_scene=true;
    reserved.prediction.frame_id=9;reserved.prediction.should_render=reserved.prediction.views_valid=reserved.prediction.actions.focused=true;
    auto& grip=reserved.prediction.actions.hands[0].grip;auto& aim=reserved.prediction.actions.hands[0].aim;
    grip.active=grip.position_valid=grip.position_tracked=grip.orientation_valid=grip.orientation_tracked=true;
    aim.active=aim.orientation_valid=aim.orientation_tracked=true;grip.pose.position={-.25f,1.25f,-.35f};
    proposal.identity=reserved.identity;proposal.valid=true;proposal.body=world;
    reload.supported=true;reload.weapon=shape.weapon;reload.epoch=7;reload.generation=2;reload.updated_ms=1000;
    p::Frame output{};check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output,true)&&output.dual_ready&&!output.free_hand,
        "current dual frame uses independent controller aim and closed grip");
    reserved.pimax_free_glove_grip_basis=true;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output,true)&&output.dual_ready&&!output.pimax_grip_fallback,
        "Pimax runtime metadata cannot turn a ready dual pistol into a free glove");
    check(!p::make_frame(reserved,proposal,reload,shape.weapon,7,1101,&output,true)&&!output.valid,"stale dual prediction rejects");
    auto bad_reserved=reserved;++bad_reserved.identity.frame_id;
    check(!p::make_frame(bad_reserved,proposal,reload,shape.weapon,7,1016,&output,true)&&!output.valid,"mismatched dual reservation rejects");
    bad_reserved=reserved;bad_reserved.anchor_generation=3;
    check(!p::make_frame(bad_reserved,proposal,reload,shape.weapon,7,1016,&output,true)&&!output.valid,"changed dual anchor generation rejects");
    bad_reserved=reserved;bad_reserved.prediction.actions.hands[0].aim.orientation_tracked=false;
    check(!p::make_frame(bad_reserved,proposal,reload,shape.weapon,7,1016,&output,true)&&!output.valid,"lost left aim tracking rejects");
    auto bad_reload=reload;++bad_reload.weapon;
    check(!p::make_frame(reserved,proposal,bad_reload,shape.weapon,7,1016,&output,true)&&!output.valid,"changed reload identity rejects dual pose");
    bad_reload=reload;bad_reload.updated_ms=900;
    check(!p::make_frame(reserved,proposal,bad_reload,shape.weapon,7,1016,&output,true)&&!output.valid,"stale reload publication cannot supply dual pose");
    bad_reserved=reserved;auto bad_proposal=proposal;bad_reserved.identity.reservation_serial=bad_proposal.identity.reservation_serial=0;
    check(!p::make_frame(bad_reserved,bad_proposal,reload,shape.weapon,7,1016,&output,true)&&!output.valid,"matching zero reservation cannot manufacture dual authority");
    for(const auto stage:{m::Stage::available,m::Stage::held,m::Stage::insertion,m::Stage::charge,m::Stage::charging,m::Stage::committing}){
        reload.stage=stage;reload.block_attack=true;reload.charge_fraction=stage==m::Stage::charging?.5f:0.f;
        p::Frame legacy{},dual{};p::Result legacy_pose{},dual_pose{};
        check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&legacy,false)&&
              p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&dual,true)&&!dual.dual_ready,
              "active paired reload retains existing frame route");
        check(p::pose(row,legacy,native,&legacy_pose)&&p::pose(row,dual,native,&dual_pose)&&
              legacy_pose.changed==dual_pose.changed&&!std::memcmp(legacy_pose.matrices.data(),dual_pose.matrices.data(),shape.total*32),
              "every active paired reload stage is byte-identical to previous pose behavior");
    }
}

void pimax_glove_tests(){
    namespace sf=synthetic_profile_fixture;
    check(bovr::t5::pimax_free_glove_runtime("Pimax OpenXR"),"reported runtime selects Pimax visual compatibility");
    check(bovr::t5::pimax_free_glove_runtime("PiOpenXR"),"PiOpenXR identity selects visual compatibility");
    check(bovr::t5::pimax_free_glove_runtime("pIMaX OpenXR"),"runtime identity is case insensitive");
    for(const auto name:{"","SteamVR/OpenXR","VirtualDesktopXR","Oculus","OpenXR Simulator Runtime"})
        check(!bovr::t5::pimax_free_glove_runtime(name),"other runtimes keep generic grip anatomy");

    sf::Fixture fixture;auto& arms=fixture.copies.before[0];
    arms.bones[5].parent_delta=5;arms.bones[9].parent_delta=9;
    arms.bones[10].parent_delta=5;arms.bones[22].parent_delta=22;arms.bones[23].parent_delta=2;
    fixture.recanonical();const auto row=fixture.enroll();
    check(row!=b::invalid_runtime_row,"independent left/right arm fixture registers");
    const auto& profile=*b::get(row);const auto& shape=*b::shape(row);
    const int shoulder=p::bone(profile,"j_shoulder_le"),wrist=p::bone(profile,"j_wrist_le"),
        finger=p::bone(profile,"j_index_le_0"),thumb=p::bone(profile,"j_thumb_le_0");
    check(shoulder>=0&&wrist>=0&&finger>=0&&thumb>=0,"fixture has anatomical left-hand landmarks");
    const a::Axis identity{{1,0,0},{0,1,0},{0,0,1}},quarter{{0,1,0},{-1,0,0},{0,0,1}};
    const std::array<a::Axis,3> axes{{
        identity,quarter,{{0,0,-1},{0,1,0},{1,0,0}}
    }};
    std::array<a::NativeMat32,b::max_profile_bones> native{},animated{};
    const a::Transform base{{-1200,750,87},quarter},authored{{-1210,740,95},{{0,0,-1},{0,1,0},{1,0,0}}};
    for(unsigned i=0;i<shape.total;++i){
        a::NativeMat32 bind{};std::memcpy(&bind,profile.bone[i].bind.data(),32);
        a::Transform local{};check(a::convert_native(bind,&local),"synthetic bone starts rigid");
        native[i]=p::matrix(a::compose(base,local));
        animated[i]=p::matrix(a::compose(i<shape.arms&&p::descendant(profile,i,unsigned(shoulder))?authored:base,local));
    }
    // An independently authored closed-finger orientation and non-neutral
    // thumb position distinguish preserving live animation from copying bind.
    a::Transform posed_finger{},posed_thumb{};
    check(a::convert_native(animated[finger],&posed_finger)&&a::convert_native(animated[thumb],&posed_thumb),
        "animated finger landmarks are valid");
    posed_finger.axis=a::compose(posed_finger,{{},quarter}).axis;
    posed_thumb.origin=a::add(posed_thumb.origin,{.2f,-.15f,.1f});
    animated[finger]=p::matrix(posed_finger);animated[thumb]=p::matrix(posed_thumb);
    p::Frame frame{};frame.valid=frame.free_hand=true;frame.weapon=shape.weapon;
    frame.hand={{-1150,700,100},identity};frame.supply={{-1160,690,80},identity};
    const std::array stages{m::Stage::ready,m::Stage::available,m::Stage::held,m::Stage::insertion,
        m::Stage::charge,m::Stage::charging,m::Stage::committing,m::Stage::ready};
    for(const auto& axis:axes)for(const auto stage:stages){
        const auto& raw=stage==m::Stage::ready?native:animated;
        const auto input=std::span<const a::NativeMat32>{raw.data(),shape.total};
        auto visual=frame;visual.hand.axis=axis;visual.stage=stage;visual.charge=.5f;
        p::Result generic{},pimax{},surface{},both{};a::Transform palm{};
        check(p::pose(row,visual,input,&generic)&&p::palm(profile,{generic.matrices.data(),shape.total},&palm),
            "generic neutral/reload glove is admitted");
        check(a::axis_error(palm.axis,axis)<.004f,"non-Pimax glove keeps original grip anatomy in every stage");
        visual.pimax_grip_fallback=true;
        check(p::pose(row,visual,input,&pimax)&&p::palm(profile,{pimax.matrices.data(),shape.total},&palm),
            "Pimax neutral/reload glove is admitted");
        const a::Axis expected{axis.up,p::scale(axis.left,-1),axis.forward};
        check(a::axis_error(palm.axis,expected)<.004f&&a::rigid(palm.axis)&&a::distance(palm.origin,visual.hand.origin)<.005f,
            "neutral to reload to neutral retains known Pimax finger/thumb/palm directions and grip position");
        for(unsigned i=0;i<shape.total;++i)if(i>=shape.arms||!p::descendant(profile,i,unsigned(shoulder)))
            check(pimax.changed[i]==generic.changed[i]&&!std::memcmp(&pimax.matrices[i],&generic.matrices[i],32),
                "Pimax visual alignment leaves right arm, weapon, muzzle, magazine and handle output byte-identical");
        check(!pimax.changed[shape.gun]&&!pimax.changed[shape.flash]&&!pimax.changed[22],
            "Pimax visual alignment cannot take ownership of primary aim or right wrist");
        if(stage!=m::Stage::ready){
            a::Transform old_wrist{},new_wrist{};
            check(a::convert_native(raw[wrist],&old_wrist)&&a::convert_native(pimax.matrices[wrist],&new_wrist),
                "live authored wrists are rigid");
            for(unsigned i=0;i<shape.arms;++i)if(p::descendant(profile,i,unsigned(shoulder))){
                a::Transform before{},after{};
                check(a::convert_native(raw[i],&before)&&a::convert_native(pimax.matrices[i],&after),
                    "live authored reload limb remains rigid");
                const auto old_local=a::relative(old_wrist,before),new_local=a::relative(new_wrist,after);
                check(a::distance(old_local.origin,new_local.origin)<.005f&&a::axis_error(old_local.axis,new_local.axis)<.004f,
                    "reload glove preserves evaluated arm and closed-finger relationships instead of neutral bind");
            }
        }
        visual.pimax_grip_fallback=false;visual.palm_surface=true;visual.glove={{-1130,720,105},axis};
        check(p::pose(row,visual,input,&surface)&&p::palm(profile,{surface.matrices.data(),shape.total},&palm)&&
            a::axis_error(palm.axis,expected)<.004f&&a::distance(palm.origin,visual.glove.origin)<.005f,
            "real palm surface keeps its actual pose across neutral and reload");
        visual.pimax_grip_fallback=true;
        check(p::pose(row,visual,input,&both)&&same_result(surface,both,shape.total),
            "valid real palm has precedence with no double anatomical rotation");
    }
    const auto input=std::span<const a::NativeMat32>{animated.data(),shape.total};
    {
        auto visual=frame;visual.free_hand=false;p::Result before{},after{};
        check(p::pose(row,visual,input,&before),"paired support baseline is admitted");
        visual.pimax_grip_fallback=true;
        check(p::pose(row,visual,input,&after)&&same_result(before,after,shape.total),
            "paired support pose does not consume free-glove compatibility");
        visual.free_hand=true;visual.pimax_grip_fallback=false;visual.kind=m::ReloadKind::internal_rounds;
        visual.pump_gripped=true;visual.stage=m::Stage::charging;visual.charge=.75f;
        check(p::pose(row,visual,input,&before),"physical pump baseline is admitted");
        visual.pimax_grip_fallback=true;
        check(p::pose(row,visual,input,&after)&&same_result(before,after,shape.total),
            "held physical pump and authored hand stay byte-identical");
    }
    bovr::input::ReservedWeaponFrame reserved{};bovr::input::BorrowedPlacementValues proposal{};m::Output reload{};
    reserved.identity={2,9,3,4};reserved.anchor_generation=2;reserved.published_ms=1000;
    reserved.exclusively_reserved_for_scene=reserved.pimax_free_glove_grip_basis=true;
    reserved.prediction.frame_id=9;reserved.prediction.should_render=reserved.prediction.views_valid=reserved.prediction.actions.focused=true;
    auto& grip=reserved.prediction.actions.hands[0].grip;auto& surface=reserved.prediction.actions.hands[0].palm;
    grip.active=grip.position_valid=grip.position_tracked=grip.orientation_valid=grip.orientation_tracked=true;
    grip.pose.position={-.2f,1.3f,-.4f};proposal.identity=reserved.identity;proposal.valid=true;proposal.body={{},identity};
    reload.supported=true;reload.weapon=shape.weapon;reload.epoch=7;reload.generation=2;reload.updated_ms=1000;
    for(const auto stage:stages){
        reload.stage=stage;p::Frame output{};
        std::array<unsigned char,sizeof(reserved.prediction.actions)> original{};
        std::memcpy(original.data(),&reserved.prediction.actions,original.size());
        check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.pimax_grip_fallback&&!output.palm_surface,
            "Pimax fallback metadata spans neutral and live reload free-hand stages");
        check(!std::memcmp(original.data(),&reserved.prediction.actions,original.size()),
            "visual frame never mutates raw grip, aim, palm, grenade or button actions");
        surface=grip;surface.pose.position.x+=.05f;
        check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.palm_surface&&!output.pimax_grip_fallback&&
            a::distance(output.glove.origin,output.hand.origin)>1,"valid actual palm supersedes Pimax fallback in every stage");
        surface.position_tracked=false;
        check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&!output.palm_surface&&output.pimax_grip_fallback,
            "untracked palm uses a visual grip fallback without fabricating palm provenance");
        surface={};
    }
    p::Frame output{};grip.orientation_tracked=false;
    check(!p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output),"fallback cannot admit untracked grip orientation");
    grip.orientation_tracked=true;grip.position_valid=false;
    check(!p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output),"fallback cannot admit invalid grip position");
    grip.position_valid=true;
    check(!p::make_frame(reserved,proposal,reload,shape.weapon,7,1101,&output),"fallback cannot admit stale reserved prediction");
    proposal.support.paired=true;reload.block_attack=false;reload.stage=m::Stage::ready;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&!output.free_hand&&!output.pimax_grip_fallback,
        "paired support frame does not enable free-glove basis");
    proposal.support.paired=false;reload.kind=m::ReloadKind::internal_rounds;reload.pump_gripped=true;
    auto& squeeze=reserved.prediction.actions.hands[0].squeeze;squeeze.active=true;squeeze.current=1;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.pump_gripped&&!output.pimax_grip_fallback,
        "current held pump frame retains authored weapon attachment");
    reload.pump_gripped=false;reserved.pimax_free_glove_grip_basis=false;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&!output.pimax_grip_fallback,
        "generic runtime frame retains generic grip basis");
}

int main(){
    dual_pose_tests();
    pimax_glove_tests();
    const auto row=synthetic_profile_fixture::row(0);const auto& profile=*b::get(row);const auto& shape=*b::shape(row);
    std::array<a::NativeMat32,b::max_profile_bones> raw{};
    const a::Transform world{{-1200,750,87},{{0,1,0},{-1,0,0},{0,0,1}}};
    for(unsigned i=0;i<shape.total;++i){a::NativeMat32 bind{};std::memcpy(&bind,profile.bone[i].bind.data(),32);
        a::Transform local{};check(a::convert_native(bind,&local),"authored bind is rigid");raw[i]=p::matrix(a::compose(world,local));}
    const std::span<const a::NativeMat32> native(raw.data(),shape.total);
    const a::Axis identity{{1,0,0},{0,1,0},{0,0,1}};
    p::Frame frame{};frame.valid=frame.free_hand=true;frame.weapon=shape.weapon;frame.hand={{-1150,700,100},identity};frame.supply={{-1160,690,80},identity};
    p::Result result{};check(p::pose(row,frame,native,&result)&&result.hand&&!result.device,"free support hand follows its controller");
    check(!result.changed[shape.gun]&&!result.changed[shape.flash],"support hand never changes the gun root or muzzle");
    a::Transform palm{};check(p::palm(profile,{result.matrices.data(),shape.total},&palm)&&a::distance(palm.origin,frame.hand.origin)<.005F,"posed palm reaches controller");

    for(const auto stage:{m::Stage::available,m::Stage::held,m::Stage::insertion}){
        frame.stage=stage;frame.kind=m::ReloadKind::magazine;
        check(p::pose(row,frame,native,&result)&&result.hand&&result.device,"authored removable magazine follows supply hand and insertion stages");
        check(!result.changed[shape.gun]&&!result.changed[shape.flash],"magazine motion preserves aim bones");
    }
    frame.stage=m::Stage::charging;frame.charge=.5F;
    check(p::pose(row,frame,native,&result)&&result.handle,"authored charging handle follows physical progress");
    check(!result.changed[shape.gun]&&!result.changed[shape.flash],"charging preserves gun root and muzzle");

    frame.kind=m::ReloadKind::internal_rounds;frame.pump_gripped=true;frame.stage=m::Stage::charge;frame.charge=0;
    check(p::pose(row,frame,native,&result)&&result.hand,"held pump keeps the support hand attached at rest");
    frame.stage=m::Stage::charging;frame.charge=.75F;
    check(p::pose(row,frame,native,&result)&&result.hand&&result.handle,"held pump and hand move through the stroke together");
    check(!result.changed[shape.gun]&&!result.changed[shape.flash],"pump stroke preserves the barrel authority bones");
    frame.pump_gripped=false;frame.stage=m::Stage::charge;frame.charge=0;
    check(p::pose(row,frame,native,&result)&&result.hand&&!result.handle,"releasing pump returns to independent hand control");

    auto invalid=frame;++invalid.weapon;check(!p::pose(row,invalid,native,&result)&&!result.count,"wrong weapon binding rejects all pose writes");
    invalid=frame;invalid.hand.origin.x=std::numeric_limits<float>::quiet_NaN();check(!p::pose(row,invalid,native,&result)&&!result.count,"nonfinite tracking rejects all pose writes");

    bovr::input::ReservedWeaponFrame reserved{};bovr::input::BorrowedPlacementValues proposal{};m::Output reload{};p::Frame output{};
    reserved.identity={2,9,3,4};reserved.anchor_generation=2;reserved.published_ms=1000;reserved.exclusively_reserved_for_scene=true;
    reserved.prediction.frame_id=9;reserved.prediction.should_render=reserved.prediction.views_valid=reserved.prediction.actions.focused=true;
    auto& grip=reserved.prediction.actions.hands[0].grip;grip.active=grip.position_valid=grip.position_tracked=grip.orientation_valid=grip.orientation_tracked=true;
    grip.pose.position={-.2F,1.3F,-.4F};proposal.identity=reserved.identity;proposal.valid=true;proposal.body={{},identity};
    reload.supported=true;reload.weapon=shape.weapon;reload.epoch=7;reload.generation=2;reload.updated_ms=1000;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.free_hand,"current reserved prediction creates a reload pose frame");
    proposal.support.paired=true;check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&!output.free_hand,"two-hand pairing retains native support pose");
    reload.block_attack=true;check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.free_hand,"reload releases support hand");
    reload.stage=m::Stage::charge;reload.cycle_pending=true;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&!output.free_hand&&reload.block_attack,
        "waiting chamber action accepts paired support without changing the firing gate");
    check(p::pose(row,output,native,&result)&&!result.hand&&!result.handle&&!result.changed[shape.gun]&&!result.changed[shape.flash],
        "waiting-action support retains authored hand and unchanged barrel without faking a charging animation");
    proposal.support.paired=false;
    check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.free_hand,
        "waiting action only attaches the support hand when current geometric pairing succeeds");
    proposal.support.paired=true;
    for(const auto stage:{m::Stage::available,m::Stage::held,m::Stage::insertion,m::Stage::charging,m::Stage::committing}){
        reload.stage=stage;
        check(p::make_frame(reserved,proposal,reload,shape.weapon,7,1016,&output)&&output.free_hand,
            "feed handling, captured charging and commit override a previously paired support pose");
    }
    reload.stage=m::Stage::charge;
    for(unsigned captured=0;captured<3;++captured){auto active=reload;
        if(captured==0)active.held_device=true;if(captured==1)active.request_commit=true;if(captured==2)active.pump_gripped=true;
        check(p::make_frame(reserved,proposal,active,shape.weapon,7,1016,&output)&&output.free_hand,
            "pending charge cannot retain foregrip visual ownership over a held device, commit or pump capture");
    }
    check(!p::make_frame(reserved,proposal,reload,shape.weapon,7,1101,&output),"stale prediction rejects");
    std::printf("PASS %u synthetic runtime reload pose checks\n",checks);return 0;
}
