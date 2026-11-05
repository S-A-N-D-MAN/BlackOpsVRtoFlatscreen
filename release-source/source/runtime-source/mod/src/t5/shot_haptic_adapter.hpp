// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace wawvr::xr { class OpenXrRuntime; }
namespace bovr::vr { struct FrameTicket; }
namespace bovr::t5 {
struct PreCommandSnapshot;
// Requires the explicitly installed native bullet and command observers.
// The callback only queues copied confirmed local-shot metadata.
bool configure_shot_haptic_adapter(bool bullet_hook,bool command_hook) noexcept;
// Optional read-only predicted-event source at the already installed command
// observer. This path never enables the legacy bullet observer.
bool configure_predicted_shot_feedback(bool command_hook) noexcept;
void observe_predicted_shot_feedback(const PreCommandSnapshot&) noexcept;
float predicted_shot_kickback(std::uint64_t generation,std::uint64_t epoch,unsigned weapon,std::uint64_t now_ms) noexcept;
// XR owner only. Pass the original ticket, never a freshly timestamped copy.
// A null ticket invalidates acceptance and stops the previous pulse once.
void service_shot_haptics(wawvr::xr::OpenXrRuntime&,const vr::FrameTicket*) noexcept;
// Call on the XR owner before reset/shutdown, while the runtime is reachable.
void reset_shot_haptics(wawvr::xr::OpenXrRuntime&) noexcept;
}
