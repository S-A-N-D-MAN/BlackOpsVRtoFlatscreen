// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bovr::input {
inline constexpr std::uint32_t profile_identity_schema=1;
inline constexpr unsigned profile_max_models=32,profile_max_bones=160,profile_max_name_bytes=255;
inline constexpr std::uint64_t profile_max_copy_age_ms=100;
inline constexpr std::size_t profile_max_payload_bytes=65536;
inline constexpr char profile_sp_image_sha256[]="db00603ddea3ee201c5870c7bb27e27fc569528839e92a3f78689c256dd73ff3";

// Source pointers never appear in this interface. The native reader must copy
// and bound data inside its verified borrow before constructing these values.
struct ProfileTextCopy {
    std::string bytes;
    bool readable{},terminated{};
    friend bool operator==(const ProfileTextCopy&,const ProfileTextCopy&)=default;
};
struct ProfileBoneCopy {
    ProfileTextCopy name;
    std::uint16_t script_id{},observed_ref_count{};
    std::uint16_t parent_delta{}; // 0 for roots; positive ancestor delta otherwise.
    // Quaternion XYZW, translation XYZ, then native transWeight. Exact LE bits.
    std::array<std::uint32_t,8> bind_bits{};
};
struct ProfileModelCopy {
    ProfileTextCopy name;
    std::uint16_t root_count{};
    std::vector<ProfileBoneCopy> bones;
};
struct ProfileCopyStamp {
    std::uint64_t borrow_serial{},native_epoch{},observed_ms{};
    std::uint32_t native_thread{};
};
struct ProfileCopyRechecks {
    bool current_native_scope{},complete_bounded_reads{},owner_equal{},pool_equal{};
    bool model_storage_equal{},server_string_arena_equal{};
};
struct ProfileObservationCopies {
    std::string image_sha256;
    ProfileCopyStamp before_stamp{},after_stamp{};
    ProfileCopyRechecks rechecks{};
    std::vector<ProfileModelCopy> before,after;
    std::uint16_t selected_model{},selected_local_bone{};
};
enum class ProfileIdentityStatus {
    accepted,invalid_argument,unsupported_layout,unproved_borrow,stale_scope,
    count_bound,invalid_text,invalid_string_entry,invalid_topology,
    invalid_bind_geometry,changed_copy,ambiguous_barrel,payload_bound,allocation_failure,canonical_mismatch,
};
struct CanonicalProfileIdentity {
    std::uint32_t schema{};
    std::uint16_t selected_model{},selected_local_bone{};
    std::vector<std::vector<std::uint8_t>> model_payloads;
    std::vector<std::uint8_t> assembly_payload;
};
// Pure copied-value operation. No clock, process, native function, hashing,
// pointer, or global state access. Empty output on every failure.
// A successful result is identity data, never a native-lifetime/write lease.
ProfileIdentityStatus canonical_profile_identity(const ProfileObservationCopies&,
    std::uint32_t current_thread,std::uint64_t current_native_epoch,
    std::uint64_t now_ms,CanonicalProfileIdentity*,bool optimized=true) noexcept;
// Independently validates all copied evidence and compares the exact v1 byte
// stream with an existing owned identity without allocating a second identity.
// This does not extend the native reader's borrow or authorize native writes.
ProfileIdentityStatus verify_canonical_profile_identity(const ProfileObservationCopies&,
    std::uint32_t current_thread,std::uint64_t current_native_epoch,
    std::uint64_t now_ms,const CanonicalProfileIdentity&,bool optimized=true) noexcept;
} // namespace bovr::input
