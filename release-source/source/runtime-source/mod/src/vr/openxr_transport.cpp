// SPDX-License-Identifier: GPL-3.0-only
#include "openxr_transport.hpp"
#include "base_producer_wait_policy.hpp"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bovr::vr {
namespace {
// Optional CPU-wall measurements around existing calls only. This does not
// issue GPU queries, add fences, wait, retain images, or change frame ordering.
enum class TransportPerfStage : unsigned {
    submission, retirement, scope_retirement, capture, acquire,
    scope, scope_capture, scope_acquire, composition, end_frame,
    release, scope_release, count
};
constexpr std::array<const char*,static_cast<unsigned>(TransportPerfStage::count)> transport_perf_names{
    "submission", "retirement", "scope_retirement", "capture", "acquire",
    "scope", "scope_capture", "scope_acquire", "composition", "end_frame",
    "release", "scope_release"
};
struct TransportPerfCounter final {
    std::uint64_t calls{},failures{},sum_ticks{},max_ticks{};
};
struct TransportPerfWindow final {
    const OpenXrTransport* owner{};
    xr::HostCallbacks host{};
    std::array<TransportPerfCounter,transport_perf_names.size()> stages{};
    std::uint64_t frequency{},started{};
    bool enabled{};
};
thread_local TransportPerfWindow transport_perf;
std::uint64_t transport_perf_clock() noexcept {
    const DWORD error=GetLastError();LARGE_INTEGER tick{};
    const bool ok=QueryPerformanceCounter(&tick)!=FALSE&&tick.QuadPart>0;
    SetLastError(error);return ok?static_cast<std::uint64_t>(tick.QuadPart):0;
}
void configure_transport_perf(const OpenXrTransport* owner,const xr::HostCallbacks& host) noexcept {
    const DWORD error=GetLastError();transport_perf={};
    wchar_t requested[4]{};LARGE_INTEGER frequency{};
    const DWORD length=GetEnvironmentVariableW(L"BOVR_TRANSPORT_PERF_DIAGNOSTIC",requested,4);
    if(length==1&&requested[0]==L'1'&&host.log&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0){
        transport_perf.owner=owner;transport_perf.host=host;
        transport_perf.frequency=static_cast<std::uint64_t>(frequency.QuadPart);
        transport_perf.started=transport_perf_clock();transport_perf.enabled=transport_perf.started!=0;
    }
    SetLastError(error);
}
TransportPerfWindow* current_transport_perf(const OpenXrTransport* owner) noexcept {
    return transport_perf.enabled&&transport_perf.owner==owner?&transport_perf:nullptr;
}
void report_transport_perf(TransportPerfWindow* window) noexcept {
    if(!window)return;
    const DWORD error=GetLastError();const auto now=transport_perf_clock();
    if(!now||now<window->started||now-window->started<window->frequency*10){SetLastError(error);return;}
    const double milliseconds_per_tick=1000.0/static_cast<double>(window->frequency);
    const double elapsed_ms=static_cast<double>(now-window->started)*milliseconds_per_tick;
    // Emit at most one bounded window per ten seconds on the existing owner
    // thread. Submission/scope/composition include nested stages: never sum.
    for(unsigned i=0;i<window->stages.size();++i){
        const auto& value=window->stages[i];if(!value.calls)continue;
        char message[384]{};
        std::snprintf(message,sizeof(message),
            "BlackOps transport CPU-wall window_ms=%.3f stage=%s calls=%llu failures=%llu sum_ms=%.3f avg_ms=%.6f max_ms=%.6f nested_stages_do_not_sum=1 additional_gpu_queries=0",
            elapsed_ms,transport_perf_names[i],static_cast<unsigned long long>(value.calls),
            static_cast<unsigned long long>(value.failures),static_cast<double>(value.sum_ticks)*milliseconds_per_tick,
            static_cast<double>(value.sum_ticks)*milliseconds_per_tick/static_cast<double>(value.calls),
            static_cast<double>(value.max_ticks)*milliseconds_per_tick);
        try{window->host.log(window->host.user_data,xr::LogLevel::Info,message);}catch(...){}
    }
    window->stages={};window->started=now;SetLastError(error);
}
class TransportPerfScope final {
public:
    TransportPerfScope(TransportPerfWindow* window,TransportPerfStage stage) noexcept
        :window_(window),stage_(stage),started_(window?transport_perf_clock():0){}
    ~TransportPerfScope() noexcept {
        if(!window_||!started_)return;
        const auto stopped=transport_perf_clock();if(!stopped||stopped<started_)return;
        auto& value=window_->stages[static_cast<unsigned>(stage_)];const auto elapsed=stopped-started_;
        ++value.calls;value.failures+=success_?0u:1u;value.sum_ticks+=elapsed;
        if(elapsed>value.max_ticks)value.max_ticks=elapsed;
        if(stage_==TransportPerfStage::submission)report_transport_perf(window_);
    }
    void succeeded(bool success=true) noexcept {success_=success;}
private:
    TransportPerfWindow* window_{};TransportPerfStage stage_{};
    std::uint64_t started_{};bool success_{};
};
template<class Call>
bool timed_transport_call(TransportPerfWindow* window,TransportPerfStage stage,Call&& call){
    TransportPerfScope timing(window,stage);const bool accepted=call();timing.succeeded(accepted);return accepted;
}
}
namespace openxr_transport_detail {
BaseProducerWaitPolicy read_base_producer_wait_policy() noexcept {
    const DWORD error=GetLastError();
    wchar_t requested[8]{};
    const DWORD length=GetEnvironmentVariableW(L"BOVR_BASE_PRODUCER_WAIT_US",requested,8);
    const auto result=length>0&&length<8?
        select_base_producer_wait(std::wstring_view(requested,length)):BaseProducerWaitPolicy{};
    SetLastError(error);return result;
}
bool select_quad_reticle(const xr::QuadLayer* quad,QuadReticleProvider provider,
                         void* context,xr::CompositorReticle* output) noexcept {
    if(!output)return false;
    *output={};
    if(!quad||!provider||quad->source_eye>=xr::kEyeCount)return false;
    xr::CompositorReticle candidate{};
    if(!provider(context,&candidate)||!candidate.visible||
       !std::isfinite(candidate.u)||!std::isfinite(candidate.v)||
       candidate.u<0.0f||candidate.u>1.0f||candidate.v<0.0f||candidate.v>1.0f)return false;
    candidate.target_eye=quad->source_eye;
    *output=candidate;return true;
}
}

