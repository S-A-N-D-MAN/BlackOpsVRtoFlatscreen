// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "openxr_runtime.h"
#include "input_focus_gate.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

struct IDirect3DDevice9;

namespace bovr::vr {
namespace xr = wawvr::xr;

struct FrameKey final {
    std::uint64_t generation{};
    std::uint64_t frame_id{};
    friend bool operator==(const FrameKey&, const FrameKey&) = default;
};
struct FrameTicket final {
    FrameKey key{};
    xr::FrameState frame{};
    std::uint64_t published_ms{};
};
// A native producer creates one build serial for a frontend buffer/frame.
// Buffer addresses and native frame counters can both be reused; the serial
// must advance for each producer attempt, including attempts after map restart.
struct NativeBuildIdentity final {
    std::uintptr_t buffer_identity{};
    std::uint32_t native_frame{};
    std::uint64_t build_serial{};
    friend bool operator==(const NativeBuildIdentity&,const NativeBuildIdentity&) = default;
};
struct SceneReservation final {
    FrameTicket ticket{};
    NativeBuildIdentity native{};
    std::uint64_t serial{};
};
enum class ReservationState : std::uint8_t { none, waiting, invalid, consumed };
enum class RenderRejectionResult : std::uint8_t { key_not_current,invalidated,preserved_unowned };
// Optional copied diagnostic, filled at the existing reservation lock. It does
// not reserve, refresh, or retain a prediction and is never an admission token.
struct SceneReservationDiagnostic final {
    const char* reason{"not_checked"};
    FrameKey key{};
    NativeBuildIdentity reserved_native{};
    std::uint64_t published_ms{},reservation_serial{};
    std::uint64_t backend_producer{};
    std::uintptr_t backend_buffer{};
    std::uint32_t eye_mask{};
    bool initialized{},busy{},runtime_faulted{},reset_blocked{},pending{};
    bool invalid{},claimed{},staged{},menu{},menu_complete{},should_render{},views_valid{},awaiting_tracked_reservation{};
};
// Read-only pacing evidence, including generation while no ticket is pending.
// It grants no reservation/claim/placement permission.
// Finish-only copied proof. It is not an XR/native reservation and cannot
// survive the transport submit call or authorize any rendering operation.
struct ProducerPacingFlight final {
    std::uint64_t serial{};
    SceneReservationDiagnostic completed{};
};
struct ProducerPacingSnapshot final {
    std::uint64_t generation{};
    SceneReservationDiagnostic pending{};
    ProducerPacingFlight flight{};
};
using PublicationClock = std::uint64_t(*)() noexcept;
// Supplied by the engine adapter when it binds the predicted frame to a native
// backend buffer. producer_serial advances whenever that buffer is reused; a
// pointer alone cannot distinguish alternating SMP buffers from stale work.
struct BackendFrameIdentity final {
    std::uintptr_t buffer_identity{};
    std::uint64_t producer_serial{};
    friend bool operator==(const BackendFrameIdentity&, const BackendFrameIdentity&) = default;
};
enum class BeginStatus : std::uint8_t { started, idle, exit_requested, failed };
enum class TransportStatus : std::uint8_t {
    projection, zero_layer, retirement_failed, capture_failed, acquire_failed,
    metadata_mismatch, capture_validation_failed, compositor_failed, end_failed,
    quad
};
struct TransportResult final {
    TransportStatus status{TransportStatus::end_failed};
    std::uint64_t capture_serial{};
    std::uint32_t released_eye_mask{};
    bool frame_ended{};
};

// Runtime/graphics calls belong to one owner thread. The interface is also the
// injection boundary for tests; it exposes no T5 structures or hook addresses.
class FrameTransport {
public:
    virtual ~FrameTransport() = default;
    virtual bool initialize(const xr::RuntimeConfig&, const xr::HostCallbacks&) = 0;
    virtual BeginStatus begin(xr::FrameState*) = 0;
    virtual TransportResult discard(const xr::FrameState&) = 0;
    virtual TransportResult submit(IDirect3DDevice9*, const xr::FrameState&,
                                   const xr::StereoSourceLayout&) = 0;
    // Explicit finite native-menu composition; an unsupported transport must
    // discard instead of reinterpreting this as infinite-depth projection.
    virtual TransportResult submit_quad(IDirect3DDevice9*,const xr::FrameState& frame,
                                        const xr::QuadLayer&) { return discard(frame); }
    virtual bool before_reset(bool already_lost) = 0;
    virtual bool after_reset(bool was_lost, bool succeeded) = 0;
    // Returns false when GPU ownership cannot yet retire. Caller must retry
    // before releasing the device/runtime; never turn false into forced reset.
    virtual bool shutdown() = 0;
};

enum class CoordinatorStatus : std::uint8_t {
    started, idle, exit_requested, failed, wrong_thread, not_initialized,
    pending_frame, reset_blocked, runtime_faulted, no_frame, submitted, discarded_incomplete,
    discarded_nonrendering, discarded_cancelled, submitted_quad
};
struct BeginResult final {
    CoordinatorStatus status{CoordinatorStatus::failed};
    FrameTicket ticket{};
};
struct PresentResult final {
    CoordinatorStatus status{CoordinatorStatus::failed};
    FrameKey key{};
    BackendFrameIdentity backend{};
    TransportResult transport{};
};

// A prediction is published once, claimed once by the scene builder, and only
// becomes submit-eligible after BOTH backend eyes completed. Metadata locks
// never span engine callbacks, OpenXR calls, D3D calls, or waits.
class FrameCoordinator final {
public:
    explicit FrameCoordinator(FrameTransport& transport,PublicationClock clock=nullptr)
        : transport_(transport),clock_(clock) {}
    FrameCoordinator(const FrameCoordinator&) = delete;
    FrameCoordinator& operator=(const FrameCoordinator&) = delete;

