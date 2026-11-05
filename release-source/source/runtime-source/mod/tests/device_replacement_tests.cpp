// SPDX-License-Identifier: GPL-3.0-only
#include "render/device_replacement.hpp"
#include "vr/frame_coordinator.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace bovr;
using namespace bovr::vr;
using render::DeviceReplacementResult;
unsigned checks{};
void check(bool value,const char* why) {
    ++checks;if (!value) {std::printf("FAIL %s\n",why);std::exit(1);}
}
// Only IUnknown is exercised: there is no graphics driver, game or OpenXR API.
struct Device {
    void** table{};
    ULONG references{1},queries{},adds{},releases{};
    bool ex{true};
    Device* different_identity{};
    IDirect3DDevice9* get() {return reinterpret_cast<IDirect3DDevice9*>(this);}
};
ULONG STDMETHODCALLTYPE add_ref(IUnknown* value) {
    auto& d=*reinterpret_cast<Device*>(value);++d.adds;return ++d.references;
}
ULONG STDMETHODCALLTYPE release(IUnknown* value) {
    auto& d=*reinterpret_cast<Device*>(value);++d.releases;
    check(d.references>0,"COM reference never underflows");return --d.references;
}
HRESULT STDMETHODCALLTYPE query(IUnknown* value,REFIID iid,void** result) {
    auto& d=*reinterpret_cast<Device*>(value);++d.queries;*result=nullptr;
    if (!d.ex || iid!=__uuidof(IDirect3DDevice9Ex)) return E_NOINTERFACE;
    auto* target=d.different_identity?d.different_identity:&d;
    add_ref(reinterpret_cast<IUnknown*>(target));*result=target;return S_OK;
}
std::array<void*,3> device_table{reinterpret_cast<void*>(&query),reinterpret_cast<void*>(&add_ref),reinterpret_cast<void*>(&release)};
enum Event {initialize_event,discard_event,shutdown_event,block_event,clear_event,staging_release_event,device_release_event};
struct Transport final:FrameTransport {
    std::vector<Event>& events;
    bool permit_shutdown{true},fail_end{},throw_shutdown{},fail_initialize{};
    bool* quit_during_shutdown{};
    unsigned begins{},ends{},shutdowns{},initializations{};
    explicit Transport(std::vector<Event>& events):events(events){}
    bool initialize(const xr::RuntimeConfig&,const xr::HostCallbacks&) override {
        events.push_back(initialize_event);++initializations;begins=0;return !fail_initialize;
    }
    BeginStatus begin(xr::FrameState* value) override {
        *value={};value->frame_id=++begins;value->should_render=value->views_valid=true;
        value->actions.sequence=begins;return BeginStatus::started;
    }
    TransportResult discard(const xr::FrameState&) override {
        ++ends;events.push_back(discard_event);return {fail_end?TransportStatus::end_failed:TransportStatus::zero_layer,0,0,!fail_end};
    }
    TransportResult submit(IDirect3DDevice9*,const xr::FrameState&,const xr::StereoSourceLayout&) override {
        check(false,"replacement cannot submit old pixels");return {};
    }
    bool before_reset(bool) override{return true;}
    bool after_reset(bool,bool succeeded) override{return succeeded;}
    bool shutdown() override {
        ++shutdowns;events.push_back(shutdown_event);
        if (quit_during_shutdown) *quit_during_shutdown=true;
        if (throw_shutdown) throw std::runtime_error("synthetic retirement exception");
        return permit_shutdown;
    }
};
struct Host {
    std::vector<Event> events;
    Transport transport{events};FrameCoordinator coordinator{transport};
    Device old{device_table.data()},next{device_table.data()},alias{device_table.data()};
    IDirect3DDevice9* retained{old.get()};
    DWORD owner{GetCurrentThreadId()};
    bool stop{},pending{},initialized{true},attempted{true};
    render::DevicePublicationGate publications_gate;
    // Sentinels represent external immutable scene/pixel/action publications.
    std::array<unsigned,7> publications{1,2,3,4,5,6,7};
    bool staging{true};unsigned commits{},quit_acknowledgements{};
    std::uint64_t producer_serial{41},native_serial{73},menu_serial{18};
    Host() {check(coordinator.initialize({}),"old coordinator initializes");retained->AddRef();events.clear();}
    ~Host() {transport.fail_end=false;transport.throw_shutdown=false;transport.permit_shutdown=true;
        coordinator.shutdown();if(retained)retained->Release();}
    render::DeviceReplacementRequest request() {
        return {retained,next.get(),{10,owner,true},{11,owner,true},owner,GetCurrentThreadId(),initialized,false,false};
    }
    bool stop_requested() const {return stop;}
    bool runtime_faulted() const {return coordinator.runtime_faulted();}
    void begin_retirement() {pending=true;publications_gate.close();events.push_back(block_event);}
    bool retire_runtime() {return publications_gate.quiescent()&&coordinator.shutdown();}
    void commit_retirement() {
        ++commits;events.push_back(clear_event);publications={};
        events.push_back(staging_release_event);staging=false;
        events.push_back(device_release_event);auto* previous=retained;retained=nullptr;previous->Release();
        initialized=attempted=false;
    }
    bool initialize_candidate() {
        check(!retained&&!initialized&&!attempted,"new initialization follows old release and state retirement");
        if(stop)return false;
        attempted=true;
        if(!coordinator.initialize({})){stop=true;return false;}
        next.get()->AddRef();retained=next.get();initialized=true;pending=false;publications_gate.reopen();return true;
    }
};
auto attempt(Host& h) {return render::retire_for_device_replacement(h.request(),h);}
}
int main() {
    {
        Host h;const auto begun=h.coordinator.begin_frame();SceneReservation reservation{};
        check(h.coordinator.reserve_scene_frame({0x1111,0,73},&reservation),"old native ticket reserved");
        h.transport.permit_shutdown=false;
        check(attempt(h)==DeviceReplacementResult::retry_retirement,"GPU retirement refusal is retryable");
        check(h.pending&&h.retained==h.old.get()&&h.old.references==2&&h.staging&&h.commits==0,
              "refusal retains exact old device reference and staging ownership");
        check(h.publications==std::array<unsigned,7>{1,2,3,4,5,6,7},"refusal does not destroy external publications prematurely");
        check(h.next.references==1&&h.next.queries==1&&h.next.adds==h.next.releases,"candidate QI reference balances on refusal");
        check(h.coordinator.begin_frame().status==CoordinatorStatus::reset_blocked,"retirement blocks new prediction");
        FrameTicket stale{};
        check(!h.coordinator.claim_render_frame(&stale)&&!h.coordinator.consume_scene_reservation(reservation,reservation.native,&stale),
              "old reserved producer cannot consume after retirement starts");
        check(h.transport.ends==1&&h.transport.shutdowns==1,"pending frame ends once before failed GPU retirement");
        h.events.clear();h.transport.permit_shutdown=true;
        check(attempt(h)==DeviceReplacementResult::retired,"same-owner replacement succeeds on later retry");
        check(h.events==std::vector<Event>{block_event,shutdown_event,clear_event,staging_release_event,device_release_event},
              "successful retirement precedes publication clear then staging release then retained device release");
        check(h.old.references==1&&h.next.references==1&&h.commits==1&&!h.staging&&!h.retained,
              "exactly one old owner reference retired and no new reference retained early");
        check(h.publications==std::array<unsigned,7>{}&&h.transport.ends==1,"successful retry clears old publications without duplicate EndFrame");
        check(h.producer_serial==41&&h.native_serial==73&&h.menu_serial==18&&h.quit_acknowledgements==0,
              "replacement preserves monotonic serials and never acknowledges quit");
        check(h.initialize_candidate(),"normal new-device initialization succeeds");
        const auto fresh=h.coordinator.begin_frame();
        check(fresh.ticket.key.generation>begun.ticket.key.generation&&fresh.ticket.key.frame_id==begun.ticket.key.frame_id,
              "fresh generation disambiguates runtime frame ID reuse");
        check(!h.coordinator.stage_render_frame(begun.ticket.key,{0x1111,41},{})&&
              !h.coordinator.mark_eye_complete(begun.ticket.key,{0x1111,41},0),"late old scene/pixel completions cannot enter replacement session");
        SceneReservation newer{};check(h.coordinator.reserve_scene_frame({0x1111,0,74},&newer)&&newer.serial>reservation.serial,
              "reservation serial also remains monotonic after shutdown and reinitialize");
        check(h.next.references==2&&h.initialized&&!h.pending,"new device gets one owner reference only after initialization");
    }
    {
        Host h;std::promise<void> entered,finish;auto finished=finish.get_future();
        std::thread frontend([&]{
            render::DevicePublicationLease lease(h.publications_gate);
            if(!lease.admitted)std::abort();entered.set_value();finished.wait();
        });
        entered.get_future().wait();
        check(attempt(h)==DeviceReplacementResult::retry_retirement&&h.transport.shutdowns==0&&h.old.references==2,
              "in-flight native scene defers runtime retirement and preserves device ownership");
        check(!h.publications_gate.enter()&&h.commits==0,"closed publication gate rejects a later scene without waiting");
        finish.set_value();frontend.join();
        check(attempt(h)==DeviceReplacementResult::retired&&h.transport.shutdowns==1,
              "native scene return admits a later retirement retry");
        check(h.initialize_candidate()&&h.publications_gate.enter(),"fresh session reopens native scene publication");
        h.publications_gate.leave();
    }
    {
        Host h;h.stop=true;auto r=h.request();r.caller=h.owner+1;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::stopped&&h.next.queries==0&&h.events.empty(),
              "explicit stop wins over owner validation and performs no QI or replacement work");
        h.stop=false;r.stopped=true;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::stopped,"already stopped session cannot restart");
        r.stopped=false;r.caller=h.owner+1;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::wrong_owner,"foreign caller rejected");
        DeviceReplacementResult foreign{};std::thread other([&]{foreign=attempt(h);});other.join();
        check(foreign==DeviceReplacementResult::wrong_owner&&h.next.queries==0&&h.transport.shutdowns==0,
              "actual foreign thread calls no COM or runtime method");
        r=h.request();r.retained_creation={};
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::unproven_creation,"unknown original creation blocks adoption");
        r=h.request();r.candidate_creation.serial=10;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::unproven_creation,"same or older creation serial cannot replace");
        r=h.request();r.candidate_creation.thread_id=h.owner+1;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::unproven_creation,"foreign-created candidate rejected even on Present owner");
        r=h.request();r.candidate=h.retained;
        check(render::retire_for_device_replacement(r,h)==DeviceReplacementResult::same_device,"old retained identity is not recreation");
        h.next.ex=false;
        check(attempt(h)==DeviceReplacementResult::not_extended&&!h.pending&&h.commits==0,"non-Ex candidate leaves healthy old session unchanged");
        h.next.ex=true;h.next.different_identity=&h.alias;
        check(attempt(h)==DeviceReplacementResult::not_extended&&h.alias.references==1,"QI alias mismatch balances returned reference and rejects");
    }
    {
        Host h;h.coordinator.begin_frame();h.transport.quit_during_shutdown=&h.stop;
        check(attempt(h)==DeviceReplacementResult::retired_stopping&&h.commits==1&&h.stop,
              "quit arriving during retirement is observed after successful resource release");
        check(!h.initialize_candidate()&&h.transport.initializations==1&&h.next.references==1&&h.quit_acknowledgements==0,
              "concurrent quit cannot initialize replacement or be acknowledged by recovery helper");
    }
    {
        Host h;h.coordinator.begin_frame();h.transport.fail_end=true;
        check(attempt(h)==DeviceReplacementResult::runtime_faulted&&h.coordinator.runtime_faulted(),"failed EndFrame remains a hard runtime fault");
        check(h.commits==0&&h.old.references==2&&h.staging,"runtime failure preserves ownership for explicit stop cleanup");
        const auto queries=h.next.queries;
        check(attempt(h)==DeviceReplacementResult::runtime_faulted&&h.next.queries==queries,"runtime failure never silently recreates on retry");
    }
    {
        Host h;h.transport.throw_shutdown=true;
        check(attempt(h)==DeviceReplacementResult::runtime_faulted&&h.commits==0&&h.old.references==2,
              "throwing transport shutdown faults and retains cleanup ownership");
    }
    {
        Host h;check(attempt(h)==DeviceReplacementResult::retired,"empty session can retire");
        h.transport.fail_initialize=true;
        check(!h.initialize_candidate()&&h.stop&&!h.retained&&h.next.references==1,
              "failed replacement initialization stops without retaining candidate or reviving old device");
    }
    std::printf("PASS %u checks; no game, graphics driver or runtime launched\n",checks);return 0;
}
