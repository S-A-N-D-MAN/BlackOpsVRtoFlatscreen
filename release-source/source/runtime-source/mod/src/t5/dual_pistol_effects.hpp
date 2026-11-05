// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "t5/bullet_probe.hpp"
namespace bovr::t5::dual_pistol_effects {
// Exact DrawBulletImpacts local branch at its AngleVectors CALL. Other callers
// and the shared zombietron branch must retain the original native results.
struct CallLayout {
    std::uintptr_t entry{},return_address{},angles{},forward{},right{},up{};
};
inline bool regular_local_layout(const CallLayout& c,std::uintptr_t low,std::uintptr_t high) noexcept {
    if(low>high||high-low<0x190||c.entry<low||c.entry>high-0x190)return false;
    const auto frame=c.entry+20;
    return c.return_address==0x799c3a&&c.angles==frame+0x80&&c.forward==frame+0x4c&&
        c.right==frame+0x58&&c.up==frame+0x64;
}
// Events choose the actual firing pistol, not the currently held trigger or
// latest rendering pose. A left event names the validated partner weapon.
inline bool event_weapon(unsigned event,unsigned event_weapon,unsigned primary,unsigned partner) noexcept {
    if(!primary||primary>=256||!partner||partner>=256||primary==partner)return false;
    return ((event==29||event==30)&&event_weapon==primary)||
        ((event==31||event==32)&&event_weapon==partner);
}
// Explicit diagnostic/candidate selection; absent or unknown values preserve
// the released ordinary-weapon behavior. The existing dual policy is separate.
enum class OrdinaryMode : unsigned { disabled, observe, correct };
inline OrdinaryMode ordinary_mode() noexcept {
    wchar_t text[16]{};const auto n=GetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",text,16);
    if(n==7&&!wcscmp(text,L"observe"))return OrdinaryMode::observe;
    if(n==7&&!wcscmp(text,L"correct"))return OrdinaryMode::correct;
    return OrdinaryMode::disabled;
}
inline bool ordinary_event(unsigned event,unsigned event_weapon,unsigned primary) noexcept {
    return primary>0&&primary<256&&event_weapon==primary&&(event==29||event==30);
}
inline bool ordinary_weapon(int type,int kind,int fire_type,unsigned dual) noexcept {
    // Stacked weapons apply a native per-projectile angular fan before this
    // seam. Preserve that special policy instead of flattening its spread.
    return type==0&&kind>=0&&kind<=4&&dual==0&&fire_type>=0&&fire_type<=6&&fire_type!=5;
}
// Decode the exact gun words delivered to native command processing, including
// its quantization. Preserve native eye origin; only the F/R/U basis changes.
inline bool command_geometry(std::uint32_t packed,const NativeBulletGeometry& original,
                             NativeBulletGeometry* out) noexcept {
    if(!out)return false;
    for(float v:original.muzzle)if(!std::isfinite(v))return false;
    constexpr float radians_per_word=6.2831853071795864769f/65536.f;
    const float pitch=static_cast<std::int16_t>(packed&65535u)*radians_per_word;
    const float yaw=static_cast<std::int16_t>(packed>>16)*radians_per_word;
    const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    auto value=original;
    value.forward[0]=cp*cy;value.forward[1]=cp*sy;value.forward[2]=-sp;
    value.right[0]=sy;value.right[1]=-cy;value.right[2]=0;
    value.up[0]=sp*cy;value.up[1]=sp*sy;value.up[2]=cp;
    *out=value;return true;
}
inline bool apply_basis(OrdinaryMode mode,bool dual,const NativeBulletGeometry& selected,
                        float* forward,float* right,float* up) noexcept {
    if((!dual&&mode!=OrdinaryMode::correct)||!forward||!right||!up)return false;
    memcpy(forward,selected.forward,12);memcpy(right,selected.right,12);memcpy(up,selected.up,12);
    return true;
}
inline bool apply_command_scratch(OrdinaryMode mode,bool dual,const NativeBulletGeometry& selected,
                                  const unsigned char* original,unsigned char* scratch) noexcept {
    if((!dual&&mode!=OrdinaryMode::correct)||!original||!scratch)return false;
    memcpy(scratch,original,68);memcpy(scratch,&selected,36);
    return true;
}
}
namespace bovr::t5 {
// Prepare performs identity/hash work before the coordinator suspends peers.
// Commit only rechecks copied bytes and changes the reviewed call instruction.
bool prepare_dual_pistol_effects(HMODULE executable) noexcept;
bool commit_dual_pistol_effects() noexcept;
bool install_dual_pistol_effects(HMODULE executable) noexcept;
}
