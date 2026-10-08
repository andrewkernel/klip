# Windows compatibility plan and evidence

Updated: 2026-09-27. This document defines what “universal” means for Klip and tracks
the coverage needed to improve it. It is not a certification claim.

## Product scope

Klip is a native Windows capture application. The near-term target is supported x64
Windows PCs, not Linux, macOS, or Windows on ARM64. Its capture path depends on
Windows Graphics Capture, D3D11 video processing, Windows audio APIs, and FFmpeg.
The published minimum remains 64-bit Windows 10 version 1903 or newer; compatibility
with every driver, GPU, protected game, and audio endpoint cannot be guaranteed from a
single development PC.

“Works on a PC” must mean more than launching. At minimum, a supported configuration
must initialize WGC, show the correct source, produce a decodable clip and recording,
keep decoded timestamps valid, preserve audio behavior where an endpoint exists, and
make any software fallback visible. Encoder FPS metadata alone is not acceptance.

## Compatibility behavior

- Prefer an available hardware H.264 encoder on the selected D3D11 adapter. Probe by
  actually opening the encoder, then fall through when the driver or FFmpeg backend
  refuses it.
- Do not probe NVENC on a known non-NVIDIA adapter or AMF on a known non-AMD adapter.
  Media Foundation hardware and software paths remain in the fallback chain. Unknown
  adapter vendor IDs keep the configured candidate list so virtual/unusual devices are
  not prematurely excluded.
- When a specific vendor adapter is requested, attempt every matching hardware adapter
  before failing instead of stopping at the first matching device. This improves the
  multi-adapter selection path but does not replace physical hybrid-GPU and monitor-routing
  validation.
- Keep Windows Media Foundation hardware encoding as a vendor-neutral attempt and
  Media Foundation software encoding as the last compatibility path. Software mode
  can raise CPU use significantly; the app must not silently reduce user-selected
  quality or resolution.
- If a runtime encoder fails, clear potentially dependent replay history, reopen on a
  fresh keyframe, keep an active recording alive when possible, and report the
  discontinuity. This is recovery, not gap-free failover.
- Preserve WGC compositor timestamps for pacing when present. Older or unsupported
  timing interfaces must degrade to callback-arrival timestamps and be diagnosed.
- Log the GPU driving the captured monitor and whether it matches the WGC D3D device, which
  identifies likely cross-adapter capture. For window capture, report the window's current monitor
  as a routing hint; a window spanning multiple displays may move between adapters.
- Keep optional devices optional: absence of a microphone or playback endpoint must
  not prevent video-only capture. Report which audio feature is unavailable.
- Treat GPU vendor IDs and advertised capabilities as hints. A backend is supported
  only after device-bound encoder initialization succeeds.

## Current evidence by configuration

All current physical runs were made on one Windows 11 Home build 26200 system with a
Ryzen 5 7600X, 31 GiB RAM, an NVIDIA RTX 3060 (driver 32.0.15.9595), and an AMD Radeon
Graphics adapter (driver 32.0.21030.2001). The attached displays are NVIDIA-driven.
Therefore AMD results are cross-adapter; this is not low-end coverage.

