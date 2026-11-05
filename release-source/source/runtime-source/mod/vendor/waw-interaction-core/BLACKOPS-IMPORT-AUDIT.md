# Black Ops portable interaction import

## Identity and bounds

The 34 files listed in `IMPORTED-FILES.json` are byte-for-byte copies from `phase1/references/WorldWarVR-beta1-matching-source.zip` (7,864,292 bytes, SHA-256 `bf8ec6110b4759c2c387c489bc8671bcbb137f4d6b14f4f7d454d8abd503046e`). Each manifest record identifies the original ZIP entry, destination, size, and SHA-256. The archive root is `WorldWarVR-v0.4.0-beta.1-Source/`. This is the source snapshot matching the accepted WaW beta.1 package; it is not represented as a clean upstream commit. The intentionally dirty original WaW and COD4 projects are unchanged.

The original GPL-3.0 license, provenance, and third-party notices are preserved unchanged. Their broader WaW project statements remain historical source attribution, not claims that the full WaW project was imported. This audit and the prototype build files are Black Ops-owned additions. Retain the license, notices, imported manifest, and corresponding source when distributing a binary incorporating these modules.

Only portable math and state logic was imported. No T4 native structure layout, pointer, address, hook, reload enum, weapon profile table, model calibration table, runtime loader, or native input mapping was imported. Some unchanged names/comments mention T4 and one button-setting constant retains a WaW name; these are not native bindings. The prototype calls no setting/environment reader. BO code should expose its own configuration names.

## Reusable modules and required Black Ops facts

| Imported module | Portable behavior available now | Native facts still required |
| --- | --- | --- |
| `gameplay/manual_reload_logic` | Profile and transaction state machine, interaction ownership, native-open/commit/cancel requests, visibility/suppression decisions. Profiles include native-only, detachable magazine, stripper clip, and single-round/tube. | Verified weapon profile; native reload ability and state edges; exact once-only native open/commit completion; authoritative ammo/chamber state; validated insertion zones and feed-device attachment. A returned commit request is not an ammo update. |
| `gameplay/tracked_attachment_math` | Captures a fixed local calibration and resolves it against the current tracked hand. Optional orientation guidance cannot replace the current hand position. | BO weapon/model/tag identity and calibration. Generic transforms can support an optical lens pose, but no lens profile, scope rendering, or sight alignment is included. |
| `mod/weapon_grip_logic` | Chest/right/left/two-hand ownership, grip hysteresis, tracking grace, explicit release, rearm after prolonged loss, weapon identity, and reserved interaction ownership. | Canonical dominant/support hand routing, physical-controller output mapping, BO weapon geometry, and native control suppression. Existing right-rear/left-front naming does not establish fully calibrated left-dominant two-hand geometry. |
| `mod/current_head_local_controller_pose_logic` | Same-sample head-local controller pose, grip translation with aim orientation, smoothing, finite/discontinuity/staleness checks, immutable repeated-sample output. | One monotonic **sample publication** generation, timestamps, units, current head/grip/aim poses, and reset on session/weapon/calibration changes. This generation is not `FrameKey.generation`, which only changes on lifecycle reset. |
| `mod/head_relative_pose_freeze_logic` | Pure diagnostic transforms between body, tracking anchor, and head spaces. | Explicit diagnostic policy and current BO camera/body transform. This is not the default live weapon placement policy. |
| `mod/magazine_charging_logic` | Calibrated travel, trigger hysteresis, either-free-hand transaction ownership, spring return, and manual/automatic charging semantics. | BO handle/tag geometry, travel/threshold calibration, authoritative cartridge availability and chamber confirmation. No round may be manufactured from a gesture. |
| `mod/manual_grenade_logic` | Grab/release ownership, frag/tactical slots, bounded velocity history and throw calibration, deliberate drop, held-input rearm, focus/loss reset. | Verified BO frag/tactical identity and ammo, throwback state, native spawn/commit authority, exactly-once release consumption, current monotonic poses and BO-calibrated belt/velocity conventions. Inherited belt coordinates are a starting configuration requiring BO/physical validation. |
| `mod/button_grenade_logic` | Optional held-button grenade ownership with caller-provided masks; blocked presses require release; transient missing samples preserve an existing hold. | Verified BO button bits and a distinction between a transient tracking gap and positively inactive gameplay/focus. Session/focus loss must not be represented as a transient missing sample that keeps a throw armed. |
| `mod/firing_haptic_logic` | Fresh, coalesced, bounded pulse mailbox driven by confirmed local shots. | Authoritative BO local-shot event, active/focused heartbeat, physical-hand routing, and XR haptic delivery. A held trigger alone does not count as a shot. |

