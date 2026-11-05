// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include "input/action_router.hpp"
#include <cstdint>

namespace bovr::t5 {
struct NativeMenuPage final {
    std::uintptr_t focused_menu{};
    int stack_count{},menu_type{};
    friend bool operator==(const NativeMenuPage&,const NativeMenuPage&)=default;
};
struct MenuPointerVisual final {
    bool visible{};
    float u{},v{};
    std::uint64_t generation{},sample_sequence{},submission_serial{};
};
// Pure bounded reads equivalent to the verified native focused-menu stack scan.
// No UI function is invoked, so the renderer can sample a page identity safely.
bool read_native_menu_page(std::uintptr_t image_base,NativeMenuPage*) noexcept;
// Thread-safe copied pointer state for the quad compositor. A true return means
// the current tracked controller ray intersects the last interactive menu panel.
bool read_menu_pointer_visual(MenuPointerVisual*) noexcept;
// Disabled by default. The caller must have installed the exact, guarded native
// event-loop seam and call service only AFTER its original event drain returns.
bool configure_menu_input_adapter(HMODULE executable, bool event_hook_installed) noexcept;
void service_menu_input_after_events() noexcept;

namespace menu_input_detail {
// Separate value/ABI boundary for machine tests; no native addresses are accepted
// by the public configuration path unless the pinned retail identity matches.
using NativeKeyEvent = void(__cdecl*)(int local_client,int key,int down,unsigned time);
using NativeMouseEvent = void(__cdecl*)(int local_client,int absolute_x,int absolute_y);
constexpr int key_enter=13,key_escape=27,key_up=154,key_down=155,key_mouse1=200;
enum class Intent : std::uint8_t { none,accept,back,up,down,pause };
struct Context final {
    bool valid{}, menu_exists{}, native_capture{}, panel_current{};
    int connection_state{},key_catchers{};
};
class Policy final {
public:
    explicit Policy(bool left_handed=false):router_(left_handed){}
    Intent update(const wawvr::xr::ActionSnapshot&,bool live,std::uint64_t generation,
                  const Context&,std::uint64_t now_ms) noexcept;
    void invalidate() noexcept;
private:
    input::ActionRouter router_;
    std::uint64_t generation_{},sequence_{};
    bool generation_seen_{},sequence_seen_{};
};
struct PointerSurface final {
    wawvr::xr::QuadLayer panel{};
    std::uint64_t submission_serial{};
    std::uint32_t source_width{},source_height{};
};
struct PointerUpdate final {
    bool hit{},move{},click{};
    float u{},v{};
    int absolute_x{},absolute_y{};
};
// Pure right-controller (or configured dominant-controller) pointer policy.
// It targets only the exact finite menu surface that reached the headset and
// requires a physical trigger release after every lifecycle/source reset.
class PointerPolicy final {
public:
    explicit PointerPolicy(bool left_handed=false):left_handed_(left_handed){}
    PointerUpdate update(const wawvr::xr::ActionSnapshot&,bool live,std::uint64_t generation,
                         const Context&,const PointerSurface&) noexcept;
    void invalidate() noexcept;
private:
    enum class TriggerSource : std::uint8_t { none,boolean_click,analog };
    std::uint64_t generation_{},sequence_{},submission_serial_{};
    TriggerSource trigger_source_{TriggerSource::none};
    bool left_handed_{},generation_seen_{},sequence_seen_{},armed_{},held_{};
};
enum class TapStatus : std::uint8_t { sent,invalid_key,physical_conflict,read_failed,native_failed };
struct TapResult final { TapStatus status{TapStatus::native_failed}; int pending_release_key{}; };
// read_key_down observes native key ownership on the same event thread. Taps
// skip any held key and issue a balanced pair before returning. On a native SEH
// failure, a failed key-up is returned for explicit owner-thread retry.
struct KeyBindings final {
    NativeKeyEvent dispatch{};
    std::uintptr_t read_context{};
    bool (*read_key_down)(std::uintptr_t,int,bool*) noexcept{};
};
TapResult dispatch_tap(const KeyBindings&,int key,unsigned native_time) noexcept;
bool retry_release(NativeKeyEvent,int key,unsigned native_time) noexcept;
bool dispatch_cursor(NativeMouseEvent,int absolute_x,int absolute_y) noexcept;
int key_for(Intent) noexcept;
}
}
