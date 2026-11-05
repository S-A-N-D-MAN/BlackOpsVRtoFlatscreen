// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace bovr::native_resolution::guards {
inline constexpr std::uintptr_t monitor_dimensions_address=0x6b6850;
inline constexpr std::size_t monitor_dimensions_size=116;
inline constexpr char monitor_dimensions_sha256[]="08d5ef284653a42caf2d5f60c7acb776513f2bf837d23855d83abc28049915de";
inline constexpr std::uintptr_t custom_resolution_address=0x6b7be0;
inline constexpr std::size_t custom_resolution_size=96;
inline constexpr char custom_resolution_sha256[]="e4bb74338b5ba6c7161854db6c7ab6a37278f43d582faeb9c5d8161fc5ada090";
inline constexpr std::uintptr_t window_parameters_address=0x6b7c40;
inline constexpr std::size_t window_parameters_size=282;
inline constexpr char window_parameters_sha256[]="bef7b9248a01e6aba7ee6f10c5724e88815e9fbbd680b0ef5c1614ad084e26fa";
inline constexpr std::uintptr_t device_native_cache_address=0x6b7560;
inline constexpr std::size_t device_native_cache_size=30;
inline constexpr char device_native_cache_sha256[]="fd1c20577def7b1fdc445e99f63dc3ed9a77478710230f61169795aa4a731481";
}
