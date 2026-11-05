# Black Ops VR v0.1.0-beta.4 source snapshot

This source-only archive matches the public beta.4 release. It supersedes the
October 5 R113 snapshot for work against the current public version.

Start with BUILD-SOURCE.md for prerequisites and native/launcher build commands.

- source/runtime-source: complete matching native VR source, tests and vendored dependencies.
- source/release: launcher, shared Core, launcher checks, settings defaults and release documentation.
- source/tools/package_beta4.py: public release packaging tool. Its referenced historical validation inputs are not distributed; it is retained for provenance, not presented as a one-click release generator.
- licenses/, LICENSE.txt and THIRD-PARTY-NOTICES.txt: retained original licenses/notices. First-party mod and launcher source is GPL-3.0-only.
- BUILD-INFO.json and source inventories: release identity and bounded validation scope.
- CONTROLS.txt, KNOWN-ISSUES.txt and CHANGELOG.md: current behavior, limitations and changes.

No retail game files/assets, compiled binaries, personal profiles/saves, private
logs, customer diagnostics or credentials are included. Optional native asset
fixtures require your own legitimate game data. The Steam game is required to
play; this source archive is not an installer.

The original beta.4 validation was 178 native tests and 139 launcher checks.
Packaging this source did not rerun the game or establish new headset acceptance.
Compiler/SDK differences may change rebuilt binary hashes; byte-identical
rebuilds on another machine are not promised.

Public release: https://www.patreon.com/J_Play/posts/black-ops-vr-v0-172109738
Current outstanding issue/status update is in the BO1 general channel.
