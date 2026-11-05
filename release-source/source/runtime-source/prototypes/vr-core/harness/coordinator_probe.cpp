// SPDX-License-Identifier: GPL-3.0-only
#include "probe_support.hpp"
#include "openxr_transport.hpp"
#include <array>

namespace bo=bovr::vr;
namespace {
struct CaptureProof final {
    std::uint32_t producer_serial{};
    std::uint64_t expected_frame_id{};
    unsigned checked{},mismatched{},after_native_present{};
    bool native_present_already_called{};
    ComPtr<ID3D11Texture2D> staging;
    static bool validate(void* user,ID3D11Device* device,ID3D11DeviceContext* context,
                         const xr::FrameState& frame,const xr::D3D11SourceFrame& source) {
        auto& proof=*static_cast<CaptureProof*>(user);
        ++proof.checked;
        if (proof.native_present_already_called) ++proof.after_native_present;
        D3D11_TEXTURE2D_DESC desc{}; source.texture->GetDesc(&desc);
        const bool supported_format=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM ||
            desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM ||
            desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if (desc.Width!=kWidth || desc.Height!=kHeight || !supported_format) { ++proof.mismatched; return false; }
        if (!proof.staging) {
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
            if (FAILED(device->CreateTexture2D(&desc,nullptr,&proof.staging))) {
                ++proof.mismatched; return false;
            }
        }
        context->CopyResource(proof.staging.Get(),source.texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(proof.staging.Get(),0,D3D11_MAP_READ,0,&mapped))) {
            ++proof.mismatched; return false;
        }
        const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
        auto rgb=[&](unsigned x,unsigned y) {
            const auto* p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
            return (std::uint32_t(p[bgra?2:0])<<16)|(std::uint32_t(p[1])<<8)|p[bgra?0:2];
        };
        const auto left=rgb(400,400),right=rgb(900,400);
        const bool pixels=left==(proof.producer_serial&0xffffffu) &&
            right==((proof.producer_serial^0xaa55aau)&0xffffffu) &&
            rgb(400,450)==0xdc1e1eu && rgb(900,450)==0x193cdcu;
        context->Unmap(proof.staging.Get(),0);
        const bool match=pixels && frame.frame_id==proof.expected_frame_id && !proof.native_present_already_called;
        if (!match) {
            ++proof.mismatched;
            log_file << "Capture validation rejected frame=" << frame.frame_id << " expected="
                     << proof.expected_frame_id << " serial=" << source.serial << " producer="
                     << proof.producer_serial << " left=" << left << " right=" << right << '\n';
        }
        return match;
    }
};
const char* transport_name(bo::TransportStatus status) {
    switch(status) {
    case bo::TransportStatus::projection:return "projection";
    case bo::TransportStatus::zero_layer:return "zero_layer";
    case bo::TransportStatus::retirement_failed:return "retirement_failed";
    case bo::TransportStatus::capture_failed:return "capture_failed";
    case bo::TransportStatus::acquire_failed:return "acquire_failed";
    case bo::TransportStatus::metadata_mismatch:return "metadata_mismatch";
    case bo::TransportStatus::capture_validation_failed:return "capture_validation_failed";
    case bo::TransportStatus::compositor_failed:return "compositor_failed";
    default:return "end_failed";
    }
}
bool live_input(const xr::ActionSnapshot& actions) {
    bool live=actions.menu.active || actions.menu.current;
    for(const auto& hand:actions.hands) live=live || hand.trigger.active || hand.trigger.current!=0 ||
        hand.squeeze.active || hand.squeeze.current!=0 || hand.stick.active || hand.stick.current.x!=0 ||
        hand.stick.current.y!=0 || hand.primary.active || hand.primary.current || hand.secondary.active ||
        hand.secondary.current || hand.trigger_click.active || hand.trigger_click.current ||
        hand.stick_click.active || hand.stick_click.current || hand.thumbrest.active || hand.thumbrest.current;
    return live;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    wchar_t runtime_path[32768]{},app_data[32768]{};
    if(!GetEnvironmentVariableW(L"XR_RUNTIME_JSON",runtime_path,32768) ||
       !GetEnvironmentVariableW(L"LOCALAPPDATA",app_data,32768) ||
       std::wstring(runtime_path).find(L"simulator-runtime-x86")==std::wstring::npos ||
       !fs::exists(runtime_path)) return 3;
    const auto output=fs::absolute(argv[1]);
    const auto simulator=fs::path(app_data)/"OpenXR-Simulator";
    fs::create_directories(output); fs::create_directories(simulator);
    log_file.open(output/"host.log"); std::ofstream frames(output/"frames.jsonl");
    bo::OpenXrTransport transport; bo::FrameCoordinator coordinator(transport);
    CaptureProof proof;
    transport.set_capture_validator(&CaptureProof::validate,&proof);
    auto config=blackopsvr::default_runtime_config();
    config.application_name="Black Ops VR exact coordinator pre-Present probe"; config.render_scale=1.0f;
    if(!coordinator.initialize(config,{nullptr,&log_callback})) return 4;
    auto& runtime=transport.runtime();
    if(std::string(runtime.active_runtime_name()).find("Simulator")==std::string::npos) {
        coordinator.shutdown(); return 5;
    }
    TestSurface surface;
    if(!surface.initialize(runtime.d3d11_device())) { coordinator.shutdown(); return 6; }
    struct NativeWork {bo::FrameKey key{}; std::uint64_t serial{};};
    std::array<NativeWork,2> work_buffers{};
    unsigned steps=0,projections=0,zero_frames=0,errors=0,present_calls=0;
    unsigned reset_passes=0,post_reset_projections=0,incomplete_discards=0;
    unsigned focused=0,unfocused=0,unfocused_live=0,post_recovery=0,moved=0;
    bool focus_baseline=false,focus_lost=false,focus_recovered=false,reset_generation_changed=false;
    std::uint64_t pre_reset_generation=0;
    const bool focus_test=GetEnvironmentVariableW(L"BOVR_PROBE_FOCUS_TEST",nullptr,0)>0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(focus_test?75:20);
    while(steps<(focus_test?5400u:720u) && std::chrono::steady_clock::now()<deadline) {
        auto begun=coordinator.begin_frame();
        if(begun.status==bo::CoordinatorStatus::idle) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue;
        }
        if(begun.status!=bo::CoordinatorStatus::started) { ++errors; break; }
        ++steps;
        const auto& frame=begun.ticket.frame;
        if(frame.actions.focused) ++focused; else { ++unfocused; if(live_input(frame.actions)) ++unfocused_live; }
        if(focus_test && steps>360) {
            if(frame.actions.focused) { focus_baseline=true; if(focus_lost) {focus_recovered=true; ++post_recovery;} }
            else if(focus_baseline) focus_lost=true;
        }
        if(steps==10) write_command(simulator,"head_pose_command.json",R"({"x":0.25,"y":1.75,"z":-0.15,"yaw":0.35,"pitch":-0.1,"roll":0.07})");
        if(steps==20) write_command(simulator,"controller_pose_command.json",R"({"hand":1,"pose":"both","scripted":1,"tracking":1,"posX":0.3,"posY":-0.25,"posZ":-0.35,"grip":0.8,"trigger":0.7})");
        if(steps>40 && std::abs(frame.head_center.position.x-0.25f)<0.01f &&
            std::abs(frame.head_center.orientation.y)>0.1f) ++moved;
        if(steps==100 || steps==350) write_command(simulator,"screenshot_request.json",R"({"eye":"both","layer":"projection","include_ui":false})");
        if(steps==145) save_screenshot(simulator,output,"coordinator-before-reset");
        if(steps==395) save_screenshot(simulator,output,"coordinator-after-reset");
        auto& work=work_buffers[steps%2]; work={begun.ticket.key,steps};
        const bo::BackendFrameIdentity stamp{reinterpret_cast<std::uintptr_t>(&work),work.serial};
        proof.producer_serial=steps; proof.expected_frame_id=frame.frame_id; proof.native_present_already_called=false;
        bo::FrameTicket claimed{};
        bool staged=false;
        if(frame.should_render && frame.views_valid) {
            staged=coordinator.claim_render_frame(&claimed) && coordinator.stage_render_frame(claimed.key,stamp,{});
            if(!staged) ++errors;
            if(staged) {
                if(!surface.draw_eye(0,steps) || !coordinator.mark_eye_complete(work.key,stamp,0)) ++errors;
                if(steps!=120 && (!surface.draw_eye(1,steps) || !coordinator.mark_eye_complete(work.key,stamp,1))) ++errors;
            }
        }
        bo::PresentResult result{};
        HRESULT native_hr=S_OK;
        if(steps==240) {
            pre_reset_generation=begun.ticket.key.generation;
            const bool prepared=coordinator.before_reset(false);
            if(prepared) native_hr=surface.reset();
            const bool recovered=prepared && coordinator.after_reset(SUCCEEDED(native_hr));
            if(!recovered) ++errors; else ++reset_passes;
            result.status=recovered?bo::CoordinatorStatus::discarded_cancelled:bo::CoordinatorStatus::failed;
            result.key=begun.ticket.key; result.backend=stamp;
            result.transport={bo::TransportStatus::zero_layer,0,0,prepared};
            ++zero_frames;
        } else {
            result=(steps==155 || steps==156)?coordinator.discard_pending():coordinator.present(surface.device.Get());
            // This is deliberately AFTER capture/acquire/composite/xrEndFrame.
            // No native Present occurs between producing this pair and submit.
            proof.native_present_already_called=true;
            native_hr=surface.device->PresentEx(nullptr,nullptr,nullptr,nullptr,0); ++present_calls;
            if(FAILED(native_hr)) ++errors;
            if(result.status==bo::CoordinatorStatus::submitted) {
                ++projections;
                if(result.backend!=stamp || result.key!=begun.ticket.key ||
                   result.transport.released_eye_mask!=3 || !result.transport.frame_ended) ++errors;
                if(reset_passes) { ++post_reset_projections; reset_generation_changed=result.key.generation!=pre_reset_generation; }
            } else if(result.status==bo::CoordinatorStatus::discarded_cancelled ||
                      result.status==bo::CoordinatorStatus::discarded_nonrendering ||
                      result.status==bo::CoordinatorStatus::discarded_incomplete) {
                ++zero_frames;
                if(steps==120 && result.status==bo::CoordinatorStatus::discarded_incomplete) ++incomplete_discards;
            } else ++errors;
        }
        frames << "{\"step\":" << steps << ",\"xr_frame\":" << frame.frame_id
               << ",\"generation\":" << begun.ticket.key.generation << ",\"backend_buffer\":" << stamp.buffer_identity
               << ",\"producer_serial\":" << stamp.producer_serial << ",\"capture_serial\":" << result.transport.capture_serial
               << ",\"transport_status\":\"" << transport_name(result.transport.status) << "\",\"coordinator_status\":" << unsigned(result.status)
               << ",\"released_eye_mask\":" << result.transport.released_eye_mask << ",\"frame_ended\":" << result.transport.frame_ended
               << ",\"native_present_hr\":" << static_cast<unsigned long>(native_hr) << ",\"focused\":" << frame.actions.focused
               << ",\"game_live_input\":" << live_input(frame.actions) << ",\"head_x\":" << frame.head_center.position.x << "}\n";
        if(steps%90==0) frames.flush();
        if(focus_test && post_recovery>=90 && post_reset_projections>=200 && projections>=500) break;
    }
    runtime.RequestExit(); runtime.PollEvents(); const bool exit_observed=runtime.exit_requested();
    const bool clean_shutdown=coordinator.shutdown();
    const bool pass=errors==0 && projections>=500 && proof.checked==projections && proof.mismatched==0 &&
        proof.after_native_present==0 && reset_passes==1 && post_reset_projections>=200 && reset_generation_changed &&
        incomplete_discards==1 && zero_frames>=4 && unfocused_live==0 && moved>0 && exit_observed && clean_shutdown &&
        (!focus_test || (focus_lost && focus_recovered && post_recovery>=90));
    std::ofstream summary(output/"summary.json");
    summary << "{\n\"pass\":" << (pass?"true":"false") << ",\n\"exact_bo_coordinator_transport\":true"
        << ",\n\"native_present_order\":\"after_coordinator_present\",\n\"steps\":" << steps
        << ",\n\"projection_frames\":" << projections << ",\n\"zero_layer_frames\":" << zero_frames
        << ",\n\"errors\":" << errors << ",\n\"frame_coded_pixel_checks\":" << proof.checked
        << ",\n\"pixel_mismatches\":" << proof.mismatched << ",\n\"captures_after_native_present\":" << proof.after_native_present
        << ",\n\"native_present_calls\":" << present_calls << ",\n\"healthy_reset_passes\":" << reset_passes
        << ",\n\"post_reset_projections\":" << post_reset_projections << ",\n\"reset_generation_changed\":" << reset_generation_changed
        << ",\n\"intentional_incomplete_pair_discards\":" << incomplete_discards << ",\n\"focused_frames\":" << focused
        << ",\n\"unfocused_frames\":" << unfocused << ",\n\"unfocused_live_input_frames\":" << unfocused_live
        << ",\n\"moved_head_frames\":" << moved << ",\n\"focus_test_requested\":" << focus_test
        << ",\n\"focus_loss_observed\":" << focus_lost << ",\n\"focus_recovery_observed\":" << focus_recovered
        << ",\n\"post_focus_recovery_frames\":" << post_recovery << ",\n\"runtime_exit_observed\":" << exit_observed
        << ",\n\"clean_shutdown\":" << clean_shutdown << ",\n\"physical_or_game_test\":false\n}\n";
    std::printf("Exact BO coordinator simulator probe: %s; projections=%u errors=%u\n",pass?"PASS":"FAIL",projections,errors);
    return pass?0:10;
}
