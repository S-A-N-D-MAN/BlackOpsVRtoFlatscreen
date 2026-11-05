// SPDX-License-Identifier: GPL-3.0-only
#include "probe_support.hpp"
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    wchar_t runtime_json[32768]{}, local_data[32768]{};
    if (!GetEnvironmentVariableW(L"XR_RUNTIME_JSON",runtime_json,32768) ||
        !GetEnvironmentVariableW(L"LOCALAPPDATA",local_data,32768) ||
        std::wstring(runtime_json).find(L"simulator-runtime-x86") == std::wstring::npos ||
        !fs::exists(runtime_json)) return 3;
    const fs::path output = fs::absolute(argv[1]);
    const fs::path simulator = fs::path(local_data) / "OpenXR-Simulator";
    fs::create_directories(output);
    fs::create_directories(simulator);
    log_file.open(output / "host.log");
    std::ofstream frames(output / "frames.jsonl");
    xr::OpenXrRuntime runtime;
    xr::D3D9ExSharedTextureBridge bridge;
    xr::D3D11Compositor compositor;
    TestSurface surface;
    xr::HostCallbacks callbacks{nullptr,&log_callback};
    auto config = blackopsvr::default_runtime_config();
    config.application_name = "Black Ops VR standalone simulator transport probe";
    config.render_scale = 1.0f;
    if (!runtime.Initialize(config,callbacks)) return 4;
    if (std::string(runtime.active_runtime_name()).find("Simulator") == std::string::npos) {
        runtime.Shutdown(); return 5;
    }
    if (!surface.initialize(runtime.d3d11_device()) ||
        !bridge.Initialize({},callbacks) || !compositor.Initialize(runtime,callbacks)) return 6;
    unsigned projection_frames=0, zero_frames=0, errors=0, complete_eye_frames=0;
    unsigned focused_frames=0, unfocused_frames=0, pose_changed_frames=0;
    unsigned right_tracking_lost_frames=0, right_tracking_recovered_frames=0, analog_frames=0;
    unsigned completed_steps=0,intentional_zero_frames=0,post_recovery_frames=0,unfocused_live_input_frames=0;
    unsigned raw_unfocused_live_input_frames=0;
    bovr::vr::InputFocusGate input_focus_gate;
    bool pixels_verified=false, screenshot_requested=false;
    bool source_pose_exact=true, clean_invalidation=false, exit_observed=false;
    const bool focus_test=GetEnvironmentVariableW(L"BOVR_PROBE_FOCUS_TEST",nullptr,0)>0;
    bool focus_baseline=false,focus_loss_observed=false,focus_recovery_observed=false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(focus_test?75:20);
    for (unsigned step=0; step<(focus_test?5400u:360u) && std::chrono::steady_clock::now()<deadline;) {
        if (!runtime.PollEvents() || runtime.exit_requested()) break;
        xr::FrameState frame{};
        if (!runtime.BeginFrame(&frame)) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        ++step;
        completed_steps=step;
        if (frame.actions.focused) ++focused_frames; else ++unfocused_frames;
        auto has_live_input=[](const xr::ActionSnapshot& actions) {
            bool live=actions.menu.active || actions.menu.current;
            for (const auto& hand : actions.hands) {
                live=live || hand.trigger.active || hand.trigger.current!=0 ||
                    hand.squeeze.active || hand.squeeze.current!=0 || hand.stick.active ||
                    hand.stick.current.x!=0 || hand.stick.current.y!=0 ||
                    hand.primary.active || hand.primary.current || hand.secondary.active ||
                    hand.secondary.current || hand.trigger_click.active || hand.trigger_click.current ||
                    hand.stick_click.active || hand.stick_click.current || hand.thumbrest.active || hand.thumbrest.current;
            }
            return live;
        };
        if (!frame.actions.focused && has_live_input(frame.actions)) ++raw_unfocused_live_input_frames;
        const bool game_input_enabled=input_focus_gate.apply(frame.actions);
        if (!frame.actions.focused && has_live_input(frame.actions)) ++unfocused_live_input_frames;
        if (focus_test && step>360) {
            if (frame.actions.focused) {
                focus_baseline=true;
                if (focus_loss_observed) { focus_recovery_observed=true; ++post_recovery_frames; }
            } else if (focus_baseline) focus_loss_observed=true;
        }
        if (step > 80 && std::abs(frame.head_center.position.x-0.25f)<0.01f &&
            std::abs(frame.head_center.position.y-1.75f)<0.01f &&
            std::abs(frame.head_center.orientation.y)>0.1f) ++pose_changed_frames;
        const auto& right = frame.actions.hands[1];
        if (step>190 && step<225 && !right.grip.position_valid && !right.grip.orientation_valid)
            ++right_tracking_lost_frames;
        if (step>265 && right.grip.position_valid && right.grip.orientation_valid)
            ++right_tracking_recovered_frames;
        if (step>135 && step<165 && right.trigger.active && right.squeeze.active &&
            std::abs(right.trigger.current-0.7f)<0.01f && std::abs(right.squeeze.current-0.8f)<0.01f)
            ++analog_frames;
        if (step == 10) write_command(simulator,"head_pose_command.json",
            R"({"x":0,"y":1.6,"z":0,"yaw":0,"pitch":0,"roll":0})");
        if (step == 20) write_command(simulator,"controller_pose_command.json",
            R"({"hand":0,"pose":"both","scripted":1,"tracking":1,"posX":-0.2,"posY":-0.3,"posZ":-0.4,"grip":0,"trigger":0})");
        if (step == 35) write_command(simulator,"controller_pose_command.json",
            R"({"hand":1,"pose":"both","scripted":1,"tracking":1,"posX":0.2,"posY":-0.3,"posZ":-0.4,"grip":0,"trigger":0})");
        if (step == 60) write_command(simulator,"head_pose_command.json",
            R"({"x":0.25,"y":1.75,"z":-0.15,"yaw":0.35,"pitch":-0.1,"roll":0.07})");
        if (step == 110) write_command(simulator,"controller_pose_command.json",
            R"({"hand":1,"pose":"both","scripted":1,"tracking":1,"posX":0.3,"posY":-0.25,"posZ":-0.35,"grip":0.8,"trigger":0.7,"stickX":0.4,"stickY":-0.3})");
        if (step == 170) write_command(simulator,"controller_pose_command.json",
            R"({"hand":1,"tracking":0})");
        if (step == 230) write_command(simulator,"controller_pose_command.json",
            R"({"hand":1,"tracking":1,"grip":0,"trigger":0})");
        if (step == 100 || step == 290) {
            std::error_code ec; fs::remove(simulator/"screenshot.bmp",ec);
            screenshot_requested = write_command(simulator,"screenshot_request.json",
                R"({"eye":"both","layer":"projection","include_ui":false})");
        }
        if (step == 145) save_screenshot(simulator,output,"stereo-initial");
        if (step == 335) save_screenshot(simulator,output,"stereo-recovered");
        if (step == 345) { std::ofstream request(simulator/"projection_log_dump_request"); request << "1"; }
        xr::EndFrameResult ended{};
        std::uint64_t serial=0;
        std::uint32_t eye_mask=0;
        const char* transport_status="zero_layer";
        HRESULT native_present_result=S_OK;
        // Two intentional zero-layer frames exercise the lifecycle's discard route.
        if (step == 155 || step == 156 || !frame.should_render || !frame.views_valid) {
            ended = runtime.EndFrame(frame,{});
            ++zero_frames;
            if (step==155 || step==156) ++intentional_zero_frames;
        } else {
            xr::SharedFrameToken token{};
            bool acquired=false;
            const bool drawn=surface.draw();
            const bool retired=drawn && bridge.RetireCompletedFrames(runtime.d3d11_device(),runtime.d3d11_context());
            const bool captured=retired && bridge.CaptureBackBuffer(surface.device.Get(),&frame,&serial);
            transport_status=!drawn?"source_draw_failed":!retired?"retirement_failed":!captured?"capture_failed":"acquire_failed";
            if (captured) {
                native_present_result=surface.device->PresentEx(nullptr,nullptr,nullptr,nullptr,0);
                acquired=bridge.AcquireSerial(runtime.d3d11_device(),runtime.d3d11_context(),serial,&token);
            }
            if (acquired) {
                const bool exact_source = token.source.serial==serial &&
                    token.source.rendered_views_valid &&
                    std::memcmp(token.source.rendered_eyes,frame.eyes,sizeof(frame.eyes))==0;
                source_pose_exact=source_pose_exact && exact_source;
                if (!pixels_verified) pixels_verified=verify_shared_pixels(
                    runtime.d3d11_device(),runtime.d3d11_context(),token.source);
                const bool composed=exact_source && runtime.BeginGraphicsFrame() &&
                    compositor.RenderStereo(runtime,frame,token.source,{},nullptr,&eye_mask);
                if (composed && eye_mask==3) {
                    ++complete_eye_frames;
                    ended=runtime.EndFrame(frame,{xr::CompositionLayerKind::projection,&token.source,nullptr});
                    transport_status="projection";
                } else {
                    transport_status=exact_source?"compositor_failed":"metadata_mismatch";
                    ++errors; ended=runtime.EndFrame(frame,{});
                }
                bridge.Release(runtime.d3d11_context(),token);
            } else {
                ++errors;
                if (captured) bridge.AbandonSerial(serial);
                ended=runtime.EndFrame(frame,{});
            }
        }
        if (ended.accepted_kind==xr::CompositionLayerKind::projection) ++projection_frames;
        if (!ended.frame_ended) ++errors;
        frames << "{\"step\":" << step << ",\"frame_id\":" << frame.frame_id
               << ",\"serial\":" << serial << ",\"released_eye_mask\":" << eye_mask
               << ",\"projection\":" << (ended.accepted_kind==xr::CompositionLayerKind::projection)
               << ",\"transport_status\":\"" << transport_status << "\""
               << ",\"frame_ended\":" << ended.frame_ended
               << ",\"native_present_hr\":" << static_cast<unsigned long>(native_present_result)
               << ",\"game_input_enabled\":" << game_input_enabled
               << ",\"focused\":" << frame.actions.focused
               << ",\"head_x\":" << frame.head_center.position.x
               << ",\"head_y\":" << frame.head_center.position.y
               << ",\"head_z\":" << frame.head_center.position.z
               << ",\"head_qy\":" << frame.head_center.orientation.y
               << ",\"right_position_valid\":" << right.grip.position_valid
               << ",\"right_orientation_valid\":" << right.grip.orientation_valid
               << ",\"right_trigger\":" << right.trigger.current
               << ",\"right_grip\":" << right.squeeze.current << "}\n";
        if (step%90==0) frames.flush();
        if (focus_test && post_recovery_frames>=90) break;
    }
    runtime.RequestExit();
    runtime.PollEvents();
    exit_observed=runtime.exit_requested();
    clean_invalidation=bridge.Invalidate(runtime.d3d11_context());
    bridge.Shutdown();
    compositor.Shutdown();
    runtime.Shutdown();
    const bool pass = projection_frames>=300 && complete_eye_frames==projection_frames &&
        errors==0 && pixels_verified && source_pose_exact && clean_invalidation && exit_observed &&
        pose_changed_frames>0 && right_tracking_lost_frames>0 && right_tracking_recovered_frames>0 &&
        analog_frames>0 && intentional_zero_frames==2 && unfocused_live_input_frames==0 &&
        (!focus_test || (focus_loss_observed && focus_recovery_observed && post_recovery_frames>=90));
    std::ofstream summary(output/"summary.json");
    summary << "{\n  \"pass\":" << (pass?"true":"false")
      << ",\n  \"projection_frames\":" << projection_frames
      << ",\n  \"complete_eye_frames\":" << complete_eye_frames
      << ",\n  \"completed_steps\":" << completed_steps
      << ",\n  \"zero_layer_frames\":" << zero_frames
      << ",\n  \"intentional_zero_frames\":" << intentional_zero_frames
      << ",\n  \"errors\":" << errors
      << ",\n  \"shared_pixels_verified\":" << pixels_verified
      << ",\n  \"capture_pose_metadata_exact\":" << source_pose_exact
      << ",\n  \"focused_frames\":" << focused_frames
      << ",\n  \"unfocused_frames\":" << unfocused_frames
      << ",\n  \"unfocused_live_input_frames\":" << unfocused_live_input_frames
      << ",\n  \"raw_unfocused_live_input_frames\":" << raw_unfocused_live_input_frames
      << ",\n  \"moved_head_frames\":" << pose_changed_frames
      << ",\n  \"right_tracking_lost_frames\":" << right_tracking_lost_frames
      << ",\n  \"right_tracking_recovered_frames\":" << right_tracking_recovered_frames
      << ",\n  \"analog_input_frames\":" << analog_frames
      << ",\n  \"clean_bridge_invalidation\":" << clean_invalidation
      << ",\n  \"runtime_exit_observed\":" << exit_observed
      << ",\n  \"screenshot_request_sent\":" << screenshot_requested
      << ",\n  \"focus_test_requested\":" << (focus_test?"true":"false")
      << ",\n  \"focus_loss_observed\":" << (focus_loss_observed?"true":"false")
      << ",\n  \"focus_recovery_observed\":" << (focus_recovery_observed?"true":"false")
      << ",\n  \"post_focus_recovery_frames\":" << post_recovery_frames
      << ",\n  \"physical_or_game_test\":false\n}\n";
    std::printf("Simulator transport probe: %s; projection=%u errors=%u\n",pass?"PASS":"FAIL",projection_frames,errors);
    return pass?0:10;
}
