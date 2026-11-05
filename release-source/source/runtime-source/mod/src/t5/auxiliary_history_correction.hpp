// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/auxiliary_history.hpp"
#include <memory>
#include <atomic>
namespace bovr::t5 {
struct HistoryCustomPass {
    std::array<std::uint8_t,48> enabled{};
    std::array<std::uint32_t,48> material{};
    friend bool operator==(const HistoryCustomPass&,const HistoryCustomPass&)=default;
};
struct HistoryCorrectionSnapshot {
    AuxiliaryHistorySnapshot native{};
    std::array<HistoryCustomPass,2> custom{};
    std::uint32_t forced_dvar{},texture{};
    std::uint8_t forced{};
    friend bool operator==(const HistoryCorrectionSnapshot&,const HistoryCorrectionSnapshot&)=default;
};
inline bool custom_history_path(const HistoryCustomPass& value) noexcept {
    for(unsigned i=0;i<48;++i)if(value.enabled[i]&&value.material[i])return true;
    return false;
}
bool read_history_correction(std::uintptr_t,const AuxiliaryViewLayout&,unsigned,HistoryCorrectionSnapshot*) noexcept;
bool validate_history_correction_guards(HMODULE) noexcept;
// Scalar methods and fault retirement perform no COM work. All owned resources
// remain with this renderer owner until normal completion/owner retirement.
class AuxiliaryHistoryCorrection {
public:
    AuxiliaryHistoryCorrection() noexcept;
    ~AuxiliaryHistoryCorrection();
    AuxiliaryHistoryCorrection(const AuxiliaryHistoryCorrection&)=delete;
    AuxiliaryHistoryCorrection& operator=(const AuxiliaryHistoryCorrection&)=delete;
    void begin(bool,std::uintptr_t,const AuxiliaryViewLayout&,const AuxiliaryPostfxKey&,
               std::uint64_t backend_serial,std::uint64_t reset_epoch,IDirect3DDevice9*,HistoryCurrent) noexcept;
    void before_native(unsigned eye) noexcept;
    void after_native(unsigned eye) noexcept;
    // Returns false only for a transaction which may have changed native data
    // and must invalidate this exact ordinary staged pair.
    bool finish(bool ordinary_staging_complete) noexcept;
    void native_fault() noexcept;
    void retire(std::uint64_t reset_epoch=0) noexcept;
    bool selected() const noexcept;
private:struct State;std::unique_ptr<State> state_;bool aborted_invocation_{};
};
// Native original is deliberately outside C++/SEH catch containment. Finally
// only restores mod-owned scalar/index state and defers resource cleanup.
void invoke_history_original(void(__cdecl*)(),AuxiliaryHistoryCorrection*,AuxiliaryHistoryCapture*,
                             bool* native_fault,std::atomic<bool>* stop,std::uint32_t* index,std::uint32_t original_index);
void restore_history_index(std::uint32_t*,std::uint32_t) noexcept;
}
