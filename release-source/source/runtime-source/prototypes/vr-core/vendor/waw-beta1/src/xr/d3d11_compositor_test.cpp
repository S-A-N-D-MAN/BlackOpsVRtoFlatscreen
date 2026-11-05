// SPDX-License-Identifier: GPL-3.0-only

#include "d3d11_compositor.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{

int failures = 0;

void Expect(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool Near(const float left, const float right)
{
    return std::abs(left - right) < 0.000001F;
}

void TestPackedEyesHaveDisjointBilinearBounds()
{
    using wawvr::xr::compositor_detail::BuildEyeLocalBloomSamplingRegion;
    using wawvr::xr::compositor_detail::EyeLocalBloomSamplingRegion;

    EyeLocalBloomSamplingRegion left = {};
    EyeLocalBloomSamplingRegion right = {};
    Expect(BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &left),
           "left packed-eye bloom bounds build");
    Expect(BuildEyeLocalBloomSamplingRegion(
               {0.5F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &right),
           "right packed-eye bloom bounds build");

    const float half_texel_u = 0.5F / 2560.0F;
    const float half_texel_v = 0.5F / 1440.0F;
    Expect(Near(left.min_u, half_texel_u) &&
               Near(left.max_u, 0.5F - half_texel_u),
           "left taps remain half a texel inside the left crop");
    Expect(Near(right.min_u, 0.5F + half_texel_u) &&
               Near(right.max_u, 1.0F - half_texel_u),
           "right taps remain half a texel inside the right crop");
    Expect(left.max_u < 0.5F && right.min_u > 0.5F &&
               left.max_u < right.min_u,
           "bilinear footprints cannot cross the packed-eye seam");
    Expect(Near(left.min_v, half_texel_v) &&
               Near(left.max_v, 1.0F - half_texel_v) &&
               Near(right.min_v, left.min_v) &&
               Near(right.max_v, left.max_v),
           "vertical taps remain inside both complete-height crops");
}

void TestInvalidSamplingRegionsFailClosed()
{
    using wawvr::xr::compositor_detail::BuildEyeLocalBloomSamplingRegion;
    using wawvr::xr::compositor_detail::EyeLocalBloomSamplingRegion;

    EyeLocalBloomSamplingRegion region = {};
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 0, 1440, &region),
           "zero-width source rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 2560, 1440, nullptr),
           "null bloom-region output fails closed");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.75F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &region),
           "out-of-texture crop rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.25F / 2560.0F, 1.0F}, 2560, 1440,
               &region),
           "sub-texel crop rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.5F, 1.0F},
               2560, 1440, &region),
           "non-finite crop rejects bloom");
}

void TestBloomRequiresExplicitCompleteOptIn()
{
    using wawvr::xr::CompositorEffects;
    using wawvr::xr::compositor_detail::ShouldUseEyeLocalBloom;

    const CompositorEffects default_effects = {};
    Expect(!ShouldUseEyeLocalBloom(default_effects, true, true),
           "default compositor behavior remains the exact copy path");

    CompositorEffects enabled = {};
    enabled.enable_eye_local_bloom = true;
    Expect(!ShouldUseEyeLocalBloom(enabled, false, true),
           "missing optional shader falls back to the copy path");
    Expect(!ShouldUseEyeLocalBloom(enabled, true, false),
           "invalid eye-local bounds fall back to the copy path");
    Expect(ShouldUseEyeLocalBloom(enabled, true, true),
           "explicit opt-in uses bloom only with a complete safe path");
}

void TestExactOptionalBloomShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::EyeLocalBloomShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_eye_local_bloom.hlsl", nullptr,
        nullptr, "PSBloom", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer())
                  << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "the exact optional eye-local bloom shader compiles for ps_4_0");
}

wawvr::xr::FrameState BuildBinocularScopeFrame()
{
    wawvr::xr::FrameState frame = {};
    frame.views_valid = true;
    for (std::uint32_t eye = 0; eye < wawvr::xr::kEyeCount; ++eye)
    {
        frame.eyes[eye].pose.position.x =
            eye == 0 ? -0.032F : 0.032F;
        frame.eyes[eye].fov = {
            -0.78F,
            0.78F,
            0.78F,
            -0.78F,
        };
    }
    return frame;
}

