// SPDX-License-Identifier: GPL-3.0-only
#include "frame_coordinator.hpp"
#include "frame_discard_diagnostic.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <stdexcept>
#include <future>
#include <chrono>
#include <utility>
#include <cstring>

using namespace bovr::vr;
namespace {
void require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
}
struct FakeTransport final : FrameTransport {
    unsigned begins{},discards{},submits{},preflights{},finalizes{},stops{};
    bool reset_ready=true,stop_ready=true,render=true,end_success=true,begin_success=true;
    bool throw_init=false,throw_begin=false,throw_submit=false,throw_stop=false;
    bool focused=true;
    bool views_valid=true;
    void (*metadata_probe)(void*){};
    void* metadata_context{};
    std::uint64_t action_sequence_override{};
    bool initialize(const xr::RuntimeConfig&,const xr::HostCallbacks&) override {
        if (throw_init) throw std::runtime_error("injected initialize failure");
        return true;
    }
    BeginStatus begin(xr::FrameState* frame) override {
        if (metadata_probe) metadata_probe(metadata_context);
        if (throw_begin) throw std::runtime_error("injected begin failure");
        if (!begin_success) return BeginStatus::failed;
        *frame={}; frame->frame_id=++begins; frame->views_valid=views_valid;
        frame->head_center.position={1,2,3};
        frame->actions.focused=focused;
        frame->actions.sequence=action_sequence_override ? action_sequence_override : frame->frame_id;
        frame->actions.menu={true,true,true};
        for (auto& hand : frame->actions.hands) {
            hand.grip.active=true; hand.grip.position_valid=true; hand.grip.pose.position={4,5,6};
            hand.aim.active=true; hand.aim.orientation_valid=true;
            hand.trigger={true,0.7f,true}; hand.squeeze={true,0.8f,true};
            hand.stick.active=true; hand.stick.current={0.3f,0.4f};
            hand.primary={true,true,true}; hand.secondary={true,true,true};
            hand.trigger_click={true,true,true}; hand.stick_click={true,true,true};
            hand.thumbrest={true,true,true};
        }
        frame->should_render=render; return BeginStatus::started;
    }
    TransportResult discard(const xr::FrameState&) override {
        if (metadata_probe) metadata_probe(metadata_context);
        ++discards; return {end_success ? TransportStatus::zero_layer : TransportStatus::end_failed,0,0,end_success};
    }
    TransportResult submit(IDirect3DDevice9*,const xr::FrameState& frame,
                           const xr::StereoSourceLayout&) override {
        if (metadata_probe) metadata_probe(metadata_context);
        if (throw_submit) throw std::runtime_error("injected submit failure");
        ++submits; return {end_success ? TransportStatus::projection : TransportStatus::end_failed,frame.frame_id+100,3,end_success};
    }
    bool before_reset(bool) override { if (metadata_probe) metadata_probe(metadata_context); ++preflights; return reset_ready; }
    bool after_reset(bool,bool success) override { ++finalizes; return success; }
    bool shutdown() override {
        if (metadata_probe) metadata_probe(metadata_context);
        ++stops;
        if (throw_stop) throw std::runtime_error("injected shutdown failure");
        return stop_ready;
    }
};
BackendFrameIdentity backend(const FrameTicket& ticket) {
    return {0x3B40100u+(ticket.key.frame_id%2)*0x186300u,ticket.key.frame_id+1000};
}
FrameTicket stage(FrameCoordinator& coordinator) {
    const auto begun=coordinator.begin_frame();
    require(begun.status==CoordinatorStatus::started,"frame begins");
    FrameTicket claimed{};
    require(coordinator.claim_render_frame(&claimed),"scene claims prediction");
    require(claimed.key==begun.ticket.key,"published and claimed identity agree");
    require(!coordinator.claim_render_frame(&claimed),"prediction cannot be claimed twice");
    require(coordinator.stage_render_frame(claimed.key,backend(claimed),{}),"packed layout stages with native provenance");
    return claimed;
}
void complete(FrameCoordinator& coordinator,const FrameTicket& ticket) {
    require(coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"left backend completes");
    require(coordinator.mark_eye_complete(ticket.key,backend(ticket),1),"right backend completes");
}
void exact_pair_and_once_only() {
    FakeTransport transport; FrameCoordinator coordinator(transport);
    require(coordinator.initialize({}),"initialize");
    const auto first=stage(coordinator);
    require(coordinator.begin_frame().status==CoordinatorStatus::pending_frame && transport.begins==1,
            "second begin cannot advance XR or simulation under pending frame");
    require(coordinator.mark_eye_complete(first.key,backend(first),0),"single eye completes");
    auto presented=coordinator.present(nullptr);
    require(presented.status==CoordinatorStatus::discarded_incomplete && transport.submits==0 &&
            transport.discards==1,"one eye cannot reach capture/compositor");
    require(coordinator.present(nullptr).status==CoordinatorStatus::no_frame,
            "present cannot end the same frame twice");
    const auto second=stage(coordinator);
    require(!coordinator.mark_eye_complete(first.key,backend(first),1),"late prior-frame eye ignored");
    complete(coordinator,second);
    presented=coordinator.present(nullptr);
    require(presented.status==CoordinatorStatus::submitted && transport.submits==1 &&
            presented.key==second.key && presented.backend==backend(second) &&
            presented.transport.capture_serial==second.key.frame_id+100 &&
            presented.transport.released_eye_mask==3,"complete pair preserves exact submission identity");
    require(coordinator.shutdown(),"shutdown after consumed frames");
}
void duplicates_and_invalid_layout() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    const auto ticket=stage(coordinator);
    require(coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"first eye accepted");
    require(!coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"duplicate eye rejected");
    require(!coordinator.mark_eye_complete(ticket.key,backend(ticket),1),"poisoned pair refuses later eye completion");
    require(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete &&
            transport.submits==0,"duplicate-eye history poisons pair and cannot be repaired by later completion");
    auto begun=coordinator.begin_frame(); FrameTicket claimed{};
    require(coordinator.claim_render_frame(&claimed),"claim for invalid-layout check");
    xr::StereoSourceLayout duplicate{}; duplicate.eyes[1]=duplicate.eyes[0];
    require(!coordinator.stage_render_frame(begun.ticket.key,backend(claimed),duplicate),"same source region cannot feed both eyes");
    require(!coordinator.mark_eye_complete(begun.ticket.key,backend(claimed),0),"unstaged backend completion rejected");
    require(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete,
            "invalid layout stays zero layer");
    require(coordinator.shutdown(),"shutdown");
}
void reset_generation_and_retirement() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    const auto old=stage(coordinator); complete(coordinator,old);
    transport.reset_ready=false;
    require(!coordinator.before_reset(false),"unretired GPU ownership blocks reset");
    require(transport.discards==1 && transport.submits==0,"reset drains completed scene as zero layer");
    require(coordinator.begin_frame().status==CoordinatorStatus::reset_blocked,
            "no new frame while reset preflight is blocked");
    require(!coordinator.after_reset(true),"cannot acknowledge a reset that was never prepared");
    transport.reset_ready=true;
    require(coordinator.before_reset(false),"retired bridge permits real reset");
    require(!coordinator.after_reset(false),"failed device reset does not resume rendering");
    require(coordinator.begin_frame().status==CoordinatorStatus::reset_blocked,"lost reset stays blocked");
    require(coordinator.before_reset(true) && coordinator.after_reset(true),"successful lost-device route resumes");
    const auto fresh=stage(coordinator);
    require(fresh.key.generation!=old.key.generation,"reset changes source generation");
    require(!coordinator.mark_eye_complete(old.key,backend(old),0),"old generation cannot complete new frame");
    complete(coordinator,fresh);
    require(coordinator.present(nullptr).status==CoordinatorStatus::submitted,"fresh pair submits");
    transport.stop_ready=false;
    require(!coordinator.shutdown(),"shutdown reports outstanding GPU retirement");
    require(coordinator.begin_frame().status==CoordinatorStatus::reset_blocked,"failed shutdown prevents new frames");
    transport.stop_ready=true; require(coordinator.shutdown(),"shutdown can be retried safely");
}
void thread_and_nonrendering_contract() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    const auto ticket=stage(coordinator);
    CoordinatorStatus other_begin{};
    std::thread wrong_owner([&]{other_begin=coordinator.begin_frame().status;}); wrong_owner.join();
    require(other_begin==CoordinatorStatus::wrong_thread && transport.begins==1,
            "foreign owner cannot call OpenXR begin");
    bool left=false,right=false;
    std::thread backend_left([&]{left=coordinator.mark_eye_complete(ticket.key,backend(ticket),0);});
    std::thread backend_right([&]{right=coordinator.mark_eye_complete(ticket.key,backend(ticket),1);});
    backend_left.join(); backend_right.join();
    require(left && right && coordinator.present(nullptr).status==CoordinatorStatus::submitted,
            "metadata completion supports backend threads without graphics under locks");
    transport.render=false;
    require(coordinator.begin_frame().status==CoordinatorStatus::started,"nonrendering runtime frame still begins");
    FrameTicket ignored{};
    require(!coordinator.claim_render_frame(&ignored),"shouldRender=false cannot claim game stereo");
    require(coordinator.present(nullptr).status==CoordinatorStatus::discarded_nonrendering,
            "nonrendering runtime frame ends with zero layer");
    require(coordinator.shutdown(),"shutdown");
}
void native_backend_provenance() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    auto begun=coordinator.begin_frame(); FrameTicket ticket{};
    require(coordinator.claim_render_frame(&ticket),"claim native provenance frame");
    auto stamp=backend(ticket);
    require(!coordinator.stage_render_frame(ticket.key,{},{}),"zero backend stamp rejected");
    require(!coordinator.stage_render_frame(ticket.key,{stamp.buffer_identity,0},{}),"pointer alone cannot stage");
    require(!coordinator.stage_render_frame(ticket.key,{0,stamp.producer_serial},{}),"serial alone cannot stage");
    require(coordinator.stage_render_frame(ticket.key,stamp,{}),"exact native backend stamp stages");
    require(!coordinator.mark_eye_complete(ticket.key,{stamp.buffer_identity+0x186300,stamp.producer_serial},0),
            "different SMP buffer cannot complete current prediction");
    require(!coordinator.mark_eye_complete(ticket.key,{stamp.buffer_identity,stamp.producer_serial-1},1),
            "reused native buffer with old serial cannot complete current prediction");
    complete(coordinator,ticket);
    const auto completed_stamp=stamp; stamp={};
    const auto result=coordinator.present(nullptr);
    require(result.status==CoordinatorStatus::submitted && result.backend==completed_stamp,
            "completion record survives native backend pointer clearing before present");
    require(coordinator.shutdown(),"shutdown");
}
void runtime_fault_requires_recreation() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    const auto ticket=stage(coordinator); complete(coordinator,ticket);
    transport.end_success=false;
    require(coordinator.present(nullptr).status==CoordinatorStatus::failed,"failed xrEndFrame is not successful submission");
    require(coordinator.begin_frame().status==CoordinatorStatus::runtime_faulted && transport.begins==1,
            "failed termination cannot advance into another runtime frame");
    require(!coordinator.before_reset(false),"device reset cannot repair failed runtime lifecycle");
    require(coordinator.shutdown(),"faulted runtime can shut down for recreation");
    require(coordinator.initialize({}),"explicit lifecycle recreation clears runtime fault");
    transport.begin_success=false;
    require(coordinator.begin_frame().status==CoordinatorStatus::failed,"failed runtime begin is surfaced");
    require(coordinator.begin_frame().status==CoordinatorStatus::runtime_faulted,
            "hard begin error is not retried as normal idle state");
    require(coordinator.shutdown(),"failed begin runtime shutdown");
}
void throwing_transport_is_contained() {
    FakeTransport transport; FrameCoordinator coordinator(transport);
    transport.throw_init=true;
    require(!coordinator.initialize({}),"throwing initialize reports failure");
    require(coordinator.begin_frame().status==CoordinatorStatus::runtime_faulted,
            "partially initialized transport stays blocked");
    require(coordinator.shutdown() && transport.stops==1,"partial initialization remains reachable for cleanup");
    transport.throw_init=false;
    require(coordinator.initialize({}),"clean lifecycle recreates after init exception");
    const auto ticket=stage(coordinator); complete(coordinator,ticket);
    transport.throw_submit=true;
    require(coordinator.present(nullptr).status==CoordinatorStatus::failed,"throwing submit reports failure");
    require(coordinator.begin_frame().status==CoordinatorStatus::runtime_faulted,"throwing submit blocks later runtime use");
    transport.throw_stop=true;
    require(!coordinator.shutdown(),"throwing cleanup is contained and retryable");
    transport.throw_stop=false;
    require(coordinator.shutdown(),"busy guard released after throwing transport call");
    require(coordinator.initialize({}),"initialize again after cleanup");
    transport.throw_begin=true;
    require(coordinator.begin_frame().status==CoordinatorStatus::failed && coordinator.shutdown(),
            "throwing begin also permits explicit cleanup");
}
void focused_fresh_input_only() {
    FakeTransport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    transport.action_sequence_override=99;
    auto ticket=stage(coordinator);
    require(ticket.frame.actions.hands[0].trigger.active && ticket.frame.actions.hands[0].trigger.current==0.7f,
            "fresh focused input reaches the published ticket");
    complete(coordinator,ticket); coordinator.present(nullptr);
    transport.focused=false; transport.action_sequence_override=100;
    ticket=stage(coordinator);
    const auto& hand=ticket.frame.actions.hands[0];
    require(!ticket.frame.actions.menu.active && !hand.trigger.active && hand.trigger.current==0 &&
            !hand.squeeze.active && hand.squeeze.current==0 && !hand.stick.active && hand.stick.current.x==0 &&
            !hand.primary.active && !hand.primary.current && !hand.secondary.active && !hand.trigger_click.active &&
            !hand.stick_click.active && !hand.thumbrest.active,"unfocused raw actions cannot reach game controls");
    require(ticket.frame.views_valid && ticket.frame.head_center.position.x==1 && hand.grip.position_valid &&
            hand.grip.pose.position.x==4 && hand.aim.orientation_valid,"focus loss preserves head/eye/controller graphics tracking");
    complete(coordinator,ticket); coordinator.present(nullptr);
    transport.focused=true;
    ticket=stage(coordinator);
    require(ticket.frame.actions.focused && !ticket.frame.actions.hands[0].trigger.active,
            "focus recovery cannot reuse the previously unfocused sample");
    complete(coordinator,ticket); coordinator.present(nullptr);
    transport.action_sequence_override=101;
    ticket=stage(coordinator);
    require(ticket.frame.actions.hands[0].trigger.active && ticket.frame.actions.menu.active,
            "fresh post-recovery sample restores controls");
    complete(coordinator,ticket); coordinator.present(nullptr);
    require(coordinator.shutdown(),"focused input cleanup");
}
unsigned pending_key_checks{};
void key_require(bool value,const char* reason) { ++pending_key_checks; require(value,reason); }
std::uint64_t key_publication_clock() noexcept { return 777; }
static_assert(!noexcept(std::declval<const FrameCoordinator&>().read_pending_key(nullptr)));
static_assert(!noexcept(std::declval<const FrameCoordinator&>().read_pending(nullptr)));