| Configuration | Evidence | Status / limits |
|---|---|---|
| NVIDIA NVENC, WGC display capture, 1080p60 | Fresh 10.03 s recording and concurrent 5.05 s clip; full decode, 602/303 frames, 60 unique test-scene transitions/s, no consecutive repeats, monotonic 16.667 ms intervals | Passed on RTX 3060; short synthetic-scene acceptance, not a game benchmark |
| NVIDIA NVENC, WGC display capture, 720p120 | Fresh 10.02 s recording and 5.03 s clip; 1,203/604 frames, 120 unique transitions/s, no repeats, zero invalid PTS intervals, max 8.334 ms | Passed on RTX 3060 using moving D3D11 scene; not a game benchmark or low-end guarantee |
| NVIDIA NVENC, WGC selected-window capture, 1080p60 | Fresh 10.02 s recording and concurrent 5.03 s clip; 602/303 decoded frames, 60 transitions/s, no repeats, zero invalid timestamp intervals, max 16.667 ms | Passed on RTX 3060; synthetic moving window, not a protected game or exclusive-fullscreen test |
| AMD AMF, cross-adapter WGC display capture, 1080p60 | Fresh 10.00 s recording and concurrent 4.98 s clip; 601/300 decoded frames, 60 transitions/s, no repeats, zero invalid timestamp intervals, max 16.667 ms | Passed on Radeon Graphics with NVIDIA-driven display; does not certify AMD-native display routing |
| Pinned FFmpeg n8.1.3-2, NVIDIA NVENC, WGC display capture, 720p120 | Fresh 10.02 s recording and concurrent 5.02 s clip; 1,203/603 decoded frames, 119.9/119.8 unique transitions/s, one repeated image per file, zero invalid timestamps, max 8.334 ms | Passed after switching the local validator from mutable FFmpeg `latest` to a version-and-SHA-pinned release; same RTX 3060 system |
| Pinned FFmpeg n8.1.3-2, AMD AMF cross-adapter WGC, 1080p60 | 600-frame recording and 300-frame concurrent clip; both 60 unique transitions/s, no repeats, zero invalid timestamps, max 16.667 ms | Passed on Radeon Graphics while display remained NVIDIA-driven; not AMD-native display coverage |
| Pinned FFmpeg n8.1.3-2, Media Foundation software, WGC, 720p60 Performance | 601-frame recording and 289-frame clip; both 60 unique transitions/s, no repeats, zero invalid timestamps, max 16.667 ms | Passed on this system; does not establish minimum CPU requirements |
| Release-version FFmpeg n8.1.2-line, NVENC WGC display, 720p120 | 1,203-frame recording and 604-frame clip; 119.8/120 unique transitions/s, 0.17%/0% repeats, zero invalid PTS, max 8.334 ms | Passed on RTX 3060. Validator package is BtbN commit `gb2f422d306`; not the exact patched vcpkg binary |
| Release-version FFmpeg n8.1.2-line, AMD AMF cross-adapter WGC, 1080p60 | 601-frame recording and 300-frame clip; both 60 transitions/s, no repeats, zero invalid PTS, max 16.667 ms | Passed on Radeon Graphics with NVIDIA-driven display; not AMD-native display coverage |
| Release-version FFmpeg n8.1.2-line, Media Foundation software, WGC, 720p60 Performance | 603-frame recording and 291-frame clip; both 60 transitions/s, no repeats, zero invalid PTS, max 16.667 ms | Passed on this system; does not establish minimum CPU requirements |
| Release-version FFmpeg n8.1.2-line, audiovisual fixture, NVENC 1080p60 | 5/5 clip and 10/10 recording audio/visual markers; offsets -9.00 ms and -7.33 ms; zero invalid PTS, max 16.667 ms | Passed sync threshold; fixture itself had about 47 unique visual transitions/s and 21–22% repeats, so not encoder-cadence evidence |
| Video-only setting, NVENC WGC display capture, 1080p60 | Fresh recording and clip; 602/303 decoded frames, 60 transitions/s, no repeats, zero invalid timestamp intervals; AAC tracks decode to approximately -91 dBFS | Passed; the muxer keeps a silent AAC track to permit live audio toggles. This does not simulate a missing physical audio endpoint |
| Feature level 10.0 + D3D11 event-query sync, NVENC with A/V, 1080p60 | Clip/record decode; 5/10 matched A/V markers within -21.3 to +13.7 ms, zero invalid PTS intervals, max 16.667 ms | Passed on RTX 3060 with forced lower feature level; fixture motion was only about 49 unique changes/s, so this verifies fallback initialization/sync and A/V tolerance, not 60-fps source motion |
| NVENC WGC audiovisual fixture, 1080p60 | Clip/record decoded; 5/10 matched markers; offsets -24.7..+8.7 ms (clip) and -23.0..+10.3 ms (recording), zero invalid PTS intervals | Passed on same host; FFplay/compositor fixture delivered only 50.0/50.2 visual transitions/s and ~16% repeats, so motion-rate data is fixture-limited |
| Media Foundation software, WGC, 720p30 | Fresh 10.07 s recording and 4.67 s clip; 302/140 frames, 30 transitions/s, no repeats, monotonic timestamps, max interval 33.334 ms | Passed on this fast CPU; not proof it meets the same load on older CPUs |
| Media Foundation software, WGC, 720p60 | Fresh 10.05 s recording and 4.85 s clip; 603/291 frames, 60 transitions/s, no repeats, monotonic timestamps, max interval 16.667 ms | Passed on this system |
| Media Foundation software, WGC, 720p120 | Fresh 10.03 s recording and 4.94 s clip; 1,203/592 frames, about 119.8/119.6 transitions/s, one consecutive repeat in each, monotonic timestamps | Passed the harness on this system; source is synthetic and this is not a low-end guarantee |
| AMD AMF, cross-adapter WGC, 60/120 FPS | Separate clip-plus-record runs, including an A/V marker source-switch test | Passed in earlier recorded runs; AMD-native display cadence remains untested |
| Intel hardware encoder | No Intel GPU available | Untested; do not imply Intel certification |
| Integrated-only / entry-level PC | No such physical system available | Untested; one-core affinity on a 7600X is only a stress approximation |
| Older Windows 10, 1903–21H2 | No physical/VM OS matrix | Untested; current OS meets the minimum |
| Windows 10 N / missing Media Feature Pack | No such image available | Untested; MF software fallback may be absent until the feature pack is installed |
| ARM64 Windows | No ARM64 build or hardware validation | Out of current scope |

