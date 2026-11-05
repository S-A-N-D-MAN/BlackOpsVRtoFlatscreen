// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bovr::t5 {
inline constexpr std::uint64_t kSpFileSize = 8101944;
inline constexpr std::uint64_t kSpImageFileSize = 8096256;
inline constexpr std::uint64_t kSpMaximumCertificateSize = 131072;
inline constexpr char kSpSha256[] = "db00603ddea3ee201c5870c7bb27e27fc569528839e92a3f78689c256dd73ff3";
struct Validation {
    bool accepted{};
    std::string sha256;
    std::string reason;
};
std::vector<std::uint8_t> read_file(const std::wstring& path);
std::string sha256(std::span<const std::uint8_t> bytes);
Validation validate_file(std::span<const std::uint8_t> bytes);
Validation validate_path(const std::wstring& path);
bool validate_loaded_headers(HMODULE module, std::string& reason);
std::wstring executable_path();
}
