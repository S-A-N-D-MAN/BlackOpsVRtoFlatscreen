// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/native_use_gaze_adapter.hpp"
#include "t5/stereo_view_math.hpp"
#include "t5/auxiliary_view.hpp"
#include "t5/auxiliary_postfx.hpp"
#include "t5/auxiliary_history.hpp"
#include "t5/auxiliary_history_correction.hpp"
#include "t5/bloom_shader_native.hpp"
#include "t5/bloom_shader_probe.hpp"
#include "t5/bloom_correction_replay.hpp"
#include "t5/bloom_episode_source.hpp"
#include "t5/bloom_episode_reuse_source.hpp"
#include "t5/cinematic_movie_log_budget.hpp"
#include "t5/quit_gate.hpp"
#include "loader/d3d9_observer.hpp"
#include "loader/trace.hpp"
#include "render/eye_staging.hpp"
#include "render/scope_staging.hpp"
#include "t5/thermal_scene_scope.hpp"
#include "render/device_replacement.hpp"
#include "render/hud_view_binding.hpp"
#include "render/hud_eye_commands.hpp"
#include "render/hud_pixel_diagnostics.hpp"
#include "t5/command_probe.hpp"
#include "t5/mounted_gun_runtime.hpp"
#include "t5/hud_command_publication.hpp"
#include "t5/hud_compass_material.hpp"
#include "t5/hud_hind_material.hpp"
#include "t5/hud_pass_probe.hpp"
#include "input/action_snapshot_cache.hpp"
#include "input/rig_snapshot_cache.hpp"
#include "t5/final_ui_probe.hpp"
#include "t5/weapon_scene_transaction.hpp"
#include "t5/weapon_render_admission.hpp"
#include "t5/native_melee_scene.hpp"
#include "t5/native_shadow_state.hpp"
#include "t5/shadow_pair_diagnostic.hpp"
#include "t5/stereo_dynamic_entities.hpp"
#include "t5/sun_shadow_lod_hook.hpp"
#include "t5/stereo_lod_fov_scope.hpp"
#include "t5/ordinary_bloom_scope.hpp"
#include "t5/ordinary_exposure_scope.hpp"
#include "t5/vpos_eye_scope.hpp"
#include "t5/water_reflection_correction.hpp"
#include "t5/water_vpos_correction.hpp"
#include "t5/ui_production_epoch.hpp"
#include "t5/menu_frame_epoch.hpp"
#include "t5/cinematic_frame.hpp"
#include "t5/cinematic_diagnostics.hpp"
#include "t5/cinematic_production.hpp"
#include "t5/cinematic_movie_frame.hpp"
#include "render/hud_commands.hpp"
#include "t5/frame_begin_probe.hpp"
#include "t5/render_producer_pacing.hpp"
#include "t5/weapon_lifecycle.hpp"
#include "t5/pose_clear_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/level_tracking_anchor.hpp"
#include "input/room_scale_camera.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/shot_haptic_adapter.hpp"
#include "ui/menu_panel.hpp"
#include "vr/frame_coordinator.hpp"
#include "vr/frame_discard_diagnostic.hpp"
#include "vr/openxr_transport.hpp"
#include "t5/manual_reload_overlay.hpp"
#include "t5/crossbow_adapter.hpp"
#include "t5/hand_mesh_draw.hpp"
#include "t5/free_glove_runtime_policy.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <memory>

