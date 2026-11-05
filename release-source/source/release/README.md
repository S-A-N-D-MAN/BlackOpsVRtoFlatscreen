# Black Ops VR v0.1.0-beta.4

Black Ops VR v0.1.0-beta.4 is a graphics update for Campaign and Zombies.

This release fixes the outdoor foliage/detail popping that remained after beta.3 and improves consistent rendering in both eyes, including the left-eye corruption found in the first private foliage test. Texture filtering is also set up automatically by the installer.

It carries forward beta.3's fixes for unequal stereo shadows, duplicate glass rendering, water lighting/reflection differences and shaky weapon/hand positioning while walking, along with its reload/grip recovery and performance improvements. The major graphics problems we've been working through now have fixes included; there can still be smaller glitches and animation issues to polish.

The newest foliage changes were checked in the OpenXR Simulator using preserved campaign checkpoints and paired-eye captures. The exact runtime passed all 178 native tests. A targeted High-resolution simulator check delivered about 60.2 successful projection frames per second at 2560 x 2880 per eye, with no recorded empty, reused or failed frames. This is a short fixed-scene test, not a headset FPS or every-mission guarantee. There has not been a new full campaign playthrough or fresh physical-headset acceptance of these newest foliage changes.

Campaign and Zombies remain supported. I completed the campaign earlier and fixed the progression blockers I encountered. Competitive multiplayer is unsupported.

To update: Save and close Black Ops, extract the entire new ZIP into a NEW folder, open this release's BlackOpsVR.exe and select the SAME Steam game folder as before. Retain your preferred controls/settings, choose Install / Update, then connect the headset/controllers before Launch VR. Choose No if Black Ops asks to reset graphics to optimal settings. Separate VR campaign saves are preserved by the launcher. Keep older releases as backups rather than mixing their files.

The installer includes HMD/controller movement, snap/smooth turning, manual/automatic reload, motion/button grenades and Performance/Standard/High rendering presets. Full weapon, vehicle, helicopter, boat and scripted-mission mappings are in CONTROLS.txt.

Runtime-specific startup/black-screen reports, duplicate/displaced bullet-mark reports and remaining weapon/reload/grenade animation polish are still being investigated. See KNOWN-ISSUES.txt and use Open logs plus Copy compatibility report when reporting a problem.

Complete matching native and launcher source, credits and third-party licenses are included. No game assets, saves or private logs are included. Requires Steam Call of Duty: Black Ops (2010) single-player and a compatible 32-bit OpenXR runtime.

Rendering presets: Performance 1280 x 1440, Standard 1920 x 2160 (default), High 2560 x 2880 per eye. The headset runtime output resolution is separate. Higher presets need more GPU work.

The exact native runtime SHA-256 is `6fd7edc61eca82276ff632eca6f21cc3bcca9db54af269ed5ebbe24f2f4b41d1`. BUILD-INFO.json binds artifacts and validation scope.