void key_matches(const FrameCoordinator& coordinator,FrameKey expected) {
    FrameKey key{0xfedcba9876543210ULL,0x123456789abcdef0ULL};
    FrameTicket ticket{};
    key_require(coordinator.read_pending_key(&key) && coordinator.read_pending(&ticket),"both metadata readers observe pending existence");
    key_require(key==expected && ticket.key==expected,"key-only and full metadata copies have exact generation and frame");
    key_require(ticket.published_ms==777 && ticket.frame.head_center.position.x==1,"full-ticket reader still copies publication and frame fields");
    key_require(!(key==FrameKey{expected.generation^0x8000000000000000ULL,expected.frame_id}) &&
                !(key==FrameKey{expected.generation,expected.frame_id^0x8000000000000000ULL}),"caller equality compares both complete key words");
    key_require(!coordinator.read_pending_key(nullptr) && !coordinator.read_pending(nullptr),"null output is refused even while pending");
}
void key_absent(const FrameCoordinator& coordinator) {
    const FrameKey sentinel{0xfedcba9876543210ULL,0x123456789abcdef0ULL};
    FrameKey key=sentinel;FrameTicket ticket{};ticket.key=sentinel;ticket.published_ms=987;
    key_require(!coordinator.read_pending_key(&key) && key==sentinel,"absence leaves complete key output unchanged");
    key_require(!coordinator.read_pending(&ticket) && ticket.key==sentinel && ticket.published_ms==987,"unchanged full reader preserves absent output");
    key_require(!coordinator.read_pending_key(nullptr) && !coordinator.read_pending(nullptr),"null output is refused while absent");
}
void pending_key_lifecycle() {
    FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);
    key_absent(coordinator);
    key_require(coordinator.initialize({}),"key lifecycle initialize");key_absent(coordinator);
    auto begun=coordinator.begin_frame();
    key_require(begun.status==CoordinatorStatus::started,"key lifecycle begins");key_matches(coordinator,begun.ticket.key);
    FrameKey saved{};key_require(coordinator.read_pending_key(&saved),"key snapshot saved before retirement");
    bool foreign=false;
    std::thread reader([&]{FrameKey key{};FrameTicket ticket{};foreign=coordinator.read_pending_key(&key)&&coordinator.read_pending(&ticket)&&key==saved&&ticket.key==saved;});reader.join();
    key_require(foreign,"foreign metadata reader needs no lifecycle ownership");
    FrameTicket ticket{};key_require(coordinator.claim_render_frame(&ticket),"key lifecycle claim");key_matches(coordinator,saved);
    key_require(coordinator.stage_render_frame(saved,backend(ticket),{}),"key lifecycle stage");key_matches(coordinator,saved);
    key_require(coordinator.mark_eye_complete(saved,backend(ticket),0),"key lifecycle first eye");key_matches(coordinator,saved);
    key_require(coordinator.mark_eye_complete(saved,backend(ticket),1),"key lifecycle second eye");key_matches(coordinator,saved);
    key_require(coordinator.present(nullptr).status==CoordinatorStatus::submitted,"key reads do not claim or prevent submission");key_absent(coordinator);
    key_require(saved==begun.ticket.key,"returned key is a value snapshot after retirement");
    begun=coordinator.begin_frame();key_matches(coordinator,begun.ticket.key);
    key_require(begun.ticket.key.generation==saved.generation && begun.ticket.key.frame_id!=saved.frame_id,"new prediction changes frame identity");
    key_require(coordinator.discard_pending().status==CoordinatorStatus::discarded_cancelled,"explicit discard clears current prediction");key_absent(coordinator);
    begun=coordinator.begin_frame();key_matches(coordinator,begun.ticket.key);
    transport.reset_ready=false;
    key_require(!coordinator.before_reset(false),"blocked reset still retires pending prediction");key_absent(coordinator);
    transport.reset_ready=true;
    key_require(coordinator.before_reset(false)&&coordinator.after_reset(true),"key lifecycle recovers reset");
    const auto fresh=coordinator.begin_frame();key_matches(coordinator,fresh.ticket.key);
    key_require(fresh.ticket.key.generation!=begun.ticket.key.generation,"reset generation is included in key read");
    transport.stop_ready=false;
    key_require(!coordinator.shutdown(),"incomplete shutdown leaves no pending metadata");key_absent(coordinator);
    transport.stop_ready=true;key_require(coordinator.shutdown(),"shutdown retry");key_absent(coordinator);
    key_require(coordinator.initialize({}),"key lifecycle explicit recreation");
    const auto recreated=coordinator.begin_frame();key_matches(coordinator,recreated.ticket.key);
    key_require(recreated.ticket.key.generation!=fresh.ticket.key.generation,"recreation cannot reuse saved generation");
    key_require(coordinator.shutdown(),"key lifecycle cleanup");key_absent(coordinator);
}
void pending_key_metadata_only() {
    FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);
    key_require(coordinator.initialize({}),"metadata-only initialize");
    const NativeBuildIdentity native{0x123400,17,31};
    auto begun=coordinator.begin_frame();SceneReservation reservation{};
    key_require(coordinator.reserve_scene_frame(native,&reservation),"metadata-only reservation");key_matches(coordinator,begun.ticket.key);
    key_require(coordinator.cancel_scene_reservation(begun.ticket.key,reservation.serial),"metadata-only cancelled reservation");key_matches(coordinator,begun.ticket.key);
    FrameTicket ticket{};key_require(!coordinator.consume_scene_reservation(reservation,native,&ticket),"invalid reservation remains ineligible despite readable key");
    coordinator.discard_pending();
    begun=coordinator.begin_frame();key_require(coordinator.reserve_scene_frame(native,&reservation),"metadata-only fresh reservation");
    key_require(coordinator.consume_scene_reservation(reservation,native,&ticket),"metadata-only consume reservation");key_matches(coordinator,begun.ticket.key);
    key_require(coordinator.stage_render_frame(ticket.key,backend(ticket),{}),"metadata-only stage consumed reservation");
    key_require(coordinator.mark_eye_complete(ticket.key,backend(ticket),0)&&!coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"metadata-only duplicate eye invalidates pair");
    key_matches(coordinator,begun.ticket.key);
    key_require(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete,"readable invalid pair cannot submit");
    transport.render=false;begun=coordinator.begin_frame();key_matches(coordinator,begun.ticket.key);
    key_require(!coordinator.claim_render_frame(&ticket),"readable nonrendering prediction remains unclaimable");coordinator.discard_pending();
    transport.render=true;transport.views_valid=false;begun=coordinator.begin_frame();key_matches(coordinator,begun.ticket.key);
    key_require(!coordinator.claim_render_frame(&ticket),"readable invalid-view prediction remains unclaimable");coordinator.discard_pending();
    transport.views_valid=true;transport.begin_success=false;
    key_require(coordinator.begin_frame().status==CoordinatorStatus::failed && coordinator.runtime_faulted(),"failed runtime has no key");key_absent(coordinator);
    key_require(coordinator.shutdown(),"metadata-only cleanup");
}
struct KeyProbe final { FrameCoordinator* coordinator{};unsigned calls{}; };
void key_probe_outside_lock(void* context) {
    auto& probe=*static_cast<KeyProbe*>(context);
    std::promise<bool> result;auto done=result.get_future();
    std::thread reader([&]{
        FrameKey key{51,53};FrameTicket ticket{};ticket.key={51,53};
        const bool key_absence=!probe.coordinator->read_pending_key(&key)&&key==FrameKey{51,53};
        const bool full_absence=!probe.coordinator->read_pending(&ticket)&&ticket.key==FrameKey{51,53};
        const bool null_rejected=!probe.coordinator->read_pending_key(nullptr);
        const bool not_faulted=!probe.coordinator->runtime_faulted();
        result.set_value(key_absence&&full_absence&&null_rejected&&not_faulted);
    });
    // A lock-scope regression exits the fixture within a fixed deadline rather
    // than hanging a coordinator callback or detaching a thread with references.
    key_require(done.wait_for(std::chrono::seconds(2))==std::future_status::ready,"transport callback permits bounded foreign metadata reads");
    reader.join();key_require(done.get(),"metadata locks are released before subsequent reads and transport callbacks");++probe.calls;
}
void pending_key_lock_scope() {
    FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);KeyProbe probe{&coordinator};
    key_require(coordinator.initialize({}),"lock-scope initialize");
    transport.metadata_context=&probe;transport.metadata_probe=key_probe_outside_lock;
    key_require(coordinator.begin_frame().status==CoordinatorStatus::started,"begin callback has no metadata lock");
    key_require(coordinator.discard_pending().status==CoordinatorStatus::discarded_cancelled,"discard callback has no metadata lock");
    key_require(coordinator.before_reset(false)&&coordinator.after_reset(true),"reset callback has no metadata lock");
    key_require(coordinator.shutdown(),"shutdown callback has no metadata lock");
    key_require(probe.calls==4,"all four bounded transport callback probes ran");
}
void rejected_native_render_never_submits() {
    FakeTransport transport;FrameCoordinator coordinator(transport);
    require(coordinator.initialize({}),"rejected render fixture initializes");
    require(!coordinator.reject_render_frame({1,1}),"missing pending frame is never fabricated by rejection");
    for(unsigned stage_case=0;stage_case<5;++stage_case) {
        const auto begun=coordinator.begin_frame();FrameTicket ticket{};
        require(begun.status==CoordinatorStatus::started,"rejection case starts fresh prediction");
        auto wrong=begun.ticket.key;++wrong.frame_id;
        require(!coordinator.reject_render_frame(wrong),"different frame rejection cannot invalidate current prediction");
        if(stage_case)require(coordinator.claim_render_frame(&ticket),"wrong key left current prediction claimable");
        else ticket=begun.ticket;
        xr::QuadLayer quad{};quad.size_meters={1,1};
        if(stage_case==2||stage_case==3) {
            require(coordinator.stage_render_frame(ticket.key,backend(ticket),{}),"projection source stages before late rejection");
            require(coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"first eye completes before rejection");
            if(stage_case==3)require(coordinator.mark_eye_complete(ticket.key,backend(ticket),1),"full old pair completes before rejection");
        }
        if(stage_case==4) {
            require(coordinator.stage_menu_frame(ticket.key,backend(ticket),quad),"quad source stages before rejection");
            require(coordinator.mark_menu_complete(ticket.key,backend(ticket)),"quad source completes before rejection");
        }
        const auto ended=transport.discards,submitted=transport.submits;
        bool rejected=false;std::thread frontend([&]{rejected=coordinator.reject_render_frame(ticket.key);});frontend.join();
        require(rejected&&coordinator.reject_render_frame(ticket.key),"foreign frontend metadata rejection is same-key idempotent");
        require(transport.discards==ended&&transport.submits==submitted,"rejection never calls XR or submits from frontend");
        FrameTicket replacement{};
        require(!coordinator.claim_render_frame(&replacement),"rejected prediction cannot claim an ordinary stock-gun pair");
        require(!coordinator.stage_render_frame(ticket.key,backend(ticket),{}),"rejected prediction cannot stage or restage projection");
        require(!coordinator.stage_menu_frame(ticket.key,backend(ticket),quad),"rejected native scene cannot become a fallback quad");
        require(!coordinator.mark_eye_complete(ticket.key,backend(ticket),1)&&!coordinator.mark_menu_complete(ticket.key,backend(ticket)),
                "late backend completion cannot revive rejected production");
        require(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete&&
                transport.discards==ended+1&&transport.submits==submitted,"owner ends even completed rejected source with zero layers");
        require(!coordinator.reject_render_frame(ticket.key),"retired key cannot reject a later prediction");
    }
    const auto recovered=stage(coordinator);complete(coordinator,recovered);
    require(coordinator.present(nullptr).status==CoordinatorStatus::submitted,"next independently tracked frame recovers without reset or old-pose reuse");
    require(coordinator.shutdown(),"rejection fixture shuts down");
}

