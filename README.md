# Klip 2.0

A focused Windows x64 clipping app built around stock OBS/libobs 32.1.2.
The new engine handles Game Capture, Display Capture, WASAPI audio, hardware
encoding, replay buffering and recording. Klip keeps its native C++20,
Win32/D3D11/ImGui dashboard and customizable global shortcuts.

## Public beta

This is the new-engine edition (native version 2.0.0), replacing the legacy
3.0.x downloads. It is unsigned, and compatibility/performance vary by game,
GPU and drivers. No universal OBS-parity, zero-overhead or stable-release
claim is made. NVIDIA RTX 3060 capture has been tested; broader hardware,
live microphone and clean-machine testing remain incomplete.

- Save recent gameplay with Alt+C or your custom shortcut.
- Record full sessions and save clips while recording using one shared encoder.
- Adjust desktop/microphone levels, devices and separate audio tracks.
- Select 60 or 120 FPS and a supported hardware encoder; unavailable selections
  fail visibly rather than silently changing quality.
- Use explicit NVENC Performance tuning when its quality/compression tradeoff
  suits you. Balanced remains unchanged. The optional game-copy limiter is not
  a guaranteed smoothness fix and does not cap your game's frame rate.
- Hidden/minimized dashboards stop rendering and computing visualization-only
  audio meters. Actual capture/audio continue.
- Replay stores compressed packets with bounded memory; memory caps can shorten
  saved clips. Clips and recordings use MKV.

## Download and run

[Website](https://klip-ten-zeta.vercel.app/) ·
[Releases and sources](https://github.com/andrewkernel/klip/releases)

Install the complete `Klip-2.0.0-win64-setup.exe`, or extract the complete
portable ZIP and open its root `Klip.exe`. Keep bin, data, obs-plugins and
licenses together. Requires Windows x64, supported graphics drivers and the
[Microsoft VC++ 2015–2022 x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe).
Verify the release SHA256SUMS; do not disable antivirus or Windows security.
Close old Klip instances before upgrading. The × button hides the dashboard;
use the tray menu's Quit Klip to stop capture and exit.

Media, settings and logs stay local. Review logs for private target/device
names before sharing. See [privacy](PRIVACY.md).

## Source, build and verification

Klip is **GPL-3.0-or-later**; see [LICENSE](LICENSE) and
[third-party notices](THIRD_PARTY_NOTICES.md). The previous proprietary EULA
is superseded for Klip 2.0. Corresponding upstream source/build archives are
published alongside each 2.0 release, not merely linked to an upstream tip.

See [2.0 build/release instructions](docs/release-2.0.md),
[architecture](docs/architecture.md),
[gameplay performance evidence](docs/gameplay-performance-results.md) and
[libobs verification](docs/libobs-results.md). The legacy custom WGC/FFmpeg
engine remains in source behind `KLIP_USE_LIBOBS=OFF`; it is not the shipping
2.0 engine. The default and release builds use libobs with diagnostic hooks OFF.

## Original project demonstration

https://x.com/andrew1x_/status/2079314121766396172

This earlier video demonstrates the original concept, not a benchmark or
verification of the current 2.0 beta.
