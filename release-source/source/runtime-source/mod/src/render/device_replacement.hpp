// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "loader/d3d9_observer.hpp"
#include <atomic>

namespace bovr::render {
// Frontend native scene calls may overlap the render owner's Present. Closing
// publication refuses new leases; retirement retries until an already admitted
// synchronous scene has returned and restored its borrowed native refdef.
class DevicePublicationGate {
public:
    bool enter() noexcept {
        if (closed_.load()) return false;
        ++users_;
        if (closed_.load()) {--users_;return false;}
        return true;
    }
    void leave() noexcept {--users_;}
    void close() noexcept {closed_=true;}
    bool quiescent() const noexcept {return users_.load()==0;}
    void reopen() noexcept {closed_=false;}
private:
    std::atomic<bool> closed_{};
    std::atomic<unsigned> users_{};
};
struct DevicePublicationLease {
    DevicePublicationGate& gate;
    bool admitted;
    explicit DevicePublicationLease(DevicePublicationGate& gate):gate(gate),admitted(gate.enter()){}
    ~DevicePublicationLease(){if(admitted)gate.leave();}
    DevicePublicationLease(const DevicePublicationLease&)=delete;
    DevicePublicationLease& operator=(const DevicePublicationLease&)=delete;
};
struct DeviceReplacementRequest {
    IDirect3DDevice9* retained{};
    IDirect3DDevice9* candidate{}; // Borrowed for this Present only.
    DeviceCreationInfo retained_creation{},candidate_creation{};
    DWORD owner{},caller{};
    bool initialized{},starting{},stopped{};
};
enum class DeviceReplacementResult {
    stopped,wrong_owner,not_ready,same_device,unproven_creation,not_extended,
    retry_retirement,runtime_faulted,retired,retired_stopping
};
// The host supplies the actual coordinator and resource ownership operations.
// No lock spans QI, runtime retirement or Release. No candidate reference is kept
// after a deferred attempt. Failed retirement never invokes commit_retirement.
// This transaction deliberately has no quit acknowledgement or native hook.
template<class Host>
DeviceReplacementResult retire_for_device_replacement(const DeviceReplacementRequest& r,Host& host) {
    if (r.stopped || host.stop_requested()) return DeviceReplacementResult::stopped;
    if (!r.owner || r.owner!=r.caller) return DeviceReplacementResult::wrong_owner;
    if (host.runtime_faulted()) return DeviceReplacementResult::runtime_faulted;
    if (!r.initialized || r.starting || !r.retained || !r.candidate) return DeviceReplacementResult::not_ready;
    if (r.retained==r.candidate) return DeviceReplacementResult::same_device;
    if (!r.retained_creation.serial || r.candidate_creation.serial<=r.retained_creation.serial ||
        r.retained_creation.thread_id!=r.owner || r.candidate_creation.thread_id!=r.owner ||
        !r.retained_creation.extended || !r.candidate_creation.extended)
        return DeviceReplacementResult::unproven_creation;
    IDirect3DDevice9Ex* extended{};
    const HRESULT hr=r.candidate->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(&extended));
    const bool same_identity=SUCCEEDED(hr)&&extended==r.candidate;
    if (extended) extended->Release();
    if (!same_identity) return DeviceReplacementResult::not_extended;
    if (host.stop_requested()) return DeviceReplacementResult::stopped;
    host.begin_retirement(); // Blocks publication; old ownership stays intact.
    if (!host.retire_runtime()) return host.runtime_faulted() ?
        DeviceReplacementResult::runtime_faulted : DeviceReplacementResult::retry_retirement;
    host.commit_retirement(); // Clears metadata, staging, old AddRef, in order.
    return host.stop_requested() ? DeviceReplacementResult::retired_stopping : DeviceReplacementResult::retired;
}
} // namespace bovr::render