wawvr::xr::PhysicalScopeComposition BuildVisibleScope()
{
    wawvr::xr::PhysicalScopeComposition scope = {};
    scope.active = true;
    scope.source = {0.8F, 0.0F, 0.2F, 1.0F};
    scope.lens_origin_reference_meters = {0.0F, 0.0F, -0.35F};
    scope.lens_right_reference = {1.0F, 0.0F, 0.0F};
    scope.lens_up_reference = {0.0F, 1.0F, 0.0F};
    scope.lens_radius_meters = 0.032F;
    return scope;
}

void TestPhysicalScopeRequiresBinocularlyValidGeometry()
{
    using wawvr::xr::compositor_detail::CanCompositePhysicalScopeBinocularly;

    const wawvr::xr::PhysicalScopeComposition scope = BuildVisibleScope();
    wawvr::xr::FrameState frame = BuildBinocularScopeFrame();
    Expect(CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "a valid lens pose enables the physical optic binocularly");

    frame.eyes[1].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "one invalid eye disables the optional optic for both eyes");

    wawvr::xr::PhysicalScopeComposition inactive = scope;
    inactive.active = false;
    Expect(!CanCompositePhysicalScopeBinocularly(
               inactive, BuildBinocularScopeFrame(), 3200, 1600),
           "an inactive scope remains on the base-stereo path");
}

void TestExactPhysicalScopeShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::PhysicalScopeShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_physical_scope.hlsl", nullptr,
        nullptr, "PSScope", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer())
                  << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "the exact COD4-style physical scope shader compiles for ps_4_0");
}

void TestPhysicalScopeAcceptsSmallAuthoredOpticCrop()
{
    using wawvr::xr::compositor_detail::CanCompositePhysicalScopeBinocularly;
    const wawvr::xr::FrameState frame = BuildBinocularScopeFrame();
    wawvr::xr::PhysicalScopeComposition scope = BuildVisibleScope();
    // Rebirth Enfield IR: measured rear lens radius and observed normalized
    // source crop. At simulator resolution this is only 2.56 x 2.844 pixels.
    scope.lens_radius_meters = 0.0042F;
    scope.source = {0.749F, 0.498F, 0.002F, 0.00395F};
    Expect(CanCompositePhysicalScopeBinocularly(scope, frame, 1280, 720),
           "the measured Enfield IR lens and crop compose at simulator resolution");
    Expect(CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "the same authored Enfield IR lens composes at headset resolution");

    scope.source.width = 2.0F / 1280.0F;
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 1280, 720),
           "a crop without horizontal room after one-pixel safety margins rejects");
    scope.source.width = 0.002F;
    scope.source.height = 2.0F / 720.0F;
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 1280, 720),
           "a crop without vertical room after one-pixel safety margins rejects");
    scope.source.height = 0.0F;
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "a degenerate crop remains rejected with the adaptive inset");
    scope.source.height = 0.00395F;
    scope.source.x = 0.999F;
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "an out-of-bounds crop remains rejected with the adaptive inset");
}

wawvr::xr::StereoSourceLayout BuildVisibleWorldAimMarker()
{
    wawvr::xr::StereoSourceLayout layout = {};
    layout.world_aim_marker.active = true;
    layout.world_aim_marker.origin_reference_meters = {0.0F, 0.0F, -5.0F};
    return layout;
}

void TestWorldAimMarkerHasPhysicalStereoDepth()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    const FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle near_marker[kEyeCount] = {};
    CompositorReticle far_marker[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, near_marker),
           "world aim marker projects into both eyes");
    Expect(near_marker[0].visible && near_marker[1].visible &&
               near_marker[0].target_eye == 0 && near_marker[1].target_eye == 1,
           "world marker has two explicit eye outputs, not a menu single eye");
    Expect(near_marker[0].u > 0.5F && near_marker[1].u < 0.5F &&
               Near(near_marker[0].v, 0.5F) && Near(near_marker[1].v, 0.5F),
           "one physical target has binocular disparity rather than head lock");

    layout.world_aim_marker.origin_reference_meters.z = -50.0F;
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, far_marker),
           "distant world aim marker projects");
    const float near_disparity = near_marker[0].u - near_marker[1].u;
    const float far_disparity = far_marker[0].u - far_marker[1].u;
    Expect(far_disparity > 0.0F &&
               std::abs(near_disparity - 10.0F * far_disparity) < 0.00001F,
           "marker disparity follows physical target distance");
}