void reservation_refusal_diagnostic_is_observation_only() {
    FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);
    const NativeBuildIdentity native{0x3b40100u,71,99};SceneReservation reservation{};SceneReservationDiagnostic d{};
    require(!coordinator.reserve_scene_frame(native,&reservation,&d)&&!std::strcmp(d.reason,"not_initialized")&&!d.initialized,"diagnostic identifies uninitialized without native work");
    require(coordinator.initialize({}),"reservation diagnostic initializes");
    require(!coordinator.reserve_scene_frame(native,&reservation,&d)&&!std::strcmp(d.reason,"no_pending")&&!d.pending,"diagnostic identifies absent prediction");
    struct BusyProbe {FrameCoordinator* coordinator;bool seen{};} probe{&coordinator};
    transport.metadata_context=&probe;
    transport.metadata_probe=[](void* opaque){auto& p=*static_cast<BusyProbe*>(opaque);SceneReservation out{};SceneReservationDiagnostic state{};
        p.seen=!p.coordinator->reserve_scene_frame({1,1,1},&out,&state)&&!std::strcmp(state.reason,"busy")&&state.busy&&!state.pending;};
    const auto begun=coordinator.begin_frame();transport.metadata_probe=nullptr;
    require(probe.seen&&begun.status==CoordinatorStatus::started,"foreign transport begin boundary observes busy without changing begin");
    require(coordinator.reserve_scene_frame(native,&reservation,&d),"diagnostic does not deny valid reservation");
    SceneReservation refused{};
    require(!coordinator.reserve_scene_frame(native,&refused,&d)&&!std::strcmp(d.reason,"reserved")&&d.pending&&!d.claimed&&
        d.key==begun.ticket.key&&d.reservation_serial==reservation.serial&&d.reserved_native==native&&d.published_ms==begun.ticket.published_ms,"reserved refusal preserves exact copied identity");
    FrameTicket consumed{};require(coordinator.consume_scene_reservation(reservation,native,&consumed),"diagnostic does not retire or consume reservation");
    require(!coordinator.reserve_scene_frame(native,&refused,&d)&&!std::strcmp(d.reason,"claimed")&&d.claimed&&!d.invalid,"claimed refusal remains distinct from reserved");
    require(coordinator.reject_render_frame(consumed.key),"diagnostic fixture marks same key invalid");
    require(!coordinator.reserve_scene_frame(native,&refused,&d)&&!std::strcmp(d.reason,"invalid")&&d.invalid&&d.claimed,"invalid refusal retains claimed context");
    require(coordinator.discard_pending().status==CoordinatorStatus::discarded_cancelled,"diagnostics never add transport calls");
    for(unsigned variant=0;variant<2;++variant){transport.render=variant!=0;transport.views_valid=variant==0;
        require(coordinator.begin_frame().status==CoordinatorStatus::started,"invalid render fixture begins normally");
        require(!coordinator.reserve_scene_frame(native,&refused,&d)&&!std::strcmp(d.reason,variant?"invalid_views":"not_rendering"),"renderability refusal identifies original failed gate");
        require(coordinator.discard_pending().status==CoordinatorStatus::discarded_cancelled,"unrenderable prediction keeps original discard behavior");}
    require(transport.begins==3&&transport.discards==3&&transport.submits==0&&coordinator.shutdown(),"diagnostic changed no lifecycle or transport counts");
}
void actual_finish_diagnostic_and_bounded_reporting() {
    FrameDiscardDiagnosticBudget budget;
    for(unsigned i=0;i<300;++i)require(!budget.admit(false,true),"disabled discard diagnostic consumes no budget");
    for(unsigned i=0;i<8;++i)require(budget.admit(true,false),"bounded startup discard is never modulo sampled");
    require(!budget.admit(true,false)&&budget.gameplay==0,"startup cannot consume gameplay drop budget");
    for(unsigned i=0;i<256;++i)require(budget.admit(true,true),"every admitted gameplay discard is recorded");
    require(!budget.admit(true,true)&&!budget.available(true,true),"gameplay output remains bounded");
    for(unsigned variant=0;variant<5;++variant){
        FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);require(coordinator.initialize({}),"finish diagnostic initializes");
        FrameTicket ticket{};
        if(variant==0)ticket=coordinator.begin_frame().ticket;
        else {ticket=stage(coordinator);require(coordinator.mark_eye_complete(ticket.key,backend(ticket),0),"finish diagnostic first eye");
            if(variant!=1)require(coordinator.mark_eye_complete(ticket.key,backend(ticket),1),"finish diagnostic second eye");}
        if(variant==3)require(coordinator.reject_render_frame(ticket.key),"finish diagnostic invalidates complete pair");
        SceneReservationDiagnostic d{};const auto result=variant==4?coordinator.discard_pending(&d):coordinator.present(nullptr,&d);
        const char* expected[]{"unclaimed","incomplete_eyes","complete","invalid","explicit_discard"};
        require(d.pending&&d.key==ticket.key&&d.published_ms==ticket.published_ms&&!std::strcmp(d.reason,expected[variant]),"actual finish snapshot retains original locked key and reason after retirement");
        FrameTicket retired{};require(!coordinator.read_pending(&retired),"diagnostic never retains pending prediction");
        require((variant==2?result.status==CoordinatorStatus::submitted:result.status==CoordinatorStatus::discarded_incomplete||result.status==CoordinatorStatus::discarded_cancelled)&&
            transport.submits==(variant==2?1u:0u)&&transport.discards==(variant==2?0u:1u),"diagnostic invokes original submit or discard exactly once");
        if(variant)require(d.backend_producer==backend(ticket).producer_serial&&d.backend_buffer==backend(ticket).buffer_identity&&d.eye_mask==(variant==1?1u:3u),"completed backend identity and eye ownership copied at finish");
        require(coordinator.shutdown(),"finish diagnostic shuts down");
    }
}
void unowned_rejection_age_and_reservation_guard() {
    for(unsigned variant=0;variant<7;++variant){
        FakeTransport transport;FrameCoordinator coordinator(transport,key_publication_clock);
        require(coordinator.initialize({}),"unowned preservation initializes");
        if(variant==5)transport.render=false;
        if(variant==6)transport.views_valid=false;
        const auto begun=coordinator.begin_frame();const auto published=begun.ticket.published_ms;
        const auto now=variant==2?published-1:published+(variant==1?101:100);
        const auto age=variant==3?0u:100u;
        if(variant==4)require(coordinator.reject_render_frame(begun.ticket.key),"pre-invalid fixture is explicit");
        const auto outcome=coordinator.reject_render_frame_or_preserve_unowned(begun.ticket.key,true,now,age);
        require(outcome==(variant==0?RenderRejectionResult::preserved_unowned:RenderRejectionResult::invalidated),"only current renderable pristine prediction within unchanged100ms boundary is preserved");
        FrameTicket rejected{};require(!coordinator.claim_render_frame(&rejected),"preserved or invalid scene never becomes generic stereo/menu claim");
        if(variant==0){
            require(coordinator.reject_render_frame_or_preserve_unowned(begun.ticket.key,true,now,age)==RenderRejectionResult::preserved_unowned,"repeat failed native invocation does not change prediction lifetime");
            FrameTicket same{};require(coordinator.read_pending(&same)&&same.key==begun.ticket.key&&same.published_ms==published&&same.frame.head_center.position.x==begun.ticket.frame.head_center.position.x,"deferred prediction keeps original key age and pose");
            SceneReservation reserved{};SceneReservationDiagnostic d{};
            require(!coordinator.reserve_scene_frame({0,9,7},&reserved,&d)&&d.awaiting_tracked_reservation,"failed native reservation cannot clear awaiting-tracked state");
            require(!coordinator.claim_render_frame(&rejected),"failed reservation leaves all ordinary fallback paths barred");
            require(coordinator.reserve_scene_frame({0x3b40100,9,7},&reserved),"independent native reservation still works");
            require(coordinator.cancel_scene_reservation(reserved.ticket.key,reserved.serial),"later native body failure retains cancellation policy");
        }
        SceneReservationDiagnostic ended{};
        require(coordinator.discard_pending(&ended).status==CoordinatorStatus::discarded_cancelled&&transport.submits==0&&transport.discards==1,"owner bounded timeout/cancellation can still end the original prediction once");
        require(coordinator.shutdown(),"unowned guard fixture shuts down");
    }
}
unsigned deadline_checks{};
std::uint64_t deadline_publication=1000;
std::uint64_t deadline_clock() noexcept {return deadline_publication;}
void deadline_require(bool value,const char* reason){++deadline_checks;require(value,reason);}
void pristine_prediction_uses_original_deadline() {
    FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
    deadline_require(!c.fresh_unowned_prediction({2,1},1047,100),"uninitialized coordinator never grants extra wait");
    deadline_require(c.initialize({}),"deadline fixture initializes");
    deadline_require(!c.fresh_unowned_prediction({2,1},1047,100),"absent prediction cannot grant extra wait");
    deadline_publication=1000;const auto begun=c.begin_frame();const auto key=begun.ticket.key;
    // Reproduce the five native Presents within47ms from both actual failures.
    // These metadata reads must not retire, claim, reserve, or republish XR.
    for(const auto elapsed:{0u,16u,31u,47u,47u,99u,100u})
        deadline_require(c.fresh_unowned_prediction(key,1000+elapsed,100),"rapid Presents retain the same pristine prediction through inclusive100ms");
    FrameTicket retained{};
    deadline_require(c.read_pending(&retained)&&retained.key==key&&retained.published_ms==1000&&
        retained.frame.frame_id==begun.ticket.frame.frame_id&&retained.frame.head_center.position.x==begun.ticket.frame.head_center.position.x&&
        retained.frame.actions.sequence==begun.ticket.frame.actions.sequence,
        "repeated deadline reads preserve original pose, input, key and publication timestamp");
    deadline_require(transport.begins==1&&transport.discards==0&&transport.submits==0,
        "waiting produces no XR begin/end, capture or pixel submission");
    deadline_require(!c.fresh_unowned_prediction(key,0,100)&&!c.fresh_unowned_prediction(key,999,100),"zero/backward current clock cannot extend publication");
    deadline_require(!c.fresh_unowned_prediction(key,1047,0),"zero validity window cannot extend publication");
    deadline_require(!c.fresh_unowned_prediction({key.generation+1,key.frame_id},1047,100)&&
        !c.fresh_unowned_prediction({key.generation,key.frame_id+1},1047,100),"wrong generation or frame cannot borrow another prediction deadline");
    bool foreign=false;std::thread frontend([&]{foreign=c.fresh_unowned_prediction(key,1047,100);});frontend.join();
    deadline_require(foreign,"deadline query is metadata-only and safe from another thread");
    SceneReservation r{};const NativeBuildIdentity native{0x3b40100,71,99};
    deadline_require(c.reserve_scene_frame(native,&r),"fresh native scene can still acquire original prediction after fifth Present");
    deadline_require(!c.fresh_unowned_prediction(key,1100,100),"reserved work never uses pristine timeout exception");
    deadline_require(c.present(nullptr).status==CoordinatorStatus::pending_frame&&transport.discards==0,
        "reserved scene retains its separate existing owner lifetime");
    FrameTicket consumed{};
    deadline_require(c.consume_scene_reservation(r,native,&consumed)&&consumed.key==key&&consumed.published_ms==1000,
        "native consumer receives unchanged canonical ticket");
    deadline_require(!c.fresh_unowned_prediction(key,1100,100),"consumed ownership cannot use pristine exception");
    deadline_require(c.stage_render_frame(key,backend(consumed),{}),"new native scene stages its own backend pair");
    complete(c,consumed);
    const auto submitted=c.present(nullptr);
    deadline_require(submitted.status==CoordinatorStatus::submitted&&submitted.key==key&&submitted.transport.released_eye_mask==3&&
        transport.submits==1&&transport.discards==0,"only both newly completed eyes submit, exactly once");

    deadline_publication=2000;const auto overdue=c.begin_frame();
    deadline_require(c.fresh_unowned_prediction(overdue.ticket.key,2099,100)&&c.fresh_unowned_prediction(overdue.ticket.key,2100,100),
        "expiry fixture accepts99ms and100ms without changing timestamp");
    deadline_require(!c.fresh_unowned_prediction(overdue.ticket.key,2101,100),"101ms expires pristine prediction regardless of rapid Present count");
    SceneReservationDiagnostic ended{};
    deadline_require(c.discard_pending(&ended).status==CoordinatorStatus::discarded_cancelled&&ended.published_ms==2000&&
        !ended.claimed&&!ended.staged&&!ended.reservation_serial&&transport.discards==1&&transport.submits==1,
        "deadline expiry ends original unowned frame zero-layer once without old pixel reuse");
    deadline_require(!c.fresh_unowned_prediction(overdue.ticket.key,2101,100),"expired key cannot be revived");
    deadline_publication=2101;const auto next=c.begin_frame();
    deadline_require(next.ticket.key!=overdue.ticket.key&&!c.mark_eye_complete(overdue.ticket.key,backend(overdue.ticket),0),
        "later independent prediction refuses old-frame completion");
    deadline_require(c.shutdown(),"deadline fixture shuts down normally");
}
void pristine_deadline_refusals_preserve_existing_paths() {
    // Reach each ownership state through real coordinator operations. The
    // exception must not alter the existing claimed/partial/menu timeout path.
    for(unsigned state=0;state<9;++state){
        deadline_publication=3000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
        deadline_require(c.initialize({}),"deadline ownership fixture initializes");
        const auto begun=c.begin_frame();const auto key=begun.ticket.key;FrameTicket t{};
        if(state==0)deadline_require(c.reject_render_frame(key),"explicit invalidation remains immediate");
        else if(state==1){SceneReservation r{};deadline_require(c.reserve_scene_frame({0x3b40100,1,1},&r),"reserve negative fixture");}
        else if(state==2)deadline_require(c.reject_render_frame_or_preserve_unowned(key,true,3047,100)==RenderRejectionResult::preserved_unowned,
            "awaiting tracked reservation retains its existing rejection policy");
        else {
            deadline_require(c.claim_render_frame(&t),"ownership fixture claims once");
            if(state>=4&&state<=6){deadline_require(c.stage_render_frame(key,backend(t),{}),"ownership fixture stages exact pixels");
                if(state>=5)deadline_require(c.mark_eye_complete(key,backend(t),0),"ownership fixture completes first eye");
                if(state==6)deadline_require(c.mark_eye_complete(key,backend(t),1),"ownership fixture completes second eye");}
            if(state>=7){xr::QuadLayer quad{};quad.size_meters={1,1};
                deadline_require(c.stage_menu_frame(key,backend(t),quad),"menu fixture stages its existing quad");
                if(state==8)deadline_require(c.mark_menu_complete(key,backend(t)),"menu fixture completes its existing quad");}
        }
        deadline_require(c.fresh_unowned_prediction(key,3047,100)==(state==2),
            "only unowned awaiting marker can wait; invalid/reserved/claimed/staged/partial/completed/menu remain refused");
        if(state==2)deadline_require(!c.claim_render_frame(&t),"waiting never clears the tracked-only claim restriction");
        deadline_require(transport.begins==1&&transport.submits==0&&transport.discards==0,"refusal query makes no transport calls");
        deadline_require(c.discard_pending().status==CoordinatorStatus::discarded_cancelled&&transport.discards==1&&transport.submits==0,
            "explicit cancellation still retires every refused state immediately");
        deadline_require(c.shutdown(),"ownership fixture shutdown");
    }
    for(unsigned variant=0;variant<4;++variant){
        deadline_publication=variant==0?0:4000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
        if(variant==1)transport.render=false;
        if(variant==2)transport.views_valid=false;
        if(variant==3)transport.begins=~0u; // The fake producer wraps its frame id to zero.
        deadline_require(c.initialize({}),"invalid prediction fixture initializes");const auto t=c.begin_frame().ticket;
        deadline_require(!c.fresh_unowned_prediction(t.key,4047,100),"zero timestamp/frame or nonrendering/invalid views refuse grace");
        deadline_require(c.discard_pending().status==CoordinatorStatus::discarded_cancelled&&transport.discards==1,
            "malformed/nonrendering prediction retains explicit zero-layer retirement");
        deadline_require(c.shutdown(),"invalid prediction fixture shutdown");
    }
    deadline_publication=5000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
    deadline_require(c.initialize({}),"lifecycle deadline fixture initializes");const auto old=c.begin_frame().ticket;
    transport.reset_ready=false;
    deadline_require(!c.before_reset(false)&&!c.fresh_unowned_prediction(old.key,5047,100),"reset-blocked lifecycle cannot retain pre-reset prediction");
    transport.reset_ready=true;deadline_require(c.before_reset(false)&&c.after_reset(true),"reset can recover normally");
    const auto fresh=c.begin_frame().ticket;
    deadline_require(fresh.key.generation!=old.key.generation&&!c.fresh_unowned_prediction(old.key,5047,100),"reset generation retires prior wait permission");
    transport.end_success=false;
    deadline_require(c.discard_pending().status==CoordinatorStatus::failed&&c.runtime_faulted()&&
        !c.fresh_unowned_prediction(fresh.key,5047,100),"runtime failure cannot receive extra time");
    deadline_require(c.shutdown()&&!c.fresh_unowned_prediction(fresh.key,5047,100),"shutdown retires permission");
}
void awaiting_tracked_deadline_never_grants_scene_ownership() {
    deadline_publication=6000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
    deadline_require(c.initialize({}),"tracked-wait fixture initializes");const auto t=c.begin_frame().ticket;
    deadline_require(c.reject_render_frame_or_preserve_unowned(t.key,true,6000,100)==RenderRejectionResult::preserved_unowned,
        "failed unbound native invocation marks original unowned prediction as tracked-only");
    SceneReservationDiagnostic d{};FrameTicket claimed{};xr::QuadLayer quad{};quad.size_meters={1,1};
    for(const auto elapsed:{0u,32u,99u,100u}){
        deadline_require(c.fresh_unowned_prediction(t.key,6000+elapsed,100,&d)&&d.pending&&d.awaiting_tracked_reservation&&
            !d.claimed&&!d.staged&&!d.reservation_serial&&d.key==t.key&&d.published_ms==6000,
            "32/99/100ms waiting observes but never mutates original tracked-only metadata");
        deadline_require(!c.claim_render_frame(&claimed)&&!c.stage_render_frame(t.key,backend(t),{})&&
            !c.stage_menu_frame(t.key,backend(t),quad),"no generic stock scene, old pixels or menu can acquire tracked-only prediction while waiting");
    }
    SceneReservation r{};
    deadline_require(!c.reserve_scene_frame({0,1,1},&r,&d)&&d.awaiting_tracked_reservation,
        "failed native reservation cannot clear marker or refresh original lifetime");
    deadline_require(!c.fresh_unowned_prediction(t.key,5999,100,&d)&&d.awaiting_tracked_reservation&&d.published_ms==6000,
        "tracked-only wait also rejects a future publication without clearing marker");
    deadline_require(transport.begins==1&&transport.submits==0&&transport.discards==0,
        "all wait checks are metadata-only, with no fabricated frame or submit");
    const NativeBuildIdentity native{0x3b40100,82,100};
    deadline_require(c.reserve_scene_frame(native,&r),"independently validated later tracked producer may reserve original prediction");
    deadline_require(!c.fresh_unowned_prediction(t.key,6100,100,&d)&&!d.awaiting_tracked_reservation&&d.reservation_serial==r.serial&&d.published_ms==6000,
        "only successful native reservation clears marker and ends unowned wait permission");
    deadline_require(c.consume_scene_reservation(r,native,&claimed)&&claimed.key==t.key&&claimed.published_ms==6000,
        "same producer consumes canonical original key and time");
    deadline_require(c.stage_render_frame(t.key,backend(claimed),{}),"tracked consumer stages its own new backend pair");complete(c,claimed);
    deadline_require(c.present(nullptr).status==CoordinatorStatus::submitted&&transport.submits==1&&transport.discards==0,
        "only both newly produced eyes submit once after genuine tracked acquisition");

    deadline_publication=7000;const auto expired=c.begin_frame().ticket;
    deadline_require(c.reject_render_frame_or_preserve_unowned(expired.key,true,7000,100)==RenderRejectionResult::preserved_unowned,
        "expiry case receives independent original tracked-only prediction");
    deadline_require(!c.fresh_unowned_prediction(expired.key,7101,100,&d)&&d.awaiting_tracked_reservation&&d.published_ms==7000,
        "101ms expires tracked-only waiting without refreshing timestamp or clearing marker");
    deadline_require(!c.claim_render_frame(&claimed)&&c.discard_pending(&d).status==CoordinatorStatus::discarded_cancelled&&
        d.awaiting_tracked_reservation&&d.published_ms==7000&&transport.discards==1&&transport.submits==1,
        "expired tracked-only frame is still forbidden to generic claims and ends zero-layer once");
    const auto invalid=c.begin_frame().ticket;
    deadline_require(c.reject_render_frame_or_preserve_unowned(invalid.key,true,7000,100)==RenderRejectionResult::preserved_unowned&&
        c.reject_render_frame(invalid.key),"explicit rejection can still invalidate a tracked-only waiting frame");
    deadline_require(!c.fresh_unowned_prediction(invalid.key,7032,100,&d)&&d.invalid&&d.awaiting_tracked_reservation,
        "tracked-only marker never overrides explicit invalidation");
    deadline_require(c.shutdown(),"tracked-wait fixture shuts down");
}
void deferred_reservation_is_atomic_and_nonrenewable() {
    const NativeBuildIdentity native{0x3b40100,91,105};
    for(unsigned fault=0;fault<13;++fault){
        deadline_publication=8000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
        deadline_require(c.initialize({}),"deferred atomic fixture initializes");const auto t=c.begin_frame().ticket;
        deadline_require(c.reject_render_frame_or_preserve_unowned(t.key,true,8000,100)==RenderRejectionResult::preserved_unowned,
            "deferred fixture preserves original pristine key");
        auto key=t.key;auto published=t.published_ms;auto candidate=native;
        std::uint64_t now=8032,limit=100;FrameTicket claimed{};SceneReservation r{};
        switch(fault){
        case 0:key.generation++;break;case 1:key.frame_id++;break;
        case 2:published++;break;case 3:published=0;break;case 4:now=7999;break;
        case 5:now=8101;break;case 6:limit=0;break;case 7:candidate.buffer_identity=0;break;
        case 8:candidate.build_serial=0;break;
        case 9:deadline_require(c.reject_render_frame(t.key),"explicit invalidation precedes special reservation");break;
        case 10:deadline_require(c.reserve_scene_frame(native,&r),"existing native owner wins before special reservation");break;
        case 11:deadline_require(c.reserve_scene_frame(native,&r)&&c.consume_scene_reservation(r,native,&claimed),"existing consumed owner wins");break;
        case 12:deadline_require(c.reserve_scene_frame(native,&r)&&c.consume_scene_reservation(r,native,&claimed)&&
            c.stage_render_frame(t.key,backend(claimed),{})&&c.mark_eye_complete(t.key,backend(claimed),0),"partial new pixels already own frame");break;
        }
        r.serial=999;SceneReservationDiagnostic d{};
        deadline_require(!c.reserve_deferred_scene_frame(key,published,candidate,now,limit,&r,&d)&&!r.serial&&
            d.key==t.key&&d.published_ms==8000,"wrong key/time, age, invalid state or any existing ownership refuses and clears output only");
        deadline_require(transport.begins==1&&!transport.submits&&!transport.discards,"refused reservation performs no transport work");
        deadline_require(c.shutdown(),"deferred refusal fixture shutdown");
    }
    for(const auto age:{0u,99u,100u}){
        deadline_publication=9000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
        deadline_require(c.initialize({}),"deferred age fixture initializes");const auto t=c.begin_frame().ticket;
        SceneReservation r{};FrameTicket claimed{};
        deadline_require(!c.reserve_deferred_scene_frame(t.key,9000,native,9000+age,100,&r)&&c.claim_render_frame(&claimed),
            "special reservation requires marker and cannot steal ordinary prediction");
        deadline_require(c.discard_pending().status==CoordinatorStatus::discarded_cancelled,"ordinary fixture retires");
        const auto waiting=c.begin_frame().ticket;
        deadline_require(c.reject_render_frame_or_preserve_unowned(waiting.key,true,9000,100)==RenderRejectionResult::preserved_unowned&&
            c.reserve_deferred_scene_frame(waiting.key,9000,native,9000+age,100,&r)&&r.ticket.published_ms==9000,
            "exact original key and timestamp allow only inclusive original deadline");
        deadline_require(!c.claim_render_frame(&claimed)&&c.consume_scene_reservation(r,native,&claimed)&&
            !c.reserve_deferred_scene_frame(waiting.key,9000,native,9000+age,100,&r),"real reservation is once-only and never generic");
        deadline_require(c.shutdown(),"deferred age fixture shutdown");
    }
    deadline_publication=10000;FakeTransport transport;FrameCoordinator c(transport,deadline_clock);
    deadline_require(c.initialize({}),"retirement race fixture initializes");const auto old=c.begin_frame().ticket;
    deadline_require(c.reject_render_frame_or_preserve_unowned(old.key,true,10000,100)==RenderRejectionResult::preserved_unowned,
        "race captures old waiting proof");
    deadline_require(c.discard_pending().status==CoordinatorStatus::discarded_cancelled,"race retires old key between policy and reserve");
    deadline_publication=10020;const auto newer=c.begin_frame().ticket;SceneReservation r{};FrameTicket claimed{};
    deadline_require(!c.reserve_deferred_scene_frame(old.key,old.published_ms,native,10032,100,&r)&&
        c.claim_render_frame(&claimed)&&claimed.key==newer.key&&claimed.published_ms==10020,
        "stale copied policy cannot reserve new prediction and leaves its original generic ownership unchanged");
    deadline_require(c.before_reset(false)&&c.after_reset(true),"reset retires consumed prediction");
    deadline_require(!c.reserve_deferred_scene_frame(newer.key,newer.published_ms,native,10032,100,&r),"reset generation blocks old special reservation");
    deadline_require(c.shutdown(),"retirement race fixture shutdown");
}

