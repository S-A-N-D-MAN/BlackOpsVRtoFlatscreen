// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
namespace bovr::t5 {
enum class PoseClearCaller:unsigned {other,camera,normal_weapon};
// The object is borrowed only during this synchronous callback. The observer
// must not retain it, and serial is an observation serial, never a generation.
struct PoseClearPoint {std::uintptr_t image_base{},caller_return{},object{};int local_client{};DWORD thread{};std::uint64_t serial{};PoseClearCaller caller{};};
using PoseClearCallback=void(*)(const PoseClearPoint&);
bool install_pose_clear_probe(HMODULE,PoseClearCallback) noexcept;
std::uint64_t pose_clear_serial() noexcept;
}