void TestWorldAimMarkerTracksCapturedViewNotHeadCenter()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    const StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle baseline[kEyeCount] = {};
    CompositorReticle shifted[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, baseline),
           "baseline world marker projection succeeds");
    for (auto& eye : frame.eyes)
    {
        eye.pose.position.x += 0.20F;
        eye.pose.position.y += 0.15F;
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               shifted[0].u < baseline[0].u && shifted[1].u < baseline[1].u &&
               shifted[0].v > baseline[0].v && shifted[1].v > baseline[1].v,
           "head translation moves a fixed world marker through both images");

    source.rendered_views_valid = true;
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        source.rendered_eyes[eye] = BuildBinocularScopeFrame().eyes[eye];
        frame.eyes[eye].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               Near(shifted[0].u, baseline[0].u) &&
               Near(shifted[1].u, baseline[1].u),
           "capture-time eye poses override a newer render-worker head pose");

    constexpr float kHalfYaw = 0.10F;
    for (auto& eye : source.rendered_eyes)
    {
        eye.pose.orientation = {0.0F, std::sin(kHalfYaw), 0.0F,
                                std::cos(kHalfYaw)};
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               shifted[0].u > baseline[0].u && shifted[1].u > baseline[1].u,
           "captured head yaw changes marker screen position in both eyes");

    source.rendered_eyes[1].fov = {-0.60F, 0.90F, 0.70F, -0.85F};
    CompositorReticle asymmetric[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, asymmetric) &&
               Near(asymmetric[0].u, shifted[0].u) &&
               !Near(asymmetric[1].u, shifted[1].u),
           "world marker uses each captured asymmetric eye FOV independently");
}

void TestWorldAimMarkerFailsClosedBinocularly()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle markers[kEyeCount] = {};
    auto rejected = [&]()
    {
        markers[0].visible = markers[1].visible = true;
        return !BuildWorldAimMarkerProjections(
                   layout, frame, source, nullptr, markers) &&
               !markers[0].visible && !markers[1].visible;
    };
    layout.world_aim_marker.origin_reference_meters.z = 5.0F;
    Expect(rejected(), "behind-head marker hides for both eyes");
    layout = BuildVisibleWorldAimMarker();
    layout.world_aim_marker.origin_reference_meters.x = 100.0F;
    Expect(rejected(), "offscreen marker hides instead of clamping to an edge");
    layout = BuildVisibleWorldAimMarker();
    frame.eyes[1].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    Expect(rejected(), "one behind-eye projection hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    frame.eyes[1].fov.angle_right = std::numeric_limits<float>::quiet_NaN();
    Expect(rejected(), "one non-finite FOV hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    frame.eyes[1].pose.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    Expect(rejected(), "invalid eye orientation hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    layout.world_aim_marker.origin_reference_meters.x =
        std::numeric_limits<float>::infinity();
    Expect(rejected(), "non-finite target hides both marker outputs");
    layout = BuildVisibleWorldAimMarker();
    frame.views_valid = false;
    Expect(rejected(), "invalid frame views hide both marker outputs");
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, nullptr),
           "null projection output fails closed");
}

void TestWorldAimMarkerDoesNotReplaceMenusOrScopes()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = {};
    const FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle markers[kEyeCount] = {};
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "default layout does not add a crosshair to other gameplay");
    layout = BuildVisibleWorldAimMarker();
    layout.preserve_source_aspect = true;
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "mono menu and cinematic layouts never show a world marker");
    layout = BuildVisibleWorldAimMarker();
    layout.physical_scope = BuildVisibleScope();
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "physical scopes retain their dedicated optic crosshair");
    Expect(compositor_detail::CanCompositePhysicalScopeBinocularly(
               layout.physical_scope, frame, 3200, 1600),
           "world-marker suppression does not alter physical scope geometry");
    layout = BuildVisibleWorldAimMarker();
    CompositorReticle menu = {true, 1, 0.25F, 0.75F};
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, &menu, markers) && menu.visible &&
               menu.target_eye == 1 && Near(menu.u, 0.25F) && Near(menu.v, 0.75F),
           "visible menu pointer suppresses the marker without changing menu UV");
}

void TestExactWorldAimMarkerShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::WorldAimMarkerShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_world_aim_marker.hlsl", nullptr,
        nullptr, "PSWorldAimMarker", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "exact outlined binocular world-marker shader compiles for ps_4_0");
}

