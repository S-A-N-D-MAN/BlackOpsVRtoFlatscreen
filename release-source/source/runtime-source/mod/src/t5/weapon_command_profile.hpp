// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/weapon_profile_admission.hpp"
#include "t5/command_probe.hpp"
#include <array>
#include <cstddef>

namespace bovr::t5::command_profile {
// Internal seam. Production must supply the actual pre-command snapshot,
// current thread/clock and protected copies after successful pinned-layout
// configuration. This API does not turn a caller-supplied boolean into a lease.
struct ReadHost {
    void* context{};
    bool (*copy)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
    bool (*command)(void*,PreCommandSnapshot*) noexcept{};
    DWORD (*thread)(void*) noexcept{};
    std::uint64_t (*now)(void*) noexcept{};
};
struct Request {
    CommandContext command{};
    std::uint64_t pre_sequence{}, gameplay_epoch{};
    bool olympia_enabled{};
};
enum class Status { accepted, arguments, command, context, storage, profile, changed, stale, exception };
enum class Rejection : unsigned {
    none,weapon_gate,model_count,bone_count,duplicate_storage,model_topology,total_bones,
    dual_model,static_ambiguous,string_entry,unterminated_name,tag_count,canonical,catalog,paired_dual
};
// Copied diagnostic values only. This never supplies admission or changes the
// reader's empty-on-failure Copy contract; all strings are bounded local copies.
struct Diagnostic {
    Rejection rejection{};
    unsigned weapon{},model_count{},bone_count{},tag_count{},selected_model{},selected_bone{};
    unsigned reading_model{},reading_bone{},arms_roots{},gun_roots{},canonical_status{},catalog_status{};
    std::array<char,256> arms_name{},gun_name{};
};
struct NativeValues {
    int native_time{}, refdef_time{}, native_frame_time{}, weapon_state{}, other_state{}, fire_type{}, weapon_class{};
    int weapon_type{};
    std::uint32_t stack_count{};
    bool dual_wield{};
    std::uint32_t dual_weapon{},dual_variant{},dual_definition{},dual_model_table{},dual_gun_model{};
    int dual_fire_type{},dual_weapon_class{};
    std::array<std::uint32_t,12> stock_view_bits{}; // origin, forward/left/up
    std::array<std::uint32_t,6> pose_bits{}; // persistent origin/angles, observation only
    std::uint32_t topdown{}, visibility{};
    friend bool operator==(const NativeValues&,const NativeValues&)=default;
};
struct Copy {
    input::profile_admission::Descriptor profile{};
    input::profile_admission::ObservedIdentity identity{};
    NativeValues native{};
    std::uint64_t pre_sequence{}, gameplay_epoch{}, started_ms{}, finished_ms{};
    int command_number{}, command_clock{};
    DWORD thread{};
    friend bool operator==(const Copy&,const Copy&)=default;
};
// Fresh protected current globals -> two complete exact-payload passes -> full
// storage/context tail -> same actual pre-command occurrence. No NativeWeapon-
// Context, draw argument, camera serial, lower native call or model lease.
// Output is zero on every failure. Caller owns FP/LastError preservation via
// the existing CreateCmd wrapper. No native addresses are ever written.
bool loaded_guards() noexcept;
Status read(const ReadHost&,const Request&,Copy*) noexcept;
Status read(const ReadHost&,const Request&,Copy*,Diagnostic*) noexcept;
bool protected_copy(void*,std::uintptr_t,void*,std::size_t) noexcept;
// Compare successive reads within one command without conflating observation
// timestamps with identity. Native state changes require a fresh occurrence.
bool same_current(const Copy&,const Copy&) noexcept;
} // namespace bovr::t5::command_profile
