# BO-owned frame coordinator and OpenXR transport

This directory is the new BO host-side lifecycle/metadata layer. It has no BO
executable addresses, game structures, rendering callbacks or startup hook.
Link it to the exact `BlackOpsVR::XR` core validated in
`BlackOpsVR/prototypes/vr-core`; original WaW XR source/namespaces and license
notices are retained there. These new files are GPL-3.0-only.

Instantiate `OpenXrTransport transport` and `FrameCoordinator coordinator(transport)`
on the chosen serialized render/XR owner. Initialize with BO application names
and a logging callback only after the BO adapter explicitly enables VR.

The source-engine adapter calls `begin_frame()` once to publish a `FrameTicket`.
Its scene builder calls `claim_render_frame()` once, clones the BO native view,
and records the packed source layout with `stage_render_frame(key, backend, layout)`.
`BackendFrameIdentity` contains the actual native backend buffer address and a
nonzero producer serial which advances whenever the engine reuses that buffer.
Store the prediction key and this stamp beside the native queued work. After
each original backend eye draw returns, call `mark_eye_complete(key, backend, eye)`.
Both the XR generation/frame key and native buffer/producer serial are immutable
and must match for both eye completions. The coordinator retains its completion
record until Present, even after the engine clears its current backend pointer;
`PresentResult` reports both identities plus the separate bridge capture serial.
Missing stamps and stale/reused backend buffers cannot qualify. Merely seeing
two front-end enqueue calls or two Present calls is not completion evidence.

Immediately before the selected native Present, call `present(borrowed_device)`.
An incomplete, unstaged, duplicate-eye, or nonrendering frame ends with zero
layers. A complete pair retires bridge work, captures the actual back buffer,
acquires that exact capture serial, verifies its captured poses, composites
and releases both eye swapchains, submits the producing poses, and releases
the shared token. The borrowed device must actually be D3D9Ex on the runtime's
D3D11 adapter. This initial exact-serial path uses the inherited bounded wait;
it is not yet the later asynchronously tuned BO performance path.

Metadata methods are mutex-protected and permit backend threads; no metadata
lock spans a graphics/runtime call. OpenXR/transport lifecycle calls reject a
different owner thread. A second begin cannot advance while a frame is pending;
a second Present cannot submit the same frame twice.
Hard runtime begin/end failures block subsequent predictions until shutdown and
explicit reinitialization; a D3D reset alone cannot clear an XR lifecycle fault.
Predictions apply the BO `InputFocusGate`: unfocused or non-fresh raw action
samples publish inactive/zero buttons and analog controls while retaining head,
eye and controller tracking for graphics. Focus recovery requires a fresh action
sequence, and session recreation resets the sequence gate.
Transport exceptions are contained at this boundary, release the metadata busy
guard and keep the lifecycle blocked/reachable for explicit cleanup. Host
callbacks should not throw. These checks do not catch native access violations.

For a healthy reset, `before_reset(false)` must succeed **before** calling the
native Reset/ResetEx, then report its actual success to `after_reset(succeeded)`.
For an already lost Ex device use `before_reset(true)`. Failed preflight defers
the real reset; failed reset keeps new frames blocked. A later successful
lost-device route retires its old generation. `shutdown()` can return false
while GPU work remains: retry retirement and preserve the device/runtime.

Caller responsibilities still include one native simulation update, actual
two-eye BO camera/render behavior, identical producing frame ownership through
the native backend, correct Ex creation/resources/reset routing, input mapping,
runtime restart policy after failures, and physical headset acceptance. This
library is not evidence that BO stereo rendering is implemented.

`frame_coordinator_tests.cpp` injects a transport to test incomplete/duplicate
and late-eye rejection, once-only lifecycle, generation invalidation, failed
reset/shutdown retirement, backend metadata concurrency, owner-thread rejection,
and mandatory zero-layer termination for nonrendering frames. The separate
standalone simulator probe exercises the real underlying XR/graphics core.

The explicitly launched `bovr-coordinator-simulator-probe` target now exercises
this exact coordinator/transport, including capture before native Present,
per-frame eye markers read back through an optional capture validator, and
healthy ResetEx recovery. It is built with the mod and launched by the prototype
runner's `-CoordinatorTest` switch. It is not registered as an unattended test.
The optional capture validator is owner-thread diagnostic code, installed only
before initialization; it borrows the source only until return and rejection
ends the frame with zero layers.
