// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <atomic>
namespace bovr::t5 {
// True means there is no live XR ownership and native quit may continue. False
// requests deferred retirement; callback must return promptly, without waits.
using QuitRetirementCallback = bool(*)(void* context);
class QuitRetirementGate {
public:
    bool may_call_native(QuitRetirementCallback callback, void* context) noexcept;
    bool acknowledge_retired() noexcept;
    bool retirement_requested() const noexcept;
private:
    enum class State { idle, requesting, waiting, retired };
    std::atomic<State> state_{State::idle};
};
// Explicit installation only: requires BOVR_QUIT_GATE=1, BOVR_INSTRUMENT=1 and
// complete pinned host identity, loaded-site bytes and registration evidence.
// Never called automatically by the proxy. Install/bind before creating XR.
bool install_quit_gate(HMODULE executable) noexcept;
bool quit_gate_installed() noexcept;
// Process-lifetime context, or preserve it until successful unbind. Returns false
// while a callback is in flight. A pending quit still needs ACK after unbinding.
bool set_quit_retirement_callback(QuitRetirementCallback callback, void* context) noexcept;
bool quit_retirement_requested() noexcept;
// Owner-render-thread call only after all XR/device ownership is retired. One
// winning ACK invokes native, internally synchronized Cbuf_AddText(0,"quit\n").
// True means invoked, not a receipt from the void native command-buffer API.
bool acknowledge_quit_retired_and_queue() noexcept;
}
