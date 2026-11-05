// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/weapon_profile_reader.hpp"
namespace bovr::t5::profile_detail {
struct RecordOutput {
    bool complete{};
    std::string selected_asset_sha256,ordered_models_sha256;
    std::wstring json_path;
};
// Existing directory only. Every file uses CREATE_NEW. JSON is created last as
// the completion marker; partial I/O failure is reported, never overwritten.
RecordOutput write_record(const std::wstring&,DWORD,std::uint64_t,const ProfileRecord&) noexcept;
bool protected_profile_copy(void*,std::uintptr_t,void*,std::size_t) noexcept;
}