The software-fallback 30/60/120 runs were WGC/D3D11 capture runs; only video encoding
used Media Foundation software. They do **not** show Klip working without a D3D11
graphics adapter. Read each `.analysis.json` for decoded cadence and each
`resource-samples.csv` for host-specific process sampling. Artifacts are under
`work/universal-followup-nvenc-60/` and `work/universal-followup-mf-software-{30,60,120}/`.
Over the 10-second sample, cumulative Klip CPU time was 0.89 s for 1080p60 NVENC versus
1.56 s, 2.84 s, and 5.91 s for 720p software at 30/60/120 FPS. That is approximately
9%, 16%, 28%, and 59% of one logical processor, respectively. Software-fallback peak
working sets were about 227–229 MiB, with private bytes about 343–347 MiB; NVENC peaked
at about 180 MiB working set and 241 MiB private. These one-second Windows process samples
on a 7600X describe only this host and are not low-end minimum requirements.

After aligning the local FFmpeg validator to the release workflow's upstream n8.1.2 line, the
production-style Release binary was rebuilt against BtbN's Windows LGPL shared build
`n8.1.2-267-gb2f422d306` (archive SHA-256 is pinned in `tools/bootstrap-validation.ps1`). It is not
byte-identical to the release workflow's patched vcpkg build. The Klip executable SHA-256 is
`5d5bf6c6e08854f4ca02280fd52d9056d27c6a381fad30ea27f89ff13f1bc3d5`. The pinned build passed real
WGC clip-plus-record tests for NVIDIA NVENC at 720p120, AMD AMF cross-adapter at 1080p60, and Media
Foundation software at 720p60. The audio/video marker run paired 5/5 clip markers and 10/10
recording markers, with offsets of -9.00 ms and -7.33 ms. Debug and Release CTest each passed 2/2.
Artifacts are under `work/release-ffmpeg812-nvenc120-20260927/`,
`work/release-ffmpeg812-amd60-20260927/`, `work/release-ffmpeg812-software60-20260927/`, and
`work/release-ffmpeg812-av60-20260927/`.

With test hooks temporarily enabled, two injected NVENC submission faults also recovered to
`h264_mf_software` and finalized both outputs. The files decoded without repeated frames or invalid
PTS, but had a single 183.33 ms gap during encoder restart; runtime fallback is functional, not
seamless. Test hooks were disabled again, and the final production-style binary passed another
60 FPS WGC clip-plus-record run (303-frame clip, 602-frame recording, no repeated frames, zero
invalid PTS, 16.667 ms max spacing). That build's hash was the value above; the current binary
hash and post-filter run are recorded below.

