// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_episode.hpp"
namespace bovr::t5 {
enum class BloomEpisodeReject : unsigned { none, planned_record_cap, argument_shape, argument_source, argument_code, custom_samplers };
struct BloomEpisodeRejection {
    BloomEpisodeReject reason{};std::uint32_t buffer{},native_frame{},eye{},role{},material{},pass{},argument_index{},planned_draws{};
    BloomArgument argument{};
};
bool validate_bloom_episode_guards(HMODULE) noexcept;
bool read_bloom_episode_shader(std::uintptr_t,const AuxiliaryViewLayout&,const BloomEpisodeCursor&,
    BloomDrawSnapshot*,BloomEpisodePlan*,BloomReadStatus* =nullptr,BloomEpisodeRejection* =nullptr) noexcept;
}