void TestSeparateScopeSourceRequiresCurrentNativeImage()
{
    using namespace wawvr::xr;
    using compositor_detail::SelectPhysicalScopeSource;
    auto frame = BuildBinocularScopeFrame();
    frame.frame_id = 41;
    frame.predicted_display_time = 123456789;
    frame.should_render = frame.views_valid = frame.actions.focused = true;
    auto scope = BuildVisibleScope();
    D3D11SourceFrame base{};
    D3D11SourceFrame auxiliary{};
    auxiliary.texture = reinterpret_cast<ID3D11Texture2D*>(1);
    auxiliary.view = reinterpret_cast<ID3D11ShaderResourceView*>(2);
    auxiliary.width = 1280; auxiliary.height = 720;
    auxiliary.serial = 7;
    auxiliary.rendered_views_valid = true;
    auxiliary.rendered_frame_id = frame.frame_id;
    auxiliary.rendered_display_time = frame.predicted_display_time;
    std::memcpy(auxiliary.rendered_eyes, frame.eyes, sizeof(frame.eyes));
    Expect(SelectPhysicalScopeSource(scope, frame, base, &auxiliary) == &base,
           "ordinary scope keeps base source even when an auxiliary image exists");
    scope.use_separate_source = true;
    Expect(SelectPhysicalScopeSource(scope, frame, base, &auxiliary) == &auxiliary,
           "thermal scope uses its independently captured exact-frame source");
    Expect(SelectPhysicalScopeSource(scope, frame, base, nullptr) == nullptr,
           "missing native thermal image never falls back to ordinary stereo");
    auto reject = [&](D3D11SourceFrame candidate, const char* reason) {
        Expect(SelectPhysicalScopeSource(scope, frame, base, &candidate) == nullptr, reason);
    };
    auto bad = auxiliary; --bad.rendered_frame_id;
    reject(bad, "previous frame is rejected even when all eye poses are identical");
    bad = auxiliary; ++bad.rendered_display_time;
    reject(bad, "wrong predicted display time is rejected independently of frame id");
    bad = auxiliary; bad.rendered_eyes[1].pose.position.x += 0.01F;
    reject(bad, "one mismatched eye prevents thermal overlay in both eyes");
    bad = auxiliary; bad.rendered_views_valid = false;
    reject(bad, "invalid captured eye metadata is rejected");
    bad = auxiliary; bad.serial = 0;
    reject(bad, "unacquired auxiliary serial is rejected");
    bad = auxiliary; bad.texture = nullptr;
    reject(bad, "missing auxiliary texture cannot sample released pixels");
    bad = auxiliary; bad.view = nullptr;
    reject(bad, "missing auxiliary view is rejected");
    bad = auxiliary; bad.width = 0;
    reject(bad, "invalid auxiliary dimensions are rejected");
    frame.actions.focused = false;
    Expect(SelectPhysicalScopeSource(scope, frame, base, &auxiliary) == nullptr,
           "focus loss prevents reuse of an otherwise current thermal image");
    frame.actions.focused = true;
    scope.active = false;
    Expect(SelectPhysicalScopeSource(scope, frame, base, &auxiliary) == nullptr,
           "lowering the optic immediately stops auxiliary sampling");
    scope.active = true;
    auxiliary.rendered_frame_id = ++frame.frame_id;
    auxiliary.rendered_display_time = ++frame.predicted_display_time;
    Expect(SelectPhysicalScopeSource(scope, frame, base, &auxiliary) == &auxiliary,
           "raising the optic accepts a new current frame without reusing old ownership");
}

} // namespace

int main()
{
    TestSeparateScopeSourceRequiresCurrentNativeImage();
    TestPackedEyesHaveDisjointBilinearBounds();
    TestInvalidSamplingRegionsFailClosed();
    TestBloomRequiresExplicitCompleteOptIn();
    TestPhysicalScopeRequiresBinocularlyValidGeometry();
    TestPhysicalScopeAcceptsSmallAuthoredOpticCrop();
    TestExactOptionalBloomShaderCompiles();
    TestExactPhysicalScopeShaderCompiles();
    TestWorldAimMarkerHasPhysicalStereoDepth();
    TestWorldAimMarkerTracksCapturedViewNotHeadCenter();
    TestWorldAimMarkerFailsClosedBinocularly();
    TestWorldAimMarkerDoesNotReplaceMenusOrScopes();
    TestExactWorldAimMarkerShaderCompiles();
    if (failures != 0)
    {
        std::cerr << failures << " D3D11 compositor test(s) failed\n";
        return 1;
    }
    std::cout << "D3D11 compositor tests passed\n";
    return 0;
}
