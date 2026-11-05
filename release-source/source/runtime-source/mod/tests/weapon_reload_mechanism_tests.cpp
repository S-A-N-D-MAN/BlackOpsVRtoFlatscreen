// SPDX-License-Identifier: GPL-3.0-only
#include "input/weapon_reload_mechanism.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
namespace m=bovr::input::manual;
using S=m::ReloadClassificationStatus;
using K=m::ReloadKind;
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
bool point(wawvr::xr::Vec3f a,wawvr::xr::Vec3f b){return std::fabs(a.x-b.x)<1.e-7f&&std::fabs(a.y-b.y)<1.e-7f&&std::fabs(a.z-b.z)<1.e-7f;}
struct InventoryRow {m::NativeWeaponReloadValues values;S expected;};
constexpr std::array<InventoryRow,15> inventory{{
#include "fixtures/synthetic_weapon_reload_inventory.inc"
}};
m::NativeWeaponReloadValues weapon(std::string_view name){
    for(const auto& row:inventory)if(row.values.internal_name==name)return row.values;
    check(false,"test weapon is in the authored boundary fixture");return {};
}
void classified(std::string_view name,K kind,unsigned insert,bool charge,bool cycle){
    const auto r=m::classify_native_reload(weapon(name));
    check(r.status==S::supported&&r.mechanism.valid,"representative native metadata supported");
    check(r.mechanism.kind==kind&&r.mechanism.rounds_per_insert==insert,"representative physical feed kind and increment");
    check(r.mechanism.charge_on_empty==charge&&r.mechanism.cycle_after_shot==cycle,"native bolt and charging semantics");
}
int main(){
    unsigned supported{},dual{},fuel{};std::array<unsigned,5> kinds{};
    // Authored metadata covers classifier boundaries independent of native asset presence.
    for(const auto& row:inventory){const auto r=m::classify_native_reload(row.values);
        check(r.status==row.expected,"all installed handheld candidate classifications have explicit outcomes");
        check(r.mechanism.valid==(r.status==S::supported),"rejected metadata never creates usable reload descriptor");
        if(r.status==S::supported){++supported;++kinds[static_cast<unsigned>(r.mechanism.kind)];
            check(r.mechanism.rounds_per_insert<=static_cast<unsigned>(row.values.capacity),"insertion cannot exceed native clip capacity");
            check(r.minimum_missing_rounds>0&&r.minimum_missing_rounds<=static_cast<unsigned>(row.values.capacity),"bounded native partial-reload eligibility");
            check(r.mechanism.grip_pump==(row.values.weapon_type==0&&row.values.weapon_class==3&&row.values.bolt_action&&r.mechanism.kind!=K::break_action),
                  "only native pump-action bullet shotguns receive grip-operated back-and-return control");
        }else if(r.status==S::dual_requires_separate_hands)++dual;
        else if(r.status==S::fuel_tank_requires_native_exchange)++fuel;
    }
    check(supported==13&&dual==1&&fuel==1,"authored rows partition ordinary, paired and timed-fuel routes");
    classified("authored_magazine",K::magazine,0,true,false);
    classified("authored_belt",K::belt,0,true,false);
    classified("python_authored",K::internal_rounds,1,false,false);
    classified("authored_tube",K::internal_rounds,1,false,false);
    classified("authored_pump",K::internal_rounds,1,true,true);
    classified("rottweil72_sp",K::break_action,2,false,false);
    classified("l96a1_authored",K::magazine,0,true,true);
    classified("mosin_scope_sp",K::internal_rounds,5,true,true);
    classified("authored_segmented_launcher",K::internal_rounds,1,true,true);
    classified("gl_authored",K::single_round,1,false,false);
    classified("authored_projectile_magazine",K::magazine,0,false,false);
    classified("crossbow_authored",K::single_round,1,true,false);
    classified("authored_gas",K::magazine,0,false,false);
    check(m::classify_native_reload(weapon("authored_pump")).mechanism.grip_pump,"authored pump shotgun selects support grip");
    for(const auto* name:{"authored_magazine","authored_tube","rottweil72_sp","l96a1_authored","mosin_scope_sp","crossbow_authored","authored_segmented_launcher"})
        check(!m::classify_native_reload(weapon(name)).mechanism.grip_pump,"non-pump mechanisms retain their charging control");
    const auto magazine=m::classify_native_reload(weapon("authored_magazine"));
    check(point(magazine.mechanism.receiver_offset,{0,-.08f,-.10f})&&point(magazine.mechanism.handle_offset,{0,.045f,-.035f}),"default physical coordinates remain stable");
    const auto gl=m::classify_native_reload(weapon("gl_authored"));
    check(!point(gl.mechanism.receiver_offset,magazine.mechanism.receiver_offset),"underbarrel mode gets its separate loading port");
    auto v=weapon("authored_magazine");v.internal_name="future_supported_rifle_sp";
    check(m::classify_native_reload(v).status==S::supported,"ordinary weapons do not require enumerated names");
    v.internal_name="rottweil72_sp";check(m::classify_native_reload(v).mechanism.kind==K::magazine,"name cannot turn a native rifle into break action");
    v=weapon("rottweil72_sp");v.internal_name="rottweil720_sp";
    check(m::classify_native_reload(v).mechanism.kind==K::magazine,"break-action exception requires exact known name");
    v=weapon("authored_magazine");v.no_partial=true;
    check(m::classify_native_reload(v).minimum_missing_rounds==30,"native no-partial magazine requires empty clip");
    v.reload_add=3;check(m::classify_native_reload(v).minimum_missing_rounds==3,"native no-partial increment requires room for full increment");
    v=weapon("authored_gas");check(m::classify_native_reload(v).minimum_missing_rounds==100,"authored no-partial full reload requirement retained");
    v=weapon("authored_tube");v.reload_add=0;v.start_add=0;
    check(m::classify_native_reload(v).mechanism.rounds_per_insert==1,"segmented zero-add metadata falls back to bounded individual round gesture");
    v=weapon("authored_magazine");v.capacity=4096;
    check(m::classify_native_reload(v).status==S::supported,"bounded carry capacity accepted");
    v.capacity=4097;check(m::classify_native_reload(v).status==S::invalid_values,"oversized native capacity rejected");
    v=weapon("authored_magazine");v.capacity=0;
    check(m::classify_native_reload(v).status==S::reloadless,"scripted zero-capacity hands cannot invent reload ammo");
    v.capacity=-1;check(m::classify_native_reload(v).status==S::invalid_values,"negative capacity rejected");
    for(int type:{1,3,5,6,7,8,-1}){v=weapon("authored_magazine");v.weapon_type=type;
        check(m::classify_native_reload(v).status==S::unsupported_type,"throwables and equipment need their separate input path");}
    for(int cls:{-1,5,6,7,8,9,10,11,12,13}){v=weapon("authored_magazine");v.weapon_class=cls;
        check(m::classify_native_reload(v).status==S::unsupported_class,"nonordinary bullet classes cannot acquire hand reload policy");}
    v=weapon("authored_projectile_magazine");v.weapon_class=7;check(m::classify_native_reload(v).status==S::unsupported_class,"projectile turret excluded");
    v=weapon("authored_gas");v.weapon_class=0;check(m::classify_native_reload(v).status==S::unsupported_class,"gas class mismatch rejected");
    for(int bad:{-1,4097}){v=weapon("authored_magazine");v.reload_add=bad;check(m::classify_native_reload(v).status==S::invalid_values,"invalid native insert count rejected");
        v=weapon("authored_magazine");v.start_add=bad;check(m::classify_native_reload(v).status==S::invalid_values,"invalid native initial insert count rejected");}
    for(int fire:{-1,7}){v=weapon("authored_magazine");v.fire_type=fire;check(m::classify_native_reload(v).status==S::invalid_values,"invalid fire enum rejected");}
    v=weapon("authored_magazine");std::string oversized(128,'x');v.internal_name=oversized;
    check(m::classify_native_reload(v).status==S::invalid_values,"bounded native name required");
    v.internal_name=std::string_view("m16\0_sp",7);check(m::classify_native_reload(v).status==S::invalid_values,"embedded terminator rejected");
    std::printf("%u reload mechanism checks passed; authored15 supported13 dual1 timed_fuel1; magazine=%u internal=%u break=%u single=%u belt=%u\n",checks,kinds[0],kinds[1],kinds[2],kinds[3],kinds[4]);
}

