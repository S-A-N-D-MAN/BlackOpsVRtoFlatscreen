// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <Windows.h>
namespace bovr::t5 {
struct OrdinaryExposureOwner {void* context{};bool(*current)(void*) noexcept{};};
bool configure_ordinary_exposure_scope(HMODULE) noexcept;
bool ordinary_exposure_enabled() noexcept;
// Replay only the accumulated exposure before the second native camera. Each
// native camera retains its own lighting result; commit one temporal step.
class OrdinaryExposureScope {
public:
    ~OrdinaryExposureScope(){finish();}
    bool begin(std::uintptr_t base,std::uintptr_t buffer,std::uint32_t frame,
               std::uintptr_t refdef,std::uint64_t producer,OrdinaryExposureOwner) noexcept;
    bool prepare_right() noexcept;
    void after_native(unsigned eye,bool completed) noexcept;
    bool finish() noexcept;
    bool safe() const noexcept{return good_&&!pending_;}
private:
    bool identity(int expected_count=-1) const noexcept;
    bool read_state(std::array<float,8>*) const noexcept;
    bool commit_left() noexcept;
    void report() noexcept;
    std::uintptr_t base_{},buffer_{},refdef_{},cg_{},views_{};
    std::uint64_t producer_{};std::uint32_t frame_{};DWORD thread_{};
    int time_{},frame_time_{},refdef_time_{};
    OrdinaryExposureOwner owner_{};
    std::array<float,8> before_{},left_{},right_{};
    std::array<float,2> view_exposure_{};
    bool enabled_{},active_{},have_left_{},prepared_{},right_done_{},pending_{},good_{true},reported_{};
    const char* reason_="none";
};
// Normal and exceptional native exits share the same small journal cleanup.
// Native body is called exactly once; no gameplay time or camera is changed.
void invoke_ordinary_exposure_native(OrdinaryExposureScope*,unsigned eye,void(*)(void*),void*);
}
