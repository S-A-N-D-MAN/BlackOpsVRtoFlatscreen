// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_values.hpp"
namespace bovr::t5 {
bool validate_bloom_shader_guards(HMODULE) noexcept;
bool read_bloom_shader(std::uintptr_t,const AuxiliaryViewLayout&,BloomDrawSnapshot*,BloomReadStatus* =nullptr) noexcept;
}
