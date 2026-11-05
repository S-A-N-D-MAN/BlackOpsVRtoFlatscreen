// SPDX-License-Identifier: GPL-3.0-only
#include "frame_coordinator.hpp"
#include <cmath>
#include <chrono>

namespace bovr::vr {
namespace {
bool valid_layout(const xr::StereoSourceLayout& layout) {
    for (unsigned eye=0; eye<xr::kEyeCount; ++eye) {
        const auto& r=layout.eyes[eye];
        const auto& v=layout.destinations[eye];
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
            !std::isfinite(r.height) || r.x<0 || r.y<0 || r.width<=0 || r.height<=0 ||
            r.x+r.width>1.00001f || r.y+r.height>1.00001f ||
            !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.width) ||
            !std::isfinite(v.height) || v.width<=0 || v.height<=0 ||
            !std::isfinite(layout.source_aspect_multipliers[eye]) ||
            layout.source_aspect_multipliers[eye]<=0) return false;
    }
    const auto& a=layout.eyes[0]; const auto& b=layout.eyes[1];
    return a.x+a.width<=b.x || b.x+b.width<=a.x ||
           a.y+a.height<=b.y || b.y+b.height<=a.y;
}
bool valid_quad(const xr::QuadLayer& quad) {
    const auto& p=quad.pose.position; const auto& q=quad.pose.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::isfinite(norm) && std::fabs(norm-1.0f)<0.001f &&
           std::isfinite(quad.size_meters.x) && std::isfinite(quad.size_meters.y) &&
           quad.size_meters.x>0 && quad.size_meters.y>0 && quad.source_eye<xr::kEyeCount;
}
}
bool FrameCoordinator::owner_locked() const noexcept {
    return owner_==std::this_thread::get_id();
}
bool FrameCoordinator::initialize(const xr::RuntimeConfig& config,
                                  const xr::HostCallbacks& host) {
    {
        std::lock_guard lock(mutex_);
        if (initialized_ || busy_) return false;
        owner_=std::this_thread::get_id(); busy_=true;
    }
    bool initialized=false,threw=false;
    try { initialized=transport_.initialize(config,host); }
    catch (...) { threw=true; }
    std::lock_guard lock(mutex_);
    // A throwing initialization may have acquired partial resources. Keep the
    // owner/lifecycle reachable for shutdown, but never permit frame production.
    initialized_=initialized || threw; busy_=false; reset_blocked_=false; runtime_faulted_=threw;
    reset_prepared_=false; pending_.reset(); ++generation_;
    input_focus_gate_.reset();
    return initialized;
}
BeginResult FrameCoordinator::begin_frame() {
    {
        std::lock_guard lock(mutex_);
        if (!owner_locked()) return {CoordinatorStatus::wrong_thread};
        if (!initialized_) return {CoordinatorStatus::not_initialized};
        if (runtime_faulted_) return {CoordinatorStatus::runtime_faulted};
        if (reset_blocked_) return {CoordinatorStatus::reset_blocked};
        if (pending_ || busy_) return {CoordinatorStatus::pending_frame};
        busy_=true;
    }
    xr::FrameState frame{};
    auto begun=BeginStatus::failed;
    try { begun=transport_.begin(&frame); } catch (...) {}
    std::lock_guard lock(mutex_); busy_=false;
    if (begun==BeginStatus::idle) return {CoordinatorStatus::idle};
    if (begun==BeginStatus::exit_requested) return {CoordinatorStatus::exit_requested};
    if (begun!=BeginStatus::started) {
        runtime_faulted_=true; return {CoordinatorStatus::failed};
    }
    Pending pending{};
    input_focus_gate_.apply(frame.actions);
    const auto published=clock_ ? clock_() : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    pending.ticket={{generation_,frame.frame_id},frame,published};
    pending_=pending;
    return {CoordinatorStatus::started,pending.ticket};
}
bool FrameCoordinator::read_pending(FrameTicket* output) const {
    if (!output) return false;
    std::lock_guard lock(mutex_);
    if (!pending_) return false;
    *output=pending_->ticket; return true;
}
bool FrameCoordinator::read_pending_key(FrameKey* output) const {
    if (!output) return false;
    std::lock_guard lock(mutex_);
    if (!pending_) return false;
    *output=pending_->ticket.key; return true;
}
bool FrameCoordinator::fresh_unowned_prediction(FrameKey key,std::uint64_t now,std::uint64_t maximum_age,
                                               SceneReservationDiagnostic* diagnostic) const {
    if(diagnostic)*diagnostic={};
    std::lock_guard lock(mutex_);
    if(diagnostic)copy_pending_diagnostic_locked(*diagnostic);
    if(!initialized_||busy_||runtime_faulted_||reset_blocked_||!pending_||pending_->ticket.key!=key)return false;
    const auto& p=*pending_;const auto& ticket=p.ticket;
    return !p.invalid&&!p.claimed&&!p.reservation.serial&&
        !p.staged&&!p.backend.buffer_identity&&!p.backend.producer_serial&&!p.eye_mask&&!p.menu&&!p.menu_complete&&
        key.generation&&key.frame_id&&ticket.published_ms&&ticket.frame.should_render&&ticket.frame.views_valid&&
        maximum_age&&now>=ticket.published_ms&&now-ticket.published_ms<=maximum_age;
}
bool FrameCoordinator::claim_render_frame(FrameTicket* output) {
    if (!output) return false;
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->invalid || pending_->claimed || pending_->reservation.serial || pending_->awaiting_tracked_reservation || reset_blocked_ ||
        !pending_->ticket.frame.should_render || !pending_->ticket.frame.views_valid) return false;
    pending_->claimed=true; *output=pending_->ticket; return true;
}
bool FrameCoordinator::reject_render_frame(FrameKey key) {
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->ticket.key!=key) return false;
    pending_->invalid=true;return true;
}
RenderRejectionResult FrameCoordinator::reject_render_frame_or_preserve_unowned(FrameKey key,bool allow_preservation,
        std::uint64_t now,std::uint64_t maximum_age) {
    std::lock_guard lock(mutex_);
    if(!pending_||pending_->ticket.key!=key)return RenderRejectionResult::key_not_current;
    const auto& p=*pending_;const auto& ticket=p.ticket;
    if(allow_preservation&&initialized_&&!busy_&&!runtime_faulted_&&!reset_blocked_&&!p.invalid&&!p.claimed&&!p.reservation.serial&&
        !p.staged&&!p.backend.buffer_identity&&!p.backend.producer_serial&&!p.eye_mask&&!p.menu&&!p.menu_complete&&
        ticket.key.generation&&ticket.key.frame_id&&ticket.published_ms&&ticket.frame.should_render&&ticket.frame.views_valid&&
        maximum_age&&now>=ticket.published_ms&&now-ticket.published_ms<=maximum_age){
        pending_->awaiting_tracked_reservation=true;
        return RenderRejectionResult::preserved_unowned;
    }
    pending_->invalid=true;return RenderRejectionResult::invalidated;
}
ProducerPacingSnapshot FrameCoordinator::producer_pacing_snapshot() const {
    std::lock_guard lock(mutex_);ProducerPacingSnapshot snapshot{};
    snapshot.generation=generation_;copy_pending_diagnostic_locked(snapshot.pending);
    snapshot.pending.reason="producer_pacing_readonly";
    if(initialized_&&!runtime_faulted_&&!reset_blocked_&&busy_&&!pending_)
        snapshot.flight=pacing_flight_;
    return snapshot;
}
void FrameCoordinator::copy_pending_diagnostic_locked(SceneReservationDiagnostic& d) const noexcept {
    d.initialized=initialized_;d.busy=busy_;d.runtime_faulted=runtime_faulted_;d.reset_blocked=reset_blocked_;d.pending=pending_.has_value();
    if(pending_){
        d.key=pending_->ticket.key;d.published_ms=pending_->ticket.published_ms;d.reservation_serial=pending_->reservation.serial;
        d.reserved_native=pending_->reservation.native;d.backend_producer=pending_->backend.producer_serial;d.backend_buffer=pending_->backend.buffer_identity;
        d.invalid=pending_->invalid;d.claimed=pending_->claimed;d.staged=pending_->staged;d.menu=pending_->menu;d.menu_complete=pending_->menu_complete;d.eye_mask=pending_->eye_mask;
        d.should_render=pending_->ticket.frame.should_render;d.views_valid=pending_->ticket.frame.views_valid;
        d.awaiting_tracked_reservation=pending_->awaiting_tracked_reservation;
    }
}
bool FrameCoordinator::reserve_scene_frame(NativeBuildIdentity native,SceneReservation* output,SceneReservationDiagnostic* diagnostic) {
    if(diagnostic)*diagnostic={};
    if (!output) return false;
    *output={};
    std::lock_guard lock(mutex_);
    if (!initialized_ || busy_ || runtime_faulted_ || reset_blocked_ || !pending_ ||
        pending_->invalid || pending_->claimed || pending_->reservation.serial ||
        !native.buffer_identity || !native.build_serial || !pending_->ticket.published_ms ||
        !pending_->ticket.key.generation || !pending_->ticket.key.frame_id ||
        !pending_->ticket.frame.should_render || !pending_->ticket.frame.views_valid) {
        if(diagnostic){
            auto& d=*diagnostic;
            copy_pending_diagnostic_locked(d);
            d.reason=!initialized_?"not_initialized":busy_?"busy":runtime_faulted_?"runtime_faulted":reset_blocked_?"reset_blocked":!pending_?"no_pending":
                pending_->invalid?"invalid":pending_->claimed?"claimed":pending_->reservation.serial?"reserved":
                !native.buffer_identity?"native_buffer":!native.build_serial?"native_build":!pending_->ticket.published_ms?"publication_time":
                !pending_->ticket.key.generation?"generation":!pending_->ticket.key.frame_id?"frame_id":
                !pending_->ticket.frame.should_render?"not_rendering":"invalid_views";
        }
        return false;
    }
    pending_->reservation={pending_->ticket,native,++reservation_serial_};
    pending_->awaiting_tracked_reservation=false;
    pending_->producer_thread=std::this_thread::get_id();
    *output=pending_->reservation;return true;
}
bool FrameCoordinator::reserve_deferred_scene_frame(FrameKey key,std::uint64_t published,NativeBuildIdentity native,
        std::uint64_t now,std::uint64_t maximum_age,SceneReservation* output,SceneReservationDiagnostic* diagnostic) {
    if(diagnostic)*diagnostic={};
    if(!output)return false;
    *output={};std::lock_guard lock(mutex_);
    if(diagnostic)copy_pending_diagnostic_locked(*diagnostic);
    if(!initialized_||busy_||runtime_faulted_||reset_blocked_||!pending_)return false;
    const auto& p=*pending_;const auto& ticket=p.ticket;
    if(ticket.key!=key||!key.generation||!key.frame_id||!published||ticket.published_ms!=published||
       !p.awaiting_tracked_reservation||p.invalid||p.claimed||p.reservation.serial||p.staged||
       p.backend.buffer_identity||p.backend.producer_serial||p.eye_mask||p.menu||p.menu_complete||
       !ticket.frame.should_render||!ticket.frame.views_valid||!maximum_age||now<published||now-published>maximum_age||
       !native.buffer_identity||!native.build_serial)return false;
    pending_->reservation={ticket,native,++reservation_serial_};
    pending_->awaiting_tracked_reservation=false;
    pending_->producer_thread=std::this_thread::get_id();
    *output=pending_->reservation;return true;
}
bool FrameCoordinator::consume_scene_reservation(const SceneReservation& reservation,NativeBuildIdentity native,FrameTicket* output) {
    if (!output) return false;
    *output={};
    std::lock_guard lock(mutex_);
    if (!pending_ || busy_ || reset_blocked_ || runtime_faulted_ || pending_->invalid || pending_->claimed ||
        !reservation.serial || pending_->reservation.serial!=reservation.serial ||
        pending_->ticket.key!=reservation.ticket.key || pending_->reservation.native!=native || reservation.native!=native ||
        pending_->producer_thread!=std::this_thread::get_id()) return false;
    pending_->claimed=true;*output=pending_->ticket;return true;
}
bool FrameCoordinator::cancel_scene_reservation(FrameKey key,std::uint64_t serial) {
    std::lock_guard lock(mutex_);
    if (!pending_ || !serial || pending_->ticket.key!=key || pending_->reservation.serial!=serial) return false;
    pending_->invalid=true;return true;
}
bool FrameCoordinator::expire_scene_reservation(std::uint64_t now,std::uint64_t maximum_age) {
    std::lock_guard lock(mutex_);
    if (!pending_ || !pending_->reservation.serial || pending_->claimed || pending_->invalid) return false;
    const auto published=pending_->ticket.published_ms;
    if (published && now>=published && now-published<=maximum_age) return false;
    pending_->invalid=true;return true;
}
ReservationState FrameCoordinator::scene_reservation_state() const {
    std::lock_guard lock(mutex_);
    if (!pending_ || !pending_->reservation.serial) return ReservationState::none;
    if (pending_->invalid) return ReservationState::invalid;
    return pending_->claimed ? ReservationState::consumed : ReservationState::waiting;
}
bool FrameCoordinator::validate_scene_reservation(const SceneReservation& r,NativeBuildIdentity native) const {
    std::lock_guard lock(mutex_);
    return initialized_&&!busy_&&!reset_blocked_&&!runtime_faulted_&&pending_&&!pending_->claimed&&!pending_->invalid&&
        r.serial&&pending_->reservation.serial==r.serial&&pending_->ticket.key==r.ticket.key&&
        pending_->reservation.native==native&&r.native==native&&pending_->producer_thread==std::this_thread::get_id();
}
bool FrameCoordinator::stage_render_frame(FrameKey key, BackendFrameIdentity backend,
                                          const xr::StereoSourceLayout& layout) {
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->invalid || pending_->ticket.key!=key || !pending_->claimed ||
        pending_->staged || pending_->eye_mask || !backend.buffer_identity ||
        !backend.producer_serial || !valid_layout(layout)) return false;
    pending_->backend=backend; pending_->layout=layout; pending_->staged=true; return true;
}
bool FrameCoordinator::mark_eye_complete(FrameKey key, BackendFrameIdentity backend, std::uint32_t eye) {
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->invalid || pending_->ticket.key!=key || !pending_->staged ||
        pending_->backend!=backend || pending_->menu || eye>=xr::kEyeCount)
        return false;
    const auto bit=std::uint32_t{1}<<eye;
    if (pending_->eye_mask&bit) { pending_->invalid=true; return false; }
    pending_->eye_mask|=bit; return true;
}
bool FrameCoordinator::stage_menu_frame(FrameKey key,BackendFrameIdentity backend,const xr::QuadLayer& quad) {
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->invalid || pending_->ticket.key!=key || !pending_->claimed || pending_->staged ||
        pending_->eye_mask || !backend.buffer_identity || !backend.producer_serial || !valid_quad(quad)) return false;
    pending_->backend=backend; pending_->quad=quad; pending_->menu=true; pending_->staged=true; return true;
}
bool FrameCoordinator::mark_menu_complete(FrameKey key,BackendFrameIdentity backend) {
    std::lock_guard lock(mutex_);
    if (!pending_ || pending_->invalid || pending_->ticket.key!=key || !pending_->staged || !pending_->menu ||
        pending_->backend!=backend) return false;
    if (pending_->menu_complete) { pending_->invalid=true; return false; }
    pending_->menu_complete=true; return true;
}
PresentResult FrameCoordinator::finish(IDirect3DDevice9* device, bool explicit_discard,SceneReservationDiagnostic* diagnostic) {
    if(diagnostic)*diagnostic={};
    Pending frame{};
    {
        std::lock_guard lock(mutex_);
        if (!owner_locked()) return {CoordinatorStatus::wrong_thread};
        if (!initialized_) return {CoordinatorStatus::not_initialized};
        if (busy_) return {CoordinatorStatus::pending_frame};
        if (!pending_) return {CoordinatorStatus::no_frame};
        if (pending_->reservation.serial && !pending_->claimed && !pending_->invalid && !explicit_discard)
            return {CoordinatorStatus::pending_frame,pending_->ticket.key};
        if(diagnostic){
            copy_pending_diagnostic_locked(*diagnostic);
            diagnostic->reason=explicit_discard?"explicit_discard":!pending_->ticket.frame.should_render?"not_rendering":!pending_->ticket.frame.views_valid?"invalid_views":
                pending_->invalid?"invalid":!pending_->claimed?"unclaimed":!pending_->staged?"not_staged":
                pending_->menu&&!pending_->menu_complete?"incomplete_menu":!pending_->menu&&pending_->eye_mask!=3?"incomplete_eyes":"complete";
        }
        pacing_flight_={};
        const auto& p=*pending_;
        if(!explicit_discard&&!runtime_faulted_&&!reset_blocked_&&!p.invalid&&
           p.claimed&&p.staged&&!p.menu&&!p.menu_complete&&!p.awaiting_tracked_reservation&&p.eye_mask==3&&
           p.ticket.frame.should_render&&p.ticket.frame.views_valid&&p.ticket.key.generation==generation_&&
           p.ticket.key.frame_id&&p.ticket.published_ms&&p.reservation.serial&&p.reservation.native.build_serial&&
           p.reservation.native.buffer_identity&&p.backend.buffer_identity==p.reservation.native.buffer_identity&&
           p.backend.producer_serial&&pacing_flight_serial_!=UINT64_MAX) {
            copy_pending_diagnostic_locked(pacing_flight_.completed);
            pacing_flight_.completed.reason="producer_pacing_finish_only";
            pacing_flight_.serial=++pacing_flight_serial_;
        }
        frame=*pending_; pending_.reset(); busy_=true;
    }
    const bool rendering=frame.ticket.frame.should_render && frame.ticket.frame.views_valid;
    const bool complete=frame.claimed && frame.staged && !frame.invalid &&
                        (frame.menu ? frame.menu_complete : frame.eye_mask==3);
    const bool submit=rendering && complete && !explicit_discard;
    TransportResult result{};
    try {
        result=submit ? (frame.menu ? transport_.submit_quad(device,frame.ticket.frame,frame.quad)
                                   : transport_.submit(device,frame.ticket.frame,frame.layout))
                      : transport_.discard(frame.ticket.frame);
    } catch (...) {}
    {
        std::lock_guard lock(mutex_); busy_=false;
        pacing_flight_={}; // All success/failure/exception outcomes retire copied proof.
        if (!result.frame_ended) runtime_faulted_=true;
    }
    auto status=CoordinatorStatus::failed;
    if (result.frame_ended) {
        if (submit && frame.menu && result.status==TransportStatus::quad &&
            (result.released_eye_mask & (1u<<frame.quad.source_eye)))
            status=CoordinatorStatus::submitted_quad;
        else if (submit && !frame.menu && result.status==TransportStatus::projection && result.released_eye_mask==3)
            status=CoordinatorStatus::submitted;
        else if (!submit)
            status=explicit_discard ? CoordinatorStatus::discarded_cancelled :
                rendering ? CoordinatorStatus::discarded_incomplete : CoordinatorStatus::discarded_nonrendering;
    }
    return {status,frame.ticket.key,frame.backend,result};
}
PresentResult FrameCoordinator::present(IDirect3DDevice9* device,SceneReservationDiagnostic* diagnostic) { return finish(device,false,diagnostic); }
PresentResult FrameCoordinator::discard_pending(SceneReservationDiagnostic* diagnostic) { return finish(nullptr,true,diagnostic); }
bool FrameCoordinator::before_reset(bool already_lost) {
    {
        std::lock_guard lock(mutex_);
        if (!owner_locked() || !initialized_ || busy_ || runtime_faulted_) return false;
        reset_blocked_=true;
    }
    const auto discarded=discard_pending();
    if (discarded.status!=CoordinatorStatus::no_frame && !discarded.transport.frame_ended) return false;
    {
        std::lock_guard lock(mutex_); busy_=true; ++generation_;
    }
    bool ready=false;
    try { ready=transport_.before_reset(already_lost); } catch (...) {}
    std::lock_guard lock(mutex_);
    busy_=false; reset_prepared_=ready; reset_was_lost_=already_lost;
    return ready;
}
bool FrameCoordinator::after_reset(bool succeeded) {
    bool was_lost=false;
    {
        std::lock_guard lock(mutex_);
        if (!owner_locked() || !initialized_ || busy_ || !reset_prepared_) return false;
        busy_=true; was_lost=reset_was_lost_;
    }
    bool recovered=false;
    try { recovered=transport_.after_reset(was_lost,succeeded); } catch (...) {}
    std::lock_guard lock(mutex_);
    busy_=false; reset_prepared_=false; reset_blocked_=!recovered;
    return recovered;
}
bool FrameCoordinator::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (!initialized_) return true;
        if (!owner_locked() || busy_) return false;
        reset_blocked_=true;
    }
    const auto discarded=discard_pending();
    if (discarded.status!=CoordinatorStatus::no_frame && !discarded.transport.frame_ended) return false;
    { std::lock_guard lock(mutex_); busy_=true; }
    bool stopped=false,threw=false;
    try { stopped=transport_.shutdown(); } catch (...) { threw=true; }
    std::lock_guard lock(mutex_);
    busy_=false;
    if (threw) runtime_faulted_=true;
    if (stopped) { initialized_=false; pending_.reset(); ++generation_; }
    return stopped;
}
bool FrameCoordinator::runtime_faulted() const {
    std::lock_guard lock(mutex_); return runtime_faulted_;
}
} // namespace bovr::vr
