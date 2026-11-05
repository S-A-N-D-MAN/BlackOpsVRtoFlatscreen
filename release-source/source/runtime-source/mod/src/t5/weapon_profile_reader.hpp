// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/profile_identity.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/command_probe.hpp"
#include <cstddef>

namespace bovr::t5::profile_detail {
// Internal reader/test seam. No native function invocation, allocation lease,
// retained native address, or writer authority is exposed by ProfileRecord.
struct ReadHost {
    void* context{};
    bool (*copy)(void*,std::uintptr_t,void*,std::size_t){};
    std::uint64_t (*now)(void*){};
    bool (*command)(void*,PreCommandSnapshot*){};
    bool optimized_identity{true};
};
struct ReadWorkCounts {unsigned canonical_builds{};};
enum class ReadStatus { accepted,context,command,read_failed,count,changed,assembly,canonical,exception };
struct AssemblyDiagnostic {
    // Separate diagnostic schema, explicitly excluded from v1 canonical bytes.
    std::vector<std::uint8_t> model_parents,duplicate_data;
    std::uint16_t total_bones{};
    friend bool operator==(const AssemblyDiagnostic&,const AssemblyDiagnostic&)=default;
};
struct ProfileRecord {
    input::ProfileObservationCopies copies;
    input::CanonicalProfileIdentity canonical;
    AssemblyDiagnostic assembly;
    input::ProfileIdentityStatus identity_status{input::ProfileIdentityStatus::invalid_argument};
    std::uint64_t command_sequence{};
    int native_time{},command_number{},command_clock{};
    std::uint32_t weapon_index{};
};
bool supported_context(const NativeWeaponContext&,DWORD) noexcept;
bool valid_assembly(const AssemblyDiagnostic&,const std::vector<input::ProfileModelCopy>&) noexcept;
ReadStatus read_profile(const ReadHost&,const NativeWeaponContext&,DWORD,std::uint64_t,ProfileRecord*,ReadWorkCounts* counts=nullptr) noexcept;
} // namespace bovr::t5::profile_detail