That run also verified the new target-routing diagnostic: the primary display and WGC device both
resolved to the RTX 3060, so this specific run was same-adapter. The validation harness now requires
an explicit same-adapter, cross-adapter, or mapping-unavailable result so lab evidence cannot
silently omit routing context. An AMD-pinned run against the NVIDIA-driven display correctly logged
`cross-adapter`; a selected-window run reported its current monitor mapping and also finalized both
outputs. These were capture-routing checks, not AMD-native-display validation.

The vendor-aware encoder candidate filter was added after runtime-recovery logs showed an
NVIDIA-bound capture attempting AMF and failing with `No such device`. Core tests cover NVIDIA,
AMD, Intel, and unknown vendor selection. A fresh injected-failure run after the filter skipped
the irrelevant AMF probe, attempted Media Foundation hardware, and successfully fell back to
software; the clip and recording fully decoded with zero repeated frames and zero invalid PTS.
The maximum timestamp interval was still 183.333 ms, so this is a cleaner compatibility probe
order, not a fix for the known recovery pause. Hardware recovery gaps remain a reliability item.

A separate test-hook-enabled 60 FPS audiovisual run injected the same two NVENC faults during
recording and saved a concurrent clip. Both MP4s decoded with monotonic PTS. The recording
detected 10/10 flash/beep pairs with offsets from -5.33 to +11.33 ms (median +11.33 ms); the clip
detected 3/3 pairs from -8.67 to +8.00 ms (median +8.00 ms). Both retained the 183.333 ms video
gap. The FFplay fixture itself delivered only 48.7–48.9 unique visual changes/s and 13–17%
repeated frames, so this confirms short-run A/V alignment through the injected fallback, not
60 FPS motion quality or long-term drift. Artifacts are under
`work/rc-runtime-recovery-av-20260927/`.

After this selection change, the encoder-exhaustion path was improved to show each attempted
backend's native failure details plus Windows N Media Feature Pack and driver troubleshooting hints.
Debug and Release CTest passed 2/2, and the test-hook-free Release binary passed another WGC/NVENC
1080p60 clip-plus-record run: 302/602 decoded frames at ~60 changed IDs/s, no repeats, zero invalid
PTS, and a 16.667 ms maximum frame interval. Its SHA-256 is
`e9b35b0272cea447527743e657c53d0505786290eb5f35000368ceda11982614`. Artifacts are under
`work/rc-final-actionable-encoder-error-20260927/`. Detailed error formatting has deterministic
unit coverage; no physical machine without a usable encoder was available to exercise the actual
UI error card.

The hotkey acceptance harness also sends Ctrl+Alt+Shift+F22/F23/F24 through Windows `SendInput`
while registering the actual global hotkeys. It toggled the initially hidden app window, started
and stopped recording, and saved a concurrent clip while capturing a separate moving WGC test
window. Both outputs decoded: 601-frame recording and 302-frame clip, ~60 unique transitions/s,
0.17%/0% consecutive repeats, zero invalid PTS, and 16.667 ms maximum spacing. The run also
verified the app's visibility state begins consistent with `SW_HIDE`. This tests OS hotkey delivery
while Klip is hidden, but not a physical keyboard or Fortnite specifically. Artifact:
`work/rc-global-hotkeys-all-actions-20260927/`. Test-hook-free Release SHA-256:
`455061ff78ad31998a4cbb22b6f53d90c4c2026bb2ad3c78dc92feb3a1648927`.

The latest window/AMD/video-only compatibility artifacts are under
`work/universal-final-window60/`, `work/universal-final-amd60/`, and
`work/universal-final-video-only60-repeat/`. Each clip and recording was decoded and cadence-
analyzed with the production-style Release binary. The AMD path was deliberately pinned to the
AMD adapter, and the acceptance log confirmed `h264_amf`; it was not an automatic selection.
Latest production-style NVENC motion and audiovisual tests are under
`work/rc-final-production120-20260927/` and `work/rc-final-production-av-20260927/`.
The most recent app-action path check is under `work/rc-final-production-hotkeys-20260927/`.
The combined feature-level/event-query audiovisual run is under
`work/universal-final-fl10-eventquery-av/`; logs confirmed feature level 10.0 and the forced
event-query path. Its fixture motion rate varied below 60 despite regular encoded timestamps,
which is why decoded cadence and source cadence remain separate compatibility checks.

