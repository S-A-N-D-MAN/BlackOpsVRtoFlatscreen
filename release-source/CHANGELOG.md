# Black Ops VR 0.1.0-beta.4

## Since beta.3

- Corrected rigid-model foliage/detail selection for the actual stereo camera FOV, reducing outdoor popping without replacing native distance/hysteresis rules.
- Fixed the left-eye admission problem seen in the first private foliage build when native view buffers were reused. Both eyes retain their ownership and restoration checks.
- Enabled the accepted foliage correction and mip-filter setting. Diagnostic foliage probes stay disabled.
- Carried forward beta.3 shadow/glass/water corrections, walking hand/weapon stability, reload/grip recovery, controls and scheduling improvements.
- Rebuilt the launcher with beta.4 identity; only its version literals changed.

## Verification and limits

The exact released native runtime is `6fd7edc61eca82276ff632eca6f21cc3bcca9db54af269ed5ebbe24f2f4b41d1`. All 178 native tests passed on the first full attempt. River-checkpoint paired-eye review covered 36 recorded stereo pairs with exact per-eye application/restoration. The same native runtime's targeted High-resolution simulator check produced 1809 successful projections over 30.031 seconds (60.2378 FPS), with no empty, reused or failed-end frames. Transport CPU-stage timing was disabled; this result measures successful projection submissions, not headset refresh or all-scene performance.

The newest foliage changes have targeted simulator validation, not a fresh full campaign playthrough or new physical-headset acceptance. Older private-test failures remain preserved in development evidence. Runtime-specific startup and bullet-effect reports are not declared resolved.

## Previous beta.3 release

# Black Ops VR 0.1.0-beta.3

## Since beta.2

- Improved support-hand availability and recovery after reload/charging; reload hand alignment is corrected in the tested cases.
- Corrected tested per-eye shadow caster, glass geometry and water lighting/reflection problems.
- Stabilized weapon/hand positioning during player movement.
- Enabled the accepted producer scheduling change and retained its bounded diagnostics. The experimental normal-profile preparation gate remains disabled.
- Retained the three resolution presets and all existing movement, turn, reload and grenade options.
- Rebuilt the public launcher with the beta.3 version identity. Launcher behavior and defaults are unchanged.

## Verification

The exact runtime is `0c4994238ea809a74390bbbc8ab127be6c775dd04f424e0eb5385d8b3e1b3539`; its full native suite passed 176 tests. Two earlier pre-assertion fixture-allocation failures were retained and audited before successful CTest retries. No assertion failure was accepted as a pass. The unchanged launcher checks ran afresh against the beta.3 Core, runtime, environment and defaults: 139 checks, zero failures, one explicitly unavailable multiplayer identity fixture.

The controlled High-resolution water-scene simulator benchmark averaged 65.0997 successful projection FPS over 60.031 seconds and 3908 projection submissions, versus 57.1743 for the baseline. All six confirmation intervals were 64.4-66.6 FPS, with no recorded empty, reused or failed-end frames. This does not guarantee headset FPS or performance in other scenes.

Targeted final-runtime cases covered both-eye water rendering, independent headset/controller poses, pause/resume, tracking-loss recovery, a campaign-to-Zombies transition and M1911 partial manual reload. A menu screenshot harness timeout and an interrupted initial reload remain recorded in the development evidence; they were not counted as passes. Image samples are not proof that every frame is artifact-free.

No new full campaign playthrough, all-weapon certification or universal headset compatibility is claimed. The earlier developer campaign completion and existing campaign fixes remain part of the mod. Some graphical and animation issues remain; Pimax startup and duplicate/displaced bullet marks are not declared resolved.