`xr_types.h` and `xr_math.{h,cpp}` provide their exact math dependency. `gameplay/rigid_submesh_extraction.hpp` is a portable test dependency operating on normalized ranges and 16-bit indices. It validates bounds and rebases rigid ranges; it does not decode BO models. A future BO decoder must prove its model/bone/index representation before supplying that normalized data.

The inherited pose filter uses 39.37007874 units/m as a caller-configurable convention, 250 ms stale rejection, and bounded translation/rotation discontinuity checks. Grip logic uses engage/release hysteresis and a 250 ms grace period. These constants are inherited behavior, not measured BO comfort settings. Imported source and tests contain the exact values and their edge-case contracts.

## Deliberate exclusions

| Excluded source family | Reason it cannot be copied as portable BO behavior |
| --- | --- |
| `weapon_placement`, `input_mapping`, controller/weapon/hand hooks | Pull native T4 input types, connection/catcher assumptions, placement authority, and engine lifecycle into otherwise reusable math. |
| `manual_reload_controller_logic` | Mixes generic edges with T4 reload-state values and fixed stripper-clip/native commit assumptions. The portable transaction takes semantic native facts from a new BO adapter instead. |
| `physical_scope_logic`, draw filters, sight/support policies | Include WaW weapon names, seven model calibrations, packed image dimensions, native draw/material policy, or weapon allowlists. Unknown BO optics must retain native ADS until BO lens geometry and render authority are proven. |
| Pistol support/hand skinning, bolt/detachable/kar98 profile and chamber tables | Include WaW models, bones, topology/blend encodings, weapon capacities, handle geometry, or weapon-specific policies. |
| `weapon_ballistics_logic` | Mixes generic math with T4 weapon-type values, projectile alignment, and native weapon-parameter routing. BO muzzle, spread and projectile authority need independent proof. |

## Adapter and fallback contract

1. Publish one coherent tracked sample and native gameplay snapshot. Convert to the module's documented coordinate space explicitly; never infer a T5 structure from a T4 layout. Reset per-weapon/session/calibration caches when identity changes.
2. Deliver focused, fresh controller actions from the BO focus gate. Poses may remain valid while OpenXR is visible but unfocused; action ownership must still be released/reset. Recovery requires a fresh sample and release/rearm where the state machine requires it.
3. Resolve native weapon/profile, tags, model lifetime, reload/chamber state, ammo, and local firing identity before opting into an interaction. Keep render payloads alive until deferred worker consumption ends. Do not store borrowed engine pointers beyond their proven lifetime.
4. Treat state-machine native requests as transactions acknowledged by verified BO behavior. Suppress native reload/fire/grenade edges only while the BO adapter owns and can complete that transaction. Do not write ammo or synthesize successful commits from a gesture.
5. Fall back to native reload/ADS/weapon handling for unknown weapon or profile, missing/unverified tags or calibration, non-finite/stale poses, incompatible scripted/dead/intermission state, native commit failure, or identity change. Release/reset interaction ownership without manufacturing a throw, shot, reload, or round. Honor the module's short tracking-gap semantics only when gameplay/focus remains positively valid.
6. Manual grenade and charging behavior remain opt-in until BO native authority and physical hand geometry are validated. Automatic/native behavior remains available for unsupported weapons. Missing ammo is a hard no-commit condition.

## Validation and limits

`prototypes/interaction-core/build-validation.log` records a successful x86 Release library build and 9/9 unchanged inherited tests. The initial missing portable header dependency was fixed by importing the exact source header; the original failed build log is retained separately. Every one of the 34 imported files was rehashed against its manifest and source ZIP after the successful build, with zero mismatches. No vendor source was edited to make tests pass.

This establishes reviewable and usable portable logic. It does not prove T5 native ABI/layouts, BO weapon profiles, two-hand geometry for all handedness modes, optic rendering, live reload/grenade binding, physical headset behavior, or phase completion. The prototype has no game/runtime launch and has not been added to the live proxy.
