// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/tracked_gun_pose.hpp"
#include "input/manual_reload.hpp"
#include "input/paired_reload.hpp"
#include "t5/borrowed_profile_shape.hpp"
#include "t5/borrowed_weapon_writer.hpp"
#include <string_view>

namespace bovr::t5::reload_pose {
namespace a=animation_detail;
namespace xr=wawvr::xr;
namespace m=input::manual;
inline constexpr bool independent_dual_pistol(int weapon_type,int weapon_class) noexcept {
    return weapon_class==4&&(weapon_type==0||weapon_type==2);
}
struct Frame {
    input::WeaponFrameIdentity identity{};
    a::Transform hand{},supply{},glove{},companion_aim{};
    m::Stage stage{};
    m::ReloadKind kind{m::ReloadKind::magazine};
    std::uint64_t epoch{},transaction{},published_ms{};
    float charge{};
    unsigned weapon{};
    bool valid{},free_hand{},palm_surface{},pump_gripped{},dual_ready{},pimax_grip_fallback{};
};
inline bool make_frame(const input::ReservedWeaponFrame& r,const input::BorrowedPlacementValues& p,
    const m::Output& reload,unsigned weapon,std::uint64_t epoch,std::uint64_t now,Frame* out,bool dual=false) noexcept {
    if(!out)return false;*out={};
    if(!p.valid||p.identity!=r.identity||!r.exclusively_reserved_for_scene||r.prediction.frame_id!=r.identity.frame_id||
       r.anchor_generation!=r.identity.generation||!r.published_ms||now<r.published_ms||now-r.published_ms>100||
       !reload.supported||reload.hand!=1||reload.weapon!=weapon||reload.epoch!=epoch||reload.generation!=r.identity.generation||
       !reload.updated_ms||now<reload.updated_ms||now-reload.updated_ms>100)return false;
    const auto& f=r.prediction;const auto& grip=f.actions.hands[0].grip;
    if(!f.should_render||!f.views_valid||!f.actions.focused||!grip.active||!grip.position_valid||!grip.position_tracked||
       !grip.orientation_valid||!grip.orientation_tracked||!a::finite(grip.pose.position)||!input::tracked_detail::unit(grip.pose.orientation)||
       !a::finite(reload.supply)||!a::finite(r.tracking_anchor.position)||!input::tracked_detail::unit(r.tracking_anchor.orientation)||
       !a::finite(p.body.origin)||!a::rigid(p.body.axis)||!std::isfinite(reload.charge_fraction))return false;
    const auto world=[&](const xr::Posef& pose){const auto local=xr::OpenXrPoseToIwRelative(pose,r.tracking_anchor,xr::kIwUnitsPerMeter);
        return a::compose(p.body,{local.position,local.axis});};
    Frame v{};v.hand=world(grip.pose);v.supply=world({grip.pose.orientation,reload.supply});
    const auto& surface=f.actions.hands[0].palm;
    v.palm_surface=surface.active&&surface.position_valid&&surface.position_tracked&&
        surface.orientation_valid&&surface.orientation_tracked&&a::finite(surface.pose.position)&&
        input::tracked_detail::unit(surface.pose.orientation);
    // Surface orientation is for the glove only. Reload magazine placement and
    // two-hand steering keep their original controller grip pose.
    if(v.palm_surface)v.glove=world(surface.pose);
    v.identity=r.identity;v.epoch=epoch;v.weapon=weapon;v.transaction=reload.transaction;v.published_ms=r.published_ms;
    v.stage=reload.stage;v.kind=reload.kind;v.charge=std::clamp(reload.charge_fraction,0.f,1.f);
    // The copied input owner supplies the physical pump capture. Recheck held
    // grip in this fresh render prediction so a release cannot leave it glued.
    const auto& squeeze=f.actions.hands[0].squeeze;
    v.pump_gripped=reload.pump_gripped&&squeeze.active&&std::isfinite(squeeze.current)&&squeeze.current>=.65f;
    v.free_hand=!p.support.paired||m::uses_offhand(reload);v.valid=true;
    if(dual&&reload.stage==m::Stage::ready){
        if(!r.identity.generation||!r.identity.frame_id||!r.identity.reservation_serial||!r.identity.native_build_serial||!epoch||
           !input::tracked_detail::compose_tracked_gun(f,r.tracking_anchor,{p.body.origin,p.body.axis},xr::Hand::Left,
                {},xr::Quaternionf{},&v.companion_aim))return false;
        v.dual_ready=true;v.free_hand=false;v.pump_gripped=false;
    }
    // Keep one visual anatomical basis across neutral and authored reload
    // states. Real palm provenance, paired support, pump and dual poses stay
    // separate; the interaction grip and supply transforms are not modified.
    v.pimax_grip_fallback=r.pimax_free_glove_grip_basis&&!v.palm_surface&&
        v.free_hand&&!v.pump_gripped;
    if(!a::finite(v.hand.origin)||!a::rigid(v.hand.axis)||!a::finite(v.supply.origin)||!a::rigid(v.supply.axis))return false;
    *out=v;return true;
}
inline int bone(const borrowed_profile::Profile& p,std::string_view name) noexcept {
    for(unsigned i=0;i<p.bone.size();++i)if(name==p.bone[i].name)return int(i);return -1;
}
inline bool descendant(const borrowed_profile::Profile& p,unsigned b,unsigned root) noexcept {
    if(b>=p.bone.size()||root>=p.bone.size())return false;
    while(b>root){const auto d=p.bone[b].parent;if(!d||d>b)return false;b-=d;}return b==root;
}
inline constexpr std::array pump_names{"j_pump","tag_pump","tag_mk_pump","tag_pump2"};
inline constexpr std::array magazine_names{"tag_clip","j_clip","tag_magazine","j_magazine",
    "tag_ext_clip","tag_double_clip","tag_dual_clip","tag_extended_clip"};
inline bool movable_part(const borrowed_profile::Profile& p,const borrowed_profile::Shape& s,int part) noexcept {
    return part>=int(s.arms)&&part<int(s.total)&&!descendant(p,s.gun,unsigned(part))&&
        !descendant(p,s.flash,unsigned(part));
}
template<std::size_t N> inline int first_part(const borrowed_profile::Profile& p,const borrowed_profile::Shape& s,
    const std::array<const char*,N>& names) noexcept {
    for(const auto* name:names){const int candidate=bone(p,name);if(movable_part(p,s,candidate))return candidate;}return -1;
}
struct FeedPart {int bone{-1};bool authored_mesh_known{};};
inline FeedPart feed_part(const borrowed_profile::Profile& p,const borrowed_profile::Shape& s,m::ReloadKind kind) noexcept {
    if(kind==m::ReloadKind::magazine||kind==m::ReloadKind::belt)
        return {first_part(p,s,magazine_names),true};
    // Rifle assemblies contain both launcher and Masterkey ammunition tags.
    // A mechanism selects only its own feed; tag_clip is deliberately excluded
    // from shell feeds because it is a revolver cylinder on other models.
    int selected=-1;
    if(kind==m::ReloadKind::internal_rounds||kind==m::ReloadKind::break_action)
        selected=first_part(p,s,std::array{"tag_shell","j_shell","tag_mk_ammo","tag_ammo2","tag_ammo","j_ammo"});
    else if(kind==m::ReloadKind::single_round)
        selected=first_part(p,s,std::array{"j_m203_ammo","j_grenade_ammo","j_bouncing_grenade_ammo","tag_arrow","tag_ammo","j_ammo"});
    // A tag's existence does not prove its mesh is currently shown: attachment
    // hide bits and animation can hide its round. Until that current visibility
    // is witnessed, use the generic held feed and leave native feed bones alone.
    // This avoids both an invisible held round and duplicate native/overlay feeds.
    return {selected,false};
}
inline a::Vec scale(a::Vec v,float s) noexcept {return {v.x*s,v.y*s,v.z*s};}
inline bool normalize(a::Vec* v) noexcept {
    const float n=std::sqrt(a::dot(*v,*v));if(!std::isfinite(n)||n<1.e-5f)return false;*v=scale(*v,1/n);return true;
}
inline a::NativeMat32 matrix(const a::Transform& t) noexcept {
    const auto q=borrowed_writer::quaternion(t.axis);return {{q.x,q.y,q.z,q.w},t.origin,2};
}
// Pose the existing authored arm and fingers as one rigid free-hand assembly.
// The palm frame is anatomical (evaluated wrist, finger bases and thumb), so
// controller rotation does not depend on a guessed weapon-specific Euler angle.
inline bool palm(const borrowed_profile::Profile& p,std::span<const a::NativeMat32> native,a::Transform* out) noexcept {
    const int wrist=bone(p,"j_wrist_le"),thumb=bone(p,"j_thumb_le_0");if(wrist<0||thumb<0)return false;
    a::Vec center{};
    for(const auto* name:{"j_index_le_0","j_mid_le_0","j_ring_le_0","j_pinky_le_0"}){
        const int i=bone(p,name);if(i<0||!descendant(p,unsigned(i),unsigned(wrist)))return false;
        center=a::add(center,native[i].origin);
    }
    center=scale(center,.25f);auto forward=a::sub(center,native[wrist].origin);
    auto toward_thumb=a::sub(native[thumb].origin,native[wrist].origin);
    if(!normalize(&forward))return false;
    toward_thumb=a::sub(toward_thumb,scale(forward,a::dot(toward_thumb,forward)));
    if(!normalize(&toward_thumb))return false;
    auto into_palm=a::cross(forward,toward_thumb);if(!normalize(&into_palm))return false;
    // Match COD4's anatomical OpenXR grip: -Z toward thumb, -X into
    // palm, +Y toward fingertips. The target uses IW's {-Z,-X,+Y} axes.
    *out={scale(a::add(center,native[wrist].origin),.5f),{toward_thumb,into_palm,forward}};
    return a::finite(out->origin)&&a::rigid(out->axis);
}
struct Result {
    std::array<a::NativeMat32,borrowed_profile::max_profile_bones> matrices{};
    std::array<bool,borrowed_profile::max_profile_bones> changed{};
    unsigned count{};bool hand{},device{},handle{},companion{};
};
// Pure copied-value edit. The adapter must separately prove current allocation,
// clock, complete exact model payload and native lock ownership before writes.
inline bool pose(unsigned row,const Frame& f,std::span<const a::NativeMat32> original,Result* out) noexcept {
    if(!out)return false;*out={};const auto* p=borrowed_profile::get(row);const auto* s=borrowed_profile::shape(row);
    if(!f.valid||!p||!s||f.weapon!=s->weapon||original.size()!=s->total||original.size()>out->matrices.size()||
       !a::finite(f.hand.origin)||!a::rigid(f.hand.axis)||!a::finite(f.supply.origin)||!a::rigid(f.supply.axis)||
       !std::isfinite(f.charge)||f.charge<0||f.charge>1||
       (f.pump_gripped&&(f.kind!=m::ReloadKind::internal_rounds||
        (f.stage!=m::Stage::ready&&f.stage!=m::Stage::charge&&f.stage!=m::Stage::charging))))return false;
    Result value{};std::copy(original.begin(),original.end(),value.matrices.begin());
    for(const auto& raw:original){a::Transform checked{};if(!a::convert_native(raw,&checked))return false;}
    const auto move=[&](unsigned i,const a::Transform& from,const a::Transform& to){
        a::Transform current{};if(!a::convert_native(original[i],&current))return false;
        const auto transformed=a::compose(to,a::relative(from,current));
        if(!a::finite(transformed.origin)||!a::rigid(transformed.axis))return false;
        value.matrices[i]=matrix(transformed);value.changed[i]=true;return true;};
    if(f.dual_ready){
        // The current command/scene adapter has already proved model2 is this
        // weapon's reciprocal native companion. Keep its evaluated closed grip
        // and every local gun animation while moving the complete assembly.
        if(f.stage!=m::Stage::ready||f.pump_gripped||!a::finite(f.companion_aim.origin)||!a::rigid(f.companion_aim.axis)||
           p->model.size()<3||p->model_parents.size()!=p->model.size())return false;
        const auto& left=p->model[2];const int shoulder=bone(*p,"j_shoulder_le"),right_wrist=bone(*p,"j_wrist_ri"),
            right_shoulder=bone(*p,"j_shoulder_ri"),attachment=bone(*p,"tag_weapon1");
        if(shoulder<0||right_wrist<0||attachment<0||unsigned(attachment)>=s->arms||
           p->model_parents[2]!=unsigned(attachment)||left.root!=1||!left.count||left.offset<s->arms||
           left.offset+left.count>s->total||p->bone[left.offset].parent!=0)return false;
        int flash=-1;
        for(unsigned i=0;i<s->total;++i)if(p->bone[i].name=="tag_flash1"){
            if(flash>=0||i<=left.offset||i>=left.offset+left.count||!descendant(*p,i,left.offset))return false;flash=int(i);}
        if(flash<0)return false;
        a::Transform source{},muzzle{};
        if(!a::convert_native(original[left.offset],&source)||!a::convert_native(original[flash],&muzzle))return false;
        const auto muzzle_local=a::relative(source,muzzle);
        const a::Transform identity{{},{{1,0,0},{0,1,0},{0,0,1}}};
        const auto target=a::compose(f.companion_aim,{{},a::relative({{},muzzle_local.axis},identity).axis});
        std::array<bool,borrowed_profile::max_profile_bones> selected{};
        for(unsigned i=0;i<s->arms;++i)selected[i]=descendant(*p,i,unsigned(shoulder))||i==unsigned(attachment);
        for(unsigned i=left.offset;i<left.offset+left.count;++i)selected[i]=true;
        // Additional model attachments follow only a proved selected parent.
        for(unsigned n=3;n<p->model.size();++n){const auto& model=p->model[n];const auto parent=p->model_parents[n];
            if(model.offset+model.count>s->total)return false;
            if(parent<model.offset&&selected[parent])for(unsigned i=model.offset;i<model.offset+model.count;++i)selected[i]=true;}
        for(unsigned i=0;i<s->total;++i)if(selected[i]){
            if(i==s->gun||i==s->flash||i==s->attachment||(i>=p->model[1].offset&&i<p->model[1].offset+p->model[1].count)||
               (i<s->arms&&(descendant(*p,i,unsigned(right_wrist))||
                (right_shoulder>=0&&descendant(*p,i,unsigned(right_shoulder)))))||!move(i,source,target))return false;
            ++value.count;
        }
        value.hand=value.companion=true;*out=value;return true;
    }
    int pump=-1;
    if(f.pump_gripped)pump=first_part(*p,*s,pump_names);
    if(pump>=0){
        // Keep the weapon's authored closed fingers wrapped around its actual
        // pump. The pump and glove receive the same rigid displacement through
        // both halves of the stroke; raw controller pose only drives progress.
        // At rest/return the zero displacement preserves the authored hold.
        a::Transform source{},gun{};const int shoulder=bone(*p,"j_shoulder_le");
        if(shoulder<0||!a::convert_native(original[pump],&source)||!a::convert_native(original[s->gun],&gun))return false;
        auto target=source;target.origin=a::add(source.origin,scale(gun.axis.forward,-m::charging_travel_meters*xr::kIwUnitsPerMeter*f.charge));
        for(unsigned i=0;i<s->arms;++i)if(descendant(*p,i,unsigned(shoulder))&&!move(i,source,target))return false;
        value.hand=true;
    }else if(f.free_hand){
        a::Transform source{};const int shoulder=bone(*p,"j_shoulder_le");
        if(shoulder<0||!palm(*p,original,&source))return false;
        const auto& target_hand=f.palm_surface?f.glove:f.hand;
        if(!a::finite(target_hand.origin)||!a::rigid(target_hand.axis))return false;
        const auto surface_basis=[&](a::Transform t){
            // COD4 palm_ext mapping: -Z points along the fingers, -X into
            // the left palm. The generic grip and surface frames differ.
            // Pimax grip fallback uses the same visual basis. In non-ready
            // states the source is the current evaluated hand, preserving its
            // authored finger/arm pose rather than substituting a neutral bind.
            if(f.palm_surface||f.pimax_grip_fallback)
                t.axis={t.axis.up,scale(t.axis.left,-1),t.axis.forward};
            return t;
        };
        if(f.stage==m::Stage::ready){
            // The admitted arms' canonical open glove replaces the weapon's
            // closed gripping fingers while free. Both remain authored poses.
            std::array<a::NativeMat32,borrowed_profile::max_profile_bones> bind{};
            for(unsigned i=0;i<s->arms;++i)std::memcpy(&bind[i],p->bone[i].bind.data(),32);
            if(!palm(*p,{bind.data(),s->arms},&source))return false;
            source=surface_basis(source);
            for(unsigned i=0;i<s->arms;++i)if(descendant(*p,i,unsigned(shoulder))){
                a::Transform current{};if(!a::convert_native(bind[i],&current))return false;
                const auto target=a::compose(target_hand,a::relative(source,current));
                value.matrices[i]=matrix(target);value.changed[i]=true;
            }
        }else {
            source=surface_basis(source);
            for(unsigned i=0;i<s->arms;++i)if(descendant(*p,i,unsigned(shoulder))&&!move(i,source,target_hand))return false;
        }
        value.hand=true;
    }
    const bool detached=f.stage==m::Stage::available||f.stage==m::Stage::held||f.stage==m::Stage::insertion;
    const bool magazine=f.kind==m::ReloadKind::magazine||f.kind==m::ReloadKind::belt;
    const auto feed=feed_part(*p,*s,f.kind);const int clip=feed.bone;
    // Some authored models have no detachable round mesh. Keep the independent
    // glove and let the overlay supply the feed device for those models.
    if(detached&&feed.authored_mesh_known&&movable_part(*p,*s,clip)){
        a::Transform source{};if(!a::convert_native(original[clip],&source))return false;
        auto target=f.stage==m::Stage::available?f.supply:f.hand;
        // Place the magazine's insertion tag above the palm; preserve authored
        // relative transforms of clip variants/children and original materials.
        target.origin=a::add(target.origin,a::add(scale(target.axis.up,.055f*xr::kIwUnitsPerMeter),
            scale(target.axis.forward,.035f*xr::kIwUnitsPerMeter)));
        for(unsigned i=s->arms;i<s->total;++i){bool selected=false;
            selected=descendant(*p,i,unsigned(clip));
            if(magazine)for(const auto* name:magazine_names){const int root=bone(*p,name);
                selected=selected||(movable_part(*p,*s,root)&&descendant(*p,i,unsigned(root)));}
            if(selected&&!move(i,source,target))return false;
        }
        value.device=true;
    }
    if(f.stage==m::Stage::charging&&f.charge>0){
        int handle=-1;
        if(f.kind==m::ReloadKind::internal_rounds)handle=first_part(*p,*s,pump_names);
        if(handle<0)handle=first_part(*p,*s,std::array{"j_charging_handle","j_bolt","tag_bolt","j_charge_handle","j_m203_slide"});
        if(handle>=0&&!descendant(*p,s->gun,unsigned(handle))&&!descendant(*p,s->flash,unsigned(handle))){a::Transform source{},gun{};
            if(!a::convert_native(original[handle],&source)||!a::convert_native(original[s->gun],&gun))return false;
            auto target=source;target.origin=a::add(source.origin,scale(gun.axis.forward,-m::charging_travel_meters*xr::kIwUnitsPerMeter*f.charge));
            for(unsigned i=s->arms;i<s->total;++i)if(descendant(*p,i,unsigned(handle))&&!move(i,source,target))return false;
            value.handle=true;
        }
    }
    // Placement, muzzle and the dominant arm are strictly native. Never let an
    // unexpected hierarchy turn a limb edit into a gun/aim edit.
    if(value.changed[s->gun]||value.changed[s->flash])return false;
    for(unsigned i=0;i<s->total;++i)if(value.changed[i])++value.count;
    *out=value;return true;
}
}
