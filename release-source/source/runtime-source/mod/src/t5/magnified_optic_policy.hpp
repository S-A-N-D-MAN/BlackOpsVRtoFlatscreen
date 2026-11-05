// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/crossbow_policy.hpp"
#include <string_view>

namespace bovr::t5::magnified_optic {
enum class Kind { none, crossbow, compact, telescopic, thermal };
inline bool lens_material(std::string_view name) noexcept {
    // Most T5 optics label their glass "lens". The scoped Mosin instead uses
    // mtl_rus_scope_glass; neither its metal scope body nor arbitrary glass is a lens.
    return name.find("lens")!=name.npos||
        (name.find("scope")!=name.npos&&name.find("glass")!=name.npos);
}
inline bool family(std::string_view name,std::string_view prefix) noexcept {
    return name==prefix||(name.starts_with(prefix)&&name.size()>prefix.size()&&name[prefix.size()]=='_');
}
// Native type/class/capacity are required in addition to the variant's optic
// metadata. Never classify guided launchers, vehicle guns or optical equipment.
inline Kind classify(int type,int weapon_class,int capacity,std::string_view name,
                     std::string_view overlay={}) noexcept {
    if(crossbow::weapon(type,weapon_class,capacity,name))return Kind::crossbow;
    if(name=="berlin_listening_device")return Kind::none;
    if(type!=0||weapon_class<0||weapon_class>4||weapon_class==3||capacity<=0||capacity>300)return Kind::none;
    if(name.find("_ir_")!=name.npos||overlay.find("infrared")!=overlay.npos)return Kind::thermal;
    if(name.find("_acog_")!=name.npos||name.find("_lps_")!=name.npos)return Kind::compact;
    if(overlay.starts_with("scope_")||name.find("_vzoom_")!=name.npos||
       family(name,"dragunov")||family(name,"l96a1")||family(name,"psg1")||
       family(name,"wa2000")||family(name,"mosin_scope"))return Kind::telescopic;
    return Kind::none;
}
}
