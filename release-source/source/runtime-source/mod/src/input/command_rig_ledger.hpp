// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bovr::input {
using NativeCommandBytes=std::array<unsigned char,0x34>;
// Copies from the actual pre-command input sample. A caller must never replace
// this with a newer rendering sample when binding the completed native command.
struct CommandRigSample final {
    wawvr::xr::FrameState frame{};
    wawvr::xr::Posef tracking_anchor{};
    std::uint64_t xr_generation{},anchor_generation{},published_ms{};
};
struct ProducedRigCommand final {
    NativeCommandBytes bytes{};
    CommandRigSample rig{};
    std::uint64_t publication_serial{},gameplay_epoch{},observed_ms{};
    std::uint32_t native_thread{};
    int command_number{};
    bool gameplay_active{};
};
struct ConsumedCommandIdentity final {
    NativeCommandBytes bytes{};
    // Unique serial from the actual local ClientThink call, not gameTime or
    // serverTime. The latter may be repeated or clamped by native code.
    std::uint64_t scope_serial{},gameplay_epoch{},xr_generation{},observed_ms{};
    std::uint32_t native_thread{};
    bool local_active{};
};
enum class CommandRigStatus : std::uint8_t {
    accepted,invalid,stale,regressed,unavailable,ambiguous,replayed,capacity_exceeded,forgotten
};
struct CommandRigMatch final {
    ProducedRigCommand produced{};
    std::uint64_t scope_serial{},ledger_revision{};
    std::uint32_t server_thread{};
    bool valid{};
};
// Association only: it does not validate a weapon lifetime, barrel calibration,
// server body frame, aim transform or authority to replace native shot data.
// No pointer, native call, runtime call, clock read or lock escape is involved.
class CommandRigLedger final {
public:
    static constexpr std::size_t capacity=128;
    static constexpr std::uint64_t maximum_age_ms=100;
    CommandRigStatus publish(const ProducedRigCommand&,std::uint64_t now);
    CommandRigStatus consume(const ConsumedCommandIdentity&,std::uint64_t now,CommandRigMatch*);
    void end_scope(std::uint64_t scope_serial);
    bool current(const CommandRigMatch&,std::uint64_t epoch,std::uint64_t generation,std::uint64_t now) const;
    void clear();
private:
    struct Entry {ProducedRigCommand value{};bool present{},consumed{};};
    void clear_locked();
    mutable std::mutex mutex_;
    std::array<Entry,capacity> entries_{};
    std::size_t next_{};
    std::uint64_t revision_{1},epoch_{},generation_{},producer_serial_{},scope_serial_{},active_scope_{},last_observed_{};
    std::uint64_t blocked_until_{};
    // Native signed command serverTime at bytes0..3 supplies chronology,
    // independently of wall-clock observation/publication freshness. Forgotten
    // time and chronology survive ordinary clear in the same gameplay/XR pair.
    std::uint64_t identity_epoch_{},identity_generation_{};
    std::int32_t produced_time_{-1},consumed_time_{-1},forgotten_time_{-1};
    int last_command_{};
    std::uint32_t producer_thread_{};
};
}
