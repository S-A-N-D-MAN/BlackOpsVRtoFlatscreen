// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/ui_producer_probe.hpp"
#include "t5/final_ui_probe.hpp"
#include <array>
namespace bovr::t5 {
struct UiProductionStamp {
    NativeUiProduction begin{},end{};
    std::uint64_t revision{},epoch{};
    bool valid{};
};
// Caller serializes all operations with its metadata mutex. Native addresses
// identify the engine-owned double buffers; this value helper dereferences none.
class UiProductionEpoch final {
public:
    explicit UiProductionEpoch(std::uintptr_t first_buffer):first_buffer_(first_buffer){}
    void begin(const NativeUiProduction& value,std::uint64_t revision) noexcept {
        const int slot=index(value.frontend);if(slot<0)return;
        entries_[slot]={};
        if(!value.valid||!value.call_serial||!revision)return;
        entries_[slot].begin=value;entries_[slot].revision=revision;
    }
    bool complete(const NativeUiProduction& before,const NativeUiProduction& after,std::uint64_t revision) noexcept {
        const int slot=index(before.frontend);if(slot<0)return false;
        auto& entry=entries_[slot];entry.valid=false;
        if(!after.completed_range||!same_ui_production_range(before,after)||entry.begin.call_serial!=before.call_serial||
            entry.begin.thread_id!=before.thread_id||entry.revision!=revision||!revision||
            before.connection_state!=after.connection_state||before.key_catchers!=after.key_catchers||
            before.menu_count!=after.menu_count||before.page!=after.page)return false;
        entry.end=after;entry.epoch=++epoch_;entry.valid=true;return true;
    }
    bool match(const NativeFinalUi& backend,std::uint64_t revision,UiProductionStamp* output) const noexcept {
        if(output)*output={};const int slot=index(backend.backend);if(slot<0||!output)return false;
        const auto& entry=entries_[slot];const auto& produced=entry.end;
        if(!entry.valid||!backend.valid||!entry.epoch||entry.revision!=revision||
            produced.frontend!=backend.backend||produced.native_frame!=backend.native_frame||produced.commands!=backend.commands||
            produced.connection_state!=backend.connection_state||produced.key_catchers!=backend.key_catchers||
            produced.menu_count!=backend.menu_count||produced.page!=backend.page)return false;
        *output=entry;return true;
    }
    void clear() noexcept {entries_={};} // Epoch stays monotonic across reset.
private:
    int index(std::uintptr_t buffer) const noexcept {
        return buffer==first_buffer_?0:buffer==first_buffer_+0x186300?1:-1;
    }
    std::uintptr_t first_buffer_{};
    std::array<UiProductionStamp,2> entries_{};
    std::uint64_t epoch_{};
};
}
