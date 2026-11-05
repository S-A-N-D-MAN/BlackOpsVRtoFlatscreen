// SPDX-License-Identifier: GPL-3.0-only
#include "vr/frame_coordinator.hpp"
#include "vr/openxr_transport.hpp"
#include "vr/base_producer_wait_policy.hpp"
#include "ui/menu_panel.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cmath>
#include <limits>
#include <string>
namespace {
using namespace bovr::vr;
bool visible_reticle(void*,xr::CompositorReticle* output) noexcept {
    if(!output)return false;*output={true,1,0.25f,0.75f};return true;
}
bool invalid_reticle(void*,xr::CompositorReticle* output) noexcept {
    if(!output)return false;*output={true,0,std::numeric_limits<float>::quiet_NaN(),0.5f};return true;
}
struct Transport final:FrameTransport {
    unsigned begins{},stereo{},quad{},discards{};
    bool initialize(const xr::RuntimeConfig&,const xr::HostCallbacks&) override {return true;}
    BeginStatus begin(xr::FrameState* f) override {*f={};f->frame_id=++begins;f->should_render=f->views_valid=true;return BeginStatus::started;}
    TransportResult discard(const xr::FrameState&) override {++discards;return {TransportStatus::zero_layer,0,0,true};}
    TransportResult submit(IDirect3DDevice9*,const xr::FrameState&,const xr::StereoSourceLayout&) override {++stereo;return {TransportStatus::projection,10,3,true};}
    TransportResult submit_quad(IDirect3DDevice9*,const xr::FrameState&,const xr::QuadLayer&) override {++quad;return {TransportStatus::quad,20,3,true};}
    bool before_reset(bool) override {return true;}
    bool after_reset(bool,bool ok) override {return ok;}
    bool shutdown() override {return true;}
};
}
int main() {
    unsigned failures{};
    auto check=[&](bool ok,const char* label){if(!ok){++failures;printf("FAIL %s\n",label);}};
    using openxr_transport_detail::select_base_producer_wait;
    for(const auto budget:{6000u,8000u,10000u}) {
        const auto text=std::to_wstring(budget);const auto selected=select_base_producer_wait(text);
        check(selected.microseconds==budget&&selected.override_accepted,"exact supported base wait accepted");
    }
    const std::array<std::wstring_view,20> invalid_waits{
        L"",L"0",L"5999",L"6001",L"8001",L"10001",L"600",L"06000",L"+6000",L"-6000",
        L"6000 ",L" 6000",L"6000\n",L"6000\t",L"6000x",L"6e3",L"0x1770",L"4294967296",
        L"999999999999999999999999999999999999",std::wstring_view(L"6000\0x",6)};
    for(const auto text:invalid_waits) {
        const auto selected=select_base_producer_wait(text);
        check(selected.microseconds==10000&&!selected.override_accepted,"malformed base wait uses bounded default");
    }
    // This isolated test process restores its inherited comparison setting.
    constexpr auto variable=L"BOVR_BASE_PRODUCER_WAIT_US";
    const DWORD prior_size=GetEnvironmentVariableW(variable,nullptr,0);
    std::wstring prior;
    if(prior_size) {
        prior.resize(prior_size);const auto copied=GetEnvironmentVariableW(variable,prior.data(),prior_size);
        check(copied+1==prior_size,"capture inherited wait setting");prior.resize(copied);
    }
    struct RestoreWaitEnvironment {
        const wchar_t* name;bool present;std::wstring value;
        ~RestoreWaitEnvironment(){SetEnvironmentVariableW(name,present?value.c_str():nullptr);}
    } restore_wait{variable,prior_size!=0,prior};
    auto environment_wait=[&](const wchar_t* value,unsigned expected,bool accepted) {
        check(SetEnvironmentVariableW(variable,value)!=FALSE,"set isolated wait test environment");
        constexpr DWORD sentinel=0x5a1729e4;SetLastError(sentinel);
        const auto selected=openxr_transport_detail::read_base_producer_wait_policy();
        check(GetLastError()==sentinel,"startup wait reader preserves last error");
        check(selected.microseconds==expected&&selected.override_accepted==accepted,"startup environment selects exact policy");
    };
    environment_wait(nullptr,10000,false);
    environment_wait(L"6000",6000,true);environment_wait(L"8000",8000,true);environment_wait(L"10000",10000,true);
    environment_wait(L" 6000",10000,false);environment_wait(L"6000 ",10000,false);
    environment_wait(L"6000x",10000,false);environment_wait(L"4294967296",10000,false);
    environment_wait(L"999999999999999999999999999999999999",10000,false);
    check(xr::D3D9ExBridgeConfig{}.producer_wait_budget_microseconds==6000,"independent scope bridge default remains6000");
    bovr::ui::MenuPanel panel; xr::Posef head{}; xr::QuadLayer first{},second{};
    head.position={1,2,3};
    check(panel.update(head,1,bovr::ui::OpaqueContext::menu,1280,720,&first),"finite panel initialized");
    check(first.pose.position.x==1 && first.pose.position.y==2 && std::fabs(first.pose.position.z-1)<1e-6f,
          "finite panel is two meters in front of entry head");
    check(std::fabs(first.size_meters.x/first.size_meters.y-1280.f/720.f)<1e-5f,"native source aspect preserved physically");
    head.position={9,9,9};
    check(panel.update(head,1,bovr::ui::OpaqueContext::menu,1280,720,&second) && second.pose.position.x==first.pose.position.x,
          "head translation does not drag finite menu");
    check(panel.update(head,2,bovr::ui::OpaqueContext::menu,1280,720,&second) && second.pose.position.x==9,
          "new XR generation relatches anchor");
    check(!panel.update(head,2,bovr::ui::OpaqueContext::none,1280,720,&second),"unproven context rejected");
    xr::CompositorReticle cursor{};
    check(openxr_transport_detail::select_quad_reticle(&first,visible_reticle,nullptr,&cursor)&&
          cursor.visible&&cursor.target_eye==first.source_eye&&cursor.u==0.25f&&cursor.v==0.75f,
          "valid menu pointer selects the quad source eye cursor");
    check(!openxr_transport_detail::select_quad_reticle(nullptr,visible_reticle,nullptr,&cursor),
          "projection submission never receives a menu cursor");
    check(!openxr_transport_detail::select_quad_reticle(&first,invalid_reticle,nullptr,&cursor),
          "nonfinite menu cursor fails closed");
    Transport transport; FrameCoordinator coordinator(transport); coordinator.initialize({});
    auto begin=coordinator.begin_frame();FrameTicket ticket{};coordinator.claim_render_frame(&ticket);
    const BackendFrameIdentity stamp{0x3b40100,44};
    check(coordinator.stage_menu_frame(ticket.key,stamp,first),"menu stages independently from stereo");
    check(!coordinator.mark_eye_complete(ticket.key,stamp,0),"stereo eye event cannot complete a menu");
    check(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete && transport.quad==0,"missing final2D completion cannot capture native backbuffer");
    begin=coordinator.begin_frame();coordinator.claim_render_frame(&ticket);
    check(coordinator.stage_menu_frame(ticket.key,stamp,first),"second menu stage");
    check(!coordinator.mark_menu_complete(ticket.key,{stamp.buffer_identity,43}),"stale producer cannot complete menu");
    check(coordinator.mark_menu_complete(ticket.key,stamp),"exact native final2D completion accepted");
    const auto result=coordinator.present(nullptr);
    check(result.status==CoordinatorStatus::submitted_quad && result.backend==stamp && transport.quad==1 && transport.stereo==0,
          "opaque image submits finite quad and never projection");
    begin=coordinator.begin_frame();coordinator.claim_render_frame(&ticket);
    coordinator.stage_menu_frame(ticket.key,stamp,first);coordinator.mark_menu_complete(ticket.key,stamp);
    check(!coordinator.mark_menu_complete(ticket.key,stamp),"duplicate UI completion rejected");
    check(coordinator.present(nullptr).status==CoordinatorStatus::discarded_incomplete,"duplicate UI event invalidates frame");
    begin=coordinator.begin_frame();coordinator.claim_render_frame(&ticket);
    auto invalid=first;invalid.size_meters.x=std::numeric_limits<float>::quiet_NaN();
    check(!coordinator.stage_menu_frame(ticket.key,stamp,invalid),"nonfinite quad rejected");
    coordinator.discard_pending();coordinator.shutdown();
    printf("menu_frame_failures=%u native_calls=0\n",failures); return failures?1:0;
}