## Lab matrix needed before broad compatibility claims

Use real hardware and record exact Windows build, system model, CPU, RAM, GPU/device
ID, driver, display routing, refresh rate, FFmpeg build, settings, output dimensions,
encoder chosen, and binary hash. Each row must test game/window and display capture,
clips, recording, clip-during-recording, audio enabled/disabled, source switch, and
decoded cadence/synchronization.

| Lab class | Required device coverage |
|---|---|
| NVIDIA | Recent and older supported NVENC generations, including Optimus/hybrid routing |
| AMD | Discrete AMD-driven display, AMD integrated-only, and mixed-vendor multi-GPU |
| Intel | Recent Arc, mainstream UHD/Iris Xe iGPU, and a hybrid laptop |
| Low-end | Older dual/four-core CPU, 8 GB RAM, integrated graphics; test 720p30/60 Performance first |
| Windows | Windows 10 1903, current Windows 10 release, supported Windows 11 versions; N edition with and without Media Feature Pack |
| Audio | USB/Bluetooth/headset endpoints, 44.1/48 kHz, microphone absent/removed/reconnected, endpoint switch, long A/V drift |
| Drivers/failures | Encoder unavailable at startup, runtime encoder error, device removal/reset, graphics-device creation failure, low disk space, output path denial |
| Displays/input | 60/120/144/240 Hz, mixed-refresh multi-monitor, display scaling/DPI, 16:9 and ultrawide, SDR/HDR, fullscreen/borderless/windowed, actual game movement |

Acceptance must distinguish hardware encoder from software fallback and record any
quality, cadence, sync, or startup discontinuity. Do not label a row “supported” on
startup alone; require real clip and recording decode. Keep untested rows explicit.
For every FPS, record encoded FPS, unique decoded transitions, repeated images, missed source
updates, and PTS gaps independently. A generated fixture's motion cadence must be checked against
the fixture file itself; fixture playback on the desktop/compositor can introduce repeats unrelated
to the encoder. Add both a deterministic native animated source and a real game/window source before
attributing a cadence defect to Klip or declaring it clear.

## Follow-up engineering order

1. Acquire or borrow representative Intel, AMD-native, low-end integrated-only, and
   older-Windows test machines. Without them, the responsible next step is a lab pass,
   not guessed code changes.
2. Test a physical fallback on a PC without a usable hardware H.264 encoder and
   separate encoder absence from graphics-device absence. Klip still needs D3D11 for
   WGC and conversion; a software video encoder does not remove that graphics
   requirement.
3. Measure cold-start and steady-state CPU/RAM/GPU load per tier and decide whether
   to make 720p30 or 720p60 the recommendation for weak machines. Do not infer CPU
   capacity from a single logical-core affinity test on a modern processor.
4. Only after the matrix is green, consider a separate ARM64 build, an alternative
   capture backend for unsupported Windows builds, or graceful WARP/software graphics.
   Those are separate architecture projects and must prove WGC, video processing,
   timestamps, and sustained capture before becoming promises.

## Reproduction

From PowerShell at the repository root, with the validation dependencies bootstrapped:

```powershell
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $false
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name universal-followup-nvenc-60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 30 -NativeScene -SoftwarePerformance -Name universal-followup-mf-software-30
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwarePerformance -Name universal-followup-mf-software-60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -SoftwarePerformance -Name universal-followup-mf-software-120
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -WindowCapture -Name universal-final-window60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Amd -Name universal-final-amd60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -VideoOnly -Name universal-final-video-only60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -FeatureLevel10 -EventQuerySync -RequireAvMarkers -Name universal-final-fl10-eventquery-av
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -Name rc-final-production120
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -HotkeyActions -Name rc-final-production-hotkeys
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -WindowCapture -HotkeyInput -Name rc-global-hotkeys-all-actions
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -RequireAvMarkers -Name rc-final-production-av
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -SourceFps 60 -LongAvSync -RequireAvMarkers -Name rc-long-av-drift
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -RuntimeEncoderFailure -RequireAvMarkers -Name rc-runtime-recovery-av
```

The current results are limited to the specific host above and are not a release or
wide-deployment certification.