    // Owner-thread lifecycle. Initialize/Begin do not render a game scene.
    bool initialize(const xr::RuntimeConfig&, const xr::HostCallbacks& = {});
    BeginResult begin_frame();
    PresentResult present(IDirect3DDevice9* borrowed_device,SceneReservationDiagnostic* diagnostic=nullptr);
    PresentResult discard_pending(SceneReservationDiagnostic* diagnostic=nullptr);
    bool before_reset(bool device_already_lost);
    bool after_reset(bool succeeded);
    bool shutdown();
    // A hard XR failure must not be reclassified as device recreation. This
    // value remains observable during retirement retries and after shutdown.
    bool runtime_faulted() const;
    ProducerPacingSnapshot producer_pacing_snapshot() const;

    // Thread-safe metadata side for a source-engine frontend/backend adapter.
    // claim succeeds once per prediction; stage records immutable packed layout
    // and native backend identity. Keep this stamp beside the native work, then
    // pass it back after each original backend eye draw returns. No native
    // backend pointer needs to remain installed when Present later retires it.
    bool claim_render_frame(FrameTicket*);
    // Metadata-only rejection of this exact pending prediction, including an
    // unreserved/already completed pair. Native frontend threads never call XR;
    // the owner later ends the rejected prediction with zero layers.
    bool reject_render_frame(FrameKey);
    // A failed native invocation does not own a later pristine prediction.
    // Preserve it only for a new native reservation; never refresh its age or
    // permit the failed stock scene to claim projection/menu publication.
    RenderRejectionResult reject_render_frame_or_preserve_unowned(FrameKey,bool allow_preservation,
        std::uint64_t now_ms,std::uint64_t maximum_age_ms);
    bool stage_render_frame(FrameKey, BackendFrameIdentity, const xr::StereoSourceLayout&);
    bool mark_eye_complete(FrameKey, BackendFrameIdentity, std::uint32_t eye);
    bool stage_menu_frame(FrameKey,BackendFrameIdentity,const xr::QuadLayer&);
    bool mark_menu_complete(FrameKey,BackendFrameIdentity);
    bool read_pending(FrameTicket*) const;
    bool read_pending_key(FrameKey*) const;
    // Read-only permission to keep an unowned prediction across rapid Presents
    // while its first native scene is pending. An awaiting-tracked marker stays
    // intact: only a later valid tracked reservation can clear it. This neither
    // acquires ownership nor refreshes the original publication time or pose.
    bool fresh_unowned_prediction(FrameKey,std::uint64_t now_ms,std::uint64_t maximum_age_ms,
                                  SceneReservationDiagnostic* diagnostic=nullptr) const;
    // Metadata-only producer transaction. A reservation excludes ordinary
    // claims/menu claims and holds its exact prediction until scene consumption
    // or explicit cancellation/reset/shutdown. Consumption must occur on the
    // original native producer thread and returns the canonical stored ticket.
    bool reserve_scene_frame(NativeBuildIdentity,SceneReservation*,SceneReservationDiagnostic* diagnostic=nullptr);
    // Only a positively identified new native transition scene may use this
    // adapter entry point. Atomically bind the exact still-unowned deferred key
    // and original timestamp; never reserve a newer prediction after a race.
    bool reserve_deferred_scene_frame(FrameKey,std::uint64_t original_published_ms,NativeBuildIdentity,
        std::uint64_t now_ms,std::uint64_t maximum_age_ms,SceneReservation*,SceneReservationDiagnostic* diagnostic=nullptr);
    bool consume_scene_reservation(const SceneReservation&,NativeBuildIdentity,FrameTicket*);
    bool cancel_scene_reservation(FrameKey,std::uint64_t reservation_serial);
    bool expire_scene_reservation(std::uint64_t now_ms,std::uint64_t maximum_age_ms);
    ReservationState scene_reservation_state() const;
    bool validate_scene_reservation(const SceneReservation&,NativeBuildIdentity) const;

private:
    struct Pending final {
        FrameTicket ticket{};
        BackendFrameIdentity backend{};
        xr::StereoSourceLayout layout{};
        xr::QuadLayer quad{};
        bool menu{};
        bool menu_complete{};
        std::uint32_t eye_mask{};
        bool claimed{};
        bool staged{};
        bool invalid{};
        bool awaiting_tracked_reservation{};
        SceneReservation reservation{};
        std::thread::id producer_thread{};
    };
    bool owner_locked() const noexcept;
    PresentResult finish(IDirect3DDevice9*, bool explicit_discard,SceneReservationDiagnostic* diagnostic=nullptr);
    void copy_pending_diagnostic_locked(SceneReservationDiagnostic&) const noexcept;
    FrameTransport& transport_;
    PublicationClock clock_{};
    InputFocusGate input_focus_gate_;
    mutable std::mutex mutex_;
    std::thread::id owner_{};
    std::optional<Pending> pending_;
    std::uint64_t generation_{1};
    std::uint64_t reservation_serial_{};
    std::uint64_t pacing_flight_serial_{};
    ProducerPacingFlight pacing_flight_{}; // Under mutex_, only while finish submits.
    bool initialized_{};
    bool busy_{};
    bool reset_blocked_{};
    bool reset_prepared_{};
    bool reset_was_lost_{};
    bool runtime_faulted_{};
};
} // namespace bovr::vr
