// SPDX-License-Identifier: GPL-3.0-only
#include "vr/frame_coordinator.hpp"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>
#include <cstring>
using namespace bovr::vr;
namespace {
unsigned checks{};std::uint64_t now=1000;
std::uint64_t clock_now() noexcept {return now;}
void check(bool value,const char* reason) {++checks;if(!value){std::printf("FAIL %s\n",reason);std::exit(1);}}
struct Transport final:FrameTransport {
    unsigned begins{},ends{},submits{},resets{};
    bool initialize(const xr::RuntimeConfig&,const xr::HostCallbacks&) override{return true;}
    BeginStatus begin(xr::FrameState* f) override {
        *f={};f->frame_id=++begins;f->should_render=f->views_valid=true;
        f->predicted_display_time=100000+begins;f->actions.sequence=200+begins;
        f->head_center.position={1,2,3};return BeginStatus::started;
    }
    TransportResult discard(const xr::FrameState&) override{++ends;return {TransportStatus::zero_layer,0,0,true};}
    TransportResult submit(IDirect3DDevice9*,const xr::FrameState&,const xr::StereoSourceLayout&) override{
        ++ends;++submits;return {TransportStatus::projection,3,3,true};
    }
    bool before_reset(bool) override{++resets;return true;}
    bool after_reset(bool,bool success) override{return success;}
    bool shutdown() override{return true;}
};
constexpr NativeBuildIdentity first{0x3b40100,0,1};
void complete(FrameCoordinator& c,const FrameTicket& ticket,NativeBuildIdentity native) {
    const BackendFrameIdentity backend{native.buffer_identity,native.build_serial};
    check(c.stage_render_frame(ticket.key,backend,{}),"consumed reservation stages once");
    check(c.mark_eye_complete(ticket.key,backend,0)&&c.mark_eye_complete(ticket.key,backend,1),"both exact backend eyes complete");
}
}
int main() {
    Transport transport;FrameCoordinator c(transport,clock_now);check(c.initialize({}),"initialize");
    const auto begun=c.begin_frame();check(begun.ticket.published_ms==1000,"publication timestamp belongs to original Begin completion");
    SceneReservation r{};check(c.reserve_scene_frame(first,&r),"native frame zero is valid with unique build serial");
    check(r.serial&&r.native==first&&r.ticket.key==begun.ticket.key,"reservation records exact native build and XR key");
    check(c.validate_scene_reservation(r,first)&&c.validate_scene_reservation(r,first),"read-only validation can repeat without consuming or refreshing");
    now=1040;SceneReservation duplicate{};FrameTicket ticket{};
    check(!c.reserve_scene_frame(first,&duplicate)&&!duplicate.serial,"duplicate producer cannot refresh timestamp or reserve twice");
    check(!c.claim_render_frame(&ticket),"ordinary scene/menu claim cannot steal reserved prediction");
    check(c.present(nullptr).status==CoordinatorStatus::pending_frame&&transport.ends==0,"ordinary Present cannot retire reserved native work");
    check(c.begin_frame().status==CoordinatorStatus::pending_frame&&transport.begins==1,"begin cannot advance prediction under native reservation");
    check(!c.consume_scene_reservation(r,{first.buffer_identity,1,1},&ticket),"native frame mismatch cannot consume");
    check(!c.consume_scene_reservation(r,{first.buffer_identity+0x186300,0,1},&ticket),"alternate native storage cannot consume");
    check(!c.consume_scene_reservation(r,{first.buffer_identity,0,2},&ticket),"reused buffer and counter need the original build serial");
    bool foreign=true;std::thread other([&]{FrameTicket t{};foreign=c.consume_scene_reservation(r,first,&t);});other.join();
    check(!foreign,"foreign native thread cannot consume another producer transaction");
    std::thread validate_other([&]{foreign=c.validate_scene_reservation(r,first);});validate_other.join();
    check(!foreign,"read-only validation also requires original producer thread");
    auto tampered=r;tampered.ticket.frame.head_center.position={99,98,97};tampered.ticket.published_ms=now;
    check(c.consume_scene_reservation(tampered,first,&ticket),"identity consumes canonical coordinator storage");
    check(ticket.published_ms==1000&&ticket.frame.head_center.position.x==1,"caller cannot replace prediction or refresh age in reservation copy");
    check(!c.consume_scene_reservation(r,first,&ticket),"same native scene cannot consume twice");
    check(!c.validate_scene_reservation(r,first),"consumed reservation is no longer valid for native production");
    ticket=begun.ticket;complete(c,ticket,first);
    check(c.present(nullptr).status==CoordinatorStatus::submitted&&transport.submits==1,"reserved scene retains normal exact stereo completion");
    check(c.scene_reservation_state()==ReservationState::none,"retirement removes reservation");

    now=2000;c.begin_frame();SceneReservation cancelled{};
    check(c.reserve_scene_frame({first.buffer_identity,0,2},&cancelled),"map-counter reuse mints a distinct reservation");
    check(cancelled.serial>r.serial,"reservation serial survives native buffer/counter reuse");
    check(!c.cancel_scene_reservation(r.ticket.key,r.serial),"late old cancellation cannot poison current prediction");
    check(c.cancel_scene_reservation(cancelled.ticket.key,cancelled.serial),"producer explicitly cancels its transaction");
    check(!c.consume_scene_reservation(cancelled,cancelled.native,&ticket),"cancelled producer cannot reach scene");
    check(!c.validate_scene_reservation(cancelled,cancelled.native),"read-only validation rejects cancellation");
    check(c.present(nullptr).status==CoordinatorStatus::discarded_incomplete&&transport.submits==1,"cancelled reservation ends zero layer");

    c.begin_frame();SceneReservation timed{};c.reserve_scene_frame({first.buffer_identity,0,3},&timed);
    check(!c.expire_scene_reservation(2100,100),"publication age boundary is inclusive");
    check(c.expire_scene_reservation(2101,100),"overdue original publication explicitly expires");
    check(!c.consume_scene_reservation(timed,timed.native,&ticket),"expired native work cannot consume later");
    check(c.present(nullptr).status==CoordinatorStatus::discarded_incomplete,"expired reservation retires zero layer");
    c.begin_frame();c.reserve_scene_frame({first.buffer_identity,0,4},&timed);
    check(c.expire_scene_reservation(1999,100),"backwards clock invalidates rather than refreshing age");c.discard_pending();

    c.begin_frame();SceneReservation reset{};c.reserve_scene_frame({first.buffer_identity,0,5},&reset);
    const auto ends=transport.ends;
    check(c.before_reset(false)&&transport.ends==ends+1,"owner reset explicitly retires reserved frame once");
    check(c.after_reset(true),"owner reset recovers");c.begin_frame();SceneReservation fresh{};
    check(c.reserve_scene_frame({first.buffer_identity,0,6},&fresh)&&fresh.ticket.key.generation!=reset.ticket.key.generation,
          "reset produces a different reservation generation");
    check(!c.consume_scene_reservation(reset,reset.native,&ticket),"pre-reset token cannot consume recreated frame");
    check(c.shutdown(),"shutdown explicitly retires live reservation");
    check(!c.consume_scene_reservation(fresh,fresh.native,&ticket),"shutdown invalidates retained producer copy");

    // Native producer is a different thread from the serialized runtime owner.
    // The owner must not steal its prediction while it is inside native work.
    c.initialize({});c.begin_frame();std::promise<SceneReservation> reserved;
    std::promise<void> proceed;auto proceed_future=proceed.get_future();bool consumed=false;
    std::thread native([&]{SceneReservation value{};c.reserve_scene_frame({first.buffer_identity,4,7},&value);
        reserved.set_value(value);proceed_future.wait();FrameTicket result{};
        consumed=c.consume_scene_reservation(value,value.native,&result);});
    const auto native_value=reserved.get_future().get();
    check(native_value.serial!=0,"non-owner native frontend can reserve metadata without XR calls");
    const auto prior_ends=transport.ends;
    check(c.present(nullptr).status==CoordinatorStatus::pending_frame&&transport.ends==prior_ends,"owner waits without native callback or XR retirement");
    proceed.set_value();native.join();check(consumed,"same producer consumes after owner Present attempt");
    check(c.discard_pending().status==CoordinatorStatus::discarded_cancelled,"explicit owner cancellation remains available after consumption");
    c.shutdown();std::printf("reservation_checks=%u failures=0 runtime_launches=0\n",checks);
}