bool OpenXrTransport::initialize(const xr::RuntimeConfig& config, const xr::HostCallbacks& host) {
    if (initialized_) return false;
    if (!runtime_.Initialize(config,host)) return false;
    const auto wait=openxr_transport_detail::read_base_producer_wait_policy();
    xr::D3D9ExBridgeConfig base_bridge_config{},scope_bridge_config{};
    base_bridge_config.producer_wait_budget_microseconds=wait.microseconds;
    if (!bridge_.Initialize(base_bridge_config,host) || !scope_bridge_.Initialize(scope_bridge_config,host) ||
        !compositor_.Initialize(runtime_,host)) {
        compositor_.Shutdown(); scope_bridge_.Shutdown(); bridge_.Shutdown(); runtime_.Shutdown(); return false;
    }
    initialized_=true;configure_transport_perf(this,host);
    // Startup evidence only. Existing fences, exact serial ownership, bounded
    // bridge policy and the independent scope's default budget remain intact.
    if(host.log) {
        const DWORD error=GetLastError();char message[320]{};
        std::snprintf(message,sizeof(message),
            "BlackOps base producer wait policy base_wait_us=%u scope_wait_us=%u startup_override_accepted=%u default_base_wait_us=10000 exact_serial_required=1 scope_policy_unchanged=1",
            static_cast<unsigned>(base_bridge_config.producer_wait_budget_microseconds),
            static_cast<unsigned>(scope_bridge_config.producer_wait_budget_microseconds),
            static_cast<unsigned>(wait.override_accepted));
        try{host.log(host.user_data,xr::LogLevel::Info,message);}catch(...){}
        SetLastError(error);
    }
    return true;
}
BeginStatus OpenXrTransport::begin(xr::FrameState* frame) {
    if (!initialized_ || !frame) return BeginStatus::failed;
    if (!runtime_.PollEvents())
        return runtime_.exit_requested() ? BeginStatus::exit_requested : BeginStatus::failed;
    if (runtime_.exit_requested()) return BeginStatus::exit_requested;
    if (!runtime_.session_running()) return BeginStatus::idle;
    if (runtime_.BeginFrame(frame)) return BeginStatus::started;
    if (runtime_.exit_requested()) return BeginStatus::exit_requested;
    return runtime_.session_running() ? BeginStatus::failed : BeginStatus::idle;
}
TransportResult OpenXrTransport::discard(const xr::FrameState& frame) {
    TransportPerfScope timing(current_transport_perf(this),TransportPerfStage::end_frame);
    const auto ended=runtime_.EndFrame(frame,{});
    timing.succeeded(ended.frame_ended);
    return {ended.frame_ended ? TransportStatus::zero_layer : TransportStatus::end_failed,
            0,0,ended.frame_ended};
}
TransportResult OpenXrTransport::submit(IDirect3DDevice9* device, const xr::FrameState& frame,
                                      const xr::StereoSourceLayout& layout) {
    return submit_layer(device,frame,layout,nullptr);
}
TransportResult OpenXrTransport::submit_quad(IDirect3DDevice9* device,const xr::FrameState& frame,
                                            const xr::QuadLayer& quad) {
    xr::StereoSourceLayout full_image{};
    full_image.eyes[0]=full_image.eyes[1]={0,0,1,1};
    // Both swapchains receive the complete opaque native image. The physical
    // quad restores its source aspect; it never uses an eye projection layout.
    return submit_layer(device,frame,full_image,&quad);
}
TransportResult OpenXrTransport::submit_layer(IDirect3DDevice9* device,const xr::FrameState& frame,
                                             const xr::StereoSourceLayout& layout,const xr::QuadLayer* quad) {
    auto* perf=current_transport_perf(this);
    TransportPerfScope submission_timing(perf,TransportPerfStage::submission);
    auto reject=[&](TransportStatus status, std::uint64_t serial=0) {
        auto result=discard(frame); if (result.frame_ended) result.status=status;
        result.capture_serial=serial; return result;
    };
    if (!timed_transport_call(perf,TransportPerfStage::retirement,[&]{
            return bridge_.RetireCompletedFrames(runtime_.d3d11_device(),runtime_.d3d11_context());}))
        return reject(TransportStatus::retirement_failed);
    // Always retire old auxiliary consumers, including while the scope is
    // lowered. Auxiliary backpressure/failure must not black out base stereo.
    const bool scope_bridge_ready=timed_transport_call(perf,TransportPerfStage::scope_retirement,[&]{
        return scope_bridge_.RetireCompletedFrames(runtime_.d3d11_device(),runtime_.d3d11_context());});
    std::uint64_t serial=0;
    if (!device || !timed_transport_call(perf,TransportPerfStage::capture,[&]{return bridge_.CaptureBackBuffer(device,&frame,&serial);}))
        return reject(TransportStatus::capture_failed);
    xr::SharedFrameToken token{};
    if (!timed_transport_call(perf,TransportPerfStage::acquire,[&]{
            return bridge_.AcquireSerial(runtime_.d3d11_device(),runtime_.d3d11_context(),serial,&token);})) {
        bridge_.AbandonSerial(serial); return reject(TransportStatus::acquire_failed,serial);
    }
    struct ScopedRelease final {
        xr::D3D9ExSharedTextureBridge& bridge;
        ID3D11DeviceContext* context;
        const xr::SharedFrameToken& token;
        bool acquired=true;
        TransportPerfWindow* perf{};
        TransportPerfStage stage{TransportPerfStage::release};
        ~ScopedRelease() noexcept {
            // Keep acquired ownership reachable even when a host callback or
            // allocation throws; the coordinator will block further frames.
            try { if(acquired){TransportPerfScope timing(perf,stage);bridge.Release(context,token);timing.succeeded();} } catch (...) {}
        }
    } release{bridge_,runtime_.d3d11_context(),token,true,perf,TransportPerfStage::release};
    if (token.source.serial!=serial || !token.source.rendered_views_valid ||
        std::memcmp(token.source.rendered_eyes,frame.eyes,sizeof(frame.eyes))!=0) {
        return reject(TransportStatus::metadata_mismatch,serial);
    }
    if (capture_validator_ && !capture_validator_(capture_validator_user_,runtime_.d3d11_device(),
            runtime_.d3d11_context(),frame,token.source))
        return reject(TransportStatus::capture_validation_failed,serial);
    std::uint32_t eye_mask=0;
    xr::CompositorReticle reticle{};
    const auto* menu_reticle=openxr_transport_detail::select_quad_reticle(
        quad,quad_reticle_provider_,quad_reticle_context_,&reticle)?&reticle:nullptr;
    auto composed_layout=layout;
    if(!quad&&scope_layout_)scope_layout_(scope_context_,frame,&composed_layout);
    xr::SharedFrameToken scope_token{};
    ScopedRelease scope_release{scope_bridge_,runtime_.d3d11_context(),scope_token,false,perf,TransportPerfStage::scope_release};
    const xr::D3D11SourceFrame* separate_scope_source=nullptr;
    if(!quad && composed_layout.physical_scope.active &&
       composed_layout.physical_scope.use_separate_source && scope_bridge_ready && scope_source_) {
        TransportPerfScope scope_timing(perf,TransportPerfStage::scope);
        auto* surface=scope_source_(scope_source_context_,device,frame);
        std::uint64_t scope_serial=0;
        if(surface && timed_transport_call(perf,TransportPerfStage::scope_capture,[&]{return scope_bridge_.CaptureSurface(device,surface,&frame,&scope_serial);})) {
            if(timed_transport_call(perf,TransportPerfStage::scope_acquire,[&]{
                    return scope_bridge_.AcquireSerial(runtime_.d3d11_device(),runtime_.d3d11_context(),scope_serial,&scope_token);})) {
                scope_release.acquired=true;
                if(scope_token.source.serial==scope_serial)
                    separate_scope_source=xr::compositor_detail::SelectPhysicalScopeSource(
                        composed_layout.physical_scope,frame,token.source,&scope_token.source);
            } else {
                scope_bridge_.AbandonSerial(scope_serial);
            }
        }
        scope_timing.succeeded(separate_scope_source!=nullptr);
    }
    const bool composed=timed_transport_call(perf,TransportPerfStage::composition,[&]{return runtime_.BeginGraphicsFrame() &&
        compositor_.RenderStereo(runtime_,frame,token.source,composed_layout,menu_reticle,&eye_mask,{},quad?nullptr:eye_overlay_,overlay_context_,separate_scope_source)&&eye_mask==3;});
    if (!composed || eye_mask!=3) {
        auto result=reject(TransportStatus::compositor_failed,serial);
        result.released_eye_mask=eye_mask;
        return result;
    }
    const auto kind=quad ? xr::CompositionLayerKind::quad : xr::CompositionLayerKind::projection;
    xr::EndFrameResult ended{};
    {TransportPerfScope timing(perf,TransportPerfStage::end_frame);
        ended=runtime_.EndFrame(frame,{kind,quad?nullptr:&token.source,quad});
        timing.succeeded(ended.frame_ended&&ended.accepted_kind==kind);}
    const bool accepted=ended.frame_ended&&ended.accepted_kind==kind;submission_timing.succeeded(accepted);
    return {accepted
                ? (quad?TransportStatus::quad:TransportStatus::projection) : TransportStatus::end_failed,
            serial,eye_mask,ended.frame_ended};
}
bool OpenXrTransport::before_reset(bool already_lost) {
    const bool base=already_lost ? bridge_.PrepareForLostDeviceReset(runtime_.d3d11_context())
                                : bridge_.Invalidate(runtime_.d3d11_context());
    const bool scope=already_lost ? scope_bridge_.PrepareForLostDeviceReset(runtime_.d3d11_context())
                                 : scope_bridge_.Invalidate(runtime_.d3d11_context());
    return base && scope;
}
bool OpenXrTransport::after_reset(bool was_lost, bool succeeded) {
    if (!succeeded) return false;
    if(!was_lost)return true;
    const bool base=bridge_.FinalizeSuccessfulLostDeviceReset();
    const bool scope=scope_bridge_.FinalizeSuccessfulLostDeviceReset();
    return base && scope;
}
bool OpenXrTransport::shutdown() {
    const bool base=bridge_.Invalidate(runtime_.d3d11_context());
    const bool scope=scope_bridge_.Invalidate(runtime_.d3d11_context());
    if(!base || !scope)return false;
    scope_bridge_.Shutdown(); bridge_.Shutdown(); compositor_.Shutdown(); runtime_.Shutdown();
    initialized_=false;if(transport_perf.owner==this)transport_perf={};return true;
}
} // namespace bovr::vr