namespace bovr::t5 {
namespace {
namespace xr = wawvr::xr;
bool menu_pointer_reticle(void*,xr::CompositorReticle*) noexcept;
const char* transport_name(vr::TransportStatus status) noexcept {
    switch(status) {
        case vr::TransportStatus::projection: return "projection";
        case vr::TransportStatus::zero_layer: return "zero_layer";
        case vr::TransportStatus::retirement_failed: return "retirement_failed";
        case vr::TransportStatus::capture_failed: return "capture_failed";
        case vr::TransportStatus::acquire_failed: return "acquire_failed";
        case vr::TransportStatus::metadata_mismatch: return "metadata_mismatch";
        case vr::TransportStatus::capture_validation_failed: return "capture_validation_failed";
        case vr::TransportStatus::compositor_failed: return "compositor_failed";
        case vr::TransportStatus::end_failed: return "end_failed";
        case vr::TransportStatus::quad: return "quad";
    }
    return "unknown";
}
constexpr std::uintptr_t frontend_rva=0x373708c, backend_rva=0x3a5b5f8;
constexpr std::uintptr_t first_buffer_rva=0x3740100, buffer_stride=0x186300;
constexpr std::uintptr_t first_view_array_rva=0x420f730, view_array_stride=0xe380;
constexpr std::size_t native_frame_offset=0x15d7a0, view_index_offset=0x16cc38;
constexpr std::size_t view_count_offset=0x16cc3c, view_pointer_offset=0x16cc40;
constexpr std::size_t parms_count_offset=0x16cbe0;
struct Buffer {
    unsigned char* address{};
    std::uint32_t frame{}, count{}, index{}, parms_count{};
    void* views{};
};
struct ThermalProduction {
    EyeShadowState shadows{};
    std::array<std::uint8_t,0x38e0> view{};
    std::uintptr_t address{};
};
struct Produced {
    bool valid{};
    bool completed{};
    vr::FrameKey key{};
    vr::BackendFrameIdentity stamp{};
    std::uint32_t native_frame{};
    std::array<RECT,2> eye_rectangles{};
    WaterEyeOrigins water_eye_origins{};
    bool menu{};
    xr::QuadLayer panel{};
    NativeFinalUi native_ui{};
    std::uint64_t menu_revision{};
    std::uint64_t ui_production_epoch{};
    std::uint64_t ui_production_ms{};
    std::uint32_t source_width{},source_height{};
    bool hud{};
    std::array<render::HudEyeDestination,2> hud_destinations{};
    bool cinematic{},cinematic_independent{};
    std::uint64_t cinematic_final_serial{};
    std::uint64_t cinematic_backend_serial{};
    NativeCinematicSnapshot cinematic_movie{};
    std::shared_ptr<const HudCommandPublication> cinematic_commands;
    bool regular_slots_copied{},auxiliary{};
    std::array<AuxiliaryViewSlot,2> regular_slots{};
    AuxiliaryViewLayout auxiliary_layout{};
    std::shared_ptr<const EyeShadowPair> shadows;
    std::shared_ptr<const ThermalProduction> thermal;
    bool thermal_captured{};
    std::int64_t predicted_display_time{};
    bool helicopter_reticle{};
};
struct PublishedHud {
    vr::FrameKey key{};
    vr::BackendFrameIdentity stamp{};
    std::uint64_t publication_serial{};
    HudCommandPublication allocation;
    bool auxiliary{};
    AuxiliaryViewLayout auxiliary_layout{};
};
struct HudPass {
    std::uint64_t call_serial{};
    Produced pixels{};
    std::shared_ptr<const PublishedHud> publication;
    std::array<render::HudEyeCommands,2> commands;
    render::HudViewCommandBinding binding;
    bool retirement_only{};
};
struct ReservationFailureEvidence {
    std::uint64_t call{};
    const char* stage{"none"};
    vr::NativeBuildIdentity native{};
    vr::SceneReservationDiagnostic copied{};
    bool snapshot{};
};
struct WeaponRejectionEvidence {
    vr::FrameKey key{};
    vr::NativeBuildIdentity native{};
    std::uint64_t call{};
    const char* reason{"none"};
    unsigned ownership{};
    ReservationFailureEvidence refusal{};
};
struct DiscardProductionEvidence {
    bool valid{},completed{},menu{};
    vr::FrameKey key{};
    vr::BackendFrameIdentity stamp{};
    std::uint32_t frame{};
};
std::uint64_t publication_clock() noexcept {return GetTickCount64();}
bool commit_native_yaw(std::uintptr_t address,float expected,float desired) noexcept {
    float current{};
    __try {
        memcpy(&current,reinterpret_cast<const void*>(address),sizeof(current));
        if(memcmp(&current,&expected,sizeof(current)))return false;
        memcpy(reinterpret_cast<void*>(address),&desired,sizeof(desired));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct Adapter {
    explicit Adapter(std::uintptr_t base):base(base), coordinator(transport,publication_clock),ui_epoch(base+first_buffer_rva),menu_frame_epoch(base),cinematic_production(base){}
    std::uintptr_t base{};
    vr::OpenXrTransport transport;
    std::atomic<bool> pimax_free_glove_grip_basis{};
    vr::FrameCoordinator coordinator;
    render::EyeStaging eye_staging;
    render::ScopeStaging scope_staging;
    std::mutex metadata;
    std::array<Produced,2> produced{};
    ShadowPairDiagnostic shadow_pair_diagnostic;
    std::uint64_t dynamic_entity_rejections{};
    Produced current_pixels{};
    std::array<std::shared_ptr<const PublishedHud>,2> hud_publications{};
    std::unique_ptr<HudPass> hud_pass;
    render::HudPixelDiagnostics hud_pixels;
    bool hud_pixel_enabled{};
    bool hud_enabled{};
    bool pacing_enabled{},pacing_wait{};
    std::int64_t pacing_frequency{};
    std::atomic<std::uint64_t> pacing_calls{},pacing_admitted{},pacing_yields{},pacing_ready{},pacing_timeouts{},pacing_cancelled{};
    std::atomic<unsigned> pacing_reports{};
    bool auxiliary_enabled{};
    bool auxiliary_postfx_enabled{};
    AuxiliaryPostfxBudget auxiliary_postfx_budget;
    bool auxiliary_history_enabled{};
    AuxiliaryHistoryBudget auxiliary_history_budget;
    std::atomic<std::uint64_t> auxiliary_history_epoch{};
    bool bloom_shader_enabled{};
    bool bloom_correction_enabled{};
    bool bloom_ordinary_correction{};
    std::atomic<bool> bloom_scene_active{};
    std::uint64_t bloom_rgb_scopes{},bloom_rgb_rejected{};
    BloomCorrectionReplay bloom_correction;
    BloomEpisodeReuseSession bloom_read_reuse;
    std::uint64_t bloom_successful_reset_serial{1};
    std::uint64_t bloom_callback_serial{},bloom_passive_token{},bloom_active_token{};
    vr::FrameKey bloom_rejected_key{};
    BloomShaderProbe bloom_shaders;
    bool history_correction_enabled{};
    AuxiliaryHistoryCorrection history_correction;
    AuxiliaryHistoryCapture history_correction_capture;
    std::atomic<std::uint64_t> history_correction_backend{};
    std::atomic<bool> history_backend_active{};
    std::uint64_t auxiliary_staged{},auxiliary_rejected{};
    std::uint64_t hud_publication_rejections{},hud_composed{},hud_rejected{};
    input::ActionSnapshotCache latest_actions;
    input::RigSnapshotCache latest_rig;
    HelicopterSceneView helicopter_view;
    struct {RoomScaleView view{};xr::Posef anchor{};std::uint64_t generation{},published_ms{};} room_scale;
    ui::MenuPanel menu_panel;
    UiProductionEpoch ui_epoch;
    MenuFrameEpoch menu_frame_epoch;
    bool menu_frame_enabled{};
    bool cinematic_enabled{},cinematic_panel_enabled{};
    CinematicFrameGate cinematic_gate;
    CinematicMovieFrameGate cinematic_movie_gate;
    bool cinematic_movie_enabled{};
    CinematicMovieLogBudget cinematic_movie_log;
    std::array<std::uint64_t,2> cinematic_movie_begin_counts{};
    std::array<std::uint64_t,8> cinematic_movie_complete_counts{};
    bool cinematic_diagnostic_enabled{};
    CinematicDiagnosticStart cinematic_diagnostic_start;
    CinematicDiagnosticBudget cinematic_diagnostic_budget;
    CinematicProductionEpoch cinematic_production;
    bool cinematic_production_enabled{};
    CinematicCensusOpen cinematic_census_open;
    std::uint64_t cinematic_production_serial{},cinematic_census_sample_ms{},cinematic_census_reports{};
    std::array<std::uint64_t,11> cinematic_production_begin_counts{},cinematic_production_end_counts{};
    std::array<std::array<char,256>,4> cinematic_census_raw_names{};
    std::array<unsigned,4> cinematic_census_raw_counts{};
    ui::MenuPanel cinematic_panel;
    NativeCinematicSnapshot cinematic_anchor_movie{};
    std::uint64_t cinematic_anchor_tick{},cinematic_anchor_native_epoch{};
    std::atomic<std::uint64_t> final_ui_serial{};
    std::atomic<std::uint64_t> cinematic_backend_serial{};
    std::uint64_t cinematic_owner_backend_serial{};
    std::uint64_t cinematic_completed{},cinematic_presented{};
    std::array<std::uint64_t,8> cinematic_status_counts{};
    std::uint64_t menu_events{},menu_event_tick{},menu_marker_serial{},menu_epoch_completed{},menu_epoch_rejected{};
    DWORD menu_event_thread{};
    bool menu_enabled{};
    bool ui_epoch_enabled{};
    std::atomic<std::uint64_t> menu_revision{1};
    std::uint64_t ui_started_revision{},ui_started_epoch{},menu_submission_serial{},submitted_menu_tick{};
    MenuPanelSnapshot submitted_menu{};
    std::uint64_t producer_serial{}, anchor_generation{};
    std::uint64_t native_build_serial{},weapon_scene_rejections{},weapon_scene_cancellations{};
    std::uint64_t weapon_render_rejections{},shadow_captures{},shadow_rejections{},shadow_restores{},shadow_backend_rejections{};
    std::uint64_t weapon_render_deferrals{},weapon_render_deferral_reports{};
    vr::FrameKey weapon_render_rejected_key{};
    WeaponSceneTransaction weapon_scene;
    native_melee_scene::Deferred native_melee_deferred;
    vr::FrameKey native_melee_diagnostic_key{};
    unsigned native_melee_deferred_reports{},native_melee_scene_reports{};
    bool weapon_reservations_enabled{};
    bool borrowed_weapon_reservations_enabled{};
    bool reservation_diagnostic_enabled{};
    bool movement_pose_correction{true},movement_pose_diagnostic{};
    unsigned movement_pose_reports{};
    std::atomic<std::uint64_t> reservation_diagnostic_refusals{},reservation_diagnostic_reports{};
    vr::FrameDiscardDiagnosticBudget discard_diagnostic_budget;
    bool diagnostic_handheld_seen{};
    ReservationFailureEvidence diagnostic_last_refusal;
    WeaponRejectionEvidence diagnostic_weapon_rejection;
    xr::Posef anchor{};
    bool anchor_valid{};
    std::atomic<bool> initialized{}, starting{}, cleanup_needed{}, stopped{}, stop_requested{}, device_lost{};
    std::atomic<bool> replacement_pending{};
    render::DevicePublicationGate scene_publications;
    bool attempted{};
    std::atomic<DWORD> owner{};
    IDirect3DDevice9* retained_device{};
    DeviceCreationInfo retained_creation{};
    std::uint64_t replacement_attempts{},device_replacements{};
    unsigned pending_presents{};
    unsigned pending_deadline_reports{};
    std::uint64_t submitted{}, discarded{};
    bool hand_mesh_active{};
    bool water_reflection_enabled{};
    bool water_admission_probe{};
    std::atomic<std::uint32_t> water_admission_states{};
    WaterReflectionCorrection water_reflections;
    WaterVPosCorrection water_vpos;
    std::uint64_t water_vpos_token{};
    std::uint64_t water_draw_token{};
};
bool menu_pointer_reticle(void* context,xr::CompositorReticle* output) noexcept {
    if(!context||!output)return false;
    *output={};MenuPointerVisual visual{};
    if(!read_menu_pointer_visual(&visual))return false;
    auto& a=*static_cast<Adapter*>(context);
    try {
        std::lock_guard lock(a.metadata);
        if(!a.submitted_menu.interactive||
           visual.generation!=a.submitted_menu.generation||
           visual.submission_serial!=a.submitted_menu.submission_serial)return false;
    } catch(...) {return false;}
    *output={true,0,visual.u,visual.v};return true;
}
// Explicit process-lifetime context: never run XR/D3D destruction from DllMain.
// Normal native quit uses the owner-thread retirement gate below.
std::atomic<Adapter*> adapter{};
// All raw native values are copied by the reader. Only the retained device
// getter APIs may mint shader/texture ownership inside the collector.
std::uint64_t before_bloom_draw(void* context,IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.water_admission_probe){
        const bool owner=GetCurrentThreadId()==a.owner.load(),same_device=owner&&device==a.retained_device;
        const bool history=a.history_backend_active.load();
        const bool bloom_blocked=!owner||(a.bloom_correction_enabled&&(a.bloom_correction.busy()||a.bloom_correction.pending()));
        const unsigned state=unsigned(owner)|(unsigned(same_device)<<1)|(unsigned(history)<<2)|
            (unsigned(a.water_reflection_enabled)<<3)|(unsigned(bloom_blocked)<<4);
        const auto bit=std::uint32_t{1}<<state;
        if(!(a.water_admission_states.fetch_or(bit,std::memory_order_relaxed)&bit))
            trace("t5_water_draw_admission","state=%u owner=%u same_device=%u history_active=%u water_enabled=%u bloom_blocked=%u lost=%u initialized=%u primitive=%u caller=%p device=%p retained=%p cap=32_unique_states",
                state,owner,same_device,history,a.water_reflection_enabled,bloom_blocked,a.device_lost.load(),a.initialized.load(),unsigned(call.type),reinterpret_cast<void*>(call.caller),device,owner?a.retained_device:nullptr);
    }
    if(a.water_reflection_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&a.water_vpos.pending()) {
        if(!a.device_lost.load()&&!a.water_vpos.busy())a.water_vpos.reconcile_normal(device,true);
        if(a.water_vpos.pending()){a.stop_requested=true;return 0;}
    }
    if(a.water_reflection_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&a.water_reflections.pending()) {
        if(!a.device_lost.load()&&!a.water_reflections.busy())a.water_reflections.reconcile_normal(device,true);
        if(a.water_reflections.pending()){a.stop_requested=true;return 0;}
    }
    if(GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&!a.device_lost.load()&&a.history_backend_active.load()&&
       before_hand_mesh_draw(device,call)){a.hand_mesh_active=true;return UINT64_MAX;}
    if(a.bloom_correction_enabled&&GetCurrentThreadId()!=a.owner.load()){a.bloom_read_reuse.revoke();return 0;}
    // A pending journal came from our own contained normal-return operation.
    // Reconcile before ANY owner draw, including unrelated native callers;
    // never infer safety from the original-native unwind abandonment flag.
    if(a.bloom_correction_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&
       !a.device_lost.load()&&a.bloom_correction.pending()&&!a.bloom_correction.busy())
        a.bloom_correction.reconcile_normal(device,true);
    if(a.bloom_correction_enabled&&a.bloom_read_reuse.open()){a.bloom_read_reuse.revoke();return 0;}
    if(a.water_reflection_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&
       !a.device_lost.load()&&a.history_backend_active.load()&&
       (!a.bloom_correction_enabled||(!a.bloom_correction.busy()&&!a.bloom_correction.pending()))) {
        if(!a.water_reflections.busy()&&!a.water_reflections.pending()){
            const auto actual=a.water_vpos.before(device,call);
            if(actual){a.water_vpos_token=actual;return UINT64_MAX-2;}
            if(a.water_vpos.pending()){a.stop_requested=true;return 0;}
        }
        const auto token=a.water_reflections.before(device,call);
        if(token){a.water_draw_token=token;return UINT64_MAX-1;}
        if(a.water_reflections.pending()){a.stop_requested=true;return 0;}
    }
    if(!a.bloom_scene_active.load())return 0;
    if((!a.bloom_shader_enabled&&!a.bloom_correction_enabled)||!a.history_backend_active.load()||GetCurrentThreadId()!=a.owner.load()||
       device!=a.retained_device||call.caller!=a.base+0x30b5d3||call.type!=D3DPT_TRIANGLELIST||call.num_vertices!=4||call.primitive_count!=2)return 0;
    if(a.bloom_correction_enabled&&a.device_lost.load())return 0;
    if(a.bloom_correction_enabled) {
        const BloomDrawCall draw{call.caller,static_cast<std::uint32_t>(call.type),call.base_vertex,call.min_vertex,call.num_vertices,call.start_index,call.primitive_count};
        a.bloom_passive_token=0;a.bloom_active_token=0;
        __try {if(a.bloom_shader_enabled)a.bloom_passive_token=a.bloom_shaders.before(device,draw);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
        __try {if(a.bloom_read_reuse.arm(draw))a.bloom_active_token=a.bloom_correction.before(device,draw);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_correction.abandon();}
        if(!a.bloom_active_token){a.bloom_read_reuse.report_refusal();a.bloom_read_reuse.close();}
        if(!a.bloom_passive_token&&!a.bloom_active_token)return 0;
        if(++a.bloom_callback_serial==0){a.bloom_read_reuse.close();a.bloom_correction.abandon();a.bloom_shaders.abandon();return 0;}
        return a.bloom_callback_serial;
    }
    std::uint64_t token{};
    __try {token=a.bloom_shaders.before(device,{call.caller,static_cast<std::uint32_t>(call.type),call.base_vertex,call.min_vertex,call.num_vertices,call.start_index,call.primitive_count});}
    __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();token=0;}
    return token;
}
void after_bloom_draw(void* context,std::uint64_t token,HRESULT result) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.hand_mesh_active&&token==UINT64_MAX){after_hand_mesh_draw();a.hand_mesh_active=false;return;}
    if(token==UINT64_MAX-2&&a.water_vpos_token){
        a.water_vpos.after(a.water_vpos_token,result);a.water_vpos_token=0;
        if(a.water_vpos.pending())a.stop_requested=true;return;
    }
    if(token==UINT64_MAX-1&&a.water_draw_token){
        a.water_reflections.after(a.water_draw_token,result);a.water_draw_token=0;
        if(a.water_reflections.pending())a.stop_requested=true;return;
    }
    if(a.bloom_correction_enabled) {
        if(GetCurrentThreadId()!=a.owner.load()){a.bloom_read_reuse.revoke();return;}
        if(token!=a.bloom_callback_serial){a.bloom_read_reuse.close();a.bloom_correction.abandon();a.bloom_shaders.abandon();return;}
        __try {if(a.bloom_passive_token)a.bloom_shaders.after(a.bloom_passive_token,result);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
        __try {if(a.bloom_active_token)a.bloom_correction.after(a.bloom_active_token,result);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_correction.abandon();}
        a.bloom_read_reuse.report_refusal();a.bloom_read_reuse.close();a.bloom_passive_token=0;a.bloom_active_token=0;return;
    }
    __try {a.bloom_shaders.after(token,result);}
    __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
}
void abandon_bloom_draw(void* context,std::uint64_t token) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.hand_mesh_active&&token==UINT64_MAX){abandon_hand_mesh_draw();a.hand_mesh_active=false;return;}
    if(token==UINT64_MAX-2&&a.water_vpos_token){
        a.water_vpos.abandon(a.water_vpos_token);a.water_vpos_token=0;
        if(a.water_vpos.pending())a.stop_requested=true;return;
    }
    if(token==UINT64_MAX-1&&a.water_draw_token){
        a.water_reflections.abandon(a.water_draw_token);a.water_draw_token=0;
        if(a.water_reflections.pending())a.stop_requested=true;return;
    }
    if(a.bloom_correction_enabled){a.bloom_read_reuse.revoke();if(GetCurrentThreadId()!=a.owner.load())return;
        a.bloom_read_reuse.close();a.bloom_correction.abandon(a.bloom_active_token);if(a.bloom_passive_token)a.bloom_shaders.abandon(a.bloom_passive_token);a.bloom_active_token=0;a.bloom_passive_token=0;}
    else a.bloom_shaders.abandon(token);
}
bool complete_bloom_draw_scope(Adapter& a,const Produced& production,bool scope_started,bool complete) noexcept {
    if(!a.bloom_correction_enabled)return complete;
    a.bloom_read_reuse.close();
    if(production.auxiliary||(a.bloom_ordinary_correction&&production.regular_slots_copied))complete=scope_started&&a.bloom_correction.finish(complete);
    if(a.bloom_correction.pending())complete=false;
    if(!complete)a.bloom_rejected_key=production.key;
    return complete;
}
void begin_native_ui(const NativeFinalUi&);
void stage_cinematic_at_present(Adapter&,IDirect3DDevice9*);
bool current_cinematic_pixels(const Adapter&,const Produced&);
void complete_native_ui(const NativeFinalUi&,const NativeFinalUi&);
void begin_ui_production(const NativeUiProduction&);
void complete_ui_production(const NativeUiProduction&,const NativeUiProduction&);
void begin_menu_frame(const FrameBeginPoint&);
void begin_cinematic_production(Adapter&,const FrameBeginPoint&) noexcept;
void publish_cinematic_production(Adapter&,const HudPassPoint&,std::shared_ptr<const HudCommandPublication>) noexcept;
void begin_cinematic_census(Adapter&,const NativeFinalUi&,std::uint64_t) noexcept;
void complete_cinematic_census(Adapter&,const NativeFinalUi&) noexcept;
void publish_menu_frame(Adapter&,const HudPassPoint&,std::shared_ptr<const HudCommandPublication>);
void publish_native_hud(const HudPassPoint&);
void begin_native_hud(const HudPassPoint&);
void complete_native_hud(const HudPassPoint&);
bool enabled(const wchar_t* name) noexcept {
    wchar_t value[8]{}; GetEnvironmentVariableW(name,value,8); return wcscmp(value,L"1")==0;
}
bool read_ui_context(Adapter& a,NativeFinalUi* output) noexcept {
    NativeFinalUi value{};
    __try {
        memcpy(&value.connection_state,reinterpret_cast<void*>(a.base+0x2510164),4);
        memcpy(&value.key_catchers,reinterpret_cast<void*>(a.base+0x2510160),4);
        memcpy(&value.menu_count,reinterpret_cast<void*>(a.base+0x216bf4c),4);
        value.valid=read_native_menu_page(a.base,&value.page);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    *output=value;return true;
}
bool known_menu(const NativeFinalUi& value) noexcept {
    return value.valid && (value.key_catchers&0x10)!=0 && value.menu_count>0 &&
           value.connection_state>=0 && value.connection_state<=10;
}
bool read_buffer(Adapter& a, bool frontend, Buffer* output) noexcept {
    Buffer b{};
    __try {
        b.address=*reinterpret_cast<unsigned char**>(a.base+(frontend?frontend_rva:backend_rva));
        const auto p=reinterpret_cast<std::uintptr_t>(b.address);
        if (p!=a.base+first_buffer_rva && p!=a.base+first_buffer_rva+buffer_stride) return false;
        memcpy(&b.frame,b.address+native_frame_offset,4);
        memcpy(&b.count,b.address+view_count_offset,4);
        memcpy(&b.index,b.address+view_index_offset,4);
        memcpy(&b.parms_count,b.address+parms_count_offset,4);
        memcpy(&b.views,b.address+view_pointer_offset,sizeof(void*));
        const std::uintptr_t slot=p==a.base+first_buffer_rva?0:1;
        if (reinterpret_cast<std::uintptr_t>(b.views)!=a.base+first_view_array_rva+slot*view_array_stride ||
            b.count>4 || (b.count && b.index>=b.count)) return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    *output=b; return true;
}
std::size_t buffer_slot(Adapter& a, const Buffer& b) noexcept {
    return reinterpret_cast<std::uintptr_t>(b.address)==a.base+first_buffer_rva?0:1;
}
// Opt-in producer pacing experiment. Copies below confer no frame authority.
namespace pace=producer_pacing;
bool pacing_live(Adapter& a) noexcept {
    // cleanup_needed records eventual resource retirement, not cleanup in progress.
    return adapter.load()==&a&&a.initialized.load()&&!a.starting.load()&&
        !a.stopped.load()&&!a.stop_requested.load()&&!a.device_lost.load()&&
        !a.replacement_pending.load()&&!quit_retirement_requested();
}
pace::Pixels pacing_pixels(Adapter& a) {
    std::lock_guard lock(a.metadata);const auto& p=a.current_pixels;
    return {p.key.generation,p.key.frame_id,p.stamp.producer_serial,p.stamp.buffer_identity,p.native_frame,
        p.valid,p.completed,p.hud,p.menu,p.auxiliary,p.cinematic||p.cinematic_independent,
        p.thermal_captured||bool(p.thermal)};
}
bool pacing_command(const PreCommandSnapshot& p,const FrameBeginPoint& point,std::uint64_t now) noexcept {
    return p.context.valid&&p.context.native_thread==point.native_thread&&p.context.local_client==0&&
        p.context.connection_state==10&&!p.context.key_catcher&&!p.context.native_override&&
        p.native_gameplay_fields_valid&&p.gameplay_active&&!p.linked_weapon_active&&p.native_health>0&&
        p.native_pm_type==0&&!(p.native_pm_flags&(0xc00u|4u|8u|0x20000u))&&
        !(p.native_entity_flags&0x4300u)&&!(p.native_entity_flags2&0x50040000u)&&
        p.gameplay_epoch&&p.sequence&&p.native_cg_identity&&p.observed_ms&&now>=p.observed_ms&&
        now-p.observed_ms<=100;
}
bool read_pacing_sample(Adapter& a,const FrameBeginPoint& point,pace::Sample* output) {
    pace::Sample s{};auto& c=s.context;
    const auto failed=[&](unsigned stage){s.copy_stage=stage;*output=s;return false;};
    c.adapter=reinterpret_cast<std::uintptr_t>(&a);c.base=a.base;c.caller=point.caller_return;
    c.thread=point.native_thread;c.owner=a.owner.load();c.known_caller=point.known_direct_caller&&point.ordinary_screen_frame;
    c.live=pacing_live(a);s.now_ms=GetTickCount64();
    if(!c.live||!c.known_caller||point.image_base!=a.base||point.native_thread!=GetCurrentThreadId()||
       !c.owner||c.owner==point.native_thread){s.copied=true;*output=s;return true;}
    Buffer before{},after{};PreCommandSnapshot pre{},post{};NativeFinalUi ui{},ui_after{};
    c.menu_revision=a.menu_revision.load();
    if(!read_pre_command_snapshot(&pre)||!pacing_command(pre,point,s.now_ms))return failed(1);
    if(!read_ui_context(a,&ui)||!ui.valid||ui.connection_state!=10||ui.key_catchers||ui.page.stack_count)return failed(2);
    if(!read_buffer(a,true,&before))return failed(3);
    c.ordinary=true;c.cg=pre.native_cg_identity;c.epoch=pre.gameplay_epoch;c.command_sequence=pre.sequence;
    c.command=pre.context.command_number;c.native_clock=pre.context.native_frame_time;c.game_time=pre.native_game_time;
    c.frontend=reinterpret_cast<std::uintptr_t>(before.address);c.views=reinterpret_cast<std::uintptr_t>(before.views);
    c.native_frame=before.frame;c.view_count=before.count;c.parms_count=before.parms_count;
    s.pixels=pacing_pixels(a); // Releases adapter metadata before coordinator copy.
    const auto snapshot=a.coordinator.producer_pacing_snapshot();
    c.generation=snapshot.generation;
    const auto copy_pending=[](const vr::SceneReservationDiagnostic& d) {
        return pace::Pending{d.key.generation,d.key.frame_id,d.published_ms,d.reservation_serial,d.reserved_native.build_serial,
            d.backend_producer,d.reserved_native.buffer_identity,d.backend_buffer,d.reserved_native.native_frame,d.eye_mask,
            d.initialized,d.busy,d.runtime_faulted,d.reset_blocked,d.pending,d.invalid,d.claimed,d.staged,
            d.menu,d.menu_complete,d.should_render,d.views_valid,d.awaiting_tracked_reservation};
    };
    s.pending=copy_pending(snapshot.pending);
    s.flight={snapshot.flight.serial,copy_pending(snapshot.flight.completed)};
    // Bound all engine copies by repeated pointer/frame/context validation.
    if(pacing_pixels(a)!=s.pixels||!read_buffer(a,true,&after)||before.address!=after.address||
       before.views!=after.views||before.frame!=after.frame||before.count!=after.count||
       before.parms_count!=after.parms_count||before.index!=after.index||
       !read_pre_command_snapshot(&post)||!pacing_command(post,point,GetTickCount64())||
       post.sequence!=pre.sequence||post.gameplay_epoch!=pre.gameplay_epoch||post.native_cg_identity!=pre.native_cg_identity||
       post.native_game_time!=pre.native_game_time||post.context.command_number!=pre.context.command_number||
       post.context.native_frame_time!=pre.context.native_frame_time||!read_ui_context(a,&ui_after)||
       !ui_after.valid||ui_after.connection_state!=10||ui_after.key_catchers||ui_after.page.stack_count||ui_after.page!=ui.page||
       a.menu_revision.load()!=c.menu_revision||a.owner.load()!=c.owner||!pacing_live(a))return failed(4);
    const auto first=a.base+first_buffer_rva;
    if(s.pixels.buffer!=first&&s.pixels.buffer!=first+buffer_stride)return failed(5);
    s.copied=true;*output=s;return true;
}
void begin_producer_pacing(const FrameBeginPoint& point) {
    auto* a=adapter.load();if(!a||!a->pacing_enabled)return;
    LARGE_INTEGER work_begin{},copy_end{},work_end{};
    QueryPerformanceCounter(&work_begin);
    const auto call=a->pacing_calls.fetch_add(1)+1;pace::Sample initial{};
    read_pacing_sample(*a,point,&initial);
    QueryPerformanceCounter(&copy_end);
    const auto result=pace::run(initial,a->pacing_wait,a->pacing_frequency,
        []()->std::int64_t {LARGE_INTEGER t{};return QueryPerformanceCounter(&t)?t.QuadPart:0;},
        [&](pace::Sample* sample){return read_pacing_sample(*a,point,sample);},
        []{SwitchToThread();}); // No metadata lock, native resource or publication lease spans this call.
    QueryPerformanceCounter(&work_end);
    const auto work_ticks=work_begin.QuadPart>0&&work_end.QuadPart>=work_begin.QuadPart?
        work_end.QuadPart-work_begin.QuadPart:0;
    const auto copy_ticks=work_begin.QuadPart>0&&copy_end.QuadPart>=work_begin.QuadPart?
        copy_end.QuadPart-work_begin.QuadPart:0;
    if(result.admitted)++a->pacing_admitted;
    a->pacing_yields.fetch_add(result.yields);
    if(result.reason==pace::Reason::ready)++a->pacing_ready;
    if(result.reason==pace::Reason::timeout||result.reason==pace::Reason::iterations)++a->pacing_timeouts;
    if(result.admitted&&result.reason!=pace::Reason::ready&&result.reason!=pace::Reason::diagnostic&&
       result.reason!=pace::Reason::timeout&&result.reason!=pace::Reason::iterations)++a->pacing_cancelled;
    // Bounded phase evidence; diagnostics cannot authorize a native/XR frame.
    if((call<=16||call%120==0)&&a->pacing_reports.fetch_add(1)<256)
        trace("t5_producer_pacing","call=%llu wait=%u reason=%s copy_stage=%u admitted=%u yields=%u elapsed_qpc=%lld frequency=%lld callback_work_qpc=%lld initial_copy_qpc=%lld excludes_trace_and_wrapper=1 flight_serial=%llu flight_frame=%llu flight_producer=%llu calls_total=%llu admitted_total=%llu yields_total=%llu ready_total=%llu timeout_total=%llu cancelled_total=%llu thread=%lu owner=%lu frontend=%p native_frame=%lu pixels_buffer=%p pixels_native_frame=%lu pixels_complete=%u pixels_hud=%u generation=%llu xr_frame=%llu producer=%llu pending=%u busy=%u claimed=%u staged=%u eyes=%u live_at_copy=%u known_at_copy=%u image_match=%u current_thread_match=%u adapter_current=%u initialized=%u starting=%u cleanup_needed=%u stopped=%u stop_requested=%u device_lost=%u replacement_pending=%u quit_pending=%u caller=%p image=%p base=%p original_calls=unchanged reservations=0 deadline_ms=2 max_yields=2048",
            call,a->pacing_wait,pace::name(result.reason),initial.copy_stage,result.admitted,result.yields,result.elapsed_ticks,a->pacing_frequency,work_ticks,copy_ticks,initial.flight.serial,initial.flight.completed.frame,initial.flight.completed.producer,
            a->pacing_calls.load(),a->pacing_admitted.load(),a->pacing_yields.load(),a->pacing_ready.load(),a->pacing_timeouts.load(),a->pacing_cancelled.load(),
            point.native_thread,a->owner.load(),reinterpret_cast<void*>(initial.context.frontend),initial.context.native_frame,
            reinterpret_cast<void*>(initial.pixels.buffer),initial.pixels.native_frame,initial.pixels.completed,initial.pixels.hud,
            initial.pending.generation,initial.pending.frame,initial.pending.producer,initial.pending.pending,initial.pending.busy,
            initial.pending.claimed,initial.pending.staged,initial.pending.eye_mask,
            initial.context.live,initial.context.known_caller,point.image_base==a->base,
            point.native_thread==GetCurrentThreadId(),adapter.load()==a,a->initialized.load(),a->starting.load(),
            a->cleanup_needed.load(),a->stopped.load(),a->stop_requested.load(),a->device_lost.load(),
            a->replacement_pending.load(),quit_retirement_requested(),reinterpret_cast<void*>(point.caller_return),
            reinterpret_cast<void*>(point.image_base),reinterpret_cast<void*>(a->base));
    // Regardless of result, the wrapper now executes the original native call once.
}

bool read_view(void* refdef, SceneView* output) noexcept {
    SceneView v{};
    __try {
        const auto* p=static_cast<const unsigned char*>(refdef);
        memcpy(&v.x,p,4); memcpy(&v.y,p+4,4);
        memcpy(&v.width,p+8,4); memcpy(&v.height,p+12,4);
        memcpy(&v.tan_half_fov_x,p+0x10,4); memcpy(&v.tan_half_fov_y,p+0x14,4);
        memcpy(&v.origin,p+0x20,sizeof(v.origin));
        memcpy(&v.axis,p+0x34,sizeof(v.axis)); memcpy(&v.near_clip,p+0x5c,4);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    *output=v; return true;
}
bool read_native_scene(Adapter& a,void* refdef,NativeSceneSample* output) noexcept {
    NativeSceneSample s{};s.native_thread=GetCurrentThreadId();
    __try {
        memcpy(&s.cg_identity,reinterpret_cast<void*>(a.base+0x2bf5354),4);
        if (!s.cg_identity || reinterpret_cast<std::uintptr_t>(refdef)!=s.cg_identity+0x8c100) return false;
        s.refdef_identity=reinterpret_cast<std::uintptr_t>(refdef);
        memcpy(&s.native_time,reinterpret_cast<void*>(s.cg_identity+0x8a348),4);
        memcpy(&s.native_frame_time,reinterpret_cast<void*>(s.cg_identity+0x8a344),4);
        memcpy(&s.refdef_time,reinterpret_cast<void*>(s.cg_identity+0x8c158),4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    if (!read_view(refdef,&s.stock_view)) return false;
    s.valid=true;*output=s;return true;
}
bool recopy_native_melee_scene(Adapter& a,const WeaponRenderWitness& witness,void* refdef,
        const Buffer& expected,const SceneView& stock,native_melee_scene::Current* output) noexcept {
    if(!output)return false;*output={};auto& current=*output;Buffer actual{};
    // This caller has taken the one-shot witness at RenderScene, after the
    // native weapon invocation returned. Reuse the existing protected data
    // reader only; it neither calls the game nor selects/rewrites a placement.
    if(!read_pre_command_snapshot(&current.before)||
       !copy_pose_clear_weapon_context(witness.producer.dobj_identity,0,witness.producer.call_serial,&current.native)||
       !read_native_scene(a,refdef,&current.scene)||!same_scene_view(stock,current.scene.stock_view)||
       !read_buffer(a,true,&actual)||actual.address!=expected.address||actual.frame!=expected.frame||
       actual.count||actual.parms_count||!read_pre_command_snapshot(&current.after))return false;
    current.copied=true;return true;
}
void write_view(void* refdef,const SceneView& v) noexcept {
    auto* p=static_cast<unsigned char*>(refdef);
    memcpy(p,&v.x,4); memcpy(p+4,&v.y,4); memcpy(p+8,&v.width,4); memcpy(p+12,&v.height,4);
    memcpy(p+0x10,&v.tan_half_fov_x,4); memcpy(p+0x14,&v.tan_half_fov_y,4);
    memcpy(p+0x20,&v.origin,sizeof(v.origin)); memcpy(p+0x34,&v.axis,sizeof(v.axis));
    memcpy(p+0x5c,&v.near_clip,4);
}
void runtime_log(void*,xr::LogLevel level,const char* message) {
    trace("xr", "level=%u %s",static_cast<unsigned>(level),message?message:"");
}
void stop_on_owner(Adapter& a) {
    if(exact_profile_aim_enabled())invalidate_native_command_aim();
    if(a.bloom_correction_enabled)a.bloom_read_reuse.revoke();
    if (GetCurrentThreadId()!=a.owner.load()) return;
    if(a.history_correction_enabled&&a.history_backend_active.load())return;
    if(a.water_reflection_enabled){
        if(a.water_vpos.busy())return;
        const bool actual_retired=quit_retirement_requested()?a.water_vpos.retire_terminal(a.retained_device):
            a.water_vpos.reconcile_normal(a.retained_device,!a.device_lost.load())&&a.water_vpos.retire();
        if(!actual_retired)return;
        if(a.water_reflections.busy())return;
        const bool retired=quit_retirement_requested()?a.water_reflections.retire_terminal(a.retained_device):
            a.water_reflections.reconcile_normal(a.retained_device,!a.device_lost.load())&&a.water_reflections.retire();
        if(!retired)return;
    }
    if(a.bloom_correction_enabled&&(a.bloom_correction.busy()||
       !a.bloom_correction.reconcile_normal(a.retained_device,!a.device_lost.load())||!a.bloom_correction.retire()))return;
    a.cinematic_census_open={};a.cinematic_movie_gate.invalidate();
    a.hud_pixels.abandon(0,"owner_stop");
    if(a.hud_pass) {
        a.hud_pass->binding.restore();
        // Command vectors stay owned until every field is restored or proven
        // to reference different storage. Never queue quit through dangling data.
        if(a.hud_pass->binding.holds_pointers())return;
        a.hud_pass.reset();
    }
    a.scene_publications.close();
    if (!a.scene_publications.quiescent()) return;
    if(!retire_hand_mesh_draw())return;
    reset_shot_haptics(a.transport.runtime());
    if (a.initialized.load()) {
        if (!a.coordinator.shutdown()) return;
    } else if (a.cleanup_needed.load()) {
        // A failed initialization can still leave partial transport ownership.
        if (!a.transport.shutdown()) return;
    }
    a.initialized=false; a.starting=false; a.cleanup_needed=false; a.stopped=true;a.replacement_pending=false;
    a.pimax_free_glove_grip_basis=false;
    std::uint64_t rejection_count{},cancellation_count{};
    { std::lock_guard lock(a.metadata); a.latest_actions.clear();a.latest_rig.clear(); a.submitted_menu={};a.weapon_scene={};a.ui_epoch.clear();a.menu_frame_epoch.clear();a.cinematic_production.clear();a.menu_event_tick=0;a.hud_publications={};
      rejection_count=a.weapon_scene_rejections;cancellation_count=a.weapon_scene_cancellations; }
    a.eye_staging.release();a.scope_staging.release();
    if(a.bloom_shader_enabled)a.bloom_shaders.retire();
    if(a.history_correction_enabled){a.history_correction.retire(a.auxiliary_history_epoch.load());a.history_correction_capture.retire();}
    if (a.retained_device) { auto* device=a.retained_device; a.retained_device=nullptr; device->Release(); }
    trace("xr_owner_stopped","submitted=%llu discarded=%llu weapon_scene_rejections=%llu weapon_scene_cancellations=%llu",
          a.submitted,a.discarded,rejection_count,cancellation_count);
    // A runtime failure alone must not close the desktop game. Queue quit only
    // when the explicit native quit gate has a matching pending retirement.
    acknowledge_quit_retired_and_queue();
}
struct ReplacementHost {
    Adapter& a;
    bool stop_requested() const {return a.stop_requested.load();}
    bool runtime_faulted() const {return a.coordinator.runtime_faulted();}
    void begin_retirement() {
        if(a.bloom_correction_enabled)a.bloom_read_reuse.revoke();
        if(exact_profile_aim_enabled())invalidate_native_command_aim();
        a.replacement_pending=true;
        a.scene_publications.close();
        reset_shot_haptics(a.transport.runtime());
    }
    bool retire_runtime() {return (!a.water_reflection_enabled||(!a.water_vpos.busy()&&a.water_vpos.reconcile_normal(a.retained_device,!a.device_lost.load())&&a.water_vpos.retire()&&!a.water_reflections.busy()&&a.water_reflections.reconcile_normal(a.retained_device,!a.device_lost.load())&&a.water_reflections.retire()))&&(!a.bloom_correction_enabled||(!a.bloom_correction.busy()&&a.bloom_correction.reconcile_normal(a.retained_device,!a.device_lost.load())&&a.bloom_correction.retire()))&&(!a.history_correction_enabled||!a.history_backend_active.load())&&a.scene_publications.quiescent() && retire_hand_mesh_draw() && a.coordinator.shutdown();}
    void commit_retirement() {
        a.hud_pixels.abandon(0,"device_replacement");
        // Successful coordinator retirement is the only admission to releasing
        // pixel storage and the old device. Keep all serial high-water marks.
        {
            std::lock_guard lock(a.metadata);
            a.produced={};a.current_pixels={};a.latest_actions.clear();a.latest_rig.clear();a.hud_publications={};
            a.submitted_menu={};a.submitted_menu_tick=0;a.menu_panel.clear();
            a.ui_epoch.clear();a.menu_frame_epoch.clear();a.cinematic_production.clear();a.menu_event_tick=0;a.ui_started_revision=0;a.ui_started_epoch=0;++a.menu_revision;
            a.weapon_scene={};a.anchor={};a.anchor_valid=false;a.anchor_generation=0;
        }
        a.eye_staging.release();a.scope_staging.release();
    if(a.bloom_shader_enabled)a.bloom_shaders.retire();
    if(a.history_correction_enabled){a.history_correction.retire(a.auxiliary_history_epoch.load());a.history_correction_capture.retire();}
        auto* retired=a.retained_device;a.retained_device=nullptr;a.retained_creation={};
        if (retired) retired->Release();
        a.attempted=false;a.initialized=false;a.starting=false;a.cleanup_needed=false;
        a.pimax_free_glove_grip_basis=false;
        a.device_lost=false;a.pending_presents=0;++a.device_replacements;
        // replacement_pending stays set until the ordinary initialization below
        // succeeds. A concurrent native quit still requests owner retirement.
    }
};
vr::PresentResult finish_frame_diagnostic(Adapter& a,IDirect3DDevice9* device,bool discard,const char* route) {
    if(!a.reservation_diagnostic_enabled)return discard?a.coordinator.discard_pending():a.coordinator.present(device);
    std::array<DiscardProductionEvidence,3> production{};WeaponRejectionEvidence rejection{};bool handheld_seen{};
    {std::lock_guard lock(a.metadata);handheld_seen=a.diagnostic_handheld_seen;
        if(a.discard_diagnostic_budget.available(true,handheld_seen)){
            const Produced* copied[]{&a.current_pixels,&a.produced[0],&a.produced[1]};
            for(unsigned i=0;i<3;++i){const auto& p=*copied[i];production[i]={p.valid,p.completed,p.menu,p.key,p.stamp,p.native_frame};}
            rejection=a.diagnostic_weapon_rejection;
        }
    }
    vr::SceneReservationDiagnostic d{};
    auto* const capture=a.discard_diagnostic_budget.available(true,handheld_seen)?&d:nullptr;
    const auto result=discard?a.coordinator.discard_pending(capture):a.coordinator.present(device,capture);
    if(!d.pending||!result.transport.frame_ended||result.status==vr::CoordinatorStatus::submitted||result.status==vr::CoordinatorStatus::submitted_quad||
        !a.discard_diagnostic_budget.admit(true,handheld_seen))return result;
    const auto& r=rejection;const auto& f=r.refusal;const auto& q=f.copied;
    trace("t5_frame_discard","route=%s coordinator=%s status=%u transport=%s now_ms=%llu generation=%llu xr_frame=%llu published_ms=%llu render=%u views=%u invalid=%u claimed=%u reserved=%llu staged=%u menu=%u menu_complete=%u eyes=%u native_buffer=0x%llx native_frame=%u native_build=%llu backend_buffer=0x%llx backend_producer=%llu capture=%llu pending_presents=%u handheld_seen=%u startup_reports=%u gameplay_reports=%u production_tuple=valid,completed,menu,generation,xr_frame,buffer,native_frame,producer pixels=%u,%u,%u,%llu,%llu,0x%llx,%u,%llu p0=%u,%u,%u,%llu,%llu,0x%llx,%u,%llu p1=%u,%u,%u,%llu,%llu,0x%llx,%u,%llu rejection_matches=%u rejection_key=%llu,%llu rejection_call=%llu rejection_reason=%s rejection_ownership=%u rejection_buffer=0x%llx rejection_frame=%u refusal_matches=%u refusal_call=%llu refusal_stage=%s refusal_reason=%s refusal_key=%llu,%llu refusal_native_frame=%u refusal_reserved_frame=%u refusal_claimed=%u refusal_staged=%u refusal_eyes=%u initialized=%u busy=%u faulted=%u reset=%u awaiting_tracked=%u admission_changed=0",
        route,d.reason,static_cast<unsigned>(result.status),transport_name(result.transport.status),GetTickCount64(),d.key.generation,d.key.frame_id,d.published_ms,
        d.should_render,d.views_valid,d.invalid,d.claimed,d.reservation_serial,d.staged,d.menu,d.menu_complete,d.eye_mask,
        static_cast<unsigned long long>(d.reserved_native.buffer_identity),d.reserved_native.native_frame,d.reserved_native.build_serial,
        static_cast<unsigned long long>(d.backend_buffer),d.backend_producer,result.transport.capture_serial,a.pending_presents,handheld_seen,a.discard_diagnostic_budget.startup,a.discard_diagnostic_budget.gameplay,
        production[0].valid,production[0].completed,production[0].menu,production[0].key.generation,production[0].key.frame_id,static_cast<unsigned long long>(production[0].stamp.buffer_identity),production[0].frame,production[0].stamp.producer_serial,
        production[1].valid,production[1].completed,production[1].menu,production[1].key.generation,production[1].key.frame_id,static_cast<unsigned long long>(production[1].stamp.buffer_identity),production[1].frame,production[1].stamp.producer_serial,
        production[2].valid,production[2].completed,production[2].menu,production[2].key.generation,production[2].key.frame_id,static_cast<unsigned long long>(production[2].stamp.buffer_identity),production[2].frame,production[2].stamp.producer_serial,
        r.key==d.key,r.key.generation,r.key.frame_id,r.call,r.reason,r.ownership,static_cast<unsigned long long>(r.native.buffer_identity),r.native.native_frame,
        r.call&&r.call==f.call,f.call,f.stage,q.reason,q.key.generation,q.key.frame_id,f.native.native_frame,q.reserved_native.native_frame,q.claimed,q.staged,q.eye_mask,
        d.initialized,d.busy,d.runtime_faulted,d.reset_blocked,d.awaiting_tracked_reservation);
    return result;
}
void before_present(void* context,IDirect3DDevice9* device) {
    auto& a=*static_cast<Adapter*>(context);
    if (a.stopped.load()) {
        // The owner can finish cleanup while the requesting quit callback is
        // still returning. Retry the ACK once it has published its waiting state.
        if (GetCurrentThreadId()==a.owner.load()) acknowledge_quit_retired_and_queue();
        return;
    }
    DWORD owner=0;
    a.owner.compare_exchange_strong(owner,GetCurrentThreadId());
    if (GetCurrentThreadId()!=a.owner.load()) return;
    if(a.history_correction_enabled&&a.history_backend_active.load())return;
    if(a.bloom_correction_enabled) {
        if(a.bloom_correction.busy())return;
        if(!a.bloom_correction.reconcile_normal(a.retained_device,!a.device_lost.load())) {
            vr::FrameTicket pending{};if(a.coordinator.read_pending(&pending)){finish_frame_diagnostic(a,nullptr,true,"bloom_reconcile");++a.discarded;}
            a.eye_staging.invalidate();return;
        }
    }
    // Explicit quit/failure takes priority even before the first initialization.
    if (a.stop_requested.load()) { stop_on_owner(a); return; }
    if (a.attempted && device!=a.retained_device) {
        DeviceCreationInfo creation{};read_device_creation(device,&creation);
        const auto previous_creation=a.retained_creation.serial;
        ReplacementHost host{a};
        const auto result=render::retire_for_device_replacement({a.retained_device,device,a.retained_creation,creation,
            a.owner.load(),GetCurrentThreadId(),a.initialized.load(),a.starting.load(),a.stopped.load()},host);
        const auto attempt=++a.replacement_attempts;
        if (attempt<=6 || attempt%120==0 || result==render::DeviceReplacementResult::retired)
            trace("xr_device_replacement","attempt=%llu result=%u old_creation=%llu new_creation=%llu creation_thread=%lu owner=%lu candidate=%p replacements=%llu",
                  attempt,static_cast<unsigned>(result),previous_creation,creation.serial,creation.thread_id,a.owner.load(),device,a.device_replacements);
        if (result==render::DeviceReplacementResult::runtime_faulted) a.stop_requested=true;
        if (a.stop_requested.load()) {stop_on_owner(a);return;}
        if (result!=render::DeviceReplacementResult::retired) return;
    } else if (a.replacement_pending.load()) return; // Never resume the retiring old session.
    if (!a.attempted) {
        a.attempted=true; a.starting=true;
        IDirect3DDevice9Ex* extended{};
        if (FAILED(device->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(&extended)))) {
            trace("xr_adapter_rejected","reason=not_d3d9ex"); a.stop_requested=true; stop_on_owner(a); return;
        }
        extended->Release();
        xr::RuntimeConfig config{}; config.application_name="Black Ops VR"; config.engine_name="Black Ops VR";
        a.pimax_free_glove_grip_basis=false;
        a.cleanup_needed=true;
        if (!a.coordinator.initialize(config,{nullptr,runtime_log})) {
            trace("xr_adapter_rejected","reason=runtime_initialize_failed"); a.stop_requested=true; stop_on_owner(a); return;
        }
        const char* const runtime_name=a.transport.runtime().active_runtime_name();
        a.pimax_free_glove_grip_basis=pimax_free_glove_runtime(runtime_name?runtime_name:"");
        trace("t5_free_glove_runtime_basis","runtime=%s pimax_grip_fallback=%u scope=free_left_glove_neutral_and_reload action_pose_writes=0",
            runtime_name?runtime_name:"",a.pimax_free_glove_grip_basis.load());
        device->AddRef(); a.retained_device=device;read_device_creation(device,&a.retained_creation);
        a.initialized=true; a.starting=false;a.replacement_pending=false;a.scene_publications.reopen();
        trace("xr_owner_started","thread=%lu device=%p creation=%llu creation_thread=%lu replacements=%llu",
              a.owner.load(),device,a.retained_creation.serial,a.retained_creation.thread_id,a.device_replacements);
    }
    if (a.stop_requested.load()) { stop_on_owner(a); return; }
    if (device!=a.retained_device) {
        trace("xr_owner_rejected","reason=device_mismatch observed_device=%p retained_device=%p owner=%lu caller=%lu",
              device,a.retained_device,a.owner.load(),GetCurrentThreadId()); return;
    }
    vr::FrameTicket pending{};
    bool weapon_pair_rejected=false;
    {std::lock_guard lock(a.metadata);weapon_pair_rejected=a.coordinator.read_pending(&pending)&&
        pending.key==a.weapon_render_rejected_key;}
    if(weapon_pair_rejected){
        a.hud_pixels.abandon(0,"current_weapon_render_rejected");finish_frame_diagnostic(a,nullptr,true,"weapon_rejected");++a.discarded;
        a.eye_staging.invalidate();a.pending_presents=0;
        std::lock_guard lock(a.metadata);
        if(a.current_pixels.key==pending.key)a.current_pixels={};
        for(auto& p:a.produced)if(p.key==pending.key)p={};
        a.weapon_render_rejected_key={};
    }
    // A rejected current gun frame cannot be restaged as a mono/quad fallback.
    if(a.cinematic_enabled&&!weapon_pair_rejected)stage_cinematic_at_present(a,device);
    if(a.bloom_correction_enabled&&a.coordinator.read_pending(&pending)&&pending.key==a.bloom_rejected_key) {
        a.hud_pixels.abandon(0,"bloom_correction_exact_pair_rejected");finish_frame_diagnostic(a,nullptr,true,"bloom_rejected");++a.discarded;
        a.eye_staging.invalidate();a.pending_presents=0;a.bloom_rejected_key={};
        std::lock_guard lock(a.metadata);a.current_pixels={};
    }
    if (a.coordinator.read_pending(&pending)) {
        service_shot_haptics(a.transport.runtime(),&pending);
        if (a.coordinator.expire_scene_reservation(GetTickCount64(),input::kMaximumWeaponPredictionAgeMs))
            trace("t5_weapon_reservation_expired","generation=%llu xr_frame=%llu published_ms=%llu age_limit_ms=%llu explicit_cancel=1 game_writes=0",
                  pending.key.generation,pending.key.frame_id,pending.published_ms,input::kMaximumWeaponPredictionAgeMs);
        const auto reservation_state=a.coordinator.scene_reservation_state();
        if (reservation_state==vr::ReservationState::waiting) return;
        if (reservation_state==vr::ReservationState::invalid) {
            a.hud_pixels.abandon(0,"invalid_reservation_discard");
            finish_frame_diagnostic(a,nullptr,true,"invalid_reservation");++a.discarded;a.pending_presents=0;
            std::lock_guard lock(a.metadata);a.current_pixels={};
        } else {
        Produced pixels{};vr::SceneReservationDiagnostic deadline{};
        { std::lock_guard lock(a.metadata); pixels=a.current_pixels; }
        if (pixels.valid && pixels.completed && pixels.key==pending.key) {
            // Both completed eyes are immutable in our own target. Compose at
            // the authoritative capture seam: later native full-target clears
            // or overlays cannot erase one eye between restoration and capture.
            NativeFinalUi current_ui{};
            const auto menu_now=GetTickCount64();
            const bool menu_current=pixels.menu ?
                ((pixels.cinematic_independent||pixels.menu_revision==a.menu_revision.load()) && read_ui_context(a,&current_ui) &&
                 (pixels.cinematic?current_cinematic_pixels(a,pixels):known_menu(current_ui)) &&
                 (!a.menu_frame_enabled||(pixels.ui_production_epoch&&pixels.ui_production_ms&&menu_now>=pixels.ui_production_ms&&menu_now-pixels.ui_production_ms<=250)) &&
                 current_ui.connection_state==pixels.native_ui.connection_state && current_ui.key_catchers==pixels.native_ui.key_catchers &&
                 current_ui.menu_count==pixels.native_ui.menu_count && current_ui.page==pixels.native_ui.page) :
                (!a.menu_enabled || (read_ui_context(a,&current_ui) && (current_ui.key_catchers&0x10)==0));
            const auto staged=!menu_current ? D3DERR_INVALIDCALL :
                (pixels.menu ? S_OK : a.eye_staging.restore_pair(device,pixels.stamp.producer_serial));
            if (FAILED(staged)) {
                a.hud_pixels.abandon(pixels.stamp.producer_serial,"pre_present_rejected");
                finish_frame_diagnostic(a,nullptr,true,menu_current?"pixel_restore":"pixel_native_context"); ++a.discarded;
                trace("t5_stereo_pixels_rejected","producer=%llu stage=%s menu=%u hr=0x%08lx",
                      pixels.stamp.producer_serial,menu_current?"restore":"native_context_recheck",pixels.menu,static_cast<unsigned long>(staged));
            } else {
            if(!pixels.menu)a.hud_pixels.capture(device,pixels.stamp.producer_serial,"pre_present",true);
            const auto result=finish_frame_diagnostic(a,device,false,"present");
            const bool submitted=result.status==vr::CoordinatorStatus::submitted || result.status==vr::CoordinatorStatus::submitted_quad;
            if (submitted) ++a.submitted; else ++a.discarded;
            {
                std::lock_guard lock(a.metadata);
                a.submitted_menu={};
                if (result.status==vr::CoordinatorStatus::submitted_quad && !pixels.cinematic && pixels.menu_revision==a.menu_revision.load()) {
                    a.submitted_menu={pixels.panel,result.key.generation,++a.menu_submission_serial,pixels.source_width,pixels.source_height,
                                      pixels.native_ui.connection_state,pixels.native_ui.key_catchers,true,pixels.native_ui.page,pixels.ui_production_epoch,pixels.ui_production_ms};
                    a.submitted_menu_tick=GetTickCount64();
                }
            }
            const bool sampled_movie=pixels.cinematic_independent&&a.cinematic_movie_log.admit(pixels.cinematic_movie.current_name,MovieLogStage::present);
            if (a.submitted<=6 || a.submitted%120==0 || !submitted || sampled_movie ||
                (pixels.auxiliary&&(a.auxiliary_staged<=12||a.auxiliary_staged%120==0)) ||
                (pixels.cinematic&&(a.cinematic_presented<=6||a.cinematic_presented%120==0)))
                trace("t5_xr_frame","xr_frame=%llu producer=%llu buffer=0x%lx capture=%llu status=%u transport=%u transport_name=%s eyes=%u submitted=%llu discarded=%llu",
                      result.key.frame_id,result.backend.producer_serial,static_cast<unsigned long>(result.backend.buffer_identity),
                      result.transport.capture_serial,static_cast<unsigned>(result.status),static_cast<unsigned>(result.transport.status),transport_name(result.transport.status),
                      result.transport.released_eye_mask,a.submitted,a.discarded);
            }
            a.pending_presents=0;
        } else if (!pending.frame.should_render || !pending.frame.views_valid ||
                   (++a.pending_presents>4&&!a.coordinator.fresh_unowned_prediction(
                       pending.key,GetTickCount64(),input::kMaximumWeaponPredictionAgeMs,
                       a.reservation_diagnostic_enabled?&deadline:nullptr))) {
            a.hud_pixels.abandon(0,"pending_frame_discard");
            finish_frame_diagnostic(a,nullptr,true,!pending.frame.should_render?"not_rendering":!pending.frame.views_valid?"invalid_views":"pending_timeout"); ++a.discarded; a.pending_presents=0;
        } else {
            // Rapid Presents are not an elapsed-time bound: during a melee
            // transition five can occur while an unowned prediction is only
            // 47ms old. It may use its original 100ms deadline, including when
            // awaiting a tracked reservation. That marker still bars ordinary
            // claims; all owned/incomplete work keeps the existing limit.
            // Never refresh the timestamp or capture unrelated/older pixels.
            if(a.pending_presents==5&&a.reservation_diagnostic_enabled&&a.pending_deadline_reports<32){
                ++a.pending_deadline_reports;
                trace("t5_pending_deadline_wait","report=%u generation=%llu xr_frame=%llu published_ms=%llu maximum_age_ms=%llu pending_presents=%u awaiting_tracked=%u claimed=%u reserved=%llu staged=%u menu=%u eyes=%u timestamp_refreshed=0 ownership_changed=0 pixel_reuse=0",
                    a.pending_deadline_reports,deadline.key.generation,deadline.key.frame_id,deadline.published_ms,input::kMaximumWeaponPredictionAgeMs,
                    a.pending_presents,deadline.awaiting_tracked_reservation,deadline.claimed,deadline.reservation_serial,deadline.staged,deadline.menu,deadline.eye_mask);
            }
            return;
        }
        }
    }
    const auto begun=a.coordinator.begin_frame();
    service_shot_haptics(a.transport.runtime(),begun.status==vr::CoordinatorStatus::started?&begun.ticket:nullptr);
    {
        std::lock_guard lock(a.metadata);
        if (begun.status==vr::CoordinatorStatus::started) {
            a.latest_actions.publish(begun.ticket.frame.actions,begun.ticket.key.generation,begun.ticket.published_ms);
            a.latest_rig.publish(begun.ticket.frame,begun.ticket.key.generation,begun.ticket.published_ms);
        } else {a.latest_actions.clear();a.latest_rig.clear();}
    }
    if (begun.status==vr::CoordinatorStatus::exit_requested || begun.status==vr::CoordinatorStatus::runtime_faulted ||
        begun.status==vr::CoordinatorStatus::failed) {
        a.stop_requested=true; stop_on_owner(a);
    }
}
bool before_reset(void* context,IDirect3DDevice9* device) {
    auto& a=*static_cast<Adapter*>(context);
    if(exact_profile_aim_enabled())invalidate_native_command_aim();
    if(a.bloom_correction_enabled)a.bloom_read_reuse.revoke();
    if(a.auxiliary_history_enabled||a.history_correction_enabled)a.auxiliary_history_epoch.fetch_add(1);
    if(a.bloom_correction_enabled&&a.bloom_correction.busy())return false;
    if(a.history_correction_enabled&&a.history_backend_active.load())return false;
    if(recovery_diagnostics_active()) {
        const auto saved=GetLastError();vr::FrameTicket pending{};
        const bool has_pending=a.coordinator.read_pending(&pending);
        trace("recovery_xr_retirement_entry","thread=%lu owner=%lu initialized=%u replacement_pending=%u scene_quiescent=%u pending=%u generation=%llu xr_frame=%llu published_ms=%llu existing_retirement_policy=1",
              GetCurrentThreadId(),a.owner.load(),a.initialized.load(),a.replacement_pending.load(),a.scene_publications.quiescent(),
              has_pending,pending.key.generation,pending.key.frame_id,pending.published_ms);
        SetLastError(saved);
    }
    trace("xr_reset_request","device=%p caller=%lu owner=%lu initialized=%u already_lost=%u",
          device,GetCurrentThreadId(),a.owner.load(),a.initialized.load(),a.device_lost.load());
    if (a.replacement_pending.load()) return false;
    if(GetCurrentThreadId()==a.owner.load()){a.cinematic_gate.invalidate();a.cinematic_movie_gate.invalidate();a.cinematic_diagnostic_start={};a.cinematic_census_open={};}
    { std::lock_guard lock(a.metadata); a.latest_actions.clear();a.latest_rig.clear(); a.submitted_menu={}; }
    if (!a.initialized.load()) return !a.starting.load() && !a.cleanup_needed.load();
    if (GetCurrentThreadId()!=a.owner.load()) {
        trace("xr_reset_deferred","reason=not_owner_thread"); return false;
    }
    // The observer may deliver a foreign Reset. These fields belong only to the
    // XR owner; even reading them must follow the owner check above. Before XR
    // initialization, the atomic readiness checks still preserve flat Reset.
    if (a.hud_pass || device!=a.retained_device) return false;
    if(a.water_reflection_enabled&&(!a.water_vpos.before_reset(device)||!a.water_reflections.before_reset(device)))return false;
    if(!retire_hand_mesh_draw())return false;
    reset_shot_haptics(a.transport.runtime());
    // Ex resources survive a normal ResetEx. The bridge still must retire and
    // invalidate its own generation before the native reset can run.
    const bool ready=a.coordinator.before_reset(a.device_lost.load());
    if (ready) {
        a.hud_pixels.abandon(0,"device_reset");
        a.eye_staging.release();a.scope_staging.release();
    if(a.bloom_shader_enabled)a.bloom_shaders.retire();
    if(a.history_correction_enabled){a.history_correction.retire(a.auxiliary_history_epoch.load());a.history_correction_capture.retire();}
        std::lock_guard lock(a.metadata); a.produced={}; a.current_pixels={}; a.anchor_valid=false;a.ui_epoch.clear();a.menu_frame_epoch.clear();a.cinematic_production.clear();a.menu_event_tick=0;a.hud_publications={};
    }
    return ready;
}
void after_reset(void* context,IDirect3DDevice9* device,HRESULT result) {
    auto& a=*static_cast<Adapter*>(context);
    if(a.bloom_correction_enabled)a.bloom_read_reuse.revoke();
    a.device_lost=FAILED(result);
    if(a.water_reflection_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device){
        a.water_vpos.after_reset(device,result);a.water_reflections.after_reset(device,result);
    }
    if(a.bloom_correction_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device) {
        // ResetEx preserves device state. A successful native return admits an
        // actual reconciliation; serial advancement alone clears no journal.
        if(result==D3D_OK&&a.bloom_successful_reset_serial!=UINT64_MAX)++a.bloom_successful_reset_serial;
        a.bloom_correction.reconcile_normal(device,result==D3D_OK);
        if(!a.bloom_correction.pending())a.bloom_correction.retire();
    }
    if (a.initialized.load()) {
        const bool recovered=a.coordinator.after_reset(SUCCEEDED(result));
        trace("xr_reset_result","native_hr=0x%08lx recovered=%u",static_cast<unsigned long>(result),recovered);
    }
}
void hud_rejected(Adapter& a,const char* stage,std::uint64_t producer,unsigned detail=0,
                  const render::HudEyeCommands* rejected=nullptr,unsigned width=0,unsigned height=0) {
    if(++a.hud_rejected<=12||a.hud_rejected%120==0)
        trace("t5_hud_composition_rejected","count=%llu producer=%llu stage=%s detail=%u source=%ux%u rejected_record=%u rejected_id=%u rejected_offset=%u rejected_length=%u rejected_flags=0x%x rejected_word4=0x%x rejected_material=0x%x compass_reason=%u compass_next_present=%u compass_next_id=%u compass_next_material=0x%x compass_after_present=%u compass_after_id=%u compass_proof_called=%u compass_proved=%u",
            a.hud_rejected,producer,stage,detail,width,height,rejected&&rejected->rejected_record,
            rejected?rejected->rejected_id:0,rejected?rejected->rejected_offset:0,rejected?rejected->rejected_length:0,
            rejected?rejected->rejected_flags:0,rejected?rejected->rejected_word4:0,rejected?rejected->rejected_material:0,
            rejected?static_cast<unsigned>(rejected->compass_rejection):0,rejected&&rejected->compass_next_present,
            rejected?rejected->compass_next_id:0,rejected?rejected->compass_next_material:0,
            rejected&&rejected->compass_after_present,rejected?rejected->compass_after_id:0,
            rejected&&rejected->compass_proof_called,rejected&&rejected->compass_proved);
}
void invalidate_hud_pixels(Adapter& a,const Produced& pixels,const char* stage,unsigned detail=0) {
    a.hud_pixels.abandon(pixels.stamp.producer_serial,stage);
    a.eye_staging.invalidate();a.pending_presents=5; // Discard, never capture partial HUD, at the next owner Present.
    {std::lock_guard lock(a.metadata);if(a.current_pixels.key==pixels.key&&a.current_pixels.stamp==pixels.stamp)a.current_pixels.completed=false;}
    hud_rejected(a,stage,pixels.stamp.producer_serial,detail);
}
render::HudEyeViewport hud_viewport(const RECT& r) {
    return {r.left,r.top,r.right-r.left,r.bottom-r.top};
}
std::array<render::HudEyeViewport,2> auxiliary_eyes(const Produced& p) noexcept {
    return {hud_viewport(p.eye_rectangles[0]),hud_viewport(p.eye_rectangles[1])};
}
bool production_auxiliary_layout(Adapter& a,bool frontend,const Produced& p,AuxiliaryViewLayout* out) noexcept {
    return a.auxiliary_enabled&&p.regular_slots_copied&&read_auxiliary_layout(a.base,frontend,out)&&
        out->buffer==p.stamp.buffer_identity&&out->frame==p.native_frame&&
        exact_auxiliary_layout(*out,auxiliary_eyes(p),p.regular_slots);
}
bool auxiliary_hud_matches(const NativeHudFrame& native,const AuxiliaryViewLayout& layout) noexcept {
    if(native.buffer!=layout.buffer||native.frame!=layout.frame||native.view_array!=layout.view_array||native.view_count!=3)return false;
    for(unsigned i=0;i<3;++i)if(native.views[i].address!=layout.slots[i].address||native.views[i].scene!=layout.slots[i].scene||
        native.views[i].display!=layout.slots[i].display||native.views[i].missile!=layout.slots[i].extra)return false;
    return true;
}
void publish_native_hud(const HudPassPoint& point) {
    auto* a=adapter.load();
    if(!a||(!a->hud_enabled&&!a->menu_frame_enabled)||point.image_base!=a->base||point.native_thread!=GetCurrentThreadId()||
       !a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||a->device_lost.load())return;
    try {
        auto publication=std::make_shared<PublishedHud>();
        if(!capture_native_hud_publication(a->base,&publication->allocation)) {
            if(a->cinematic_production_enabled){std::lock_guard lock(a->metadata);a->cinematic_production.clear();}
            if(a->menu_frame_enabled){std::lock_guard lock(a->metadata);a->menu_frame_epoch.clear();}
            if(++a->hud_publication_rejections<=6||a->hud_publication_rejections%120==0)
                trace("t5_hud_publication_rejected","count=%llu call=%llu reason=finalized_native_extent",a->hud_publication_rejections,point.call_serial);
            return;
        }
        const auto& native=publication->allocation.native;
        if(a->cinematic_production_enabled)
            publish_cinematic_production(*a,point,std::shared_ptr<const HudCommandPublication>(publication,&publication->allocation));
        if(a->menu_frame_enabled)
            publish_menu_frame(*a,point,std::shared_ptr<const HudCommandPublication>(publication,&publication->allocation));
        if(!a->hud_enabled)return;
        const auto slot=native.buffer==a->base+first_buffer_rva?0u:1u;
        std::lock_guard lock(a->metadata);
        const auto& produced=a->produced[slot];vr::FrameTicket pending{};
        // R_EndFrame and R_RenderScene share this native frontend sequence. A
        // copy only becomes eligible while its exact world ticket still exists.
        if(a->replacement_pending.load()||a->stop_requested.load()||!a->initialized.load()||
           !a->coordinator.read_pending(&pending)||produced.menu||!produced.stamp.producer_serial||
           produced.stamp.buffer_identity!=native.buffer||produced.native_frame!=native.frame||produced.key!=pending.key)return;
        if(native.view_count!=2) {
            if(!production_auxiliary_layout(*a,true,produced,&publication->auxiliary_layout)||
               !auxiliary_hud_matches(native,publication->auxiliary_layout))return;
            publication->auxiliary=true;
        }
        publication->key=produced.key;publication->stamp=produced.stamp;publication->publication_serial=point.call_serial;
        a->hud_publications[slot]=publication;
        if(point.call_serial<=12||point.call_serial%120==0)
            trace("t5_hud_published","publication=%llu generation=%llu xr_frame=%llu producer=%llu buffer=0x%lx native_frame=%lu used=%d capacity=%d views=%lu finalized=1",
                  point.call_serial,publication->key.generation,publication->key.frame_id,publication->stamp.producer_serial,
                  static_cast<unsigned long>(native.buffer),native.frame,native.used,native.capacity,native.view_count);
    } catch(...) {
        if(a->cinematic_production_enabled){std::lock_guard lock(a->metadata);a->cinematic_production.clear();}
        if(a->menu_frame_enabled){std::lock_guard lock(a->metadata);a->menu_frame_epoch.clear();}
        trace("t5_hud_publication_rejected","call=%llu reason=exception",point.call_serial);
    }
}
bool read_hud_material_bytes(std::uint32_t address,void* out,std::size_t size) noexcept {
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool verify_hud_compass(void*,std::uint32_t material) noexcept {
    return is_hud_compass_material(material,read_hud_material_bytes);
}
bool verify_hud_hind(void*,std::uint32_t material) noexcept {
    return is_hud_hind_material(material,read_hud_material_bytes);
}
void begin_native_hud(const HudPassPoint& point) {
    auto* a=adapter.load();
    if(!a||!a->hud_enabled||point.image_base!=a->base||point.native_thread!=GetCurrentThreadId()||point.native_thread!=a->owner.load()||
       !a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||a->device_lost.load()||a->hud_pass)return;
    Produced pixels{};bool restored_world=false;
    try {
        NativeFinalUi ui{};Buffer backend{};vr::FrameTicket ticket{};
        if(!read_ui_context(*a,&ui)||ui.connection_state!=10||ui.key_catchers||!read_buffer(*a,false,&backend)||
           !a->coordinator.read_pending(&ticket))return;
        auto pass=std::make_unique<HudPass>();
        {std::lock_guard lock(a->metadata);pixels=a->current_pixels;
            pass->publication=a->hud_publications[buffer_slot(*a,backend)];}
        if(!pixels.valid||!pixels.completed||pixels.menu||pixels.hud||pixels.key!=ticket.key||pixels.native_frame!=backend.frame||
           pixels.stamp.buffer_identity!=reinterpret_cast<std::uintptr_t>(backend.address))return;
        if(!pass->publication||pass->publication->key!=pixels.key||pass->publication->stamp!=pixels.stamp) {
            hud_rejected(*a,"world_publication_identity",pixels.stamp.producer_serial);return;
        }
        const auto& publication=*pass->publication;const auto& native=publication.allocation.native;
        AuxiliaryViewLayout current_auxiliary{};
        const bool view_layout=pixels.auxiliary?
            (publication.auxiliary&&backend.count==3&&backend.index==2&&
             production_auxiliary_layout(*a,false,pixels,&current_auxiliary)&&current_auxiliary==pixels.auxiliary_layout&&
             current_auxiliary==publication.auxiliary_layout&&auxiliary_hud_matches(native,current_auxiliary)):
            (!publication.auxiliary&&backend.count==2&&backend.index==1&&native.view_count==2);
        if(!view_layout||!native.views[0].commands||native.views[1].commands||native.views[0].missile||native.views[1].missile) {
            hud_rejected(*a,"unsupported_native_views",pixels.stamp.producer_serial);return;
        }
        const auto source=published_hud_list(publication.allocation,0);const auto description=a->eye_staging.last_source_description();
        render::HudVisibleRegion visible{};
        if(!render::common_visible_hud_region(pixels.hud_destinations,&visible)) {
            hud_rejected(*a,"invalid_exact_compositor_destinations",pixels.stamp.producer_serial);return;
        }
        for(unsigned eye=0;eye<2;++eye) {
            const auto viewport=hud_viewport(pixels.eye_rectangles[eye]);
            if(native.views[eye].scene!=viewport||native.views[eye].display!=viewport) {
                hud_rejected(*a,"native_eye_viewport",pixels.stamp.producer_serial,eye);return;
            }
            pass->commands[eye]=render::fit_hud_commands_to_eye(source,description.Width,description.Height,viewport,visible,
                {nullptr,verify_hud_compass,pixels.helicopter_reticle?verify_hud_hind:nullptr});
            if(pass->commands[eye].status!=render::HudEyeStatus::accepted) {
                hud_rejected(*a,"whole_list_preflight",pixels.stamp.producer_serial,static_cast<unsigned>(pass->commands[eye].status),
                    &pass->commands[eye],description.Width,description.Height);return;
            }
        }
        if(!match_native_hud_backend(a->base,publication.allocation,0)) {
            hud_rejected(*a,"native_publication_recheck",pixels.stamp.producer_serial);return;
        }
        pass->call_serial=point.call_serial;pass->pixels=pixels;
        if(a->hud_composed<12||a->hud_composed%120==0)
            trace("t5_hud_visible_region","producer=%llu generation=%llu xr_frame=%llu region=%.6f,%.6f,%.6f,%.6f left_destination=%.6f,%.6f,%.6f,%.6f right_destination=%.6f,%.6f,%.6f,%.6f common_centered=1 helicopter=%u hind_centered=%u,%u",
                  pixels.stamp.producer_serial,pixels.key.generation,pixels.key.frame_id,visible.x,visible.y,visible.width,visible.height,
                  pixels.hud_destinations[0].x,pixels.hud_destinations[0].y,pixels.hud_destinations[0].width,pixels.hud_destinations[0].height,
                  pixels.hud_destinations[1].x,pixels.hud_destinations[1].y,pixels.hud_destinations[1].width,pixels.hud_destinations[1].height,
                  pixels.helicopter_reticle,pass->commands[0].centered_reticles,pass->commands[1].centered_reticles);
        if(a->hud_pixel_enabled) {
            PreCommandSnapshot command{};const auto now=GetTickCount64();
            if(read_pre_command_snapshot(&command)&&command.gameplay_active&&command.native_gameplay_fields_valid&&
               command.native_health>0&&command.context.connection_state==10&&!command.context.key_catcher&&
               now>=command.observed_ms&&now-command.observed_ms<=250) {
                a->hud_pixels.begin(a->retained_device,{pixels.key.generation,pixels.key.frame_id,pixels.stamp.producer_serial,
                    pixels.stamp.buffer_identity,pixels.native_frame},source,pass->commands[0].bytes,pass->commands[1].bytes);
            }
        }
        const HRESULT hr=a->eye_staging.restore_pair(a->retained_device,pixels.stamp.producer_serial);
        restored_world=true; // restore consumes its pair on success or failure.
        if(FAILED(hr)) {invalidate_hud_pixels(*a,pixels,"world_restore",static_cast<unsigned>(hr));return;}
        a->hud_pixels.capture(a->retained_device,pixels.stamp.producer_serial,"world_restored");
        const std::array<std::uintptr_t,2> fields{native.views[0].address+0x2220,native.views[1].address+0x2220};
        const std::array<std::uintptr_t,2> originals{native.views[0].commands,native.views[1].commands};
        const std::array<std::uintptr_t,2> replacements{reinterpret_cast<std::uintptr_t>(pass->commands[0].bytes.data()),reinterpret_cast<std::uintptr_t>(pass->commands[1].bytes.data())};
        const bool bound=pass->binding.apply(fields,originals,replacements);
        // No throwing operation is allowed between substitution and transferring
        // ownership to the process-lifetime adapter, even when rollback failed.
        a->hud_pass=std::move(pass);
        if(!bound) {
            a->hud_pass->retirement_only=true;
            if(!a->hud_pass->binding.holds_pointers())a->hud_pass.reset();
            else a->stop_requested=true;
            invalidate_hud_pixels(*a,pixels,"native_pointer_binding");return;
        }
        // Native original is called once by the assembly wrapper between this
        // callback and complete_native_hud. The vectors live through that call.
    } catch(...) {
        if(restored_world)invalidate_hud_pixels(*a,pixels,"before_exception");
        else hud_rejected(*a,"before_exception",pixels.stamp.producer_serial);
    }
}
void complete_native_hud(const HudPassPoint& point) {
    auto* a=adapter.load();if(!a||point.native_thread!=GetCurrentThreadId()||point.native_thread!=a->owner.load()||!a->hud_pass)return;
    auto pass=std::move(a->hud_pass);
    // Restore borrowed native pointers before all optional logging, context
    // checks and graphics operations, including any path that may allocate.
    const bool restored=pass->binding.restore();
    if(pass->binding.holds_pointers()) {
        // Store the owner before logging or locking can throw. A later owner
        // stop callback retries restoration; it cannot release these vectors.
        a->hud_pass=std::move(pass);a->stop_requested=true;
        invalidate_hud_pixels(*a,a->hud_pass->pixels,"native_pointer_restore_pending");return;
    }
    try {
        Buffer backend{};vr::FrameTicket pending{};
        if(!restored||pass->retirement_only||point.call_serial!=pass->call_serial||point.image_base!=a->base||a->stop_requested.load()||
           !read_buffer(*a,false,&backend)||reinterpret_cast<std::uintptr_t>(backend.address)!=pass->pixels.stamp.buffer_identity||
           backend.frame!=pass->pixels.native_frame||!a->coordinator.read_pending(&pending)||pending.key!=pass->pixels.key||
           !match_native_hud_backend(a->base,pass->publication->allocation,0)) {
            invalidate_hud_pixels(*a,pass->pixels,"native_after_recheck",restored?1u:0u);return;
        }
        if(pass->pixels.auxiliary) {
            AuxiliaryViewLayout current{};
            if(!production_auxiliary_layout(*a,false,pass->pixels,&current)||current!=pass->pixels.auxiliary_layout) {
                invalidate_hud_pixels(*a,pass->pixels,"auxiliary_after_changed");return;
            }
        }
        a->hud_pixels.capture(a->retained_device,pass->pixels.stamp.producer_serial,"hud_completed");
        auto hr=a->eye_staging.begin(a->retained_device,pass->pixels.stamp.producer_serial,pass->pixels.eye_rectangles);
        for(unsigned eye=0;eye<2&&SUCCEEDED(hr);++eye)hr=a->eye_staging.save_eye(a->retained_device,pass->pixels.stamp.producer_serial,eye);
        if(FAILED(hr)){invalidate_hud_pixels(*a,pass->pixels,"composed_pair_capture",static_cast<unsigned>(hr));return;}
        pass->pixels.hud=true;
        {std::lock_guard lock(a->metadata);a->current_pixels=pass->pixels;
            const auto slot=buffer_slot(*a,backend);if(a->hud_publications[slot]==pass->publication)a->hud_publications[slot].reset();}
        if(++a->hud_composed<=12||a->hud_composed%120==0)
            trace("t5_hud_composed","count=%llu publication=%llu generation=%llu xr_frame=%llu producer=%llu native_frame=%lu calls=%llu draws_per_eye=%lu source=%ux%u fit_scale=%.6f offset=%.3f,%.3f eyes=3 native_outer_calls=1 native_hud_lists=2 source_mutations=0 pointers_restored=1",
                  a->hud_composed,pass->publication->publication_serial,pass->pixels.key.generation,pass->pixels.key.frame_id,
                  pass->pixels.stamp.producer_serial,pass->pixels.native_frame,point.call_serial,pass->commands[0].draws,
                  a->eye_staging.last_source_description().Width,a->eye_staging.last_source_description().Height,
                  pass->commands[0].uniform_scale,pass->commands[0].offset_x,pass->commands[0].offset_y);
    } catch(...) {invalidate_hud_pixels(*a,pass->pixels,"after_exception");}
}
}
bool configure_vr_scene_adapter(HMODULE executable,bool scene_hook,bool backend_hook,bool final_ui_hook,bool ui_producer_hook,bool hud_pass_hook,bool frame_begin_hook,bool event_hook) noexcept {
    if (!enabled(L"BOVR_XR_STEREO_PROBE")) return false;
    if (adapter.load()) return true;
    if (!scene_hook || !backend_hook || !enabled(L"BOVR_INSTRUMENT") || !enabled(L"BOVR_D3D9EX")) {
        trace("xr_adapter_rejected","reason=required_hooks_or_explicit_modes_missing"); return false;
    }
    try {
        if(!validate_shadow_native_guards(executable)){
            trace("xr_adapter_rejected","reason=per_eye_shadow_native_guards");return false;
        }
        if(!configure_sun_shadow_lod_hook(executable)){
            trace("xr_adapter_rejected","reason=sun_shadow_lod_native_guards");return false;
        }
        // Optional diagnostic only: failure does not change gameplay admission.
        configure_stereo_lod_fov_scope(executable);
        if(enabled(L"BOVR_BLOOM_CORRECTION")&&!validate_ordinary_bloom_guards(executable)){
            trace("xr_adapter_rejected","reason=ordinary_bloom_rgb_native_guards");return false;
        }
        if (!install_quit_gate(executable)) {
            trace("xr_adapter_rejected","reason=quit_retirement_gate_unavailable"); return false;
        }
        auto* a=new Adapter(reinterpret_cast<std::uintptr_t>(executable));
        a->shadow_pair_diagnostic.configure(a->base);
        a->water_reflection_enabled=configure_vpos_eye_scope(executable);
        a->water_admission_probe=enabled(L"BOVR_WATER_REFLECTION_PROBE");
        if(a->water_reflection_enabled){
            a->water_vpos.configure(a->base);
            a->water_reflections.configure(a->base);
        }
        if(!configure_stereo_dynamic_entities(executable)){delete a;
            trace("xr_adapter_rejected","reason=stereo_dynamic_entities_native_guards");return false;}
        a->bloom_ordinary_correction=enabled(L"BOVR_BLOOM_ORDINARY_CORRECTION");
        a->transport.set_eye_overlay(render_manual_reload_overlay);
        a->transport.set_scope_layout_provider(crossbow_scope_layout);
        a->transport.set_scope_source_provider([](void* context,IDirect3DDevice9* device,const xr::FrameState& frame) noexcept -> IDirect3DSurface9* {
            auto* owner=static_cast<Adapter*>(context);
            std::lock_guard lock(owner->metadata);
            const auto& p=owner->current_pixels;
            if(!owner->initialized.load()||owner->stop_requested.load()||owner->replacement_pending.load()||owner->device_lost.load()||
               !p.valid||!p.completed||p.menu||!p.thermal_captured||p.key.frame_id!=frame.frame_id||
               device!=owner->retained_device||GetCurrentThreadId()!=owner->owner.load())return nullptr;
            if(p.predicted_display_time!=frame.predicted_display_time)return nullptr;
            return owner->scope_staging.get(device,p.stamp.producer_serial);
        },a);
        a->transport.set_quad_reticle_provider(menu_pointer_reticle,a);
        a->menu_enabled=final_ui_hook && enabled(L"BOVR_MENU_QUAD_PROBE");
        a->ui_epoch_enabled=enabled(L"BOVR_UI_PRODUCER_PROBE");
        if (a->ui_epoch_enabled && !ui_producer_hook) a->menu_enabled=false;
        const bool frame_requested=enabled(L"BOVR_MENU_FRAME_EPOCH_PROBE");
        a->menu_frame_enabled=frame_requested&&frame_begin_hook&&hud_pass_hook&&event_hook&&!a->ui_epoch_enabled;
        if(frame_requested&&!a->menu_frame_enabled)a->menu_enabled=false;
        a->cinematic_enabled=a->menu_enabled&&a->menu_frame_enabled&&configure_native_cinematic_observer(executable);
        a->cinematic_panel_enabled=a->cinematic_enabled&&enabled(L"BOVR_CINEMATIC_PANEL_PROBE");
        a->weapon_reservations_enabled=enabled(L"BOVR_WEAPON_RESERVATION_PROBE");
        a->borrowed_weapon_reservations_enabled=weapon_borrowed_probe_enabled()&&enabled(L"BOVR_BORROWED_WEAPON_PROBE")&&!a->weapon_reservations_enabled;
        a->reservation_diagnostic_enabled=enabled(L"BOVR_ZOMBIE_RESERVATION_DIAGNOSTIC");
        {wchar_t value[8]{};GetEnvironmentVariableW(L"BOVR_MOVEMENT_POSE_CORRECTION",value,8);
         a->movement_pose_correction=wcscmp(value,L"0")!=0;}
        a->movement_pose_diagnostic=enabled(L"BOVR_MOVEMENT_POSE_DIAGNOSTIC");
        trace("t5_movement_pose_configured","correction=%u diagnostic=%u camera_receipt=reservation_frozen tracked_pose_writes=0 collision_queries_added=0 diagnostic_cap=2000",
            a->movement_pose_correction,a->movement_pose_diagnostic);
        if(a->reservation_diagnostic_enabled)trace("t5_weapon_reservation_diagnostic_configured","enabled=1 reports_max=64 first=12 interval=120 discard_startup_max=8 discard_gameplay_max=256 discard_modulo_sampling=0 behavior_changed=0 coordinator_snapshot=existing_lock");
        a->hud_enabled=hud_pass_hook&&enabled(L"BOVR_HUD_COMPOSITION_PROBE");
        a->hud_pixel_enabled=a->hud_enabled&&enabled(L"BOVR_HUD_PIXEL_PROBE");
        a->auxiliary_enabled=enabled(L"BOVR_AUXILIARY_STEREO_PROBE")&&validate_auxiliary_native_guards(executable);
        a->auxiliary_postfx_enabled=a->auxiliary_enabled&&enabled(L"BOVR_AUXILIARY_POSTFX_PROBE")&&validate_auxiliary_postfx_guards(executable);
        a->auxiliary_history_enabled=a->auxiliary_postfx_enabled&&enabled(L"BOVR_AUXILIARY_HISTORY_PROBE")&&validate_auxiliary_history_guards(executable);
        a->history_correction_enabled=a->auxiliary_postfx_enabled&&enabled(L"BOVR_AUXILIARY_HISTORY_CORRECTION")&&validate_auxiliary_history_guards(executable)&&validate_history_correction_guards(executable);
        configure_ordinary_exposure_scope(executable);
        if(a->history_correction_enabled)trace("t5_history_correction_configured","cold_seed=blur_off_save_on timer=0 saved_uv=eye_origin native_order=2,0 history_target=1 commit_after_staging=1 new_hooks=0 inactive_fog_fix=0");
        if(a->auxiliary_history_enabled)trace("t5_auxiliary_history_configured","active_cap=2 inactive_cap=1 new_hooks=0 native_target_writes=0 directory_env=BOVR_AUXILIARY_HISTORY_DIRECTORY");
        if(a->auxiliary_postfx_enabled)trace("t5_auxiliary_postfx_configured","attempt_cap=16 interval_ms=250 stages=before2,after2,after0 metadata_only=1 new_hooks=0 target_writes=0 timer_writes=0 copy_history_records=4 native_timer_count_proven=0");
        if(a->auxiliary_enabled)trace("t5_auxiliary_stereo_configured","new_hooks=0 native_count_required=3 selected_required=2 extra_slot=2 native_order=2,0 eye_copy_order=1,0 native_target_writes=0 native_hud_extra_skip_preserved=1");
        // All existing auxiliary byte guards above inspect the pristine loop.
        // Install only after they pass; the bridge is inert outside a current
        // shadow scope and remains installed until this game process exits.
        if(a->auxiliary_enabled&&!install_shadow_inner_dispatch(executable)){
            trace("xr_adapter_rejected","reason=auxiliary_inner_shadow_dispatch");delete a;return false;
        }
        if(a->auxiliary_enabled)trace("t5_shadow_inner_dispatch_configured","callsite=0x6eb7c8 bytes=5 extra_native_unchanged=1 right_journal=inner_view1 native_calls_added=0");
        if (!set_quit_retirement_callback(prepare_quit_for_vr,a)) { delete a; return false; }
        if (!set_graphics_callbacks({a,before_present,before_reset,after_reset,nullptr})) {
            if (set_quit_retirement_callback(nullptr,nullptr)) delete a;
            // Otherwise an in-flight quit still owns this process-lifetime context.
            return false;
        }
        adapter.store(a);
        // Publication has passed every delete/failure path before the draw
        // observer is allowed to retain this process-lifetime context.
        if(a->water_reflection_enabled&&!set_indexed_draw_callbacks({a,before_bloom_draw,after_bloom_draw,abandon_bloom_draw}))a->water_reflection_enabled=false;
        if (a->menu_enabled && !set_final_ui_callbacks(begin_native_ui,complete_native_ui)) a->menu_enabled=false;
        if (a->ui_epoch_enabled && !set_ui_producer_callbacks(begin_ui_production,complete_ui_production)) a->menu_enabled=false;
        if ((a->hud_enabled||a->menu_frame_enabled) && !set_hud_pass_callbacks(publish_native_hud,begin_native_hud,complete_native_hud)) {
            a->hud_enabled=false;if(a->menu_frame_enabled)a->menu_enabled=false;a->menu_frame_enabled=false;
        }
        if(a->menu_frame_enabled&&!set_frame_begin_callback(begin_menu_frame)) {a->menu_frame_enabled=false;a->menu_enabled=false;}
        a->pacing_wait=enabled(L"BOVR_PRODUCER_PACING_WAIT");
        a->pacing_enabled=frame_begin_hook&&(a->pacing_wait||enabled(L"BOVR_PRODUCER_PACING_DIAGNOSTIC"));
        LARGE_INTEGER pacing_frequency{};
        if(a->pacing_enabled&&QueryPerformanceFrequency(&pacing_frequency))a->pacing_frequency=pacing_frequency.QuadPart;
        if(a->pacing_enabled&&!set_frame_begin_pre_callback(begin_producer_pacing))a->pacing_enabled=false;
        if(a->pacing_enabled)trace("t5_producer_pacing_configured","wait=%u default_off=1 pre_native=1 completed_hud_required=1 deadline_ms=2 max_yields=2048 report_cap=256 native_calls_unchanged=1 reservation_changes=0",a->pacing_wait);
        trace("t5_menu_frame_epoch_configured","requested=%u enabled=%u begin_hook=%u finalized_hook=%u events_hook=%u legacy_ui_producer=%u backend_revision_fallback=0",
              frame_requested,a->menu_frame_enabled,frame_begin_hook,hud_pass_hook,event_hook,a->ui_epoch_enabled);
        a->cinematic_enabled=a->cinematic_enabled&&a->menu_enabled&&a->menu_frame_enabled;
        a->cinematic_panel_enabled=a->cinematic_panel_enabled&&a->cinematic_enabled;
        a->cinematic_diagnostic_enabled=a->cinematic_enabled&&enabled(L"BOVR_CINEMATIC_DIAGNOSTICS_PROBE");
        a->cinematic_production_enabled=a->cinematic_diagnostic_enabled&&enabled(L"BOVR_CINEMATIC_PRODUCTION_PROBE");
        a->cinematic_movie_enabled=a->cinematic_production_enabled&&a->cinematic_panel_enabled&&enabled(L"BOVR_CINEMATIC_MOVIE_PROBE")&&validate_cinematic_movie_guards(executable);
        if(a->cinematic_movie_enabled)trace("t5_cinematic_movie_configured","new_hooks=0 native_movie_calls=0 menu_event_authority=0 input_capture_required=0 world_views_required=0 native_clear_guarded=1 capture=post_tail_pre_present");
        if(a->cinematic_production_enabled)trace("t5_cinematic_production_configured","passive=1 new_hooks=0 menu_event_authority=0 rendering_changes=0 sample_interval_ms=500 max_reports=64 max_raw_bytes=8192");
        if(a->cinematic_diagnostic_enabled)trace("t5_cinematic_diagnostics_configured","passive=1 interval_ms=100 max_reports=512 admission_changed=0 native_calls_added=0 readback=0");
        trace("t5_cinematic_configured","observer=%u panel=%u native_world_views_required=0 positive_full_canvas_material_required=1 capture=authoritative_pre_present input=noninteractive new_hooks=0",
              a->cinematic_enabled,a->cinematic_panel_enabled);
        if(enabled(L"BOVR_BLOOM_SHADER_PROBE")) {
            a->bloom_shader_enabled=a->history_correction_enabled&&a->auxiliary_postfx_enabled&&a->auxiliary_enabled&&
                a->menu_enabled&&a->menu_frame_enabled&&a->hud_enabled&&validate_bloom_shader_guards(executable)&&validate_bloom_episode_guards(executable);
            if(a->bloom_shader_enabled) {
                wchar_t directory[32768]{};const auto count=GetEnvironmentVariableW(L"BOVR_BLOOM_SHADER_OUTPUT_DIR",directory,32768);
                if(count&&count<32768){configure_bloom_episode_probe(a->bloom_shaders,true,directory);a->bloom_shader_enabled=a->bloom_shaders.configured();}
                else a->bloom_shader_enabled=false;
                if(a->bloom_shader_enabled&&!set_indexed_draw_callbacks({a,before_bloom_draw,after_bloom_draw,abandon_bloom_draw}))a->bloom_shader_enabled=false;
            }
            trace("t5_bloom_shader_registration","enabled=%u additional_native_hooks=0 device_draw_slot=82 max_attempts=2 process_bytes=4194304 capture_schema=2 episode_plan=1 full_ldr_required=1",a->bloom_shader_enabled);
        }
        if(enabled(L"BOVR_BLOOM_CORRECTION")) {
            a->bloom_correction_enabled=a->history_correction_enabled&&a->auxiliary_postfx_enabled&&a->auxiliary_enabled&&
                a->menu_enabled&&a->menu_frame_enabled&&a->hud_enabled&&validate_bloom_shader_guards(executable)&&
                validate_bloom_episode_guards(executable)&&validate_bloom_quad_guards(executable);
            a->bloom_correction.configure(a->bloom_correction_enabled);
            a->bloom_correction_enabled=a->bloom_correction.configured();
            if(a->bloom_correction_enabled&&!a->bloom_read_reuse.prepare())a->bloom_correction_enabled=false;
            if(a->bloom_correction_enabled&&!set_indexed_draw_callbacks({a,before_bloom_draw,after_bloom_draw,abandon_bloom_draw}))a->bloom_correction_enabled=false;
            trace("t5_bloom_correction_registration","enabled=%u default_off=1 native_draw_unchanged=1 own_draw=UP passive_optional=%u guard_contract=C12_quad_full_ldr",a->bloom_correction_enabled,a->bloom_shader_enabled);
        }
        trace("t5_ordinary_bloom_policy","enabled=%u full_replay_optin=%u rgb_journal=%u ordinary_count=2 native_order=0,1 rgb_offsets=3638,3648 bytes_per_eye=24 alpha_film_streak_unchanged=1 new_hooks=0",
            a->bloom_correction_enabled,a->bloom_ordinary_correction,a->bloom_correction_enabled&&!a->bloom_ordinary_correction);
        trace("xr_adapter_configured","runtime_start=first_authoritative_present stereo_probe=1"); return true;
    } catch (...) { return false; }
}
// Caller owns metadata. Reuse the existing command-owner collision approval,
// retaining all current identity/freshness/near-plane checks.
static void scene_camera_locked(Adapter& a,const SceneView& stock,const xr::FrameState& raw,
    const xr::Posef& anchor,std::uintptr_t refdef,std::uint64_t generation,xr::FrameState* camera) noexcept {
    *camera=raw;PreCommandSnapshot pre{};
    const bool ordinary=read_pre_command_snapshot(&pre)&&pre.native_gameplay_fields_valid&&
        pre.native_pm_type==0&&pre.native_health>0&&!pre.context.key_catcher&&!pre.context.native_override&&
        !(pre.native_pm_flags&(0xc00u|4u|8u|0x20000u))&&
        !(pre.native_entity_flags&0x4300u)&&!(pre.native_entity_flags2&0x50040000u);
    const auto& approved=a.room_scale;const auto now=GetTickCount64();
    if(ordinary&&approved.published_ms&&approved.generation==generation&&
       approved.view.epoch==pre.gameplay_epoch&&approved.view.cg==pre.native_cg_identity&&
       approved.view.cg+0x8c100==refdef&&!memcmp(&approved.anchor,&anchor,sizeof(anchor))) {
        const bool fresh=now>=approved.published_ms&&now-approved.published_ms<=100&&
            input::room_scale::camera_clearance_current(approved.view.camera_radius,raw,stock.near_clip);
        input::room_scale::apply_approved_eye(stock.origin,stock.axis,anchor,
            fresh?approved.view.eye:stock.origin,!fresh||approved.view.vertical_clipped,camera);
    }
}
bool render_stereo_scene(void* refdef,int frame_time,NativeRenderScene original) noexcept {
    const auto weapon_render_witness=take_weapon_render_witness();
    auto* a=adapter.load();
    if (!a || !a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load())) return false;
    render::DevicePublicationLease publication(a->scene_publications);
    if (!publication.admitted) return false;
    NativeFinalUi native_ui{};
    if (a->menu_enabled && (!read_ui_context(*a,&native_ui) || (native_ui.key_catchers&0x10)!=0)) return false;
    bool native_started=false;
    try {
    Buffer before{}; SceneView stock{};
    if (!read_buffer(*a,true,&before) || before.count || before.parms_count || !read_view(refdef,&stock)) return false;
    NativeSceneSample render_scene{};read_native_scene(*a,refdef,&render_scene);
    {
        std::lock_guard lock(a->metadata);
        if(a->reservation_diagnostic_enabled&&weapon_render_witness.expected)a->diagnostic_handheld_seen=true;
        const auto status=assess_weapon_render(weapon_render_witness,render_scene,a->weapon_scene,frame_time);
        if(!weapon_render_admitted(status)){
            vr::FrameTicket pending{};
            if(a->coordinator.read_pending(&pending)){
                const std::array<WeaponRenderProductionIdentity,3> bindings{{
                    {a->produced[0].valid,a->produced[0].key,a->produced[0].stamp,a->produced[0].native_frame},
                    {a->produced[1].valid,a->produced[1].key,a->produced[1].stamp,a->produced[1].native_frame},
                    {a->current_pixels.valid,a->current_pixels.key,a->current_pixels.stamp,a->current_pixels.native_frame}}};
                const auto ownership=classify_weapon_render_rejection(pending.key,
                    reinterpret_cast<std::uintptr_t>(before.address),before.frame,bindings);
                // Keep the earlier owned pair, or a pristine newer prediction
                // that this failed native invocation never reserved. The latter
                // can only be claimed by a new independently proved reservation.
                const auto rejection_now=ownership==WeaponRenderRejectionOwnership::reject_unbound&&status==WeaponRenderAdmission::no_transaction?GetTickCount64():0;
                const auto action=resolve_weapon_render_rejection(a->coordinator,status,ownership,pending.key,rejection_now,input::kMaximumWeaponPredictionAgeMs);
                if(action==WeaponRenderRejectionAction::preserve_unowned){
                    // Keep first-call provenance for this exact unowned key.
                    // It is not permission for this rejected scene to render.
                    PreCommandSnapshot pre{};read_pre_command_snapshot(&pre);
                    const bool new_provenance=!a->native_melee_deferred.active||a->native_melee_deferred.key!=pending.key;
                    const bool retained=native_melee_scene::retain_first(a->native_melee_deferred,pending,
                        {reinterpret_cast<std::uintptr_t>(before.address),before.frame,0},weapon_render_witness,status,pre,rejection_now);
                    if(new_provenance&&a->reservation_diagnostic_enabled&&a->native_melee_deferred_reports<32){
                        ++a->native_melee_deferred_reports;
                        trace("t5_native_melee_deferred","report=%u retained=%u generation=%llu xr_frame=%llu published_ms=%llu call=%llu native_frame=%u native_time=%d command=%d pre_command=%d pre_epoch=%llu pre_sequence=%llu pre_age=%llu native_thread=%lu pre_thread=%lu cg=0x%lx pre_cg=0x%lx timestamp_refreshed=0 rejected_scene_admitted=0",
                            a->native_melee_deferred_reports,retained,pending.key.generation,pending.key.frame_id,pending.published_ms,
                            weapon_render_witness.producer.call_serial,before.frame,weapon_render_witness.producer.native_time,
                            weapon_render_witness.producer.command_number,pre.context.command_number,pre.gameplay_epoch,pre.sequence,
                            rejection_now>=pre.observed_ms?rejection_now-pre.observed_ms:UINT64_MAX,
                            weapon_render_witness.producer.native_thread,pre.context.native_thread,
                            static_cast<unsigned long>(weapon_render_witness.producer.cg_identity),static_cast<unsigned long>(pre.native_cg_identity));
                    }
                    ++a->weapon_render_deferrals;
                    if(a->reservation_diagnostic_enabled&&a->weapon_render_deferral_reports<64&&
                        (a->weapon_render_deferrals<=12||a->weapon_render_deferrals%120==0)){
                        ++a->weapon_render_deferral_reports;
                        trace("t5_weapon_render_deferred","count=%llu generation=%llu xr_frame=%llu published_ms=%llu now_ms=%llu age_ms=%llu native_buffer=0x%lx native_frame=%u call=%llu reason=no_transaction awaiting_tracked_reservation=1 timestamp_refreshed=0 pending_counter_reset=0 stock_scene_rejected=1 native_scene_once=1",
                            a->weapon_render_deferrals,pending.key.generation,pending.key.frame_id,pending.published_ms,rejection_now,rejection_now-pending.published_ms,
                            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(before.address)),before.frame,weapon_render_witness.producer.call_serial);
                    }
                }
                if(action==WeaponRenderRejectionAction::invalidate){
                    if(a->reservation_diagnostic_enabled)a->diagnostic_weapon_rejection={pending.key,
                        {reinterpret_cast<std::uintptr_t>(before.address),before.frame,0},weapon_render_witness.producer.call_serial,
                        weapon_render_admission_name(status),static_cast<unsigned>(ownership),a->diagnostic_last_refusal};
                    a->weapon_render_rejected_key=pending.key;
                    if(a->current_pixels.key==pending.key)a->current_pixels={};
                    for(auto& p:a->produced)if(p.key==pending.key)p={};
                    if(a->weapon_scene.active&&a->weapon_scene.reservation.ticket.key==pending.key){
                        a->coordinator.cancel_scene_reservation(pending.key,a->weapon_scene.reservation.serial);
                        a->weapon_scene.active=false;
                    }
                }
            }
            if(++a->weapon_render_rejections<=12||a->weapon_render_rejections%120==0)
                trace("t5_weapon_render_rejected","count=%llu reason=%s weapon=%u call=%llu observed=%u expected=%u substituted=%u stock_projection=0 native_scene_once=1",
                    a->weapon_render_rejections,weapon_render_admission_name(status),weapon_render_witness.producer.weapon_index,
                    weapon_render_witness.producer.call_serial,weapon_render_witness.observed,weapon_render_witness.expected,weapon_render_witness.substituted);
            return false;
        }
    }
    input::profile_admission::Token scene_admission{};bool scene_admission_current=true;
    if(exact_profile_aim_enabled()){
        {std::lock_guard lock(a->metadata);if(a->weapon_scene.active&&a->weapon_scene.borrowed.active)scene_admission=a->weapon_scene.borrowed.claim.command_admission;}
        if(scene_admission.serial)scene_admission_current=command_profile_admission_current(scene_admission);
    }
    vr::FrameTicket ticket{};
    xr::Posef anchor{};
    xr::Posef frozen_camera_head{};bool camera_frozen=false;
    bool reserved=false;
    {
        std::lock_guard lock(a->metadata);
        if (a->weapon_scene.active) {
            auto& transaction=a->weapon_scene;
            // Backend Present can be asleep while this native producer runs.
            // Enforce the original publication age here as well as at Present.
            a->coordinator.expire_scene_reservation(GetTickCount64(),input::kMaximumWeaponPredictionAgeMs);
            NativeSceneSample scene{};
            const vr::NativeBuildIdentity native{reinterpret_cast<std::uintptr_t>(before.address),before.frame,
                                                transaction.reservation.native.build_serial};
            WeaponLifetime lifetime{};
            const bool model_current=!transaction.producer.model_lifetime_verified ||
                (read_weapon_lifetime(transaction.producer.viewmodel_identity,transaction.producer.dobj_identity,&lifetime)&&
                 lifetime.verified&&lifetime.generation==transaction.producer.model_generation);
            const bool matched=read_native_scene(*a,refdef,&scene)&&matches_weapon_scene(transaction.producer,scene)&&
                scene.native_frame_time==frame_time&&a->anchor_valid&&a->anchor_generation==transaction.reservation.ticket.key.generation&&model_current;
            const bool anchor_current=a->anchor_valid&&a->anchor_generation==transaction.reservation.ticket.key.generation;
            bool borrowed_current=false;
            if(transaction.borrowed.active){PreCommandSnapshot command{};const auto scope=read_weapon_native_clear_scope();
                borrowed_current=read_pre_command_snapshot(&command)&&primary_weapon_active(command)&&
                    borrowed_scene_completed(transaction.borrowed,GetTickCount64(),pose_clear_serial(),weapon_native_clear_last_token(),
                        command.gameplay_epoch,command.sequence,scope.depth==0)&&
                    (!exact_profile_aim_enabled()||(scene_admission_current&&scene_admission==transaction.borrowed.claim.command_admission&&
                        command_profile_admission_witness(scene_admission)));
                if(transaction.reservation.serial<=12||transaction.reservation.serial%120==0)
                    trace("t5_borrowed_weapon_scene","reservation=%llu token=%llu camera_serial=%llu normal_serial=%llu completion=%u current=%u original_published_ms=%llu predicted_time=%lld action_sequence=%llu scratch_selected=%u native_world_verified=%u shot_writes=0",
                        transaction.reservation.serial,transaction.borrowed.claim.wrapper_token,transaction.borrowed.claim.camera_serial,transaction.borrowed.normal_serial,
                        transaction.borrowed.completed,borrowed_current,transaction.reservation.ticket.published_ms,
                        transaction.reservation.ticket.frame.predicted_display_time,transaction.reservation.ticket.frame.actions.sequence,transaction.borrowed.selected,transaction.borrowed.world_verified);
            }
            if (claim_weapon_scene_prediction(transaction,a->coordinator,native,scene,frame_time,anchor_current&&model_current,&ticket,borrowed_current)!=WeaponSceneClaim::reserved) {
                if (++a->weapon_scene_rejections<=12 || a->weapon_scene_rejections%120==0)
                    trace("t5_weapon_scene_rejected","count=%llu reservation=%llu build=%llu generation=%llu xr_frame=%llu body_match=%u model_current=%u state=%u buffer=%p expected_buffer=0x%lx native_frame=%lu expected_frame=%lu native_time=%d,%d frame_time=%d,%d,%d refdef=0x%lx,0x%lx origin=%.4f,%.4f,%.4f expected_origin=%.4f,%.4f,%.4f game_writes=0",
                          a->weapon_scene_rejections,transaction.reservation.serial,native.build_serial,transaction.reservation.ticket.key.generation,
                          transaction.reservation.ticket.key.frame_id,matched,model_current,static_cast<unsigned>(a->coordinator.scene_reservation_state()),
                          before.address,static_cast<unsigned long>(transaction.reservation.native.buffer_identity),before.frame,transaction.reservation.native.native_frame,
                          transaction.producer.native_time,scene.native_time,transaction.producer.native_frame_time,scene.native_frame_time,frame_time,
                          static_cast<unsigned long>(transaction.producer.refdef_identity),static_cast<unsigned long>(scene.refdef_identity),
                          scene.stock_view.origin.x,scene.stock_view.origin.y,scene.stock_view.origin.z,
                          transaction.producer.stock_view.origin.x,transaction.producer.stock_view.origin.y,transaction.producer.stock_view.origin.z);
                return false;
            }
            anchor=transaction.anchor;reserved=true;
            frozen_camera_head=transaction.camera_head;camera_frozen=transaction.camera_frozen;
            if (transaction.reservation.serial<=12 || transaction.reservation.serial%120==0)
                trace("t5_weapon_scene_consumed","reservation=%llu build=%llu generation=%llu xr_frame=%llu predicted_time=%lld action_sequence=%llu buffer=%p native_frame=%lu native_thread=%lu body_match=1 game_writes=0",
                      transaction.reservation.serial,native.build_serial,ticket.key.generation,ticket.key.frame_id,
                      ticket.frame.predicted_display_time,ticket.frame.actions.sequence,before.address,before.frame,GetCurrentThreadId());
        } else {
            // A prior failed handheld invocation can leave this prediction
            // tracked-only. A NEW current native melee scene may reserve it,
            // but must never clear that restriction and take a generic claim.
            vr::FrameTicket pending{};vr::SceneReservationDiagnostic state{};
            const auto& w=weapon_render_witness;const auto now=GetTickCount64();
            const bool native_melee=w.observed&&!w.expected&&!w.nested&&
                w.producer.weapon_state>=18&&w.producer.weapon_state<=20;
            const bool deferred_current=native_melee&&a->coordinator.read_pending(&pending)&&
                a->coordinator.fresh_unowned_prediction(pending.key,now,input::kMaximumWeaponPredictionAgeMs,&state)&&
                state.awaiting_tracked_reservation;
            if(deferred_current){
                const auto bound=[&](const Produced& p){return p.key==pending.key&&
                    (p.valid||p.completed||p.stamp.buffer_identity||p.stamp.producer_serial);};
                native_melee_scene::Current current{};vr::SceneReservation reservation{};
                vr::NativeBuildIdentity native{};bool claimed=false;const char* stage="scope";
                if(a->borrowed_weapon_reservations_enabled&&exact_profile_aim_enabled()&&
                   !a->stop_requested.load()&&!a->replacement_pending.load()&&!a->device_lost.load()&&
                   w.producer.native_thread==GetCurrentThreadId()&&a->native_build_serial!=UINT64_MAX&&
                   !bound(a->produced[0])&&!bound(a->produced[1])&&!bound(a->current_pixels)){
                    native={reinterpret_cast<std::uintptr_t>(before.address),before.frame,++a->native_build_serial};
                    stage="current_copy";
                    if(recopy_native_melee_scene(*a,w,refdef,before,stock,&current)){
                        stage="policy";
                        if(native_melee_scene::admit(a->native_melee_deferred,pending,native,w,current,frame_time,GetTickCount64())){
                            stage="atomic_reserve";
                            if(a->coordinator.reserve_deferred_scene_frame(pending.key,pending.published_ms,native,
                                    GetTickCount64(),input::kMaximumWeaponPredictionAgeMs,&reservation)){
                                native_melee_scene::Current again{};stage="post_reserve_copy";
                                if(recopy_native_melee_scene(*a,w,refdef,before,stock,&again)&&
                                   native_melee_scene::same_pre(current.after,again.before)&&
                                   native_melee_scene::admit(a->native_melee_deferred,reservation.ticket,native,w,again,frame_time,GetTickCount64())&&
                                   !a->stop_requested.load()&&!a->replacement_pending.load()&&!a->device_lost.load()){
                                    stage="consume";
                                    claimed=a->coordinator.consume_scene_reservation(reservation,native,&ticket);
                                }
                                if(!claimed)a->coordinator.cancel_scene_reservation(reservation.ticket.key,reservation.serial);
                            }
                        }
                    }
                }
                if(a->reservation_diagnostic_enabled&&a->native_melee_scene_reports<64&&
                   (claimed||a->native_melee_diagnostic_key!=pending.key)){
                    ++a->native_melee_scene_reports;a->native_melee_diagnostic_key=pending.key;
                    const auto& old=a->native_melee_deferred;
                    trace("t5_native_melee_scene","report=%u claimed=%u stage=%s generation=%llu xr_frame=%llu published_ms=%llu age_ms=%llu reservation=%llu build=%llu native_frame=%u prior_frame=%u call=%llu prior_call=%llu state=%d other=%d copied=%u retained=%u command=%d pre_command=%d epoch=%llu prior_epoch=%llu native_time=%d prior_time=%d native_thread=%lu pre_thread=%lu original_timestamp=1 generic_claim=0 weapon_placement_writes=0 shot_writes=0 pixel_reuse=0",
                        a->native_melee_scene_reports,claimed,stage,pending.key.generation,pending.key.frame_id,pending.published_ms,
                        GetTickCount64()-pending.published_ms,reservation.serial,native.build_serial,before.frame,old.failed_native.native_frame,
                        w.producer.call_serial,old.failed_producer.call_serial,w.producer.weapon_state,w.producer.other_weapon_state,
                        current.copied,old.active,w.producer.command_number,current.before.context.command_number,
                        current.before.gameplay_epoch,old.gameplay_epoch,w.producer.native_time,old.failed_producer.native_time,
                        w.producer.native_thread,current.before.context.native_thread);
                }
                if(!claimed)return false;
                a->native_melee_deferred={};
                // Use the current ordinary camera anchor below. Ownership is
                // already consumed, while the stock native melee stays intact.
            }else if(!a->coordinator.claim_render_frame(&ticket))return false;
        }
        if (!a->anchor_valid || a->anchor_generation!=ticket.key.generation) {
            xr::Posef leveled{};
            if (!level_tracking_anchor(ticket.frame.head_center,&leveled)) {
                a->coordinator.reject_render_frame(ticket.key);
                return false;
            }
            a->anchor=leveled; a->anchor_valid=true; a->anchor_generation=ticket.key.generation;
            trace("t5_tracking_anchor","generation=%llu xr_frame=%llu mode=level_yaw_only position=%.5f,%.5f,%.5f pitch_roll_persisted=0 near_vertical_guard=1",
                ticket.key.generation,ticket.key.frame_id,a->anchor.position.x,a->anchor.position.y,a->anchor.position.z);
        }
        if (!reserved) anchor=a->anchor;
    }
    StereoViews views{};
    auto camera_frame=ticket.frame;
    if(reserved&&camera_frozen)camera_frame.head_center=frozen_camera_head;
    else {std::lock_guard lock(a->metadata);scene_camera_locked(*a,stock,ticket.frame,anchor,
        reinterpret_cast<std::uintptr_t>(refdef),ticket.key.generation,&camera_frame);}
    if (!build_stereo_views(stock,camera_frame,anchor,&views)) return false;
    Produced production{};
    {
        std::lock_guard lock(a->metadata);
        production={true,false,ticket.key,{reinterpret_cast<std::uintptr_t>(before.address),++a->producer_serial},before.frame};
        production.predicted_display_time=ticket.frame.predicted_display_time;
        for (unsigned eye=0;eye<2;++eye) {
            const auto& v=views.eyes[eye];
            production.eye_rectangles[eye]={v.x,v.y,v.x+v.width,v.y+v.height};
            production.water_eye_origins[eye]={v.origin.x,v.origin.y,v.origin.z};
            const auto& destination=views.compositor_layout.destinations[eye];
            production.hud_destinations[eye]={destination.x,destination.y,destination.width,destination.height};
        }
        a->produced[buffer_slot(*a,before)]=production;
    }
    if (!a->coordinator.stage_render_frame(ticket.key,production.stamp,views.compositor_layout)) return false;
    // Only independently mapped camera/viewport fields are changed. Restore
    // the completed stock refdef before returning to client/game simulation.
    struct Restore { void* refdef; SceneView stock; ~Restore(){write_view(refdef,stock);} } restore{refdef,stock};
    struct ExposureOwner {Adapter* adapter;const Produced* production;};
    ExposureOwner exposure_owner{a,&production};OrdinaryExposureScope exposure;
    const bool exposure_begin=exposure.begin(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,
        reinterpret_cast<std::uintptr_t>(refdef),production.stamp.producer_serial,
        {&exposure_owner,[](void* p) noexcept {
            try{const auto& h=*static_cast<ExposureOwner*>(p);auto& owner=*h.adapter;
                if(!owner.initialized.load()||owner.stop_requested.load()||owner.replacement_pending.load())return false;
                std::lock_guard lock(owner.metadata);
                for(const auto& current:owner.produced)
                    if(current.valid&&current.key==h.production->key&&current.stamp==h.production->stamp&&
                       current.native_frame==h.production->native_frame)return true;
            }catch(...){}return false;}});
    StereoDynamicEntitiesScope dynamic_entities;
    const bool dynamic_begin=dynamic_entities.begin(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,production.stamp.producer_serial);
    auto shadows=std::make_shared<EyeShadowPair>();bool shadows_built=dynamic_begin,lod_pair_safe=true;
    auto shadow_diagnostic=a->shadow_pair_diagnostic.begin(reinterpret_cast<std::uintptr_t>(before.address),before.frame,production.stamp.producer_serial);
    for (unsigned eye=0;eye<2;++eye) {
        if(eye==1&&(!dynamic_begin||!dynamic_entities.prepare_right())){shadows_built=false;break;}
        if(eye==1&&exposure_begin&&!exposure.prepare_right()){shadows_built=false;break;}
        write_view(refdef,views.eyes[eye]); native_started=true;
        auto render_eye_body=[&]{
        if(eye==1){
            SunShadowLodScope lod_scope;
            lod_scope.begin(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,reinterpret_cast<std::uintptr_t>(refdef),production.stamp.producer_serial);
            struct Call {NativeRenderScene original;void* refdef;int frame_time;};
            Call call{original,refdef,frame_time};
            invoke_sun_shadow_lod_native(&lod_scope,[](void* context){auto* c=static_cast<Call*>(context);c->original(c->refdef,c->frame_time);},&call);
            lod_pair_safe=lod_pair_safe&&lod_scope.safe();
        }else original(refdef,frame_time);
        };
        bool foliage_safe=true;
        auto render_eye=[&]{
            if(!stereo_lod_fov_scope_enabled()){render_eye_body();return;}
            StereoLodFovScope foliage;
            const bool foliage_began=foliage.begin(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,
                reinterpret_cast<std::uintptr_t>(refdef),production.stamp.producer_serial,eye,
                {&exposure_owner,[](void* p) noexcept {
                    try{const auto& h=*static_cast<ExposureOwner*>(p);auto& owner=*h.adapter;
                        if(!owner.initialized.load()||owner.stop_requested.load()||owner.replacement_pending.load())return false;
                        std::lock_guard lock(owner.metadata);
                        for(const auto& current:owner.produced)
                            if(current.valid&&current.key==h.production->key&&current.stamp==h.production->stamp&&
                               current.native_frame==h.production->native_frame)return true;
                    }catch(...){}return false;}});
            if(!foliage_began&&!foliage.safe()){foliage_safe=false;return;}
            using Body=decltype(render_eye_body);
            invoke_stereo_lod_fov_native(&foliage,[](void* p){(*static_cast<Body*>(p))();},&render_eye_body);
            foliage_safe=foliage.safe();
        };
        using ExposureBody=decltype(render_eye);
        invoke_ordinary_exposure_native(exposure_begin?&exposure:nullptr,eye,[](void* p){(*static_cast<ExposureBody*>(p))();},&render_eye);
        if(!foliage_safe){a->stop_requested=true;shadows_built=false;break;}
        const bool copied=capture_eye_shadow(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,eye,&shadows->eyes[eye]);
        if(copied)a->shadow_pair_diagnostic.capture(shadow_diagnostic.get(),shadows->eyes[eye]);
        shadows_built=shadows_built&&copied&&lod_pair_safe;
        const auto count=copied?++a->shadow_captures:++a->shadow_rejections;
        if(count<=12||count%120==0)trace("t5_shadow_captured","count=%llu producer=%llu buffer=%p frame=%u eye=%u copied=%u sun_index=%u sun_mask=0x%x spots=%u lights=%u bytes_sun=1104 bytes_technique=16384 bytes_spots=1856",
            count,production.stamp.producer_serial,before.address,before.frame,eye,copied,shadows->eyes[eye].sun_index,
            shadows->eyes[eye].state.mask[0],shadows->eyes[eye].state.spot_count,shadows->eyes[eye].state.light_count);
    }
    const bool exposure_complete=!exposure_begin||exposure.finish();
    if(!exposure_complete)a->stop_requested=true;
    const bool dynamic_complete=dynamic_begin&&dynamic_entities.finish();
    shadows_built=shadows_built&&dynamic_complete&&exposure_complete;
    const auto dynamic_rejections=dynamic_complete?0:++a->dynamic_entity_rejections;
    if(production.stamp.producer_serial<=6||production.stamp.producer_serial%120==0||
       (dynamic_rejections&&(dynamic_rejections<=12||dynamic_rejections%120==0))){
        const auto& f=dynamic_entities.failure();
        trace("t5_stereo_dynamic_entities","producer=%llu buffer=%p frame=%u enabled=%u begin=%u prepared=%u complete=%u left_models=%u right_models=%u left_brushes=%u right_brushes=%u stage=%s field=%s address=0x%lx expected=%u actual=%u counter_write_bytes=%u geometry_writes=0",
            production.stamp.producer_serial,before.address,before.frame,dynamic_entities.enabled(),dynamic_begin,dynamic_entities.prepared(),dynamic_complete,
            dynamic_entities.left_counts()[0],dynamic_entities.right_counts()[0],dynamic_entities.left_counts()[1],dynamic_entities.right_counts()[1],
            f.stage,f.field,static_cast<unsigned long>(f.address),f.expected,f.actual,dynamic_entities.prepared()?8u:0u);}
    Buffer after{};
    const bool built=shadows_built&&read_buffer(*a,true,&after) && after.address==before.address && after.frame==before.frame &&
                      after.count==2 && after.parms_count==2 && after.index==1;
    bool shadow_diagnostic_owned=false;
    if(built){std::lock_guard lock(a->metadata);auto& current=a->produced[buffer_slot(*a,before)];
        if(current.valid&&current.key==production.key&&current.stamp==production.stamp){current.shadows=std::move(shadows);shadow_diagnostic_owned=true;}}
    a->shadow_pair_diagnostic.complete(shadow_diagnostic.get(),shadow_diagnostic_owned);
    if(built&&a->auxiliary_enabled) {
        AuxiliaryViewLayout layout{};
        if(read_auxiliary_layout(a->base,true,&layout)&&layout.count==2&&layout.selected==1&&
           layout.buffer==production.stamp.buffer_identity&&layout.frame==production.native_frame&&
           regular_eye_slot(layout.slots[0],hud_viewport(production.eye_rectangles[0]))&&
           regular_eye_slot(layout.slots[1],hud_viewport(production.eye_rectangles[1]))) {
            std::lock_guard lock(a->metadata);auto& current=a->produced[buffer_slot(*a,before)];
            if(current.valid&&current.key==production.key&&current.stamp==production.stamp) {
                current.regular_slots={layout.slots[0],layout.slots[1]};current.regular_slots_copied=true;
            }
        }
    }
    if(built&&crossbow_native_infrared(0)) {
        auto scope_layout=views.compositor_layout;
        crossbow_scope_layout(nullptr,ticket.frame,&scope_layout);
        if(scope_layout.physical_scope.active&&scope_layout.physical_scope.use_separate_source) {
            const auto& source=scope_layout.physical_scope.source;
            const unsigned source_eye=source.x>=views.compositor_layout.eyes[1].x?1u:0u;
            ThermalViewSlotScope slot;
            ThermalSceneScope thermal;
            const bool reserved_slot=slot.begin(reinterpret_cast<std::uintptr_t>(before.address),before.frame);
            const bool scoped=reserved_slot&&thermal.begin(reinterpret_cast<std::uintptr_t>(refdef),reinterpret_cast<std::uintptr_t>(before.address),before.frame);
            bool copied=false;
            auto extra=std::make_shared<ThermalProduction>();
            if(scoped){
                auto build_thermal=[&]{
                write_view(refdef,views.eyes[source_eye]);
                SunShadowLodScope lod;
                lod.begin(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,reinterpret_cast<std::uintptr_t>(refdef),production.stamp.producer_serial,3);
                struct Call{NativeRenderScene original;void* refdef;int frame_time;};Call call{original,refdef,frame_time};
                invoke_sun_shadow_lod_native(&lod,[](void* context){auto* c=static_cast<Call*>(context);c->original(c->refdef,c->frame_time);},&call);
                copied=lod.safe()&&slot.built()&&capture_thermal_shadow(a->base,reinterpret_cast<std::uintptr_t>(before.address),before.frame,&extra->shadows);
                if(copied){extra->address=slot.slot();
                    const std::uint32_t no_commands=0;const std::uint8_t no_command_buffer=0;
                    std::memcpy(reinterpret_cast<void*>(extra->address+0x2220),&no_commands,4);
                    std::memcpy(reinterpret_cast<void*>(extra->address+0x38c5),&no_command_buffer,1);
                    std::memcpy(extra->view.data(),reinterpret_cast<void*>(extra->address),extra->view.size());}
                };
                using ThermalBody=decltype(build_thermal);
                invoke_thermal_scene_native(&thermal,&slot,[](void* p){(*static_cast<ThermalBody*>(p))();},&build_thermal);
            }
            const bool restored_thermal=thermal.restore();
            const bool restored_slot=slot.restore();
            if(!restored_thermal||!restored_slot)a->stop_requested=true;
            if(copied&&restored_thermal&&restored_slot){std::lock_guard lock(a->metadata);
                auto& current=a->produced[buffer_slot(*a,before)];
                if(current.valid&&current.key==production.key&&current.stamp==production.stamp)current.thermal=std::move(extra);}
            static unsigned thermal_builds{};
            if(++thermal_builds<=12||thermal_builds%120==0)
                trace("t5_thermal_scope_built","producer=%llu reserved=%u scoped=%u copied=%u restored=%u eye=%u actors=%u peripheral_ir=0",production.stamp.producer_serial,reserved_slot,scoped,copied,restored_thermal&&restored_slot,source_eye,thermal.actor_count());
        }
    }
    if (!built) {
        std::lock_guard lock(a->metadata); a->produced[buffer_slot(*a,before)].valid=false;
        trace("t5_stereo_build_rejected","producer=%llu reason=native_view_counts_or_buffer_changed",production.stamp.producer_serial);
    } else if (production.stamp.producer_serial<=6 || production.stamp.producer_serial%120==0) {
        trace("t5_stereo_built","xr_frame=%llu producer=%llu buffer=%p native_frame=%lu views=2 left_origin=%.3f,%.3f,%.3f right_origin=%.3f,%.3f,%.3f",
              ticket.key.frame_id,production.stamp.producer_serial,before.address,before.frame,
              views.eyes[0].origin.x,views.eyes[0].origin.y,views.eyes[0].origin.z,
              views.eyes[1].origin.x,views.eyes[1].origin.y,views.eyes[1].origin.z);
    }
    if(built) {
        PreCommandSnapshot pre{};
        const bool have_pre=read_pre_command_snapshot(&pre);
        if(have_pre) {
            const input::CommandRigSample mounted_rig{ticket.frame,anchor,
                ticket.key.generation,ticket.key.generation,ticket.published_ms};
            publish_mounted_gun_scene(mounted_rig,stock.origin,stock.axis,pre,
                reinterpret_cast<std::uintptr_t>(refdef));
            publish_native_use_gaze_scene(mounted_rig,stock.origin,stock.axis,pre,
                reinterpret_cast<std::uintptr_t>(refdef));
        } else revoke_mounted_gun_scene();
        helicopter::Facts pilot{},again{};
        const helicopter::Host host{nullptr,helicopter::detail::protected_copy,nullptr,nullptr};
        if(have_pre&&pre.native_gameplay_fields_valid&&pre.gameplay_epoch&&
           pre.native_pm_type==1&&pre.native_cg_identity+0x8c100==reinterpret_cast<std::uintptr_t>(refdef)&&
           helicopter::detail::read_graph(host,pilot)&&helicopter::eligible(pilot)&&pilot.cg==pre.native_cg_identity&&
           helicopter::detail::read_graph(host,again)&&again==pilot) {
            HelicopterSceneView copy{};
            copy.owner=pilot;
            copy.rig={ticket.frame,anchor,ticket.key.generation,ticket.key.generation,ticket.published_ms};
            copy.stock={stock.origin,stock.axis};copy.refdef=reinterpret_cast<std::uintptr_t>(refdef);
            copy.epoch=pre.gameplay_epoch;copy.sequence=pre.sequence;copy.native_time=pilot.game_time;
            std::lock_guard lock(a->metadata);a->helicopter_view=copy;
            auto& produced=a->produced[buffer_slot(*a,before)];
            if(produced.valid&&!produced.menu&&!produced.cinematic&&produced.key==production.key&&
               produced.stamp==production.stamp&&produced.native_frame==production.native_frame)
                produced.helicopter_reticle=true;
        }
    }
    return true;
    } catch (...) {
        a->stop_requested=true;
        trace("t5_stereo_exception","stage=frontend native_started=%u stop_requested=1",native_started);
        // Once native work began, never issue a duplicate fallback scene.
        return native_started;
    }
}
static bool render_stereo_backend_work(NativeDraw3D original,Adapter* a,std::uint64_t correction_serial) {
    bool native_faulted=false;
    if(a->cinematic_enabled) {
        // Native frames with a null shared list skip the FinalUi CALL entirely.
        // Retire prior admission at this unconditional existing backend seam.
        const auto serial=a->cinematic_backend_serial.fetch_add(1)+1;
        if(GetCurrentThreadId()==a->owner.load()) {
            a->cinematic_owner_backend_serial=serial;a->cinematic_gate.invalidate();a->cinematic_movie_gate.invalidate();a->cinematic_diagnostic_start={};a->cinematic_census_open={};
        }
    }
    bool native_started=false;
    try {
    Buffer b{}; Produced production{};
    {
        std::lock_guard lock(a->metadata);
        if (!a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load()) || !read_buffer(*a,false,&b)) return false;
        a->current_pixels={};
        production=a->produced[buffer_slot(*a,b)];
    }
    NativeFinalUi native_ui{};
    if (a->menu_enabled && (!read_ui_context(*a,&native_ui) || (native_ui.key_catchers&0x10)!=0)) return false;
    if (!production.valid || production.native_frame!=b.frame || production.stamp.buffer_identity!=reinterpret_cast<std::uintptr_t>(b.address)) return false;
    if (GetCurrentThreadId()!=a->owner.load() || !a->retained_device) return false;
    StereoDrawPlan plan{b.index,{0,1},{0,1},false};
    if(b.count!=2||b.index!=1) {
        if(!production_auxiliary_layout(*a,false,production,&production.auxiliary_layout)) {
            if(a->auxiliary_enabled&&(++a->auxiliary_rejected<=12||a->auxiliary_rejected%120==0))
                trace("t5_auxiliary_stereo_rejected","count=%llu producer=%llu views=%lu selected=%lu reason=exact_native_layout",a->auxiliary_rejected,production.stamp.producer_serial,b.count,b.index);
            return false;
        }
        plan=auxiliary_draw_plan();production.auxiliary=true;
    }
    a->scope_staging.invalidate();
    if(production.thermal&&std::memcmp(production.thermal->view.data(),reinterpret_cast<void*>(production.thermal->address),production.thermal->view.size())==0) {
        // Slot3 is reserved only for this scope. Keep the native pair and any
        // native slot2 auxiliary camera selected everywhere outside this call.
        NativeShadowScope shadow;
        bool selected=false;
        auto draw_thermal=[&]{
            selected=shadow.begin(a->base,production.thermal->shadows);
            if(selected){struct Call{NativeDraw3D original;};Call call{original};
                native_started=true;
                invoke_shadow_native(&shadow,[](void* context){static_cast<Call*>(context)->original();},&call);
                if(shadow.good()&&shadow.restored())production.thermal_captured=a->scope_staging.capture(a->retained_device,production.stamp.producer_serial);
            }
            if(shadow.pending())a->stop_requested=true;
        };
        using ThermalDraw=decltype(draw_thermal);
        const bool thermal_restored=invoke_thermal_backend_native(reinterpret_cast<std::uintptr_t>(b.address),
            [](void* p){(*static_cast<ThermalDraw*>(p))();},&draw_thermal);
        if(!thermal_restored){a->scope_staging.invalidate();production.thermal_captured=false;a->stop_requested=true;}
        static unsigned thermal_captures{};
        if(++thermal_captures<=12||thermal_captures%120==0)
            trace("t5_thermal_scope_pixels","producer=%llu selected=%u captured=%u restored=%u reason=%s",production.stamp.producer_serial,selected,production.thermal_captured,shadow.restored(),shadow.failure().stage);
    }
    AuxiliaryViewLayout bloom_layout=production.auxiliary_layout;
    bool bloom_layout_current=production.auxiliary;
    if(!production.auxiliary&&production.regular_slots_copied&&
       read_auxiliary_layout(a->base,false,&bloom_layout)&&bloom_layout.count==2&&
       bloom_layout.buffer==production.stamp.buffer_identity&&bloom_layout.frame==production.native_frame)
        bloom_layout_current=exact_postfx_layout(bloom_layout,auxiliary_eyes(production),production.regular_slots);
    std::uint64_t postfx_attempt=0;AuxiliaryPostfxKey postfx_key{};
    if(production.auxiliary&&a->auxiliary_postfx_enabled) {
        const auto postfx_error=GetLastError();postfx_attempt=a->auxiliary_postfx_budget.claim(GetTickCount64());
        postfx_key={postfx_attempt,production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->owner.load()};
        SetLastError(postfx_error);
    }
    unsigned postfx_stage=0;
    AuxiliaryHistoryCapture legacy_history;
    auto& history=a->history_correction_enabled?a->history_correction_capture:legacy_history;
    if(a->history_correction_enabled)history.retire();
    struct HistoryOwner {Adapter* a;IDirect3DDevice9* device;std::uint64_t epoch;vr::FrameKey key;std::uint64_t backend;};
    HistoryOwner history_owner{a,a->retained_device,a->auxiliary_history_epoch.load(),production.key,correction_serial};
    const HistoryCurrent history_current{&history_owner,[](void* context) noexcept {
        try {
        const auto& h=*static_cast<HistoryOwner*>(context);vr::FrameKey key{};
        return h.a->initialized.load()&&!h.a->stop_requested.load()&&!h.a->replacement_pending.load()&&
            GetCurrentThreadId()==h.a->owner.load()&&h.device==h.a->retained_device&&h.epoch==h.a->auxiliary_history_epoch.load()&&
            (!h.backend||h.backend==h.a->history_correction_backend.load())&&h.a->coordinator.read_pending_key(&key)&&key==h.key;
        }catch(...){return false;}}};
    auto staged=a->eye_staging.begin(a->retained_device,production.stamp.producer_serial,production.eye_rectangles,plan.eyes);
    unsigned staging_step=0; // 0 begin/descriptor validation, 1/2 eye copies, 3 ownership
    if(postfx_attempt)trace_auxiliary_postfx(a->base,production.auxiliary_layout,postfx_key,0);
    if(production.auxiliary&&SUCCEEDED(staged))history.begin(a->auxiliary_history_enabled,a->auxiliary_history_budget,a->base,production.auxiliary_layout,
        {0,production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->owner.load()},a->retained_device,history_current);
    if(production.auxiliary&&SUCCEEDED(staged))a->history_correction.begin(a->history_correction_enabled,a->base,production.auxiliary_layout,
        {0,production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->owner.load()},correction_serial,history_owner.epoch,a->retained_device,history_current);
    struct BloomOwner {HistoryCurrent current;Adapter* adapter;std::uintptr_t base;const AuxiliaryViewLayout* layout;DeviceCreationInfo creation;};
    BloomOwner bloom_owner{history_current,a,a->base,&bloom_layout,a->retained_creation};
    BloomEpisodeSourceContext bloom_episode_owner{};
    BloomEpisodeReuseContext bloom_reuse_owner{};
    bool bloom_scope_started=false;
    const bool bloom_replay_scene=production.auxiliary||a->bloom_ordinary_correction;
    a->bloom_scene_active.store(bloom_replay_scene&&bloom_layout_current&&SUCCEEDED(staged));
    if((a->bloom_shader_enabled||a->bloom_correction_enabled)&&a->bloom_scene_active.load()) {
        const BloomShaderSource source{&bloom_owner,[](void* p) noexcept {
                const auto& h=*static_cast<BloomOwner*>(p);DeviceCreationInfo now{};
                return h.current.valid()&&!h.adapter->device_lost.load()&&h.creation.serial&&
                    read_device_creation(h.adapter->retained_device,&now)&&now.serial==h.creation.serial&&
                    now.thread_id==h.creation.thread_id&&now.extended==h.creation.extended;},
            nullptr,
            bloom_owner.creation.serial,bloom_owner.creation.thread_id,bloom_owner.creation.extended};
        bloom_episode_owner={source,a->base,&bloom_layout};
        if(a->bloom_shader_enabled)a->bloom_shaders.begin({0,production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->owner.load()},history_owner.epoch,a->retained_device,make_bloom_episode_source(bloom_episode_owner));
        if(a->bloom_correction_enabled)bloom_reuse_owner={source,a->base,&bloom_layout,&a->bloom_read_reuse,
            {production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->bloom_successful_reset_serial,
                history_owner.epoch,correction_serial,bloom_owner.creation.serial},a->bloom_read_reuse.bind()};
        if(a->bloom_correction_enabled&&a->bloom_successful_reset_serial!=UINT64_MAX&&bloom_reuse_owner.revision)bloom_scope_started=a->bloom_correction.begin(
            {0,production.stamp.producer_serial,production.key.generation,production.key.frame_id,a->owner.load()},
            a->bloom_successful_reset_serial,a->retained_device,make_bloom_episode_reuse_source(bloom_reuse_owner),
            {a,[](void* p,const BloomDrawSnapshot& n,const BloomDrawCall& d,BloomQuadSnapshot* q) noexcept {
                auto& owner=*static_cast<Adapter*>(p);return read_bloom_quad(owner.base,n,d,q);}});
    }
    const bool rgb_required=a->bloom_correction_enabled&&!production.auxiliary&&!a->bloom_ordinary_correction;
    bool rgb_complete=!rgb_required||bloom_layout_current;
    const OrdinaryBloomOwner rgb_owner{&bloom_owner,[](void* p) noexcept {
        const auto& h=*static_cast<BloomOwner*>(p);return h.current.valid()&&!h.adapter->device_lost.load();}};
    unsigned history_stage=0,shadow_step=0;bool shadows_complete=production.shadows!=nullptr;
    bool complete=execute_stereo_draw_plan(plan,[&](std::uint32_t index){
            if(a->history_correction_enabled&&native_faulted)restore_history_index(reinterpret_cast<std::uint32_t*>(b.address+view_index_offset),index);
            else memcpy(b.address+view_index_offset,&index,4);},
        [&]{
            const auto eye=plan.eyes[shadow_step++];NativeShadowScope shadow;
            const bool selected=production.shadows&&shadow.begin(a->base,production.shadows->eyes[eye]);
            auto native_body=[&]{
                VPosEyeScope water_scope;
                const auto& rect=production.eye_rectangles[eye];
                const bool water_scoped=a->water_reflection_enabled&&!production.auxiliary&&!production.menu&&!production.cinematic&&
                    water_scope.begin(a->base,production.stamp.buffer_identity,production.native_frame,eye,
                        {rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top},reinterpret_cast<std::uintptr_t>(a->retained_device),
                        production.stamp.producer_serial,production.key.generation,production.key.frame_id,
                        {&bloom_owner,[](void* p) noexcept {const auto& h=*static_cast<BloomOwner*>(p);
                            return h.current.valid()&&!h.adapter->device_lost.load();}},&production.water_eye_origins);
                auto draw=[&]{
                    if(a->history_correction_enabled){
                        a->history_correction.before_native(eye);
                        invoke_history_original(original,&a->history_correction,&history,&native_faulted,&a->stop_requested,
                            reinterpret_cast<std::uint32_t*>(b.address+view_index_offset),plan.original_index);
                        a->history_correction.after_native(eye);
                    }else original();
                };
                using WaterDraw=decltype(draw);
                invoke_vpos_eye_native(water_scoped?&water_scope:nullptr,[](void* p){(*static_cast<WaterDraw*>(p))();},&draw);
                if(a->water_reflection_enabled&&(a->water_vpos.pending()||a->water_reflections.pending()))a->stop_requested=true;
            };
            auto body=[&]{
                OrdinaryBloomScope rgb;
                const bool applied=rgb_required&&bloom_layout_current&&rgb.begin(a->base,bloom_layout,eye,rgb_owner);
                using InnerBody=decltype(native_body);
                invoke_ordinary_bloom_native(rgb_required?&rgb:nullptr,[](void* p){(*static_cast<InnerBody*>(p))();},&native_body);
                if(rgb_required){
                    const bool accepted=applied&&rgb.good()&&rgb.restored()&&!rgb.pending();rgb_complete=rgb_complete&&accepted;
                    if(rgb.pending())a->stop_requested=true;
                    const auto count=accepted?++a->bloom_rgb_scopes:++a->bloom_rgb_rejected;
                    if(count<=4||count%120==0)trace("t5_ordinary_bloom_rgb","count=%llu producer=%llu buffer=%p frame=%u eye=%u applied=%u restored=%u accepted=%u pending=%u reason=%s native_calls=1 rgb_bytes=24",
                        count,production.stamp.producer_serial,b.address,b.frame,eye,applied,rgb.restored(),accepted,rgb.pending(),rgb.failure());
                }
            };
            using NativeBody=decltype(body);
            if(!shadow.pending()||selected)
                invoke_shadow_native(selected?&shadow:nullptr,[](void* p){(*static_cast<NativeBody*>(p))();},&body);
            const bool restored=selected&&shadow.restored();
            const bool accepted=selected&&restored&&shadow.good();shadows_complete=shadows_complete&&accepted;
            if(shadow.pending())a->stop_requested=true;
            const auto count=accepted?++a->shadow_restores:++a->shadow_backend_rejections;
            if(count<=12||count%120==0){const auto& failure=shadow.failure();
                trace("t5_shadow_restored","count=%llu producer=%llu buffer=%p frame=%u eye=%u native_index=%u selected=%u restored=%u accepted=%u pending=%u failure_stage=%s failure_field=%s failure_address=0x%lx expected=0x%x actual=0x%x",
                    count,production.stamp.producer_serial,b.address,b.frame,eye,plan.native_indices[shadow_step-1],selected,restored,accepted,shadow.pending(),
                    failure.stage,failure.field,static_cast<unsigned long>(failure.address),failure.expected,failure.actual);}

            if(postfx_attempt)trace_auxiliary_postfx(a->base,production.auxiliary_layout,postfx_key,++postfx_stage);history.stage(++history_stage);},
        [&](unsigned eye){
            if(!shadows_complete||!rgb_complete)return false;
            staging_step=eye+1;staged=a->eye_staging.save_eye(a->retained_device,production.stamp.producer_serial,eye);
            if(FAILED(staged))return false;
            staging_step=3;return a->coordinator.mark_eye_complete(production.key,production.stamp,eye);
        },&native_started,SUCCEEDED(staged));
    complete=complete&&shadows_complete&&rgb_complete&&(!a->water_reflection_enabled||(!a->water_vpos.pending()&&!a->water_reflections.pending()));
    a->bloom_scene_active.store(false);
    if(production.auxiliary) {
        AuxiliaryViewLayout after{};
        complete=complete&&production_auxiliary_layout(*a,false,production,&after)&&after==production.auxiliary_layout;
        if(complete&&(++a->auxiliary_staged<=12||a->auxiliary_staged%120==0))
            trace("t5_auxiliary_stereo_staged","count=%llu producer=%llu generation=%llu xr_frame=%llu buffer=0x%lx native_frame=%lu views=3 selected=2 native_calls=2 native_order=2,0 extra_draws=1 eye_copy_order=1,0 targets=22,23 extra_size=%ux%u",
                  a->auxiliary_staged,production.stamp.producer_serial,production.key.generation,production.key.frame_id,
                  static_cast<unsigned long>(production.stamp.buffer_identity),b.frame,after.targets[0].width,after.targets[0].height);
    }
    complete=complete_bloom_draw_scope(*a,production,bloom_scope_started,complete);
    if(a->history_correction_enabled)complete=a->history_correction.finish(complete);
    if(a->bloom_shader_enabled)a->bloom_shaders.finish(complete);
    history.finish(complete,a->eye_staging);
    if(a->history_correction_enabled)history.retire();
    production.completed=complete;
    { std::lock_guard lock(a->metadata); a->current_pixels=production; a->produced[buffer_slot(*a,b)].valid=false; }
    if (!complete) {
        a->eye_staging.invalidate();
        if (production.stamp.producer_serial<=6 || production.stamp.producer_serial%120==0) {
            const auto& d=a->eye_staging.last_source_description();
            trace("t5_stereo_backend_rejected","producer=%llu reason=staging_or_frame_ownership step=%u hr=0x%08lx bb=%ux%u format=%u multisample=%u quality=%lu left=%ld,%ld,%ld,%ld right=%ld,%ld,%ld,%ld",
                  production.stamp.producer_serial,staging_step,static_cast<unsigned long>(staged),
                  d.Width,d.Height,static_cast<unsigned>(d.Format),static_cast<unsigned>(d.MultiSampleType),d.MultiSampleQuality,
                  production.eye_rectangles[0].left,production.eye_rectangles[0].top,production.eye_rectangles[0].right,production.eye_rectangles[0].bottom,
                  production.eye_rectangles[1].left,production.eye_rectangles[1].top,production.eye_rectangles[1].right,production.eye_rectangles[1].bottom);
        }
    } else if (production.stamp.producer_serial<=6 || production.stamp.producer_serial%120==0) {
        trace("t5_stereo_pixels_staged","producer=%llu native_frame=%lu eyes=3 restore=authoritative_pre_present",
              production.stamp.producer_serial,b.frame);
    }
    return true;
    } catch (...) {
        a->bloom_scene_active.store(false);
        a->stop_requested=true;
        trace("t5_stereo_exception","stage=backend native_started=%u stop_requested=1",native_started);
        if(a->bloom_correction_enabled){a->bloom_correction.abandon();vr::FrameTicket pending{};
            if(a->coordinator.read_pending(&pending))a->bloom_rejected_key=pending.key;}
        if(native_faulted&&a->history_correction_enabled)throw;
        return native_started;
    }
}
bool render_stereo_backend(NativeDraw3D original) {
    auto* a=adapter.load();if(!a)return false;
    if(a->bloom_correction_enabled&&a->history_backend_active.load())a->bloom_read_reuse.revoke();
    if(a->bloom_correction_enabled&&GetCurrentThreadId()==a->owner.load()&&a->bloom_correction.pending()&&!a->history_backend_active.load())
        a->bloom_correction.reconcile_normal(a->retained_device,!a->device_lost.load());
    if(!a->history_correction_enabled)return render_stereo_backend_work(original,a,0);
    const auto serial=a->history_correction_backend.fetch_add(1)+1;
    bool expected=false;
    if(!a->history_backend_active.compare_exchange_strong(expected,true))return false;
    bool result=false;
    __try {result=render_stereo_backend_work(original,a,serial);}
    __finally {
        a->bloom_scene_active.store(false);
        // Keep the backend lease until all normal diagnostic Releases return.
        // Native unwind retires only mod state; COM waits for owner cleanup.
        if(a->bloom_shader_enabled){if(AbnormalTermination())a->bloom_shaders.abandon();else a->bloom_shaders.retire();}
        if(a->bloom_correction_enabled){a->bloom_read_reuse.revoke();a->bloom_read_reuse.close();
            if(AbnormalTermination())a->bloom_correction.abandon();else a->bloom_correction.end_scope();}
        a->history_backend_active.store(false);
    }
    return result;
}
bool prepare_quit_for_vr(void* context) noexcept {
    auto* a=static_cast<Adapter*>(context);
    if (!a) return true;
    if (!a->initialized.load() && !a->starting.load() && !a->cleanup_needed.load() && !a->replacement_pending.load()) return true;
    if(a->bloom_correction_enabled)a->bloom_read_reuse.revoke();a->stop_requested=true; return false;
}
namespace {
#include "t5/cinematic_production_adapter.inc"
#include "t5/cinematic_movie_adapter.inc"
bool same_menu_context(const NativeFinalUi& a,const NativeFinalUi& b) noexcept {
    return a.valid&&b.valid&&a.connection_state==b.connection_state&&a.key_catchers==b.key_catchers&&
        a.menu_count==b.menu_count&&a.page==b.page;
}
void begin_menu_frame(const FrameBeginPoint& point) {
    auto* a=adapter.load();
    if(a)begin_cinematic_production(*a,point);
    if(!a||!a->menu_frame_enabled||point.image_base!=a->base||point.native_thread!=GetCurrentThreadId()||
       !a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||a->device_lost.load())return;
    NativeFinalUi before{},after{};NativeHudFrame source{};
    const bool copied=read_ui_context(*a,&before)&&read_native_command_frame(a->base,true,&source)&&
        read_ui_context(*a,&after)&&same_menu_context(before,after);
    std::lock_guard lock(a->metadata);vr::FrameTicket pending{};const auto now=GetTickCount64();
    if(!copied||!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||a->device_lost.load()||
       !a->coordinator.read_pending(&pending)||a->menu_event_thread!=point.native_thread||!a->menu_events||
       !a->menu_event_tick||now<a->menu_event_tick||now-a->menu_event_tick>250) {
        a->menu_frame_epoch.clear();return;
    }
    MenuFrameMarker marker{};marker.native=source;marker.context=after;marker.thread=point.native_thread;
    marker.serial=++a->menu_marker_serial;marker.generation=pending.key.generation;marker.revision=a->menu_revision.load();
    marker.events=a->menu_events;marker.published_ms=now;marker.valid=true;
    const auto status=a->menu_frame_epoch.begin(marker,point.ordinary_screen_frame);
    if(point.call_serial<=12||point.call_serial%120==0)
        trace("t5_menu_frame_begin","call=%llu marker=%llu thread=%lu status=%u generation=%llu events=%llu revision=%llu buffer=0x%lx native_frame=%lu used=%d renderer_active=%u ordinary=%u",
              point.call_serial,marker.serial,marker.thread,static_cast<unsigned>(status),marker.generation,marker.events,marker.revision,
              static_cast<unsigned long>(source.buffer),source.frame,source.used,source.renderer_active,point.ordinary_screen_frame);
}
void publish_menu_frame(Adapter& a,const HudPassPoint& point,std::shared_ptr<const HudCommandPublication> publication) {
    NativeFinalUi before{},after{};NativeHudFrame current{};
    const bool copied=publication&&read_ui_context(a,&before)&&read_native_command_frame(a.base,true,&current)&&
        publication->native==current&&read_ui_context(a,&after)&&same_menu_context(before,after);
    std::lock_guard lock(a.metadata);vr::FrameTicket pending{};const auto now=GetTickCount64();
    if(!copied||!a.initialized.load()||a.stop_requested.load()||a.replacement_pending.load()||a.device_lost.load()||
       !a.coordinator.read_pending(&pending)||a.menu_event_thread!=point.native_thread) {
        a.menu_frame_epoch.clear();return;
    }
    MenuFrameMarker marker{};marker.native=current;marker.context=after;marker.thread=point.native_thread;
    marker.context.backend=current.buffer;marker.context.commands=current.shared_commands;marker.context.native_frame=current.frame;
    marker.serial=++a.menu_marker_serial;marker.generation=pending.key.generation;marker.revision=a.menu_revision.load();
    marker.events=a.menu_events;marker.published_ms=now;marker.valid=true;
    const auto status=a.menu_frame_epoch.complete(marker,point.enclosing_return,std::move(publication));
    const auto count=status==MenuEpochStatus::accepted?++a.menu_epoch_completed:++a.menu_epoch_rejected;
    if(count<=6||count%120==0) {
        FinalizedMenuStamp stamp{};a.menu_frame_epoch.match(marker.context,marker.generation,marker.revision,now,&stamp);
        trace("t5_menu_frame_finalized","status=%u count=%llu call=%llu marker=%llu enclosing=0x%lx thread=%lu generation=%llu events=%llu revision=%llu epoch=%llu native_epoch=%llu buffer=0x%lx native_frame=%lu begin_used=%d used=%d shared=0x%lx owned_bytes=%u",
              static_cast<unsigned>(status),count,point.call_serial,marker.serial,static_cast<unsigned long>(point.enclosing_return),point.native_thread,
              marker.generation,marker.events,marker.revision,stamp.epoch,stamp.native_epoch,static_cast<unsigned long>(current.buffer),current.frame,
              stamp.begin.native.used,current.used,static_cast<unsigned long>(current.shared_commands),stamp.commands?static_cast<unsigned>(stamp.commands->bytes.size()):0u);
    }
}

bool current_cinematic_pixels(const Adapter& a,const Produced& pixels) {
    NativeCinematicSnapshot current{};
    if(!pixels.cinematic||!pixels.cinematic_commands||!pixels.cinematic_final_serial||
       a.final_ui_serial.load()!=pixels.cinematic_final_serial||a.cinematic_backend_serial.load()!=pixels.cinematic_backend_serial||
       !read_native_cinematic_snapshot(&current))return false;
    const auto classify=pixels.cinematic_independent?classify_cinematic_movie_commands:classify_cinematic_commands;
    const auto proof=classify(published_shared_list(*pixels.cinematic_commands),pixels.cinematic_movie,current,pixels.source_width,pixels.source_height);
    return proof.status==CinematicCommandStatus::full_canvas_movie_primitive&&a.final_ui_serial.load()==pixels.cinematic_final_serial&&
        a.cinematic_backend_serial.load()==pixels.cinematic_backend_serial;
}
void stage_cinematic_at_present(Adapter& a,IDirect3DDevice9* device) {
    if(a.cinematic_movie_enabled){stage_cinematic_movie_at_present(a,device);return;}
    // This is the original owner's authoritative pre-Present callback: native
    // final-list flush, caller's conditional flush and EndScene have returned.
    // No native movie/API/command replay is performed by this route.
    vr::FrameTicket pending{};NativeFinalUi current{};NativeCinematicSnapshot movie{};
    if(!a.initialized.load()||a.stop_requested.load()||a.device_lost.load()||a.replacement_pending.load()||
       device!=a.retained_device||GetCurrentThreadId()!=a.owner.load()||!a.coordinator.read_pending(&pending)||
       !read_ui_context(a,&current)||!read_native_cinematic_snapshot(&movie)) {a.cinematic_gate.invalidate();a.cinematic_movie_gate.invalidate();return;}
    CinematicFrameStamp frame{};
    if(!a.cinematic_gate.take_at_present(a.final_ui_serial.load(),a.cinematic_backend_serial.load(),pending.key.generation,a.menu_revision.load(),current,movie,GetTickCount64(),&frame))return;
    if(!a.cinematic_panel_enabled)return;
    vr::FrameTicket ticket{};
    if(!a.coordinator.claim_render_frame(&ticket)||ticket.key!=pending.key)return;
    IDirect3DSurface9* bb{};D3DSURFACE_DESC description{};
    const bool same=SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb&&SUCCEEDED(bb->GetDesc(&description))&&
        description.Width==frame.canvas.width&&description.Height==frame.canvas.height;
    if(bb)bb->Release();if(!same)return;
    xr::QuadLayer panel{};
    const auto now=GetTickCount64();
    if(!a.cinematic_anchor_tick||now<a.cinematic_anchor_tick||now-a.cinematic_anchor_tick>250||
       a.cinematic_anchor_movie.current_name!=movie.current_name||a.cinematic_anchor_movie.bink_handle!=movie.bink_handle||
       movie.active_image_frame<a.cinematic_anchor_movie.active_image_frame||a.cinematic_anchor_native_epoch!=frame.production.native_epoch)
        a.cinematic_panel.clear();
    if(!a.cinematic_panel.update(ticket.frame.head_center,ticket.key.generation,ui::OpaqueContext::cinematic,description.Width,description.Height,&panel))return;
    a.cinematic_anchor_tick=now;a.cinematic_anchor_movie=movie;a.cinematic_anchor_native_epoch=frame.production.native_epoch;
    Produced pixels{};pixels.valid=true;pixels.menu=true;pixels.cinematic=true;pixels.key=ticket.key;
    {std::lock_guard lock(a.metadata);pixels.stamp={frame.native.backend,++a.producer_serial};}
    pixels.native_frame=frame.native.native_frame;pixels.native_ui=frame.native;pixels.panel=panel;
    pixels.menu_revision=frame.production.end.revision;pixels.ui_production_epoch=frame.production.epoch;
    pixels.ui_production_ms=frame.production.begin.published_ms;pixels.source_width=description.Width;pixels.source_height=description.Height;
    pixels.cinematic_movie=movie;pixels.cinematic_commands=frame.production.commands;pixels.cinematic_final_serial=frame.final_serial;
    pixels.cinematic_backend_serial=frame.backend_serial;
    if(!a.coordinator.stage_menu_frame(ticket.key,pixels.stamp,panel)||!a.coordinator.mark_menu_complete(ticket.key,pixels.stamp))return;
    pixels.completed=true;
    {std::lock_guard lock(a.metadata);a.current_pixels=pixels;a.submitted_menu={};}
    const auto n=++a.cinematic_presented;
    if(n<=6||n%120==0)trace("t5_cinematic_panel_staged","serial=%llu backend_epoch=%llu count=%llu xr_frame=%llu generation=%llu producer=%llu epoch=%llu original_production_ms=%llu native_frame=%lu canvas=%ux%u movie=%s capture=authoritative_pre_present native_tail_returned=1 world_restore=0 interactive=0",
        frame.final_serial,frame.backend_serial,n,ticket.key.frame_id,ticket.key.generation,pixels.stamp.producer_serial,frame.production.epoch,
        frame.production.begin.published_ms,frame.native.native_frame,description.Width,description.Height,movie.current_name.data());
}

void begin_ui_production(const NativeUiProduction& value) {
    auto* a=adapter.load();
    if (!a||!a->ui_epoch_enabled||!a->initialized.load()||(a->stop_requested.load() || a->replacement_pending.load())) return;
    std::lock_guard lock(a->metadata);a->ui_epoch.begin(value,a->menu_revision.load());
}
void complete_ui_production(const NativeUiProduction& before,const NativeUiProduction& after) {
    auto* a=adapter.load();
    if (!a||!a->ui_epoch_enabled||!a->initialized.load()||(a->stop_requested.load() || a->replacement_pending.load())) return;
    std::lock_guard lock(a->metadata);
    const bool complete=a->ui_epoch.complete(before,after,a->menu_revision.load());
    if (before.call_serial<=12||before.call_serial%120==0)
        trace("t5_ui_epoch_completed","call=%llu buffer=0x%lx native_frame=%lu revision=%llu complete=%u",
              before.call_serial,static_cast<unsigned long>(after.frontend),after.native_frame,a->menu_revision.load(),complete);
}
// Optional observation is wholly separate from the submission gate. Only
// current immutable publications survive this native call, and every later
// owner backend/final entry retires them before considering eligibility.
void observe_cinematic_diagnostic_begin(Adapter& a,const NativeFinalUi& native,
        const FinalizedMenuStamp& production,bool owned_matches,std::uint64_t serial) {
    if(!a.cinematic_diagnostic_enabled)return;
    struct ErrorScope{DWORD value{GetLastError()};~ErrorScope(){SetLastError(value);}} error_scope;
    try {
    auto& s=a.cinematic_diagnostic_start;s={};
    const auto now=GetTickCount64();if(!a.cinematic_diagnostic_budget.sample(now))return;
    s.sampled=true;s.serial=serial;s.backend_serial=a.cinematic_owner_backend_serial;s.started_ms=now;
    s.native=native;s.production=production;s.owned_native_bytes=owned_matches;
    s.movie_copied=read_native_cinematic_snapshot(&s.movie); // Before input/canvas eligibility filters.
    // Metadata only: no image readback, copy, draw or native game invocation.
    if(a.retained_device&&owned_matches&&production.epoch) {
        struct SurfaceRefs{IDirect3DSurface9* bb{};IDirect3DSurface9* rt{};~SurfaceRefs(){if(rt)rt->Release();if(bb)bb->Release();}} refs;
        auto*& bb=refs.bb;auto*& rt=refs.rt;D3DSURFACE_DESC desc{};
        D3DVIEWPORT9 viewport{};RECT scissor{};DWORD enabled_scissor{};
        s.canvas_copied=SUCCEEDED(a.retained_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb&&
            SUCCEEDED(bb->GetDesc(&desc))&&SUCCEEDED(a.retained_device->GetRenderTarget(0,&rt))&&rt&&
            SUCCEEDED(a.retained_device->GetViewport(&viewport))&&
            SUCCEEDED(a.retained_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled_scissor))&&
            SUCCEEDED(a.retained_device->GetScissorRect(&scissor));
        if(s.canvas_copied){s.canvas.width=desc.Width;s.canvas.height=desc.Height;s.canvas.backbuffer_target=bb==rt;
            s.canvas.full_viewport=viewport.X==0&&viewport.Y==0&&viewport.Width==desc.Width&&viewport.Height==desc.Height;
            s.canvas.full_scissor=!enabled_scissor||(scissor.left<=0&&scissor.top<=0&&scissor.right>=static_cast<LONG>(desc.Width)&&scissor.bottom>=static_cast<LONG>(desc.Height));}
    }
    s.reasons=cinematic_diagnostic_reasons(s,a.retained_device!=nullptr,a.cinematic_backend_serial.load());
    }catch(...){a.cinematic_diagnostic_start={};} // Diagnostic failure never skips the original C1/menu path.
}
void observe_cinematic_diagnostic_complete(Adapter& a,const NativeFinalUi& native) {
    if(!a.cinematic_diagnostic_enabled)return;
    struct ErrorScope{DWORD value{GetLastError()};~ErrorScope(){SetLastError(value);}} error_scope;
    try {
    auto s=std::move(a.cinematic_diagnostic_start);a.cinematic_diagnostic_start={};if(!s.sampled)return;
    NativeCinematicSnapshot movie{};const bool copied=read_native_cinematic_snapshot(&movie);
    const bool owned=s.production.commands&&match_native_shared_backend(a.base,*s.production.commands);
    vr::FrameTicket pending{};bool has_pending{};std::uint64_t revision{};
    {std::lock_guard lock(a.metadata);has_pending=a.coordinator.read_pending(&pending);revision=a.menu_revision.load();}
    const auto serial=a.final_ui_serial.load(),backend=a.cinematic_backend_serial.load(),now=GetTickCount64();
    auto reasons=cinematic_diagnostic_finish_reasons(s,native,serial,backend,has_pending,pending.key.generation,revision,owned,copied,now);
    bool classified=cinematic_diagnostic_can_classify(reasons);
    CinematicCommandEvidence proof{};
    if(classified)proof=classify_cinematic_commands(published_shared_list(*s.production.commands),s.movie,movie,s.canvas.width,s.canvas.height);
    if(a.final_ui_serial.load()!=serial)reasons|=diagnostic_final_serial;
    if(a.cinematic_backend_serial.load()!=backend)reasons|=diagnostic_backend_epoch;
    if(a.menu_revision.load()!=revision)reasons|=diagnostic_generation_revision;
    if(!cinematic_diagnostic_can_classify(reasons)){classified=false;proof={};}
    const CinematicDiagnosticKey key{movie.current_name,movie.bink_handle,native.page.focused_menu,reasons,
        native.connection_state,native.key_catchers,native.page.stack_count,native.page.menu_type,proof.status,
        movie.initialized,movie.finished,movie.cg_fullscreen,copied,classified};
    if(!a.cinematic_diagnostic_budget.report(now,key))return;
    trace("t5_cinematic_diagnostic","report=%llu observation=%llu serial=%llu backend_epoch=%llu reason_mask=0x%x owner_entry=1 epoch=%llu generation=%llu revision=%llu native_frame=%lu views=%lu context_valid=%u state=%d catchers=0x%x registered_menus=%d stack=%d menu_type=%d focused=0x%lx before_decoder_copied=%u after_decoder_copied=%u initialized=%u finished=%u bink=0x%lx material=0x%lx frame=%d texture_set=%d fullscreen_known=%u fullscreen=%u movie=%s canvas=%ux%u canvas_read=%u viewport_full=%u scissor_full=%u bb_target=%u classified=%u primitive_status=%s movie_primitives=%lu full_canvas=%lu tagged=%lu rectangle=%.3f,%.3f,%.3f,%.3f routing_evidence_only=1 native_tail_pending=1",
        a.cinematic_diagnostic_budget.reports(),a.cinematic_diagnostic_budget.samples(),s.serial,s.backend_serial,reasons,s.production.epoch,
        s.production.end.generation,s.production.end.revision,native.native_frame,s.production.end.native.view_count,native.valid,
        native.connection_state,native.key_catchers,native.menu_count,native.page.stack_count,native.page.menu_type,static_cast<unsigned long>(native.page.focused_menu),
        s.movie_copied,copied,movie.initialized,movie.finished,static_cast<unsigned long>(movie.bink_handle),static_cast<unsigned long>(movie.cinematic_material),movie.active_image_frame,movie.bound_texture_set,
        movie.cg_fullscreen_known,movie.cg_fullscreen,movie.current_name.data(),s.canvas.width,s.canvas.height,s.canvas_copied,s.canvas.full_viewport,s.canvas.full_scissor,s.canvas.backbuffer_target,
        classified,cinematic_command_status_name(proof.status),proof.movie_primitives,proof.full_canvas_primitives,proof.tagged_movie_primitives,
        proof.last_x,proof.last_y,proof.last_width,proof.last_height);
    }catch(...){a.cinematic_diagnostic_start={};}
}
void begin_native_ui(const NativeFinalUi& before) {
    auto* a=adapter.load();
    // Even a rejected foreign-thread native final loop invalidates the last
    // owner result by serial. Only the owner accesses the copied gate state.
    const auto final_serial=a&&a->cinematic_enabled?a->final_ui_serial.fetch_add(1)+1:0;
    if (!a || !a->menu_enabled || !a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load()) || GetCurrentThreadId()!=a->owner.load()) return;
    std::shared_ptr<const HudCommandPublication> owned;
    FinalizedMenuStamp finalized{};
    if(a->cinematic_enabled){a->cinematic_gate.invalidate();a->cinematic_movie_gate.invalidate();a->cinematic_diagnostic_start={};a->cinematic_census_open={};}
    {
    std::lock_guard lock(a->metadata);
    a->ui_started_revision=a->menu_revision.load();a->ui_started_epoch=0;
    if(a->menu_frame_enabled) {
        FinalizedMenuStamp stamp{};vr::FrameTicket pending{};
        if(a->coordinator.read_pending(&pending)&&a->menu_frame_epoch.match(before,pending.key.generation,a->menu_revision.load(),GetTickCount64(),&stamp)) {
            a->ui_started_revision=stamp.end.revision;a->ui_started_epoch=stamp.epoch;owned=stamp.commands;finalized=stamp;
        } else a->ui_started_revision=0;
    } else if (a->ui_epoch_enabled) {
        UiProductionStamp stamp{};
        if (a->ui_epoch.match(before,a->menu_revision.load(),&stamp)) {
            a->ui_started_revision=stamp.revision;a->ui_started_epoch=stamp.epoch;
        } else a->ui_started_revision=0; // No backend-time revision fallback.
    }
    if ((before.key_catchers&0x10)!=0) a->current_pixels={};
    else {a->submitted_menu={};a->menu_panel.clear();}
    }
    const bool owned_matches=owned&&match_native_shared_backend(a->base,*owned);
    if(a->menu_frame_enabled&&!owned_matches)a->ui_started_revision=0;
    begin_cinematic_census(*a,before,final_serial);
    begin_cinematic_movie(*a,before,final_serial);
    observe_cinematic_diagnostic_begin(*a,before,finalized,owned_matches,final_serial);
    if(a->cinematic_enabled&&!a->cinematic_movie_enabled&&a->retained_device&&finalized.epoch&&owned_matches&&finalized.end.native.view_count==0&&before.key_catchers==0&&
       a->cinematic_owner_backend_serial&&a->cinematic_owner_backend_serial==a->cinematic_backend_serial.load()) {
        NativeCinematicSnapshot movie{};CinematicCanvas canvas{};
        IDirect3DSurface9* bb{};IDirect3DSurface9* rt{};D3DSURFACE_DESC description{};
        D3DVIEWPORT9 viewport{};RECT scissor{};DWORD scissor_enabled{};
        const bool read=SUCCEEDED(a->retained_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb&&
            SUCCEEDED(bb->GetDesc(&description))&&SUCCEEDED(a->retained_device->GetRenderTarget(0,&rt))&&rt&&
            SUCCEEDED(a->retained_device->GetViewport(&viewport))&&
            SUCCEEDED(a->retained_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor_enabled))&&
            SUCCEEDED(a->retained_device->GetScissorRect(&scissor));
        if(read) {
            canvas.width=description.Width;canvas.height=description.Height;canvas.backbuffer_target=bb==rt;
            canvas.full_viewport=viewport.X==0&&viewport.Y==0&&viewport.Width==description.Width&&viewport.Height==description.Height;
            canvas.full_scissor=!scissor_enabled||(scissor.left<=0&&scissor.top<=0&&scissor.right>=static_cast<LONG>(description.Width)&&scissor.bottom>=static_cast<LONG>(description.Height));
        }
        if(rt)rt->Release();if(bb)bb->Release();
        if(read&&read_native_cinematic_snapshot(&movie)) {
            const bool accepted=a->cinematic_gate.begin(final_serial,a->cinematic_owner_backend_serial,finalized,before,movie,canvas,true,GetTickCount64());
            if(final_serial<=12||final_serial%120==0)
                trace("t5_cinematic_final_begin","serial=%llu backend_epoch=%llu accepted=%u epoch=%llu native_frame=%lu views=%lu canvas=%ux%u viewport_full=%u scissor_full=%u bb_target=%u decoder_frame=%d texture_set=%d movie=%s",
                      final_serial,a->cinematic_owner_backend_serial,accepted,finalized.epoch,before.native_frame,finalized.end.native.view_count,
                      canvas.width,canvas.height,canvas.full_viewport,canvas.full_scissor,canvas.backbuffer_target,movie.active_image_frame,movie.bound_texture_set,movie.current_name.data());
        }
    }
}
void complete_native_ui(const NativeFinalUi& before,const NativeFinalUi& after) {
    auto* a=adapter.load();
    if (!a || !a->menu_enabled || !a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load()) || GetCurrentThreadId()!=a->owner.load()) return;
    complete_cinematic_census(*a,after);
    observe_cinematic_diagnostic_complete(*a,after);
    if(complete_cinematic_movie(*a,after)){std::lock_guard lock(a->metadata);a->submitted_menu={};return;}
    if(a->cinematic_enabled&&!a->cinematic_movie_enabled) {
        const auto commands=a->cinematic_gate.open_commands();NativeCinematicSnapshot movie{};
        const bool copied=commands&&match_native_shared_backend(a->base,*commands)&&read_native_cinematic_snapshot(&movie);
        const auto final_serial=a->final_ui_serial.load();
        const auto proof=a->cinematic_gate.complete(final_serial,a->cinematic_backend_serial.load(),after,movie,copied,GetTickCount64());
        const auto n=++a->cinematic_status_counts[static_cast<unsigned>(proof.status)];
        if(n<=6||n%120==0)trace("t5_cinematic_final_complete","serial=%llu backend_epoch=%llu status=%s count=%llu epoch=%llu native_frame=%lu state=%d catchers=0x%x movie_primitives=%lu full_canvas=%lu tagged=%lu rectangle=%.3f,%.3f,%.3f,%.3f decoder_frame=%d texture_set=%d movie=%s final_tail_flush_pending=1 capture_here=0",
            final_serial,a->cinematic_backend_serial.load(),cinematic_command_status_name(proof.status),n,a->ui_started_epoch,after.native_frame,
            after.connection_state,after.key_catchers,proof.movie_primitives,proof.full_canvas_primitives,proof.tagged_movie_primitives,
            proof.last_x,proof.last_y,proof.last_width,proof.last_height,movie.active_image_frame,movie.bound_texture_set,movie.current_name.data());
        if(proof.status==CinematicCommandStatus::full_canvas_movie_primitive) {
            ++a->cinematic_completed;
            if(a->cinematic_panel_enabled) {
                std::lock_guard lock(a->metadata);a->submitted_menu={};
                return; // Preserve the unclaimed ticket for the post-tail movie route.
            }
        }
    }
    if (!known_menu(before) || !known_menu(after) || before.backend!=after.backend || before.commands!=after.commands ||
        before.native_frame!=after.native_frame || before.connection_state!=after.connection_state ||
        before.key_catchers!=after.key_catchers || before.menu_count!=after.menu_count || before.page!=after.page ||
        !a->ui_started_revision || a->ui_started_revision!=a->menu_revision.load() || !a->retained_device) return;
    std::uint64_t original_production_ms{};
    if(a->menu_frame_enabled) {
        FinalizedMenuStamp stamp{};vr::FrameTicket pending{};
        {
            std::lock_guard lock(a->metadata);
            if(!a->coordinator.read_pending(&pending)||!a->menu_frame_epoch.match(after,pending.key.generation,a->menu_revision.load(),GetTickCount64(),&stamp)||stamp.epoch!=a->ui_started_epoch)return;
        }
        if(!stamp.commands||!match_native_shared_backend(a->base,*stamp.commands))return;
        original_production_ms=stamp.begin.published_ms;
    } else if (a->ui_epoch_enabled) {
        std::lock_guard lock(a->metadata);UiProductionStamp stamp{};
        if (!a->ui_epoch.match(after,a->menu_revision.load(),&stamp)||stamp.epoch!=a->ui_started_epoch) return;
    }
    vr::FrameTicket ticket{};
    if (!a->coordinator.claim_render_frame(&ticket)) return;
    IDirect3DSurface9* backbuffer{};D3DSURFACE_DESC description{};
    const auto got=a->retained_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
    if (FAILED(got) || !backbuffer) return;
    const auto described=backbuffer->GetDesc(&description);backbuffer->Release();
    if (FAILED(described)) return;
    xr::QuadLayer panel{};
    if (!a->menu_panel.update(ticket.frame.head_center,ticket.key.generation,ui::OpaqueContext::menu,
                              description.Width,description.Height,&panel)) return;
    Produced pixels{};pixels.valid=true;pixels.menu=true;pixels.key=ticket.key;
    {std::lock_guard lock(a->metadata);pixels.stamp={before.backend,++a->producer_serial};}
    pixels.native_frame=before.native_frame;
    pixels.panel=panel;pixels.native_ui=before;pixels.menu_revision=a->ui_started_revision;
    pixels.ui_production_epoch=a->ui_started_epoch;
    pixels.ui_production_ms=original_production_ms;
    pixels.source_width=description.Width;pixels.source_height=description.Height;
    if (!a->coordinator.stage_menu_frame(ticket.key,pixels.stamp,panel) ||
        !a->coordinator.mark_menu_complete(ticket.key,pixels.stamp)) return;
    pixels.completed=true;
    {std::lock_guard lock(a->metadata);a->current_pixels=pixels;}
    if (pixels.stamp.producer_serial<=6 || pixels.stamp.producer_serial%120==0)
        trace("t5_menu_completed","xr_frame=%llu producer=%llu native_frame=%lu source=%ux%u state=%d catchers=0x%x menus=%d revision=%llu production_epoch=%llu",
              ticket.key.frame_id,pixels.stamp.producer_serial,before.native_frame,description.Width,description.Height,
              before.connection_state,before.key_catchers,before.menu_count,pixels.menu_revision,pixels.ui_production_epoch);
}
}
void service_menu_frame_after_events() noexcept {
    service_menu_input_after_events();
    auto* a=adapter.load();if(!a||!a->menu_frame_enabled)return;
    try {
        std::lock_guard lock(a->metadata);
        if(a->stop_requested.load()||a->replacement_pending.load()||a->device_lost.load())return;
        ++a->menu_events;a->menu_event_thread=GetCurrentThreadId();a->menu_event_tick=GetTickCount64();
    }catch(...){}
}
bool menu_input_epoch_ready() noexcept {
    const auto* a=adapter.load();return a&&a->menu_enabled&&(a->ui_epoch_enabled||a->menu_frame_enabled);
}
bool read_menu_panel_snapshot(MenuPanelSnapshot* output) noexcept {
    if (!output) return false;
    *output={};auto* a=adapter.load();
    if (!a) return false;
    try {
        std::lock_guard lock(a->metadata);
        const auto now=GetTickCount64();
        if (!a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load()) || a->device_lost.load() ||
            !a->submitted_menu.interactive || now<a->submitted_menu_tick || now-a->submitted_menu_tick>250) return false;
        if(a->menu_frame_enabled&&(!a->submitted_menu.production_epoch||!a->submitted_menu.production_ms||
           now<a->submitted_menu.production_ms||now-a->submitted_menu.production_ms>250))return false;
        *output=a->submitted_menu;return true;
    } catch (...) {return false;}
}
void invalidate_menu_panel_interaction() noexcept {
    auto* a=adapter.load();if (!a) return;
    try {std::lock_guard lock(a->metadata);++a->menu_revision;a->submitted_menu={};}
    catch (...) {}
}
static bool report_weapon_reservation_refused(Adapter* a,const NativeWeaponContext& producer,const char* stage,
        vr::NativeBuildIdentity native={},const vr::SceneReservationDiagnostic* copied=nullptr) noexcept {
    if(!a||!a->reservation_diagnostic_enabled)return false;
    {std::lock_guard lock(a->metadata);a->diagnostic_last_refusal={producer.call_serial,stage,native,copied?*copied:vr::SceneReservationDiagnostic{},copied!=nullptr};}
    if(a->reservation_diagnostic_reports.load()>=64)return false;
    const auto refusal=++a->reservation_diagnostic_refusals;
    if(refusal>12&&refusal%120!=0)return false;
    if(a->reservation_diagnostic_reports.fetch_add(1)>=64)return false;
    const vr::SceneReservationDiagnostic empty{};const auto& d=copied?*copied:empty;
    trace("t5_weapon_reservation_refused","count=%llu stage=%s coordinator=%s now_ms=%llu call=%llu weapon=%u command=%d native_time=%d native_buffer=0x%llx native_frame=%u build=%llu copied=%u initialized=%u busy=%u faulted=%u reset=%u pending=%u generation=%llu xr_frame=%llu published_ms=%llu invalid=%u claimed=%u reserved=%llu staged=%u menu=%u eyes=%u render=%u views=%u reserved_buffer=0x%llx reserved_frame=%u reserved_build=%llu backend_buffer=0x%llx backend_producer=%llu admission_changed=0",
        refusal,stage,d.reason,GetTickCount64(),producer.call_serial,producer.weapon_index,producer.command_number,producer.native_time,
        static_cast<unsigned long long>(native.buffer_identity),native.native_frame,native.build_serial,copied!=nullptr,
        d.initialized,d.busy,d.runtime_faulted,d.reset_blocked,d.pending,d.key.generation,d.key.frame_id,d.published_ms,d.invalid,d.claimed,d.reservation_serial,
        d.staged,d.menu,d.eye_mask,d.should_render,d.views_valid,static_cast<unsigned long long>(d.reserved_native.buffer_identity),d.reserved_native.native_frame,
        d.reserved_native.build_serial,static_cast<unsigned long long>(d.backend_buffer),d.backend_producer);
    return false;
}
static bool reserve_weapon_scene_common(const NativeWeaponContext& producer,input::ReservedWeaponFrame* output,
                                input::WeaponBodySample* body,const BorrowedWeaponClaim* claim,
                                bool hidden_primary_weapon=false) noexcept {
    if (output) *output={};if (body) *body={};
    auto* a=adapter.load();
    const bool borrowed_route=claim||hidden_primary_weapon;
    if (!output || !body || !a || !(borrowed_route?a->borrowed_weapon_reservations_enabled:a->weapon_reservations_enabled) || !a->initialized.load() ||
        (a->stop_requested.load() || a->replacement_pending.load()) || a->device_lost.load() || producer.native_thread!=GetCurrentThreadId() ||
        !producer.primary_viewmodel || (producer.dual_wield&&(!claim||!exact_profile_aim_enabled())) ||
        (hidden_primary_weapon&&(!producer.primary_knife_variant||!expects_tracked_weapon_render(producer,weapon_borrowed_placement_enabled()))))
        return report_weapon_reservation_refused(a,producer,hidden_primary_weapon?"hidden_adapter_context":"adapter_context");
    if(claim&&exact_profile_aim_enabled()&&!command_profile_admission_current(claim->command_admission))return report_weapon_reservation_refused(a,producer,"command_admission");
    if(claim){const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot command{};
        if(!valid_borrowed_claim(*claim,GetTickCount64())||!borrowed_claim_matches_context(*claim,producer)||
           !scope.valid||scope.depth!=1||scope.token!=claim->wrapper_token||pose_clear_serial()!=claim->camera_serial||
           !read_pre_command_snapshot(&command)||!primary_weapon_active(command)||command.gameplay_epoch!=claim->key.command_epoch||
            command.sequence!=claim->key.command_sequence)return report_weapon_reservation_refused(a,producer,"claim_scope_command");
    }
    try {
        Buffer before{};NativeSceneSample actual{};
        if (!read_buffer(*a,true,&before))return report_weapon_reservation_refused(a,producer,"native_buffer_read");
        if (before.count || before.parms_count)return report_weapon_reservation_refused(a,producer,"native_buffer_not_empty",{reinterpret_cast<std::uintptr_t>(before.address),before.frame,0});
        if (!read_native_scene(*a,reinterpret_cast<void*>(producer.refdef_identity),&actual))return report_weapon_reservation_refused(a,producer,"native_scene_read");
        if (!matches_weapon_scene(producer,actual))return report_weapon_reservation_refused(a,producer,"native_scene_mismatch",{reinterpret_cast<std::uintptr_t>(before.address),before.frame,0});
        std::unique_lock lock(a->metadata);
        vr::SceneReservation reservation{};
        const vr::NativeBuildIdentity native{reinterpret_cast<std::uintptr_t>(before.address),before.frame,++a->native_build_serial};
        vr::SceneReservationDiagnostic diagnostic{};
        auto* const capture=a->reservation_diagnostic_enabled?&diagnostic:nullptr;
        if (!a->coordinator.reserve_scene_frame(native,&reservation,capture)){lock.unlock();return report_weapon_reservation_refused(a,producer,"coordinator",native,capture);}
        const auto now=GetTickCount64();
        if (!usable_weapon_scene_publication(reservation.ticket,now,input::kMaximumWeaponPredictionAgeMs)) {
            a->coordinator.cancel_scene_reservation(reservation.ticket.key,reservation.serial);
            diagnostic.reason="unusable_publication";diagnostic.key=reservation.ticket.key;diagnostic.published_ms=reservation.ticket.published_ms;
            diagnostic.reservation_serial=reservation.serial;diagnostic.reserved_native=native;diagnostic.should_render=reservation.ticket.frame.should_render;diagnostic.views_valid=reservation.ticket.frame.views_valid;
            lock.unlock();return report_weapon_reservation_refused(a,producer,"publication",native,&diagnostic);
        }
        if (!a->anchor_valid || a->anchor_generation!=reservation.ticket.key.generation) {
            xr::Posef leveled{};
            if(!level_tracking_anchor(reservation.ticket.frame.head_center,&leveled)){
                a->coordinator.cancel_scene_reservation(reservation.ticket.key,reservation.serial);
                lock.unlock();return report_weapon_reservation_refused(a,producer,"level_tracking_anchor",native);
            }
            a->anchor=leveled;a->anchor_valid=true;a->anchor_generation=reservation.ticket.key.generation;
            trace("t5_tracking_anchor","generation=%llu xr_frame=%llu mode=level_yaw_only position=%.5f,%.5f,%.5f pitch_roll_persisted=0 near_vertical_guard=1",
                reservation.ticket.key.generation,reservation.ticket.key.frame_id,a->anchor.position.x,a->anchor.position.y,a->anchor.position.z);
        }
        if(claim&&exact_profile_aim_enabled()&&!command_profile_admission_witness(claim->command_admission)){
            a->coordinator.cancel_scene_reservation(reservation.ticket.key,reservation.serial);lock.unlock();return report_weapon_reservation_refused(a,producer,"command_witness_after_reserve",native);
        }
        a->weapon_scene={true,reservation,producer,a->anchor};
        if(claim)a->weapon_scene.borrowed={true,false,*claim,0};
        a->weapon_scene.hidden_primary_weapon=hidden_primary_weapon;
        xr::FrameState camera_frame{};scene_camera_locked(*a,producer.stock_view,reservation.ticket.frame,a->anchor,
            producer.refdef_identity,reservation.ticket.key.generation,&camera_frame);
        a->weapon_scene.camera_head=camera_frame.head_center;a->weapon_scene.camera_frozen=true;
        output->identity={reservation.ticket.key.generation,reservation.ticket.key.frame_id,reservation.serial,native.build_serial};
        output->prediction=reservation.ticket.frame;output->tracking_anchor=a->anchor;
        output->anchor_generation=a->anchor_generation;output->published_ms=reservation.ticket.published_ms;
        output->exclusively_reserved_for_scene=true;
        output->pimax_free_glove_grip_basis=a->pimax_free_glove_grip_basis.load();
        xr::Vec3f translation{},raw_head{},camera_head{};
        if(!input::room_scale::camera_translation(producer.stock_view.origin,producer.stock_view.axis,a->anchor,
            reservation.ticket.frame.head_center,camera_frame.head_center,&translation,&raw_head,&camera_head)){
            a->coordinator.cancel_scene_reservation(reservation.ticket.key,reservation.serial);a->weapon_scene.active=false;
            lock.unlock();return report_weapon_reservation_refused(a,producer,"camera_translation",native);
        }
        if(a->movement_pose_correction)output->camera_translation=translation;
        if(a->movement_pose_diagnostic&&a->movement_pose_reports<2000){
            const auto right=reservation.ticket.frame.actions.hands[1].grip.pose.position;
            const auto left=reservation.ticket.frame.actions.hands[0].grip.pose.position;
            trace("t5_movement_pose","report=%u reservation=%llu generation=%llu xr_frame=%llu command=%d native_time=%d correction=%u body=%.6f,%.6f,%.6f H=%.6f,%.6f,%.6f C=%.6f,%.6f,%.6f delta=%.6f,%.6f,%.6f raw_head=%.6f,%.6f,%.6f raw_right=%.6f,%.6f,%.6f raw_left=%.6f,%.6f,%.6f raw_anchor=%.6f,%.6f,%.6f camera_frozen=1 tracked_actions_unchanged=1",
                ++a->movement_pose_reports,reservation.serial,reservation.ticket.key.generation,reservation.ticket.key.frame_id,
                producer.command_number,producer.native_time,a->movement_pose_correction,
                producer.stock_view.origin.x,producer.stock_view.origin.y,producer.stock_view.origin.z,
                raw_head.x,raw_head.y,raw_head.z,camera_head.x,camera_head.y,camera_head.z,
                translation.x,translation.y,translation.z,reservation.ticket.frame.head_center.position.x,
                reservation.ticket.frame.head_center.position.y,reservation.ticket.frame.head_center.position.z,
                right.x,right.y,right.z,left.x,left.y,left.z,a->anchor.position.x,a->anchor.position.y,a->anchor.position.z);
        }
        body->identity=output->identity;body->stock_camera={producer.stock_view.origin,producer.stock_view.axis};
        body->source=input::BodyCameraSource::completed_native_refdef;
        if (reservation.serial<=12 || reservation.serial%120==0)
            trace("t5_weapon_scene_reserved","reservation=%llu build=%llu generation=%llu xr_frame=%llu published_ms=%llu predicted_time=%lld action_sequence=%llu buffer=%p native_frame=%lu native_thread=%lu call=%llu hidden_primary_weapon=%u game_writes=0",
                  reservation.serial,native.build_serial,reservation.ticket.key.generation,reservation.ticket.key.frame_id,
                  reservation.ticket.published_ms,reservation.ticket.frame.predicted_display_time,reservation.ticket.frame.actions.sequence,
                  before.address,before.frame,GetCurrentThreadId(),producer.call_serial,hidden_primary_weapon);
        return true;
    } catch (...) {return report_weapon_reservation_refused(a,producer,"exception");}
}
bool reserve_weapon_scene_frame(const NativeWeaponContext& producer,input::ReservedWeaponFrame* output,input::WeaponBodySample* body) noexcept {
    return reserve_weapon_scene_common(producer,output,body,nullptr);
}
bool reserve_borrowed_weapon_scene_frame(const NativeWeaponContext& producer,const BorrowedWeaponClaim& claim,
    input::ReservedWeaponFrame* output,input::WeaponBodySample* body) noexcept {
    return reserve_weapon_scene_common(producer,output,body,&claim);
}
bool reserve_hidden_primary_weapon_scene_frame(const NativeWeaponContext& producer) noexcept {
    input::ReservedWeaponFrame reserved{};input::WeaponBodySample body{};
    return reserve_weapon_scene_common(producer,&reserved,&body,nullptr,true);
}
bool mark_borrowed_weapon_scene_selected(const NativeWeaponContext& current,const input::WeaponFrameIdentity& identity,
    const BorrowedWeaponClaim& claim) noexcept {
    auto* a=adapter.load();
    if(!weapon_borrowed_placement_enabled()||!a||!a->borrowed_weapon_reservations_enabled||!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
       a->device_lost.load()||GetCurrentThreadId()!=current.native_thread)return false;
    if(exact_profile_aim_enabled()&&!command_profile_admission_current(claim.command_admission))return false;
    try{
        const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot command{};
        if(!valid_borrowed_claim(claim,GetTickCount64())||!borrowed_claim_matches_context(claim,current)||
           !scope.valid||scope.depth!=1||scope.token!=claim.wrapper_token||pose_clear_serial()!=claim.camera_serial||
           !read_pre_command_snapshot(&command)||!primary_weapon_active(command)||command.gameplay_epoch!=claim.key.command_epoch||command.sequence!=claim.key.command_sequence)return false;
        Buffer buffer{};NativeSceneSample scene{};
        if(!read_buffer(*a,true,&buffer)||buffer.count||buffer.parms_count||
           !read_native_scene(*a,reinterpret_cast<void*>(current.refdef_identity),&scene)||!matches_weapon_scene(current,scene))return false;
        std::lock_guard lock(a->metadata);auto& t=a->weapon_scene;const auto& r=t.reservation;
        if(exact_profile_aim_enabled()&&!command_profile_admission_witness(claim.command_admission))return false;
        if(!t.active||!t.borrowed.active||t.borrowed.completed||t.borrowed.selected||t.borrowed.claim!=claim||
           r.serial!=identity.reservation_serial||r.native.build_serial!=identity.native_build_serial||
           r.ticket.key!=vr::FrameKey{identity.generation,identity.frame_id}||!matches_weapon_scene(t.producer,scene)||
           !usable_weapon_scene_publication(r.ticket,GetTickCount64(),input::kMaximumWeaponPredictionAgeMs)||
           !a->coordinator.validate_scene_reservation(r,{reinterpret_cast<std::uintptr_t>(buffer.address),buffer.frame,identity.native_build_serial}))return false;
        t.borrowed.selected=true;t.borrowed.world_verified=false;return true;
    }catch(...){return false;}
}
bool complete_borrowed_weapon_scene_frame(const NativeWeaponContext& current,const input::WeaponFrameIdentity& identity,
    const BorrowedWeaponClaim& claim,std::uint64_t normal_serial,bool world_verified) noexcept {
    auto* a=adapter.load();
    if(!a||!a->borrowed_weapon_reservations_enabled||!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
       a->device_lost.load()||GetCurrentThreadId()!=current.native_thread)return false;
    if(exact_profile_aim_enabled()&&!command_profile_admission_current(claim.command_admission))return false;
    try{
        const auto scope=read_weapon_native_clear_scope();PreCommandSnapshot command{};
        if(!valid_borrowed_claim(claim,GetTickCount64())||!borrowed_claim_matches_context(claim,current)||
           !scope.valid||scope.depth!=1||scope.token!=claim.wrapper_token||normal_serial!=claim.camera_serial+1||pose_clear_serial()!=normal_serial||
           !read_pre_command_snapshot(&command)||!primary_weapon_active(command)||command.gameplay_epoch!=claim.key.command_epoch||command.sequence!=claim.key.command_sequence)return false;
        Buffer buffer{};NativeSceneSample scene{};
        if(!read_buffer(*a,true,&buffer)||buffer.count||buffer.parms_count||
           !read_native_scene(*a,reinterpret_cast<void*>(current.refdef_identity),&scene)||!matches_weapon_scene(current,scene))return false;
        std::lock_guard lock(a->metadata);auto& t=a->weapon_scene;const auto& r=t.reservation;
        if(exact_profile_aim_enabled()&&!command_profile_admission_witness(claim.command_admission))return false;
        if(!t.active||!t.borrowed.active||t.borrowed.completed||t.borrowed.claim!=claim||
           r.serial!=identity.reservation_serial||r.native.build_serial!=identity.native_build_serial||
           r.ticket.key!=vr::FrameKey{identity.generation,identity.frame_id}||!matches_weapon_scene(t.producer,scene)||
           !usable_weapon_scene_publication(r.ticket,GetTickCount64(),input::kMaximumWeaponPredictionAgeMs)||
           !a->coordinator.validate_scene_reservation(r,{reinterpret_cast<std::uintptr_t>(buffer.address),buffer.frame,identity.native_build_serial}))return false;
        if(t.borrowed.selected&&!world_verified)return false;
        t.borrowed.normal_serial=normal_serial;t.borrowed.world_verified=t.borrowed.selected&&world_verified;t.borrowed.completed=true;return true;
    }catch(...){return false;}
}
void cancel_weapon_scene_frame(const input::WeaponFrameIdentity& identity) noexcept {
    auto* a=adapter.load();if (!a) return;
    try {
        std::lock_guard lock(a->metadata);
        const auto& reserved=a->weapon_scene.reservation;
        if (reserved.native.build_serial!=identity.native_build_serial || reserved.serial!=identity.reservation_serial ||
            reserved.ticket.key!=vr::FrameKey{identity.generation,identity.frame_id}) return;
        if (a->coordinator.cancel_scene_reservation({identity.generation,identity.frame_id},identity.reservation_serial)) {
            if (++a->weapon_scene_cancellations<=12 || a->weapon_scene_cancellations%120==0)
                trace("t5_weapon_scene_cancelled","count=%llu reservation=%llu build=%llu generation=%llu xr_frame=%llu game_writes=0",
                      a->weapon_scene_cancellations,identity.reservation_serial,identity.native_build_serial,identity.generation,identity.frame_id);
        }
    } catch (...) {}
}
bool validate_weapon_scene_reservation(const NativeWeaponContext& current,const input::WeaponFrameIdentity& identity) noexcept {
    auto* a=adapter.load();
    if (!a||!a->weapon_reservations_enabled||!a->initialized.load()||(a->stop_requested.load() || a->replacement_pending.load())||a->device_lost.load()||
        current.native_thread!=GetCurrentThreadId()) return false;
    try {
        Buffer buffer{};NativeSceneSample scene{};
        if (!read_buffer(*a,true,&buffer)||buffer.count||buffer.parms_count||
            !read_native_scene(*a,reinterpret_cast<void*>(current.refdef_identity),&scene)||!matches_weapon_scene(current,scene)) return false;
        std::lock_guard lock(a->metadata);
        const auto& transaction=a->weapon_scene;const auto& r=transaction.reservation;const auto& original=transaction.producer;
        WeaponLifetime lifetime{};
        const auto now=GetTickCount64();
        if (!transaction.active||r.serial!=identity.reservation_serial||r.native.build_serial!=identity.native_build_serial||
            r.ticket.key!=vr::FrameKey{identity.generation,identity.frame_id}||now<r.ticket.published_ms||
            now-r.ticket.published_ms>input::kMaximumWeaponPredictionAgeMs||!matches_weapon_scene(original,scene)||
            !current.valid||!current.body_valid||!primary_weapon_context(current)||current.weapon_index!=original.weapon_index||
            current.viewmodel_identity!=original.viewmodel_identity||current.dobj_identity!=original.dobj_identity||
            current.variant_identity!=original.variant_identity||current.definition_identity!=original.definition_identity||
            current.call_serial!=original.call_serial||current.command_clock!=original.command_clock||
            !current.model_lifetime_verified||!original.model_lifetime_verified||!current.model_generation||
            current.model_generation!=original.model_generation||
            !read_weapon_lifetime(current.viewmodel_identity,current.dobj_identity,&lifetime)||!lifetime.verified||
            lifetime.generation!=current.model_generation) return false;
        return a->coordinator.validate_scene_reservation(r,{reinterpret_cast<std::uintptr_t>(buffer.address),buffer.frame,identity.native_build_serial});
    } catch (...) {return false;}
}
bool weapon_integration_renderer_ready() noexcept {
    const auto* a=adapter.load();return a&&a->bloom_correction_enabled&&a->history_correction_enabled&&a->auxiliary_enabled&&a->menu_enabled;
}
bool read_vr_input_snapshot(xr::ActionSnapshot* actions,std::uint64_t* generation) noexcept {
    if (actions) *actions={};
    if (generation) *generation=0;
    auto* a=adapter.load();
    if (!a || !actions || !generation) return false;
    try {
        std::lock_guard lock(a->metadata);
        if (!a->initialized.load() || (a->stop_requested.load() || a->replacement_pending.load()) || a->stopped.load() || a->device_lost.load()) return false;
        return a->latest_actions.read(GetTickCount64(),actions,generation);
    } catch (...) { return false; }
}
bool read_vr_command_input_snapshot(xr::ActionSnapshot* actions,std::uint64_t* generation,
                                   input::CommandRigSample* rig) noexcept {
    if(actions)*actions={};if(generation)*generation=0;if(rig)*rig={};
    auto* a=adapter.load();
    if(!a||!actions||!generation||!rig)return false;
    try {
        std::lock_guard lock(a->metadata);
        if(!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
           a->stopped.load()||a->device_lost.load())return false;
        const auto now=GetTickCount64();
        if(!a->latest_actions.read(now,actions,generation))return false;
        if(a->anchor_valid)a->latest_rig.read(now,*actions,*generation,a->anchor,a->anchor_generation,rig);
        return true;
    }catch(...){*actions={};*generation=0;*rig={};return false;}
}
bool command_input_generation_current(const input::CommandRigSample& original) noexcept {
    auto* a=adapter.load();if(!a||!original.xr_generation)return false;
    try {
        std::lock_guard lock(a->metadata);
        return a->initialized.load()&&!a->stop_requested.load()&&!a->replacement_pending.load()&&
            !a->stopped.load()&&!a->device_lost.load()&&a->anchor_valid&&
            a->anchor_generation==original.xr_generation&&original.anchor_generation==original.xr_generation&&
            !memcmp(&a->anchor,&original.tracking_anchor,sizeof(a->anchor));
    }catch(...){return false;}
}
bool consumed_command_input_generation_current(const input::CommandRigSample& original) noexcept {
    auto* a=adapter.load();if(!a||!original.xr_generation)return false;
    try {
        std::lock_guard lock(a->metadata);
        return a->initialized.load()&&!a->stop_requested.load()&&!a->replacement_pending.load()&&
            !a->stopped.load()&&!a->device_lost.load()&&a->anchor_valid&&
            a->anchor_generation==original.xr_generation&&original.anchor_generation==original.xr_generation;
    }catch(...){return false;}
}
bool commit_vr_body_yaw(const input::CommandRigSample& original,
                        const xr::Posef& desired_anchor,
                        const float expected_native_yaw,const float desired_native_yaw) noexcept {
    auto* a=adapter.load();
    const auto finite=[](float v){return std::isfinite(v);};
    const auto unit=[&](const xr::Quaternionf& q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        return finite(n)&&n>.98f&&n<1.02f;};
    if(!a||!original.xr_generation||!original.frame.frame_id||!original.frame.actions.sequence||
       !finite(expected_native_yaw)||!finite(desired_native_yaw)||!unit(desired_anchor.orientation)||
       !finite(desired_anchor.position.x)||!finite(desired_anchor.position.y)||!finite(desired_anchor.position.z)||
       std::fabs(desired_anchor.orientation.x)>.0001f||std::fabs(desired_anchor.orientation.z)>.0001f||
       memcmp(&desired_anchor.position,&original.tracking_anchor.position,sizeof(desired_anchor.position)))return false;
    try {
        std::lock_guard lock(a->metadata);
        if(!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
           a->stopped.load()||a->device_lost.load()||!a->anchor_valid||
           a->anchor_generation!=original.xr_generation||original.anchor_generation!=original.xr_generation||
           memcmp(&a->anchor,&original.tracking_anchor,sizeof(a->anchor))||a->weapon_scene.active)return false;
        xr::ActionSnapshot actions{};std::uint64_t generation{};input::CommandRigSample current{};
        const auto now=GetTickCount64();
        if(!a->latest_actions.read(now,&actions,&generation)||generation!=original.xr_generation||
           !a->latest_rig.read(now,actions,generation,a->anchor,a->anchor_generation,&current)||
           current.xr_generation!=original.xr_generation||current.anchor_generation!=original.anchor_generation||
           current.published_ms!=original.published_ms||current.frame.frame_id!=original.frame.frame_id||
           current.frame.actions.sequence!=original.frame.actions.sequence)return false;
        constexpr std::uintptr_t native_yaw_rva=0x2511e24;
        if(!commit_native_yaw(a->base+native_yaw_rva,expected_native_yaw,desired_native_yaw))return false;
        a->anchor=desired_anchor;
        return true;
    }catch(...){return false;}
}
bool commit_vr_room_scale(const input::CommandRigSample& original,const xr::Posef& desired,
                         const RoomScaleView& view) noexcept {
    auto* a=adapter.load();
    if(!a||!view.cg||!view.epoch||!input::room_scale::finite(view.eye)||
       !std::isfinite(view.camera_radius)||view.camera_radius<0||view.camera_radius>input::room_scale::maximum_camera_radius||
       !input::room_scale::horizontal_anchor_transfer_allowed(original.tracking_anchor,
           original.frame.head_center,desired,view.horizontal_discontinuity))return false;
    try {
        std::lock_guard lock(a->metadata);
        if(!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
           a->stopped.load()||a->device_lost.load()||!a->anchor_valid||a->weapon_scene.active||
           a->anchor_generation!=original.xr_generation||original.anchor_generation!=original.xr_generation||
           memcmp(&a->anchor,&original.tracking_anchor,sizeof(a->anchor)))return false;
        xr::ActionSnapshot actions{};std::uint64_t generation{};input::CommandRigSample current{};
        const auto now=GetTickCount64();
        if(!a->latest_actions.read(now,&actions,&generation)||generation!=original.xr_generation||
           !actions.focused||(actions.menu.active&&actions.menu.current)||
           !a->latest_rig.read(now,actions,generation,a->anchor,a->anchor_generation,&current)||
           current.published_ms!=original.published_ms||current.frame.frame_id!=original.frame.frame_id||
           current.frame.actions.sequence!=original.frame.actions.sequence)return false;
        a->anchor=desired;
        a->room_scale={view,desired,generation,now};
        return true;
    }catch(...){return false;}
}
void revoke_room_scale_view() noexcept {
    auto* a=adapter.load();if(!a)return;
    try{std::lock_guard lock(a->metadata);a->room_scale={};}catch(...){}
}
bool read_helicopter_scene_view(HelicopterSceneView* out) noexcept {
    if(!out)return false;*out={};auto* a=adapter.load();if(!a)return false;
    try {
        std::lock_guard lock(a->metadata);
        if(!a->initialized.load()||a->stop_requested.load()||a->replacement_pending.load()||
           a->stopped.load()||a->device_lost.load()||!a->anchor_valid)return false;
        const auto now=GetTickCount64();const auto& v=a->helicopter_view;
        xr::ActionSnapshot actions{};std::uint64_t generation{};
        if(!v.epoch||!v.rig.published_ms||now<v.rig.published_ms||now-v.rig.published_ms>100||
           !a->latest_actions.read(now,&actions,&generation)||!actions.focused||
           (actions.menu.active&&actions.menu.current)||generation!=v.rig.xr_generation||
           a->anchor_generation!=generation||memcmp(&a->anchor,&v.rig.tracking_anchor,sizeof(a->anchor)))return false;
        // An old rendered pose cannot survive a newly reported head-tracking
        // failure, even while its original publication age remains bounded.
        input::CommandRigSample current{};
        if(!a->latest_rig.read(now,actions,generation,a->anchor,a->anchor_generation,&current)||
           !current.frame.views_valid||!current.frame.should_render)return false;
        *out=v;return true;
    }catch(...){return false;}
}
}