// Pacing-flight receipts observe only an already completed submission. They do
// not manufacture a pending ticket or permit a frontend to reserve it again.
struct PacingFlightProbe {
    FrameCoordinator* coordinator{};
    SceneReservation reservation{};
    BackendFrameIdentity submitted_backend{};
    bool expect_flight{};
    unsigned calls{};
    std::uint64_t observed_serial{};
};
void pacing_flight_probe(void* opaque) {
    auto& p=*static_cast<PacingFlightProbe*>(opaque);
    const auto check_snapshot=[&](const ProducerPacingSnapshot& s){
        require(s.pending.busy&&!s.pending.pending,"transport callback observes truthful empty and busy coordinator");
        if(!p.expect_flight){require(!s.flight.serial,"unrelated or rejected transport operation has no pacing flight");return;}
        const auto& d=s.flight.completed;
        require(s.flight.serial&&s.generation==p.reservation.ticket.key.generation,
                "in-flight receipt has exact healthy coordinator generation");
        require(d.key==p.reservation.ticket.key&&d.published_ms==p.reservation.ticket.published_ms&&
                d.reservation_serial==p.reservation.serial&&d.reserved_native==p.reservation.native&&
                d.backend_buffer==p.submitted_backend.buffer_identity&&d.backend_producer==p.submitted_backend.producer_serial,
                "in-flight receipt preserves exact old native and XR provenance");
        require(d.pending&&d.claimed&&d.staged&&!d.invalid&&!d.menu&&!d.menu_complete&&
                d.should_render&&d.views_valid&&d.eye_mask==3&&!d.awaiting_tracked_reservation,
                "only completed ordinary reserved stereo submission is pacing evidence");
        if(p.observed_serial)require(s.flight.serial==p.observed_serial,"one submit retains a stable copied operation serial");
    };
    auto first=p.coordinator->producer_pacing_snapshot();check_snapshot(first);
    if(p.expect_flight)p.observed_serial=first.flight.serial;
    // A blocked transport operation must not retain the coordinator mutex.
    // The bounded foreign reader performs no claim/reservation/native work.
    std::promise<ProducerPacingSnapshot> promise;auto ready=promise.get_future();
    std::thread reader([&]{promise.set_value(p.coordinator->producer_pacing_snapshot());});
    require(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,
            "in-flight metadata remains readable by frontend while submit callback is blocked");
    reader.join();check_snapshot(ready.get());
    FrameTicket ticket{};SceneReservation refused{};
    require(!p.coordinator->read_pending(&ticket)&&!p.coordinator->claim_render_frame(&ticket)&&
            !p.coordinator->reserve_scene_frame({0x3b40100,222,333},&refused),
            "copied in-flight identity never restores a claimable or reservable prediction");
    ++p.calls;
}
SceneReservation stage_pacing_reserved(FrameCoordinator& c,bool both=true) {
    const auto t=c.begin_frame();require(t.status==CoordinatorStatus::started,"pacing fixture begins normally");
    const auto b=backend(t.ticket);SceneReservation r{};FrameTicket consumed{};
    const NativeBuildIdentity native{b.buffer_identity,static_cast<std::uint32_t>(100+t.ticket.key.frame_id),200+t.ticket.key.frame_id};
    require(c.reserve_scene_frame(native,&r)&&c.consume_scene_reservation(r,native,&consumed),
            "pacing fixture earns an actual native reservation");
    require(c.stage_render_frame(consumed.key,b,{})&&c.mark_eye_complete(consumed.key,b,0),
            "pacing fixture stages its own exact backend");
    if(both)require(c.mark_eye_complete(consumed.key,b,1),"pacing fixture completes both eyes");
    return r;
}
void pacing_flight_lifetime() {
    FakeTransport t;FrameCoordinator c(t);require(c.initialize({}),"pacing lifetime initialize");
    std::uint64_t previous_serial{};
    for(unsigned n=0;n<2;++n){
        const auto r=stage_pacing_reserved(c);PacingFlightProbe p{&c,r,backend(r.ticket),true};
        require(!c.producer_pacing_snapshot().flight.serial,"pending completed ticket is not an in-flight operation");
        t.metadata_probe=pacing_flight_probe;t.metadata_context=&p;
        require(c.present(nullptr).status==CoordinatorStatus::submitted&&p.calls==1&&p.observed_serial>previous_serial,
                "successful submit exposes one fresh operation serial");
        previous_serial=p.observed_serial;t.metadata_probe=nullptr;
        require(!c.producer_pacing_snapshot().flight.serial,"flight ends exactly when submit returns");
    }
    PacingFlightProbe absent{&c};t.metadata_probe=pacing_flight_probe;t.metadata_context=&absent;
    require(c.begin_frame().status==CoordinatorStatus::started,"next begin has no prior pacing receipt");
    require(c.discard_pending().status==CoordinatorStatus::discarded_cancelled,"discard has no pacing receipt");
    require(c.before_reset(false)&&c.after_reset(true)&&c.shutdown(),"reset and shutdown retain no stale flight");
    require(absent.calls==4&&!c.producer_pacing_snapshot().flight.serial,"begin discard reset and shutdown all exclude generic busy");
}
void pacing_flight_refusals_and_failures() {
    // 0 explicit discard; 1 incomplete; 2 invalid; 3 unreserved; 4 menu;
    // 5 reset drain; 6 shutdown drain; 7 transport failure; 8 transport throw.
    for(unsigned mode=0;mode<9;++mode){
        FakeTransport t;FrameCoordinator c(t);require(c.initialize({}),"pacing refusal initialize");
        SceneReservation r{};
        if(mode==3){const auto ticket=stage(c);complete(c,ticket);r.ticket=ticket;}
        else if(mode==4){
            r.ticket=c.begin_frame().ticket;FrameTicket claimed{};xr::QuadLayer quad{};quad.size_meters={1,1};
            require(c.claim_render_frame(&claimed)&&c.stage_menu_frame(claimed.key,backend(claimed),quad)&&
                    c.mark_menu_complete(claimed.key,backend(claimed)),"menu fixture stages genuine quad");
        }else r=stage_pacing_reserved(c,mode!=1);
        if(mode==2)require(c.reject_render_frame(r.ticket.key),"invalidated pair stays invalid");
        PacingFlightProbe p{&c,r,backend(r.ticket),mode>=7};
        t.metadata_probe=pacing_flight_probe;t.metadata_context=&p;
        t.end_success=mode!=7;t.throw_submit=mode==8;
        if(mode==0)(void)c.discard_pending();
        else if(mode==5)require(c.before_reset(false)&&c.after_reset(true),"reset drains without flight");
        else if(mode==6)require(c.shutdown(),"shutdown drains without flight");
        else (void)c.present(nullptr);
        require(p.calls>=1&&!c.producer_pacing_snapshot().flight.serial,"all return and failure paths clear copied flight");
        if(mode>=7)require(p.observed_serial&&c.runtime_faulted(),"throw or failed end clears flight and retains native fault policy");
        t.metadata_probe=nullptr;t.throw_submit=false;t.end_success=true;
        if(mode!=6)require(c.shutdown(),"pacing refusal cleanup");
    }
}
}
int main() {
    pacing_flight_lifetime(); pacing_flight_refusals_and_failures();
    exact_pair_and_once_only(); duplicates_and_invalid_layout();
    reset_generation_and_retirement(); thread_and_nonrendering_contract();
    native_backend_provenance();
    runtime_fault_requires_recreation();
    throwing_transport_is_contained();
    focused_fresh_input_only();
    pending_key_lifecycle(); pending_key_metadata_only(); pending_key_lock_scope();
    rejected_native_render_never_submits();
    reservation_refusal_diagnostic_is_observation_only();
    actual_finish_diagnostic_and_bounded_reporting();
    unowned_rejection_age_and_reservation_guard();
    pristine_prediction_uses_original_deadline();
    pristine_deadline_refusals_preserve_existing_paths();
    awaiting_tracked_deadline_never_grants_scene_ownership();
    deferred_reservation_is_atomic_and_nonrenewable();
    std::printf("pending_key_checks=%u failures=0\n",pending_key_checks);
    std::printf("pristine_deadline_checks=%u failures=0\n",deadline_checks);
    std::puts("BO VR frame coordinator contract tests passed"); return 0;
}
