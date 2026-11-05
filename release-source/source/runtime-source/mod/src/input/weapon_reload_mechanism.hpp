// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/manual_reload.hpp"
#include <algorithm>
#include <string_view>

namespace bovr::input::manual {
// Current protected native copies only. Archive metadata or a weapon name alone
// cannot establish gameplay context, ammo ownership, or a writable allocation.
struct NativeWeaponReloadValues {
    int weapon_type{},weapon_class{},fire_type{},capacity{},reload_add{},start_add{};
    bool segmented{},bolt_action{},no_partial{},dual{},fuel_tank{};
    std::string_view internal_name{};
};
enum class ReloadClassificationStatus : unsigned {
    supported,invalid_values,reloadless,unsupported_type,unsupported_class,
    dual_requires_separate_hands,fuel_tank_requires_native_exchange
};
struct ReloadClassification {
    ReloadMechanism mechanism{};
    ReloadClassificationStatus status{ReloadClassificationStatus::invalid_values};
    // Native bNoPartialReload permits a partial reload only when at least one
    // authored reload increment is missing; otherwise it requires an empty clip.
    unsigned minimum_missing_rounds{1};
};
namespace reload_mechanism_detail {
inline bool family(std::string_view name,std::string_view base) noexcept {
    // Exact family boundary prevents "m16" matching "m160". Names select a
    // physical interaction hint only after native type/class/clip validation.
    return name==base||(name.size()>base.size()&&name.substr(0,base.size())==base&&name[base.size()]=='_');
}
inline bool named(std::string_view name,std::string_view a,std::string_view b={}) noexcept {
    return name==a||(!b.empty()&&name==b);
}
}
inline ReloadClassification classify_native_reload(const NativeWeaponReloadValues& v) noexcept {
    using Status=ReloadClassificationStatus;
    ReloadClassification out{};
    if(v.capacity<0||v.capacity>4096||v.reload_add<0||v.reload_add>4096||v.start_add<0||v.start_add>4096||
       v.fire_type<0||v.fire_type>6||v.internal_name.size()>127)return out;
    for(const unsigned char c:v.internal_name)if(c<0x20||c>0x7e)return out;
    if(!v.capacity){out.status=Status::reloadless;return out;}
    // Retail enums: bullet=0, projectile=2, gas=4. A throwable/item/vehicle
    // needs its own context and input path; this classifier cannot grant one.
    if(v.weapon_type!=0&&v.weapon_type!=2&&v.weapon_type!=4){out.status=Status::unsupported_type;return out;}
    const bool bullet=v.weapon_type==0,projectile=v.weapon_type==2,gas=v.weapon_type==4;
    if((bullet&&(v.weapon_class<0||v.weapon_class>4))||
       (projectile&&v.weapon_class!=0&&v.weapon_class!=4&&v.weapon_class!=5&&v.weapon_class!=6)||
       (gas&&v.weapon_class!=9)){out.status=Status::unsupported_class;return out;}
    if(v.dual){out.status=Status::dual_requires_separate_hands;return out;}
    // Timed fuel tanks exchange/discard remaining fuel and reset the native
    // fuel clock. Do not accidentally route them through conserved round ammo.
    if(v.fuel_tank){out.status=Status::fuel_tank_requires_native_exchange;return out;}
    out.minimum_missing_rounds=v.no_partial?static_cast<unsigned>(
        v.reload_add>0&&v.reload_add<v.capacity?v.reload_add:v.capacity):1U;
    auto& m=out.mechanism;m.valid=true;
    m.cycle_after_shot=v.bolt_action&&v.capacity>1;
    const bool bounded_increment=v.reload_add>0&&v.reload_add<v.capacity;
    if(v.segmented||bounded_increment){
        m.kind=ReloadKind::internal_rounds;
        m.rounds_per_insert=static_cast<unsigned>(v.reload_add>0?std::min(v.reload_add,v.capacity):1);
        // Revolvers and self-loading tubes do not gain a fictitious bolt step.
        m.charge_on_empty=v.bolt_action;
    }else if(bullet&&v.weapon_class==1){
        m.kind=ReloadKind::belt;
    }else if(projectile){
        m.kind=v.capacity==1?ReloadKind::single_round:ReloadKind::magazine;
        m.rounds_per_insert=v.capacity==1?1U:0U;
        m.charge_on_empty=v.bolt_action;
    }else if(gas){
        // Non-timed gas weapons (M2) use ordinary clip/reserve accounting.
        // The physical baseline is a feed-device insertion without charging.
        m.kind=ReloadKind::magazine;m.charge_on_empty=false;m.cycle_after_shot=false;
    }
    using namespace reload_mechanism_detail;
    // Exact known structural exceptions. The name never overrides an
    // incompatible native type/class or admits an otherwise rejected weapon.
    if(bullet&&v.weapon_class==3&&v.capacity<=4&&
       (named(v.internal_name,"rottweil72_sp","rottweil72_zm")||v.internal_name=="rottweil72_upgraded_zm")){
        // Keep one physical insertion aligned with the guarded native reload
        // batch (paired shells when native add is zero), never fabricate a
        // smaller ammo transaction than the retail helper can commit.
        m.kind=ReloadKind::break_action;
        m.rounds_per_insert=static_cast<unsigned>(v.reload_add>0?std::min(v.reload_add,v.capacity):v.capacity);
        m.charge_on_empty=false;m.cycle_after_shot=false;
        m.receiver_offset={0,.015f,-.11f};
    }
    if(bullet&&v.weapon_class==0&&v.bolt_action&&named(v.internal_name,"mosin_sp","mosin_scope_sp")){
        // Native zero-add Mosin reload is a stripper-clip batch.
        m.kind=ReloadKind::internal_rounds;
        m.rounds_per_insert=static_cast<unsigned>(v.reload_add>0?std::min(v.reload_add,v.capacity):v.capacity);
    }
    if(v.bolt_action&&bullet&&v.weapon_class==0&&
       (family(v.internal_name,"l96a1")||named(v.internal_name,"mosin_sp","mosin_scope_sp")))
        m.handle_offset={.065f,.045f,-.035f};
    if(v.bolt_action&&((bullet&&v.weapon_class==3)||
       (projectile&&v.weapon_class==5&&v.segmented)))
        m.handle_offset={0,-.03f,-.19f};
    // The native spread/bullet/bolt combination identifies pump shotguns,
    // including their Masterkey alternate weapons. Projectile launchers and
    // bolt rifles keep the existing trigger-operated charging interaction.
    m.grip_pump=bullet&&v.weapon_class==3&&v.bolt_action&&m.kind!=ReloadKind::break_action;
    if(bullet&&v.weapon_class==4&&v.segmented&&family(v.internal_name,"python"))
        m.receiver_offset={.05f,-.02f,-.065f};
    if(projectile&&v.weapon_class==0&&family(v.internal_name,"crossbow")){
        m.receiver_offset={0,.04f,-.18f};m.handle_offset={0,.04f,-.10f};
    }
    if(projectile&&v.weapon_class==5&&v.capacity==1&&v.internal_name.starts_with("gl_"))
        m.receiver_offset={0,-.10f,-.20f};
    // All ordinary magazines retain the accepted M16 receiver and charging
    // coordinates, including alternate attachment variants of the base rifle.
    out.status=Status::supported;return out;
}
} // namespace bovr::input::manual
