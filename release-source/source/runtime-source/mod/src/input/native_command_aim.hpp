// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/command_rig_ledger.hpp"
#include "input/two_hand_aim.hpp"
namespace bovr::input {
enum class CommandAimStatus : unsigned {accepted,identity,stale,prediction,tracking,body,pole,context,ownership,changed,storage};
struct CommandAimProposal {
    wawvr::xr::Basis3f body{};
    wawvr::xr::Vec3f forward{};
    float pitch_degrees{},yaw_degrees{};
    std::uint32_t packed{};
    SupportAimBasis support{};
};
// Pure values: no native call, clock, allocation or scene/shot authority.
CommandAimStatus propose_native_command_aim(const CommandRigSample&,
    const wawvr::xr::ActionSnapshot&,const wawvr::xr::Basis3f&,wawvr::xr::Hand,
    std::uint64_t now,CommandAimProposal*,bool two_hand_support=false) noexcept;
bool pack_native_gun_angles(float pitch,float yaw,std::uint32_t*) noexcept;
}
