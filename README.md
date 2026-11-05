# Black Ops VR

An unofficial PC VR mod for **Call of Duty: Black Ops (2010), Steam single player**, with campaign and Zombies support.

## Free download

**[Download BlackOpsVR v0.1.0-beta.4](https://github.com/jplakon/BlackOpsVR/releases/tag/v0.1.0-beta.4)**

Download **BlackOpsVR-v0.1.0-beta.4-Portable.zip** to play. The Source ZIP is for developers and is not required to play.

This is the same beta.4 build previously distributed on Patreon, including the October 21 weapon-verification documentation. This public release changes availability, not gameplay binaries. Existing users do not need to reinstall solely because the download is now public.

All of my released Call of Duty VR mods are free. Donations through [Patreon](https://www.patreon.com/J_Play) are entirely voluntary and help fund continued development. A paid membership is not required to download or play this mod.

## Features

- Stereoscopic VR and 6DoF head tracking.
- Motion-controller weapon aiming and two-handed support.
- Manual or automatic reload, motion or button grenades, and physical scopes.
- HMD- or controller-oriented movement, snap or smooth turning, and three rendering-resolution presets.
- VR menu pointing and campaign-specific vehicle, mounted-weapon and scripted-interaction controls.

See [CONTROLS.txt](release-source/CONTROLS.txt) for the complete control guide.

## Install

1. Close Black Ops and any older launcher.
2. Extract the entire Portable ZIP into a new folder; keep its files together.
3. Run **BlackOpsVR.exe** and select your compatible Steam Black Ops (2010) single-player installation.
4. Choose your options and select **Check game**, then **Install / Update**.
5. Start Steam and your VR software, connect your headset/controllers, and select **Launch VR**. If the game offers optimal graphics settings, select **No**.

Existing users should select the same Steam game folder to continue using their separate VR saves. Do not delete your existing VR profile/runtime folders. The mod uses a separate managed VR installation; no original game assets or saves are distributed.

A compatible **32-bit OpenXR runtime** is required for the game, even on 64-bit Windows. The included launcher supports an optional runtime override. Original compatible game ownership is required; multiplayer is unsupported.

## Beta status and testing limits

The campaign has been played through locally and the progression blockers encountered during that playthrough were addressed. Campaign and Zombies are supported, but graphical glitches, hand/reload animation defects, setup-specific issues and other bugs can remain. This is not certification of every weapon, mission, headset or runtime.

The October 21 documentation covers targeted weapon checks; it does not claim a complete all-weapon audit or universal headset acceptance. See the release's **WEAPON-TESTING-20261021.txt**, [KNOWN-ISSUES.txt](release-source/KNOWN-ISSUES.txt) and included feedback guide.

For an issue, include the exact release, headset/runtime, mission or Zombies map, weapon, reload mode and repeatable steps. Review diagnostic logs for personal information and share them privately when needed. Do not upload game executables, product keys or saves to public issues.

## Source, licenses and credits

The preserved corresponding source snapshot is in [release-source](release-source), including native mod and launcher sources, dependencies, license notices and build instructions. Start with [BUILD-SOURCE.md](release-source/BUILD-SOURCE.md). The exact original source archive is also attached to the release.

This snapshot is provided under **GPL-3.0-only**, with third-party components retaining their own notices and licenses. See [LICENSE.txt](LICENSE.txt) and [THIRD-PARTY-NOTICES.txt](release-source/THIRD-PARTY-NOTICES.txt). Credits and upstream provenance are preserved in the snapshot. Proprietary game/test fixtures and a turnkey reproduction of the original release-packaging environment are not included.

Other free projects: [Call of Duty 4 VR](https://github.com/jplakon/CallOfDuty4_VR/releases) and [WorldWarVR](https://github.com/jplakon/WorldWarVR/releases).

Independent fan project, not affiliated with Activision or Treyarch.
