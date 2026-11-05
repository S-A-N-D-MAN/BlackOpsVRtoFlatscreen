// SPDX-License-Identifier: GPL-3.0-only
#include "input/command_rig_ledger.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
namespace bovr::input {
namespace {
std::int32_t command_time(const NativeCommandBytes& bytes) noexcept {
    std::int32_t value{};std::memcpy(&value,bytes.data(),sizeof(value));return value;
}
bool fresh(std::uint64_t then,std::uint64_t now) noexcept {
    return then&&now>=then&&now-then<=CommandRigLedger::maximum_age_ms;
}
bool rig_fresh(const ProducedRigCommand& value,std::uint64_t now) noexcept {
    return fresh(value.observed_ms,now)&&fresh(value.rig.published_ms,now)&&
        value.rig.published_ms<=value.observed_ms;
}
bool valid(const ProducedRigCommand& value) noexcept {
    return value.publication_serial&&value.gameplay_epoch&&value.native_thread&&value.command_number>=0&&
        value.gameplay_active&&value.rig.xr_generation&&value.rig.anchor_generation&&
        value.rig.frame.frame_id&&value.rig.frame.actions.sequence&&value.rig.frame.actions.focused&&
        value.rig.frame.should_render&&value.rig.frame.views_valid;
}
}
void CommandRigLedger::clear_locked() {
    forgotten_time_=std::max(forgotten_time_,produced_time_);
    for(auto& entry:entries_) entry={};
    next_=0;epoch_=generation_=last_observed_=active_scope_=0;last_command_=0;producer_thread_=0;++revision_;
    // Process-wide dispatch serials survive reset to reject replay across maps.
}
void CommandRigLedger::clear() {std::lock_guard lock(mutex_);clear_locked();}
CommandRigStatus CommandRigLedger::publish(const ProducedRigCommand& value,std::uint64_t now) {
    std::lock_guard lock(mutex_);
    if(!valid(value)) {clear_locked();return CommandRigStatus::invalid;}
    if(!rig_fresh(value,now)) {clear_locked();return CommandRigStatus::stale;}
    const auto native_time=command_time(value.bytes);
    if(native_time<0) {clear_locked();return CommandRigStatus::invalid;}
    if(blocked_until_&&now<=blocked_until_) return CommandRigStatus::capacity_exceeded;
    if(value.publication_serial<=producer_serial_) {clear_locked();return CommandRigStatus::replayed;}
    producer_serial_=value.publication_serial;
    const bool changed=identity_epoch_!=value.gameplay_epoch||identity_generation_!=value.rig.xr_generation;
    if(changed) {
        clear_locked();identity_epoch_=value.gameplay_epoch;identity_generation_=value.rig.xr_generation;
        produced_time_=consumed_time_=forgotten_time_=-1;
    }
    if(native_time<produced_time_) {clear_locked();return CommandRigStatus::regressed;}
    if(epoch_&&(value.native_thread!=producer_thread_||value.command_number<last_command_||value.observed_ms<last_observed_)) {
        clear_locked();return CommandRigStatus::regressed;
    }
    // Never evict a still-fresh fingerprint and thereby turn an ambiguous
    // repeated command into an apparent unique match. Overflow suppresses
    // association until every record in that lost history must be stale.
    if(entries_[next_].present&&fresh(entries_[next_].value.observed_ms,now)) {
        const auto ceiling=std::numeric_limits<std::uint64_t>::max();
        blocked_until_=now>ceiling-maximum_age_ms?ceiling:now+maximum_age_ms;
        clear_locked();return CommandRigStatus::capacity_exceeded;
    }
    if(entries_[next_].present)
        forgotten_time_=std::max(forgotten_time_,command_time(entries_[next_].value.bytes));
    // Once native time has been forgotten, repeated full command bytes at or
    // below that time can no longer distinguish a delayed old consumption.
    if(native_time<=forgotten_time_) return CommandRigStatus::forgotten;
    epoch_=value.gameplay_epoch;generation_=value.rig.xr_generation;producer_thread_=value.native_thread;
    last_command_=value.command_number;last_observed_=value.observed_ms;
    produced_time_=native_time;
    entries_[next_]={value,true,false};next_=(next_+1)%capacity;
    return CommandRigStatus::accepted;
}
CommandRigStatus CommandRigLedger::consume(const ConsumedCommandIdentity& value,std::uint64_t now,CommandRigMatch* output) {
    if(output) *output={};
    std::lock_guard lock(mutex_);
    active_scope_=0;
    if(!output||!value.scope_serial||!value.gameplay_epoch||!value.xr_generation||!value.native_thread||!value.local_active)
        return CommandRigStatus::invalid;
    if(value.scope_serial<=scope_serial_) return CommandRigStatus::replayed;
    scope_serial_=value.scope_serial;
    if(!fresh(value.observed_ms,now)) return CommandRigStatus::stale;
    if(value.gameplay_epoch!=epoch_||value.xr_generation!=generation_) return CommandRigStatus::unavailable;
    const auto native_time=command_time(value.bytes);
    if(native_time<0) {clear_locked();return CommandRigStatus::invalid;}
    if(native_time<consumed_time_) {clear_locked();return CommandRigStatus::regressed;}
    consumed_time_=native_time;
    if(native_time<=forgotten_time_) return CommandRigStatus::forgotten;
    Entry* match{};unsigned count{};
    for(auto& entry:entries_) {
        if(!entry.present||entry.value.bytes!=value.bytes) continue;
        // Count all retained exact matches, including consumed/stale ones. A
        // repeated fingerprint cannot silently acquire a different newer rig.
        match=&entry;++count;
    }
    if(count>1) return CommandRigStatus::ambiguous;
    if(!match) return CommandRigStatus::unavailable;
    if(match->consumed) return CommandRigStatus::replayed;
    if(!rig_fresh(match->value,now)||match->value.observed_ms>value.observed_ms) return CommandRigStatus::stale;
    match->consumed=true;
    active_scope_=value.scope_serial;
    *output={match->value,value.scope_serial,revision_,value.native_thread,true};
    return CommandRigStatus::accepted;
}
void CommandRigLedger::end_scope(std::uint64_t scope_serial) {
    std::lock_guard lock(mutex_);
    if(active_scope_==scope_serial) active_scope_=0;
}
bool CommandRigLedger::current(const CommandRigMatch& value,std::uint64_t epoch,std::uint64_t generation,std::uint64_t now) const {
    std::lock_guard lock(mutex_);
    return value.valid&&value.ledger_revision==revision_&&value.scope_serial==active_scope_&&value.server_thread&&
        value.produced.gameplay_epoch==epoch&&epoch==epoch_&&value.produced.rig.xr_generation==generation&&generation==generation_&&
        command_time(value.produced.bytes)>forgotten_time_&&
        rig_fresh(value.produced,now);
}
}
