// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include <array>
#include <atomic>
#include <cstddef>

namespace bovr::t5::mounted_look {
// Startup-owned immutable callbacks. Their context must outlive Source and all
// reads. Production uses the existing pre-command publication, not XR polling.
struct Host {
    void* context{};
    bool (*copy)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
    bool (*command)(void*,PreCommandSnapshot*) noexcept{};
    DWORD (*thread)(void*) noexcept{};
    std::uint64_t (*now)(void*) noexcept{};
};
struct Request {CommandContext command{};std::uint64_t pre_sequence{},gameplay_epoch{};};
enum class Status {accepted,disabled,guards,arguments,owner,busy,command,storage,ineligible,changed,stale,exception};
struct CommandKey {
    int local{},number{},catcher{},connection{},clock{};
    DWORD thread{};
    bool override_active{},valid{};
    std::array<std::uint32_t,6> angle_bits{}; // view then kick, raw identity
    friend bool operator==(const CommandKey&,const CommandKey&)=default;
};
struct PreKey {
    CommandKey command{};
    std::uint64_t sequence{},observed_ms{},epoch{};
    std::uintptr_t cg{};
    int game_time{},health{},pm_type{};
    std::uint32_t pm_flags{},weapon_flags{},entity_flags{},entity_flags2{};
    bool fields_valid{},ordinary_active{};
    friend bool operator==(const PreKey&,const PreKey&)=default;
};
struct Dvar {
    std::uint32_t address{},name_address{},type{},value{};
    std::array<char,32> name{}; // exact observed name including NUL, zero tail
    friend bool operator==(const Dvar&,const Dvar&)=default;
};
// The Khe Sanh TOW hides the ordinary player viewmodel while the local player
// owns a scoped turret. Copy its real turret weapon; ps.weapon is a knife.
struct ScopedTow {
    std::uint32_t client_entities{},client_entity{},server_entity{},variant{},definition{},name{},overlay{};
    std::uint32_t number{},client_number{};
    std::uint16_t type{};
    std::uint8_t pose_type{},pose_local{},inuse{},weapon{},client_weapon{};
    int weapon_type{},weapon_class{};
    std::array<char,29> internal_name{};
    friend bool operator==(const ScopedTow&,const ScopedTow&)=default;
};
struct Facts {
    std::uint32_t cg{},ps{};
    int game_time{},health{},pm_type{};
    std::uint8_t local{};
    std::uint32_t pm_flags{},weapon_flags{},entity_flags{},entity_flags2{},viewlock{};
    std::int16_t entity{};
    // cg offsets: 8A360, C, A9D44, 1C, 14, A4780, A4680.
    std::array<int,7> camera{};
    ScopedTow scoped_tow{};
    int catcher{},connection{},override_value{},command_number{},command_clock{};
    std::array<std::uint32_t,3> view_bits{},delta_bits{};
    std::array<std::uint32_t,4> clamp_bits{}; // base pitch/yaw, range pitch/yaw
    std::array<Dvar,4> dvars{}; // topdown, forced viewlock, pitch up/down
    friend bool operator==(const Facts&,const Facts&)=default;
};
bool copy_scoped_tow(const Host&,std::int16_t entity,ScopedTow*) noexcept;
bool scoped_tow_eligible(const ScopedTow&,std::int16_t entity) noexcept;
struct Copy {
    PreKey pre{};
    Facts facts{};
    std::uintptr_t source_identity{};
    std::uint64_t revision{},started_ms{},finished_ms{};
    friend bool operator==(const Copy&,const Copy&)=default;
};
bool eligible(const Facts&) noexcept;
bool same_current(const Copy&,const Copy&) noexcept;
bool protected_copy(void*,std::uintptr_t,void*,std::size_t) noexcept;
Host production_host() noexcept;

class Source {
public:
    Source() noexcept=default; // no native reads; disabled
    Source(const Source&)=delete;
    Source& operator=(const Source&)=delete;
    Status configuration() const noexcept {return configuration_;}
    Status read(const Request&,Copy*) noexcept;
    // Atomic-only invalidation; never touches owner callbacks or copied storage.
    void revoke() noexcept;
    bool current(std::uint64_t revision) const noexcept;
private:
    friend Source make_source(bool,const Host&) noexcept;
    explicit Source(bool,const Host&) noexcept;
    Status body(const Request&,Copy*,bool&) noexcept;
    Host host_{};
    DWORD owner_{};
    Status configuration_{Status::disabled};
    std::atomic_flag running_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> revision_{1};
};
// Only explicit request + complete pinned loaded-byte guards can construct an
// enabled source. Factory is called at owner startup, outside callback traffic.
Source make_source(bool requested,const Host& host=production_host()) noexcept;
// Read-only observations, not a rearm policy or permission to dispatch. The
// existing CreateCmd wrapper retains native FP/LastError/52-byte ownership.
} // namespace bovr::t5::mounted_look
