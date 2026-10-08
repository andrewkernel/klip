# Klip release-candidate verification

Run date: 2026-09-27 (America/Chicago). These are developer-build test results, not
a deployment approval or broad Windows compatibility claim.

## Machine and test method

- The earlier motion baseline runs in this report were recorded on a Windows 11 system with an Intel Core i7-12700K and 32 GiB RAM.
- NVIDIA GeForce RTX 3060, driver `32.0.15.9595`; primary display 1920x1080 at
  approximately 239 Hz. The scene self-reported 239.64–239.69 presents/s.
- Klip used WGC display capture and NVENC (`h264_nvenc`). Portable build uses
  LLVM-MinGW, CMake/Ninja, and BtbN FFmpeg 8.1.3 (LGPL shared validation build).
  It is not the release workflow's MSVC/Inno packaged binary.
- The moving D3D test scene presents a changing binary frame ID and vertical bar.
  Analyzer decodes every frame, extracts that ID, and reads presentation timestamps.
  “Unique transitions/s” means decoded samples whose embedded ID changes; it is not
  a general perceptual score or proof that every source frame was captured.
- On final native runs, WGC delivered about 120 frames/s at a 60 FPS target (1,166
  arrivals over 9.73 capture seconds) and about 238 frames/s at a 120 FPS target
  (2,301 over 9.66 seconds), from a scene presenting at ~239.6/s. Arrival counts do
  not prove distinct pixels; decoded barcode transitions below are the motion check.
- A second fixture uses FFplay with timed flashes and audio beeps. Its observed
  WGC arrival cadence includes playback/compositor behavior, so it is useful for
  a short A/V timing check, not as a guaranteed 60/120 FPS capture source.

Most follow-up compatibility, software-fallback, runtime-recovery, and pool-ownership runs
were later made on the currently available Windows 11 Home build 26200 system: AMD Ryzen 5
7600X, 32 GiB RAM, NVIDIA RTX 3060 (`32.0.15.9595`) and AMD Radeon(TM) Graphics
(`32.0.21030.2001`). Both active displays are NVIDIA-driven; forced AMD runs are cross-adapter.
Treat measurements as host-specific and do not compare CPU/RAM measurements across the two
hosts as if they were a controlled performance comparison.

## Root causes and changes

| Finding | Evidence and action | Confidence |
|---|---|---|
| WGC commonly delivered about 60 updates/s even while Klip encoded 120 FPS | Before asking WGC for a 2x-target update rate, native-scene 120 FPS output had 50.04% consecutive repeats and about 59.95 changed IDs/s. With `MinUpdateInterval` configured, the same scene produced about 118.8 changed IDs/s and about 1% repeats. Older runtimes continue with a warning when the API is absent. | Confirmed on this machine/runtime |
| Exact `1/FPS` arrival gate rejected normal early jitter | Deterministic alternating-jitter test accepted 60/120 arrivals with the old strict gate and 120/120 with the half-interval gate. Real native-scene runs did not show output timestamp gaps. | Confirmed in test; source gating is empirically better on this setup |
| AMF performance profile emitted no periodic IDRs and recording could stop without ever reaching a keyframe | FFmpeg 8.1.3 `h264_amf` on AMD with `usage=ultralowlatency` produced 1 IDR across 1,200 frames despite `g=240`; switching to `usage=transcoding` with AMF low-latency enabled produced IDRs every 240 frames. Klip now also requests an IDR after the recording queue is registered. The next real 120 FPS AMD run saved a full 10 s/1,200-frame recording and passed decode/keyframe/duration gates. | Confirmed and fixed on the tested AMD adapter/driver |
| Output FPS metric represented successful input submission, not emitted packets | Moved the visible output counter to packets returned by `avcodec_receive_packet`; trace separately reports submitted and emitted counts. | Confirmed code/telemetry mismatch, fixed |
| NVENC had previously failed with ENOMEM and clips could reuse released input surfaces | Dedicated bounded encoder input textures and retained D3D11 resource registrations until codec teardown. A 60-second NVENC recording plus concurrent clip completed without encoder reset. The underlying driver-specific cause was not isolated. | Probable mitigation, one-minute run passed |
| Packet clone/queue loss could silently create incomplete replay or recording data | Replay push failure clears history and blocks following audio/interframes until the next video keyframe. Replay snapshots now fail atomically on clone/allocation/order failure. Recording stops accepting at first compressed packet loss and drains the accepted prefix. | Code path fixed; prefix finalization decode-tested; clone-allocation failure not injected |
| A worker's success could clear another component's visible error; polling could miss a transient error | Errors clear only for the owning component. `error_generation` is monotonic and acceptance checks it before, during, and after the test. | Regression-tested |
| Queue wakeup, partial hotkey conflict, and WASAPI sample-format variation | Stop-aware queue waits; retain independent global hotkeys when one chord is occupied; resolve PCM storage width separately from valid-bit precision; unpack signed packed-24 PCM to normalized 32-bit samples before libswresample. | Regression-tested with extrema, sign-extension, stereo, and padded-stride fixtures; real packed-24 endpoint not available |

## Before/after motion measurements

| Scenario | Decoded output | Changed IDs per second | Consecutive repeats | PTS intervals / maximum gap | Encoder and concurrent output |
|---|---:|---:|---:|---|---|
| Before WGC interval request, native scene at target 120 | 1,202 frames, prior baseline | ~59.95/s | 50.04% | ~8.333 ms cadence | RTX 3060 NVENC; baseline recording |
| Final native scene, target 60 recording | 482 frames / 8.017 s | 59.38/s | 1.04% | 16.666/16.667 ms; max 16.667 ms; zero nonmonotonic | RTX 3060 NVENC; passed while replay clip saved |
| Final native scene, target 60 clip | 303 frames / 5.033 s | 59.01/s | 1.66% | 16.666/16.667 ms; max 16.667 ms; zero nonmonotonic | Same capture session as recording |
| Final native scene, target 120 recording | 965 frames / 8.033 s | 118.76/s | 1.04% | 8.333/8.334 ms; max 8.334 ms; zero nonmonotonic | RTX 3060 NVENC; passed while replay clip saved |
| Final native scene, target 120 clip | 605 frames / 5.033 s | 118.81/s | 0.99% | 8.333/8.334 ms; max 8.334 ms; zero nonmonotonic | Same capture session as recording |
| Final stress scene, target 60 recording | 3,602 frames / 60.017 s | 59.48/s | 0.86% | 16.666/16.667 ms; max 16.667 ms; zero nonmonotonic | NVENC forced; 1280x720, 40 Mbps; passed with clip save |
| Final stress scene, target 60 clip | 2,341 frames / 39 s | 59.39/s | 1.03% | 16.666/16.667 ms; max 16.667 ms; zero nonmonotonic | Clip requested during recording; normal configured replay duration was 40 s |

All listed output clips and recordings were decoded by FFmpeg, not inferred from MP4
metadata alone. Native scene output had no sync sound stimulus. The final stress sample
had 60 one-second process samples; the scene was alive at all 60. Klip used 4.27 CPU
seconds over about 61 seconds of recording (about 7% of one logical CPU), peaked at
315.7 MiB working set and 363 MiB private bytes. GPU engine/encode utilization was not
sampled. This is one machine, one source type, one encoder, and a short soak—not a
cross-PC performance claim.

## Audio and clip/recording checks

The compatibility pass added packed 24-bit PCM conversion for WASAPI endpoints that expose
three-byte samples; a Windows regression test covers positive/negative limits and block padding.
A supervisor now reports unexpected WASAPI worker termination and retries default desktop audio
and the selected microphone with a bounded two-second retry interval; microphone discovery prefers
the saved device name and updates the available-input list. The ordinary smoke below validates
that this supervisor does not disrupt the normal capture path, but no endpoint was physically
removed/reconnected, so recovery remains unverified on real device-change events.
A fresh 60 FPS A/V-marker run on this host used its normal 48 kHz, stereo, 32-bit-float render
mix instead (the host has no active packed-24 endpoint). The 10-second recording decoded 600
video frames, detected all 10 flashes and 10 beeps, and measured a +4.667 ms median audio-minus-
visual offset. After adding endpoint supervision, a second ordinary-path run decoded 601 recording
frames and 302 clip frames; it detected all 10/5 flashes and beeps, with median offsets of -3.67 ms
and -4.33 ms. A repeat under the final recovery-policy test build measured +6.0 ms and -9.33 ms.
Both tests confirm normal endpoint capture still passes after the recovery change, but do not
simulate unplug/replug. The newly supported packed-24 path still needs a real endpoint test. Latest
artifacts are under `work/universal-audio-recovery-acceptance-20260927/`; the prior runs are under
`work/universal-audio-recovery-normal-smoke-20260927/` and
`work/universal-audio-compat-smoke-20260927/`.

The final-binary FFplay 60 FPS marker fixture produced valid H.264 plus AAC in both
simultaneous outputs. On the 8-second recording and 5-second clip the median detected
audio-beep minus visual-flash offset was +1.667 ms; one of eight recording marker pairs
and one of five clip marker pairs measured -15 ms. These values include FFplay, desktop
composition, and WASAPI loopback and cannot establish microphone latency or long-term
drift. The fixture's observed motion was source-limited: about 44.8 changed IDs/s in the
recording and 45.45 changed IDs/s in the clip, with 25.4% and 24.3% repeated frames
respectively despite nominal 60 FPS output. Do not treat this as an encoder cadence
failure; native-scene capture at the same target reached ~59.4 changed IDs/s.

A second, separately gated A/V test targeted 120 FPS using the 120 FPS flash/beep fixture. Both the
10.017-second recording and 5.008-second clip contained all expected markers; median audio-minus-
visual offset measured 8.83 ms and 4.67 ms respectively, with all matched marker pairs within
40 ms. The output timestamps were regular at 8.333/8.334 ms, but decoded motion was only about
63.1/63.2 unique marker changes/s with roughly 47% repeats. FFplay/composition therefore limited
the source updates; this run supports the A/V sync check, not 120 FPS motion quality. The native
WGC scene tests separately measure high-rate motion without audio markers.

The Windows regression suite saves an accepted keyframe through RecordingWriter, finalizes
the MP4, reopens it, and decodes exactly one frame. It also forces a four-packet queue to
overflow after accepting media, then requires the accepted prefix to be finalized and decode.
A separate zero-capacity queue test confirms overload stops with an error and publishes no
empty file. Physical disk-full and slow-device behavior remain untested.

## Verification commands and artifacts

From repository root in PowerShell:

```powershell
.\tools\bootstrap-validation.ps1
.\tools\build-validation.ps1 -Configuration Debug
.\tools\build-validation.ps1 -Configuration Release
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -FallbackDiagnostics -Name rc-encoder-fallback-diagnostics
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name rc-release-native60-20260927
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -Name rc-release-native120-20260927
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -WindowCapture -Name window-native60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -WindowCapture -Name window-native120
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -ResizeSource -Name rc-window-resize-source-native60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Stress -Name rc-final-nvenc-stress60-20260927
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -SourceFps 60 -Name rc-release-av60-20260927
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -SourceFps 120 -RequireAvMarkers -Name rc-av120-source120-gated-20260927
```

The build script runs CMake/Ninja and CTest. Both `klip_core_tests` and
`klip_windows_tests` passed in Debug and Release after the final code changes.
Capture outputs, logs, decoded `.analysis.json`, and `resource-samples.csv` are retained
under ignored `work/rc-*` directories; no historical artifacts were overwritten.
Final run directories are `work/rc-release-native60-20260927`,
`work/rc-release-native120-20260927`, `work/rc-final-nvenc-stress60-20260927`, and
`work/rc-release-av60-20260927`.

Earlier portable executable SHA-256 values (before the final pool-ownership change):

- Debug `Klip.exe`: `EE3A8D87B6B8EF6E5D9A76B69875CF2A330219B1AEBDD3C0F269FC275258BAF7`
- Release `Klip.exe`: `77B7655D61B5E1D932EFCED253C4D081A20F27BE7D02C266F25CF02CE04B05EE`

Latest final-source portable executable SHA-256, after the pool-ownership change:

- Debug `Klip.exe`: `59D451E83F9B46313B30C584FE5D37F0B31555026D3CE085CEDC7270F0B9E25B`
- Release `Klip.exe` (test hooks OFF): `4EBBE135B5688B6029358221BBE8C347B2A14936C9E241BC4B719CEB53118BDD`

Earlier controlled baselines remain under `work/cadence-native-120` and
`work/cadence-native-120-interval`; the latter was interrupted and is contaminated.
Only `work/rc-release-native120-20260927` and later final runs are used for the final
measurement above.

## Release checklist

- [x] Final portable Debug and Release builds; core and Windows regression tests.
- [x] Native WGC/NVENC 120 FPS decode analysis for both clip and recording.
- [x] WGC window capture at 60 and 120 FPS against a named moving test HWND; both clips and
  recordings decoded and the selected target/encoder were confirmed in logs.
- [x] Resize a WGC window source from 1920x1080 to 960x540 and back during recording. Klip
  observed both source changes while preserving a 1920x1080 output stream; the recording ran
  the full requested duration and both the clip and recording decoded. Motion dipped briefly
  across the resize, so this validates continuity/finalization, not zero frame loss.
- [x] 60-second native WGC/NVENC recording and simultaneous clip; decoded cadence,
  timestamps, resource sampling, and source liveness retained.
- [x] Short desktop audio/visual marker test on the current portable binary.
- [x] 120 FPS FFplay flash/beep sync test for both clip and recording, with explicit marker-pair
  tolerance; its measured visual source cadence was only ~63 updates/s and is not motion evidence.
- [x] Output packet FPS separated from input submission FPS; error generation is
  observed by acceptance mode.
- [x] Force selection-time fallback from a deliberately unavailable encoder to Media Foundation
  software encoding; require a persistent UI/log reason and verify both outputs by full decode.
- [x] Portable package assembly and extracted-ZIP first-run/DLL-loading smoke using the
  LLVM-MinGW validation build; this does not substitute for the release toolchain.
- [x] Inno Setup 6.7.3 successfully compiles the local portable validation build into an installer;
  the test installer was not run because its shared AppId could replace the existing user install.
- [ ] Official MSVC/vcpkg build and clean-machine installation/uninstallation run.
- [x] Standalone FFmpeg AMF encode using a D3D11 device explicitly created on the integrated AMD
  adapter; output decoded as 180 frames at 60 FPS.
- [x] Klip's full WGC-to-AMF path on the integrated AMD adapter, including a replay clip saved
  during recording; both outputs were decoded successfully. A 120 FPS run also retained the full
  requested duration and periodic IDRs after the AMF/record-start fixes.
- [ ] 120 FPS unique-motion stability on an AMD-owned display; the only AMD adapter on this
  hybrid host owns no active display outputs, so forced AMD capture is cross-adapter.
- [x] Test-injected NVENC runtime failure twice during an active recording, then fallback to
  Media Foundation software encoding; the ongoing recording and concurrent replay both finalized
  and fully decoded. This verifies the recovery state machine, not a driver-induced failure.
- [ ] Physical NVIDIA/AMD/Intel driver-induced runtime failures under load. Intel hardware was
  unavailable.
- [ ] A/V test with controlled microphone and desktop loopback over a long session;
  test device removal, switching, 44.1/48 kHz inputs, and audio continuity under load.
- [ ] 120 FPS multi-minute stress recording and tests across more monitors, real games,
  borderless/exclusive modes, and Windows releases; window capture is verified only against the
  moving test fixture so far.
- [x] Return/retire encoder input textures across pre-submit failures and uncertain GPU completion;
  runtime fallback and ordinary capture both rerun. Full artificial 64-surface exhaustion remains
  untested, as do driver removal and resolution-change stress on physical older GPUs.
- [ ] Exercise actual global Alt+C/Alt+R behavior with a game focused; current automated
  shortcut coverage verifies Win32 registration conflicts, not game input behavior.
- [ ] Compare the same WGC source with OBS WGC source. OBS's hooked Game Capture path
  is not equivalent. No quality/resource parity claim is supported by these runs.

This evidence supports an experimental RC candidate on the tested RTX 3060 desktop.
It does not support a “ready for thousands of Windows users” claim. Do not publish or
deploy without the user's separate instruction.

## Windows compatibility pass (2026-09-27)

This host reports an NVIDIA GeForce RTX 3060 and AMD Radeon(TM) Graphics. Initial automatic and
software-fallback acceptance runs selected the RTX 3060; AMD coverage was performed later with
the AMD adapter explicitly selected.
After the universal-Windows compatibility changes, Debug and Release builds completed and both
`klip_core_tests` and `klip_windows_tests` passed in each configuration.

The Release binary passed the real display-capture acceptance flow with automatic NVENC and with
`h264_mf_software` explicitly forced. The software-only policy skipped creation of FFmpeg's
D3D11 hardware-encoder context entirely, exercising the fallback path that should work when that
context is unavailable. Each flow saved a replay while continuous recording was
active, finalized both MP4 files, and exited cleanly. `ffprobe` found H.264 1920x1080 video at
60/1 nominal and average FPS plus stereo AAC at 48 kHz in all four files. Durations were 5.050 s
and 8.050 s for the NVENC clip/recording, and 4.925 s and 9.917 s for the software-fallback
clip/recording. No `.partial` outputs remained. The smoke scene was the desktop, not a moving
game/marker fixture; this run verifies pipeline compatibility and file finalization, not motion
quality, A/V drift, CPU cost on a low-end system, or real AMD/Intel hardware encoding.

The added `--video-only-smoke-test` passed with desktop audio disabled and no endpoint opened;
it finalized a 5.050-second clip and an 8.033-second recording, both 1920x1080 H.264 at 60/1
with silent stereo AAC. A separate audio-enabled NVENC capture passed immediately afterward,
showing that normal loopback capture still starts on this host. This exercises an intentionally
unopened endpoint, not a physically removed or defective endpoint; that failure/hot-plug path
still needs an actual no-audio-device test.

The same moving D3D scene was then captured by its actual HWND using the new hidden
`--window-capture-smoke-test` and `--window-120fps-smoke-test` acceptance modes. At 60 FPS, the
10.017-second recording decoded 601 frames with 59.90 marker transitions/s and 0.17% repeats; the
5.050-second clip decoded 303 frames with 59.80/s and 0.33% repeats. At 120 FPS, the 10.017-second
recording decoded 1,202 frames with 119.90 transitions/s and 0.08% repeats; the 5.042-second clip
decoded 605 frames with 120 transitions/s and no repeats. All PTS intervals were regular, both
recordings had periodic IDRs, and the logs confirmed the exact target title and NVENC backend.
Artifacts are retained in `work/nvenc-window-native60-fixed-20260927/` and
`work/nvenc-window-native120-20260927/`. This validates WGC window capture on the lab display and
GPU; it is not a real game/anti-cheat compatibility test.

The source-size policy was exercised with the same moving HWND resizing from 1920x1080 to
960x540 and back during a 10-second recording. The selected source changed size in the WGC log
twice; outputs stayed 1920x1080 and decoded. The first run showed brief cadence loss (57.5 unique
transitions/s recording, 55.0/s clip); a final repeat decoded 601 recording frames and 302 clip
frames, both at 60 unique transitions/s with zero repeated IDs and regular PTS. This variability
means a transient stall remains possible, so this is continuity evidence rather than a broad
no-frame-loss guarantee. Source-sized output locks to the first target size for the lifetime of
the media pipeline and scales later source sizes into that stable MP4 size. Final artifacts are
in `work/rc-window-resize-source-native60-final-20260927/`.

Encoder fallback diagnostics were tested with a deliberately nonexistent preferred encoder
(`h264_klip_test_missing`) followed by the Windows Media Foundation software encoder. Klip logged
the missing-encoder reason and warned that software encoding may use more CPU, then saved a
9.817-second recording (589 decoded frames) and a 4.833-second replay clip (290 frames) at
1920x1080. They produced 59.9 and 60 unique marker changes/s, 0.17% and 0% repeated frames,
and regular 16.667 ms maximum PTS intervals. An isolated live UI test showed an amber
“fallback active” indicator and a wrapped tooltip with the exact missing-codec reason. These
tests cover selection-time fallback, not a hardware encoder failing after a long session.
Artifacts are in `work/rc-encoder-fallback-diagnostics-final2-20260927/` and
`work/package-extract-final-20260927/isolated-data/`.

The latest forced Media Foundation software-fallback moving-scene run decoded 591 recording frames
and 290 clip frames at 60/1, with 60 unique marker transitions/s, 0 repeated marker frames, and a
maximum 16.667 ms PTS step. The 9.850-second recording and 4.833-second clip both finalized while
the clip was requested during recording. This validates one software fallback on this high-end
host; it does not establish acceptable CPU use or 120 FPS operation on low-end PCs.

A second software-fallback run used the 720p60/8 Mbps performance preset. The 9.817-second
recording (589 decoded frames) and 4.833-second clip (290 frames) both had 60 unique marker
transitions/s, no repeated frames, and a maximum 16.667 ms PTS interval. Ten process samples
measured approximately 24.4% of one logical CPU core on average, with 227 MiB peak working set and
342 MiB peak private bytes. The corresponding 1080p60 software run sampled 49.2% of one core,
390 MiB working set and 616 MiB private bytes; the separate NVENC 1080p60 run sampled 3.7% of one
core, 179 MiB working set and 242 MiB private bytes. These short, sequential runs used the same
RTX 3060/i7-12700K test PC and moving scene; they are directional single-host samples, not a
controlled benchmark or a guarantee for low-end CPU usage.

Run artifacts and isolated logs are under ignored `work/universal-acceptance-20260927/`.
The newly added D3D fallback-adapter branch has compiled and is covered by builds, but it was not
forced at runtime because the default RTX 3060 adapter initialized successfully.
Video-only and audio re-enable smoke artifacts are under ignored `work/universal-video-only-20260927/`
and `work/universal-audio-check-20260927/` directories.

## Earlier portable package smoke (2026-09-27; superseded below)

The earlier Release validation build was staged with `packaging/package-win64.ps1` as a portable
package using version label `3.0.3-rc.window.20260927`. The 52,181,664-byte ZIP was expanded
into a fresh directory and its `Klip.exe --package-smoke-test` exited with code 0, creating
first-run settings under an isolated `KLIP_DATA_ROOT`. Its checksum is in
`work/package-smoke-window-final-20260927/output/SHA256SUMS.txt`; ZIP SHA-256 is
`772083AA02740DAEA2832AB4B23D7B75B33AFF74739FB6B48EF2FC8FB450EE86`. This confirms the portable
layout and DLL loading for the latest LLVM-MinGW validation binary; it is not the official
MSVC/vcpkg release build and does not exercise Inno Setup installation or uninstallation.

After the source-resize continuity change, Release was rebuilt and both tests passed in Debug
and Release. A fresh portable package was assembled with installer creation explicitly skipped,
expanded to a new directory, and passed `Klip.exe --package-smoke-test` with an isolated
`KLIP_DATA_ROOT`. The ZIP is 53,667,627 bytes, SHA-256
`E0F99EBE48C5DC3DA156F252B1146CD4F669BB0B2F09056007A471C992A037B0`, under
`work/package-smoke-universal-20260927/`. It is a validation build only, not the official
MSVC/vcpkg installer.

After the final compatibility-diagnostics and tooltip-wrap changes, Debug and Release both passed
their two CTest targets. The final portable ZIP was extracted to a fresh directory;
`Klip.exe --package-smoke-test` exited 0 and created isolated first-run settings. It is
53,673,014 bytes, SHA-256
`dbddc79bd8ac22623d98a75bf31e84be067100695a9e251e02ff52376e67aebd`.

Inno Setup 6.7.3 compiled the corresponding unsigned validation installer successfully. It is
37,447,773 bytes, SHA-256
`f093e7f7fa8e0108219750e1d3a0620c4bceee0cd0174620b427dcbe7dd8876a`. Both artifacts are under
`work/package-installer-final2-20260927/`. The installer was not run: its shared product AppId
could replace or alter the existing user installation. These artifacts use the LLVM-MinGW
validation build, not the official MSVC/vcpkg release workflow; clean install/uninstall remains
untested.

## AMD encoder backend smoke (2026-09-27)

The host also has an AMD Radeon(TM) Graphics adapter (DXGI adapter 1, vendor/device `1002:164e`).
Using the bundled FFmpeg directly, a synthetic 1280x720/60 source was uploaded to an explicit
D3D11 device on that adapter and encoded with `h264_amf`. FFmpeg logged the AMD adapter and AMF
initialization; the MP4 contained 180 decoded H.264 frames at 60/1 and passed a full decode. This
proves the local AMD driver/FFmpeg AMF backend can encode. The same explicit adapter route was
then exercised through Klip with `--amd-hardware-smoke-test`: its log reports the AMD Radeon
adapter and `h264_amf`, and it saved a 5.0167-second replay while a 10.0167-second recording was
active. The replay contains 301 decoded H.264 High frames at 1920x1080/60 FPS plus stereo AAC at
48 kHz; the recording contains 601 video frames and stereo AAC at 48 kHz. Both files passed a full
FFmpeg decode and no partial outputs remained. These desktop captures validate the app's AMD WGC
interop, AMF setup, audio mux, concurrent clip/record, and finalization on this one AMD iGPU/driver.
They do not measure motion cadence from a moving test source, performance/quality, AMD runtime
fallback, or cover other AMD driver generations. The acceptance flag and adapter pin affect only
the hidden test invocation; normal adapter selection remains automatic.

DXGI output enumeration (`tools/list-dxgi-display-adapters.cpp`) confirmed that this host's NVIDIA
RTX 3060 owns both active displays, including primary `\\.\DISPLAY2`, while the AMD Radeon adapter
has no attached outputs. Therefore the explicit AMD WGC tests above exercised a cross-adapter path;
they validate AMD AMF, but are not representative of a display physically driven by AMD silicon.
The normal adapter choice on this host matches the display owner.

### AMD 60/120 FPS motion cadence

The `-Amd` option in `tools/run-cadence-test.ps1` runs the native moving-marker fixture against the
AMD-pinned acceptance mode and analyzes decoded frame IDs, not just MP4 FPS metadata. The source
reported about 239 presents/s during these runs.

| Target | Output | Decoded frames | Changed marker IDs/s | Repeated frames | Largest PTS step |
|---|---|---:|---:|---:|---:|
| 60 FPS | Clip | 300 | 60.00 | 0.00% | 16.667 ms |
| 60 FPS | Recording | 600 | 59.90 | 0.17% | 16.667 ms |
| 120 FPS, run 1 | Clip | 601 | 114.80 | 4.33% | 8.334 ms |
| 120 FPS, run 1 | Recording | 1,200 | 116.50 | 2.92% | 8.334 ms |
| 120 FPS, repeat | Clip | 601 | 98.20 | 18.17% | 8.334 ms |
| 120 FPS, repeat | Recording | 1,200 | 100.88 | 15.93% | 8.334 ms |
| 120 FPS, after AMF GOP fix | Clip | 602 | 102.63 | 14.48% | 8.334 ms |
| 120 FPS, after AMF GOP fix | Recording | 1,201 | 101.40 | 15.50% | 8.334 ms |

The normal NVIDIA-selected control on the same host produced 116.41 changed IDs/s in the clip
(2.99% repeats) and 117.63/s in the recording (1.98% repeats). All runs maintained regular output
timestamps and reported zero encoder submission failures. These repeated-image measurements were
captured with AMD rendering the capture device on a host whose active outputs belong to NVIDIA, so
they demonstrate a cross-adapter WGC freshness penalty and do not verify AMD-native display capture.
The latest run after the AMF and record-start fixes contained six keyframes at 0.000, 0.008, 2.008,
4.008, 6.008, and 8.008 seconds, completed a full decode, and retained the requested 10-second
duration. Do not claim AMD-owned-display 120 FPS motion is verified. Run artifacts and analyzer
output are retained in
`work/amd-wgc-native60-20260927/`, `work/amd-wgc-native120-20260927/`,
`work/amd-wgc-native120-repeat-20260927/`, `work/amd-wgc-native120-gopfix-20260927/`,
`work/amd-wgc-native120-keyframefix-20260927/`, `work/amd-wgc-native120-final-20260927/`, and
`work/nvidia-control-native120-20260927/`.

The final runner gate was also exercised on a fresh 120 FPS NVENC run after adding forced-backend
verification. It logged `CAPTURE ACCEPTANCE PASSED encoder=h264_nvenc`; the 10.017-second recording
decoded 1,202 frames with 117.50 unique marker transitions/s and 2.08% consecutive repeats, while
the 5.042-second clip decoded 605 frames with 119.80 transitions/s and 0.17% repeats. PTS was
monotonic at 8.333/8.334 ms. This is another successful but source-dependent high-refresh desktop
run—not a claim that all 120 FPS sources deliver 120 unique frames.

After the packed 24-bit audio conversion, endpoint-recovery, lower Direct3D feature-level, Intel
acceptance-mode, and event-query synchronization changes, Debug and Release were rebuilt and both
CTest targets passed. The current portable package was staged, extracted to a fresh directory, and
its isolated `Klip.exe --package-smoke-test` exited 0 and created first-run settings. The ZIP is
53,680,877 bytes with SHA-256 `40f3a805f3e05ed5cd46dd4c1397e114c741ff6329730fae77e2ae6c07ad77cd`
under `work/package-universal3-20260927/`. Installer creation was skipped; previously generated
installer artifacts predate these changes and must be rebuilt before any release. The package is
still an LLVM-MinGW validation build, not the MSVC/vcpkg production artifact.

The extracted portable package was then run through real WGC clip-plus-record acceptance on this
host. It selected NVENC at 1920x1080/60, saved a 5.033-second clip while a 10.000-second recording
continued, and exited with `CAPTURE ACCEPTANCE PASSED`. `ffprobe` found H.264 60/1 video and AAC
audio in both MP4s. These package-level media checks were performed on the NVIDIA host only.

## Lower D3D feature-level compatibility smoke

The current RTX 3060/Windows 11 host was forced to create its Direct3D device at feature level
10.1 and, separately, 10.0. Each run used the real WGC display capture path, selected NVENC,
saved a replay while recording, and passed the clip/record decode acceptance gate. In the 10.0
run, the log explicitly reported `Direct3D feature level: 10.0`; the 10.017-second recording
decoded 601 frames and the 5.0-second clip 301 frames, both at 60/1 with monotonic 16.667 ms PTS.
The marker fixture was source/compositor-limited to about 51.8 changed IDs/s and 13.67% repeats;
all 10 recording and 5 clip flash/beep markers were detected, with median A/V offsets of -13.17
and -13.33 ms. Artifacts are under `work/universal-fl100-capture-20260927/`. The separate 10.1
acceptance output is in `work/universal-fl101-capture-20260927/`.

These tests show the pipeline works when the D3D device is created with the 10.0/10.1 feature-level
contract, but the physical adapter remains an RTX 3060 capable of higher levels. They do not
validate a real low-end FL10 adapter, its WDDM driver, output format support, or performance. A
physical older adapter remains part of the compatibility matrix.

## Intel hardware acceptance path

Added a strict `--intel-hardware-smoke-test` mode and `tools/run-cadence-test.ps1 -Intel` entry.
It pins the D3D11 device to vendor `0x8086`, requests only `h264_mf` (Media Foundation hardware
encoding), and requires the output backend to match. This prevents a machine without Intel hardware
encoding from appearing compatible through another GPU or the software fallback. The harness
builds and its PowerShell script parses successfully, but it has not been run because this test
environment reports only an NVIDIA RTX 3060 and AMD Radeon(TM) Graphics adapter; no Intel adapter
is available for the hardware acceptance run.

The AMD adapter completed both a native moving-scene test and an FFplay flash/beep A/V test at
60 FPS with capture pinned to AMD (`h264_amf`). In the A/V run, replay and recording both decoded,
had monotonic 60/1 timestamps, contained all 5/10 flash and beep markers respectively, and had
median audio-minus-video offset of -4.67 ms. The fixture/compositor delivered about 49.5 changed
IDs/s with 17.53% repeats in the 10-second recording, so this is cross-adapter pipeline and A/V
evidence, not AMD-native display cadence. Artifacts are under
`work/universal-compat-amd60-current-20260927/` and
`work/universal-compat-amd-av60-20260927/`.

## Final current-host A/V acceptance

The latest Release binary completed a fresh 60 FPS WGC clip-plus-record test on an NVIDIA RTX 3060
at feature level 11.1 using NVENC. The 10.017-second recording decoded 601 frames and the 5.033-
second replay decoded 302 frames; both had exact 60/1 PTS cadence, no nonmonotonic timestamps, and
all 10 recording / 5 clip flash-and-beep markers. Median audio-minus-video offsets were -7.67 ms
for the recording and -7.33 ms for the clip. The visual fixture/compositor supplied about 50.3
unique transitions/s and 16.17% repeats in the recording, so this measures end-to-end correctness,
not 60 unique game frames/s. Artifacts are under
`work/universal-compat-final-av60-20260927/`.

## D3D11 event-query synchronization fallback

Forced the non-fence synchronization path on the RTX 3060 twice using
`tools/run-cadence-test.ps1 -EventQuerySync -RequireAvMarkers`. Both runs logged the forced fallback,
saved a clip during a recording, decoded 60/1 MP4 timestamps with no nonmonotonic frames, and
detected all flash/beep markers (5 in the clip; 10 in the recording). Run 1 had 601 recorded frames,
52.50 unique fixture transitions/s, 12.50% consecutive repeats, and +2.67 ms median audio-minus-
video offset. Run 2 had 601 frames, 48.60 transitions/s, 19.00% repeats, and +5.33 ms median offset.
These motion figures vary with the FFplay/compositor source and are not a game-performance measure.

An initial implementation explicitly flushed after every event-query submission; it dropped to about
3 encoded frames/s and failed the visual-marker gate. Removing per-frame `Flush()` and letting
`GetData` flush pending work restored normal throughput in two clean repeat runs. Preserve this
regression case; successful MP4 finalization alone did not catch it. Artifacts are under
`work/universal-event-query-sync-av60-retry1-20260927/` and
`work/universal-event-query-sync-av60-retry2-20260927/`; the failed first attempt is retained under
`work/universal-event-query-sync-av60-20260927/`.

The combined forced feature-level-10.0 plus event-query run also passed the clip/record decode and
A/V marker gates. It logged the requested 10.0 feature level and query fallback; the 10-second
recording decoded 600 frames with 52.19 unique transitions/s, 13.02% repeats, all 10 marker pairs,
and +0.33 ms median A/V offset. The clip decoded 302 frames with all five markers and +0.67 ms
median offset. This is still the RTX 3060 under a forced FL10 creation contract, not an old physical
GPU test. Artifacts are under `work/universal-fl10-query-av60-20260927/`.

## WGC compositor-clock A/B

The capture admission gate previously used callback arrival time. Trace-only paired measurements
showed the compositor's `SystemRelativeTime` advancing regularly while callback dispatch jitter
crossed the frame admission threshold. On the saved callback-clock baseline, the native moving
scene delivered 1,172 frames but Klip admitted 745 at 60 FPS (63.6%); at 120 FPS it delivered 2,274
and admitted 1,374 (60.4%). This discarded frames before conversion; encoder throughput was not the
limiting stage in those runs.

The gate now uses WGC `SystemRelativeTime` when available and latches that choice for the capture
session. It falls back to callback time if the timestamp is unavailable and logs the selected clock.
The added deterministic 120 FPS jitter test reproduces the prior rejection and requires stable
source-time admission.

On the same RTX 3060 machine, Release, NVENC, and native moving-scene fixture, source-time runs
admitted all delivered frames (1,164/1,164 at 60 FPS; 2,288/2,288 at 120 FPS). The decoded
10-second recordings changed 59.3 → 60.0 unique fixture IDs/s at 60 FPS and 119.9 → 120.0/s at
120 FPS. The 60 FPS replay and recording had 60/1 timestamps; the 120 FPS replay and recording had
120/1 timestamps. There were no nonmonotonic timestamps and no consecutive repeats in the 60 FPS
native recording; the 120 FPS recording had 0.08% consecutive repeats. The corresponding old-clock
recordings had 1.17% repeats at 60 FPS and 0.08% at 120 FPS. Artifacts are under
`work/wgc-clock-native60-20260927/`, `work/wgc-clock-native120-20260927/`,
`work/wgc-sourcetime-native60-20260927/`, and `work/wgc-sourcetime-native120-20260927/`.

An audiovisual FFplay test with the source clock also saved a replay while recording. Both decoded
with 10/10 audio-video markers; the recording contained 601 frames at 60/1, 501 unique fixture
IDs/s (fixture/compositor updates about 50/s), 16.67% intentional source repeats, no nonmonotonic
timestamps, and +6.0 ms median beep-minus-flash offset. This confirms that source-clock admission
does not replace the separate output timeline or break this short A/V case. It does not establish
long-duration drift or microphone-device compatibility. Artifacts are under
`work/wgc-sourcetime-av60-20260927/`.

Exact repeat commands:

```powershell
& tools/build-validation.ps1 -Configuration Debug
& tools/build-validation.ps1 -Configuration Release
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name wgc-sourcetime-native60-20260927
& tools/run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -Name wgc-sourcetime-native120-20260927
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -RequireAvMarkers -Name wgc-sourcetime-av60-20260927
```

These are measurements from one Windows 11 / RTX 3060 / WGC runtime with a deterministic moving
fixture. They validate the demonstrated callback-jitter failure on this machine; they do not prove
equivalent behavior on AMD-native, Intel, integrated-only, low-end, older-Windows, or game-focused
capture systems. The real release remains contingent on those distinct configurations.

## Capture-target switching during recording

The new Windows acceptance mode switches the active WGC target from the moving test window to the
primary display and back while a recording is in progress, and saves a replay clip during that
recording. The first run failed: after the switch, the recording was finalized at about 2.1 seconds
instead of its requested 10 seconds. The failure was traced to `PacketRouter::ResetTimeline`, which
also stopped `RecordingWriter`; both the UI target-selection handlers and the capture target worker
used that operation for an ordinary source change.

The reset now supports clearing replay history without stopping the independent recording writer.
Selecting a valid new target preserves the encoder/media clock and requests a keyframe rather than
restarting the encoder. While WGC opens the new target, Klip repeats the last fully synchronized
frame to keep the output timeline continuous. A completed frame that becomes stale at the GPU wait
boundary is safely returned to the bounded encoder-surface pool. Selecting no valid target still
stops capture and recording as before.

The regression test feeds a keyframe to a recording, resets the replay timeline without stopping,
feeds the next keyframe, finalizes the file, and decodes both frames. The actual Windows/RTX 3060
window → display → window run then passed: the 10-second recording decoded 601 frames at 60/1, with
59.5 unique transitions/s, 0.83% repeats, monotonic timestamps, and a maximum interval of 16.667 ms.
The concurrent 2.0-second replay decoded 121 frames at 60/1 with 59 unique transitions/s and one
repeated frame. Both files contained video and audio streams. The native fixture has no synchronized
flash/beep markers, so this run verifies continuity and decoding, not A/V sync through a source
switch. The clip contains about two seconds because replay history is deliberately cleared on
source change rather than mixing frames from the previous target. Artifacts are under
`work/source-switch-native60-20260927/` (initial failure),
`work/source-switch-native60-fixed-20260927/` (first passing recording-preservation run), and
`work/source-switch-cfr-native60-20260927/` (final continuous-CFR run).

The same source-switch acceptance also passed with capture and encode pinned to the Radeon(TM)
Graphics adapter (`h264_amf`) on this host. The AMD adapter drives no attached display here, so this
is a cross-adapter capture run. Its 9.983-second recording decoded 600 frames at 60/1, 58.90 unique
transitions/s, 1.84% repeats, no nonmonotonic timestamps, and a maximum interval of 16.667 ms. The
1.95-second concurrent clip decoded 118 frames at 60/1 with 57.44 transitions/s, 4.27% repeats,
and no timestamp gaps. Both files contained video and audio streams. Artifacts are under
`work/source-switch-amd-cross60-20260927/`. This does not certify AMD-native display cadence.

After the source-switch changes, a fresh NVENC audiovisual-marker test also passed. The 9.983-
second recording decoded 600 frames at 60/1 and the 5.017-second replay decoded 302 frames at
60/1. All 10 recording and 5 clip flash/beep pairs were detected; median audio-minus-flash offsets
were +12.0 ms and +11.67 ms, with no samples outside the 40 ms tolerance and no nonmonotonic video
timestamps. The fixture/compositor provided about 49.7 unique transitions/s in the recording and
48.2/s in the clip (17.20% and 19.60% repeated frames), so these motion counts describe the
fixture's capture cadence, not game FPS. Artifacts are under
`work/final-av60-after-switchfix-20260927/`.

The post-change native-scene 120 FPS NVENC run also passed. Its recording decoded 1,202 frames over
10.008 seconds and its concurrent replay decoded 604 frames over 5.025 seconds; both were 120/1,
had no consecutive repeated fixture IDs or nonmonotonic timestamps, and had maximum intervals of
8.334 ms. Artifacts are under `work/final-native120-after-switchfix-20260927/`.

Repeat the end-to-end scenario with:

```powershell
& tools/build-validation.ps1 -Configuration Debug
& tools/build-validation.ps1 -Configuration Release
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SourceSwitch -Name source-switch-cfr-native60-20260927
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SourceSwitch -Amd -Name source-switch-amd-cross60-20260927
```

This test covers a source change on one Windows 11/NVENC system. Source switching with AMD AMF,
Intel Media Foundation, physical low-end GPUs, and display/window targets at different dimensions
remains open.

## Universal-compatibility pass (2026-09-27)

These additional runs were made on Windows 11 Home build 26200, an AMD Ryzen 5 7600X, and 31 GiB
RAM. The host exposes an NVIDIA GeForce RTX 3060 (`32.0.15.9595`) and an AMD Radeon(TM) Graphics
adapter (`32.0.21030.2001`); both attached displays are driven by the NVIDIA card. This is a
two-vendor test system, but not a low-end CPU/GPU configuration, and the AMD runs are cross-adapter.

The first cross-device robustness correction addresses source-sized capture on unusually sized
windows. NV12 requires even frame width and height; WGC window sizes are not guaranteed to be even.
Klip now rejects capture targets larger than 8192 pixels on either side and rounds source-sized
odd dimensions down by one pixel (with a log notice) before allocating NV12 surfaces. Unit coverage
checks 1-, even-, odd-, and maximum-sized dimensions. The actual Windows 11/RTX 3060 WGC run
captured a 1281x721 test window in Source mode, logged the NV12 adjustment, and produced a
decodable 1280x720/60 recording (10.000 s) and concurrent clip (5.050 s), both with zero repeated
test-scene frames. Artifacts are under `work/universal-odd-window-20260927/`.

The expanded A/V source-switch acceptance test passed on Windows 11 with an NVIDIA RTX 3060 and
NVENC. It switched from the primary display to a named FFplay WGC window and back during a
15-second recording, saved a clip concurrently, and decoded both MP4s. Recording: 15.000 seconds,
60/1 video, 15 detected flash/beep pairs; clip: 6.817 seconds, 60/1, 7 pairs. Every detected
audio/visual marker pair was within the 40 ms gate. This verifies the marker fixture's sync through
source switching, not all audio endpoint/driver combinations. Artifacts are under
`work/final-av-source-switch-2-20260927/`.

The same A/V source-switch test also passed with the AMD Radeon(TM) Graphics adapter and AMF.
This PC routes both displays through NVIDIA, so the run is explicitly cross-adapter. The 15.000-
second recording decoded 900 frames, had monotonic timestamps and 15 marker pairs (audio offset
range -21.0 to +13.3 ms); the concurrent 6.950-second clip decoded 418 frames with 7 pairs
(-21.0 to +12.3 ms). The recording contained one 33.334 ms output interval among otherwise
16.666/16.667 ms intervals; the 60 FPS container average metadata differs slightly because of
container duration rounding. Source motion was about 50 transitions/s due to the FFplay/compositor
fixture and is not game FPS. Artifacts are under
`work/universal-amd-av-source-switch-20260927/`.

“Universal” remains scoped to supported x64 Windows PCs. The application still requires Windows
Graphics Capture and D3D11 video processing, is not a Linux/macOS/native-ARM64 build, and cannot be
certified across GPU vendors or driver versions without physical test systems. Intel hardware,
AMD-native display, integrated-only/low-end PCs, older Windows builds, audio-device unplug/replug,
and packaged MSVC release capture tests remain open.

Repeat the odd-sized target check with:

```powershell
& tools/build-validation.ps1 -Configuration Debug
& tools/build-validation.ps1 -Configuration Release
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SourceSizedWindow -Name universal-odd-window-20260927
& tools/run-cadence-test.ps1 -Configuration Release -SourceSwitchAv -RequireAvMarkers -Amd -Name universal-amd-av-source-switch-20260927
```

### Unicode Windows paths

An end-to-end capture under a test directory containing `résumé-日本` exposed a Windows path
conversion failure: capture initialized and NVENC produced its first keyframe, but recording
startup threw before it could begin because logging called `std::filesystem::path::string()` on a
path not representable in the active code page. This left the smoke process blocked behind its
fatal-error dialog. Replaced narrow path conversions throughout recording, clips, muxing,
settings diagnostics, overlays, and UI display with a shared UTF-8 conversion helper; added
multilingual-path settings round-trip coverage. The acceptance runner now detects and closes Klip's
fatal/startup dialog for its own test process instead of waiting for the full timeout.

After the fix, the same type of Unicode-root WGC run passed on Windows 11/NVENC. The 10.02-second
recording decoded 602 frames and the 5.02-second clip decoded 302 frames, both with 60 unique
transitions/s, monotonic timestamps, and a 16.67 ms maximum interval. The runner fully decoded both
MP4s from the accented/CJK path; no partial outputs remained. Core config/path tests passed in both
Debug and Release. Artifacts: `work/universal-unicode-path-résumé-日本-fixed-20260927/`.

Reproduce with:

```powershell
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name universal-unicode-path-résumé-日本
```

The runner's fatal-dialog handling was also checked with a forced Intel adapter request on this
machine, which has no Intel graphics adapter. It detected Klip's startup-failure dialog and ended
the negative test in about four seconds rather than timing out. This verifies failure reporting and
test cleanup only; it does not count as Intel encoder coverage.

### Stop-drain and non-hardware fallback checks

The acceptance harness now waits for encoded video/audio packet watermarks before treating a
normal recording stop as complete. This fixed a real stop race: the Media Foundation software
encoder could still hold roughly a dozen delayed video frames when Stop closed the MP4 queue. On
the prior 30 FPS fallback run, the requested 10-second recording was short. After the fix,
`h264_mf_software` produced a 10.067-second/30 FPS recording with monotonic 33.333/33.334 ms PTS,
and the writer logged that its encoder backlog had drained before MP4 finalization. The follow-up
30 FPS file contained 302 decoded frames over 10.067 seconds. Run artifact:
`work/universal-mfsw30-drainfix1-20260927/`.

The A/V marker analyzer was also tightened to identify the fixture's 1 kHz tone rather than
counting any loud audio as a beep; markers are paired one-to-one and unmatched events are reported.
On this host, one 60 FPS `h264_mf_software` clip-plus-record run passed all 10 recording and 5
clip marker checks. Its marker offsets were +9.0 ms median for the recording and +14.0 ms for the
clip. With the 120 FPS audiovisual source driving the compositor, however, only 46.8 unique
fixture transitions/s were measured in the 1080p/60 software-fallback recording (about 22.0%
consecutive repeats); this is a source/compositor-stressed result, not a universal fallback rate.
Enabling the app's 720p performance preset increased that fixture run to 52.9 unique transitions/s
(about 13.8% repeats). A separate native moving-window 720p/60 software-fallback test produced a
complete clip and recording with zero repeated source submissions on this Ryzen 5 7600X host.
Artifacts: `work/universal-mfsw60-avsync-20260927/`,
`work/universal-mfswperformance60-av-20260927/`, and
`work/universal-mfswperformance-native60-20260927/`.

The 30 FPS visual/audio marker check did not meet the current ±40 ms screening threshold in two
software-fallback runs: the concurrent clips measured median audio-minus-video offsets of -52.0
and -62.0 ms; the matching recordings showed a mixture of roughly -20 and -54 to -56 ms marker
offsets. A separate 30 FPS NVENC run showed offsets from about +6 to -28.7 ms across the tested
markers. Since the fixture is displayed through FFplay and WGC, this does not isolate the encoder
as the cause; it does mean 30 FPS software-fallback A/V sync has not passed this test and must not
be represented as verified. Do not loosen the gate or add a compensating delay without a
timestamped source-vs-output diagnosis. Artifacts are under
`work/universal-mfsw30-avfreq-20260927/`,
`work/universal-mfsw30-avsync-repeat-20260927/`,
`work/universal-mfswperformance30-av-20260927/`, and
`work/universal-nvenc30-avsync-20260927/`.

### 30 FPS fallback A/V follow-up

Two fresh software-fallback marker runs after the encoder-surface ownership change passed the
existing ±40 ms gate. Recording marker medians were -28.67 and -30.67 ms; concurrent clip medians
were -26.67 and -27.67 ms. Each matched marker in those files stayed inside ±40 ms. However,
the earlier runs above failed (clip medians -52 and -62 ms), and the fixtures produce only about
28.7–29.4 unique visual transitions/s with 2.0–4.35% repeated frames in these two attempts.
The repeat/replay fixture's render, player, compositor, WASAPI and analysis timing have not been
independently separated. Treat this as intermittent fixture-level evidence, not a confirmed
software encoder defect or broad A/V guarantee; do not add a compensating delay without better
timestamped source/output capture. Fresh artifacts are under
`work/universal-poolguard-sw30av-current/` and `work/universal-poolguard-sw30av-repeat/`.

After the recording-stop watermark change, an additional native moving-window NVENC run at 120 FPS
passed clip-plus-record acceptance. The 10.017-second recording decoded 1,203 frames and the
5.033-second clip 605 frames; both had 120 unique source transitions/s, zero consecutive repeats,
monotonic timestamps, and a maximum 8.334 ms presentation interval. Artifacts:
`work/universal-nvenc120-poststopfix-20260927/`.

These software-fallback checks use the same one Windows 11/RTX 3060 + Radeon system. The native
scene fallback run demonstrates a working path on this CPU, not low-end compatibility. A physical
integrated-only/older CPU test and audio-marker run on a real game remain open release gates.

### CPU-budget sensitivity smoke

The acceptance runner can optionally constrain the Klip process with
`-ProcessorAffinityMask`; this is a CPU-budget stress aid, not a substitute for a low-end PC. On
this Ryzen 5 7600X, pinning the 720p software-fallback run to one logical processor still produced
a smooth native-scene 60 FPS recording (602 decoded frames over 10.017 s, zero repeated frames,
16.667 ms maximum PTS interval) and clip (291 frames over 4.833 s, zero repeats, 16.667 ms maximum
interval). At 30 FPS under the same affinity limit, the recording decoded 296 frames over 10 s and
had two long PTS intervals (100 and 133.334 ms); the concurrent clip decoded 136 frames over
4.567 s and had one 100 ms interval. The configured test gate correctly rejected that clip. This
shows a single-core budget can expose occasional cadence stalls even when encoder calls are fast;
it is not evidence that a physical low-end CPU behaves identically. Artifacts are under
`work/universal-onecore-software30-20260927/` and
`work/universal-onecore-software60-20260927/`.

Repeat with:

```powershell
& tools/run-cadence-test.ps1 -Configuration Release -Fps 30 -NativeScene -SoftwarePerformance -ProcessorAffinityMask 1 -Name universal-onecore-software30
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwarePerformance -ProcessorAffinityMask 1 -Name universal-onecore-software60
& tools/run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -SoftwarePerformance -ProcessorAffinityMask 1 -Name universal-onecore-software120
```

The 120 FPS software-fallback path was added to the acceptance harness and exercised after this
command became available. With the same one-logical-processor affinity, its 720p Performance-mode
recording decoded 1,204 frames over 10.025 s (119.9 decoded FPS), and the concurrent clip decoded
593 frames over 4.933 s (120.2 decoded FPS). Both had monotonic timestamps, 8.334 ms maximum
presentation intervals, one repeated frame per file, and fully decoded video plus 48 kHz AAC. The
capture log reported 1,203 output packets, no skipped scheduler slots, one encoder drop, and 1,133
coalesced stale converted frames while the moving source was arriving at about 238 updates/s and
the output was capped at 120 FPS. Sampled process CPU averaged 48.4% and peaked at 59.0% of the
single logical processor; peak working set was 228 MiB. This is a successful stress result on one
modern Ryzen core, not a performance guarantee for lower-clocked or older CPUs. Artifact:
`work/universal-onecore-software120-20260927/`.

The constrained 30 FPS result was compared against both an FFplay 30 FPS source and an unrestricted
native moving window. With the 30 FPS source and one-core affinity, the recording and clip had a
maximum 33.334 ms presentation interval; the fixture/compositor yielded only about 26 unique
transitions/s, so this verifies output timing rather than full 30-FPS source motion. With the native
moving window and no affinity limit, the software encoder passed the normal acceptance gate: the
10.033-second recording and 4.633-second clip both had exactly 30 unique transitions/s, zero
repeats, monotonic timestamps, and 33.334 ms maximum intervals. The two long intervals from the
one-core native run therefore did not reproduce without the affinity constraint. This narrows the
finding to the constrained scheduling scenario, but does not establish a root cause or certify an
actual low-end PC. Artifacts are under
`work/universal-onecore-software30-source30-20260927/` and
`work/universal-software30-native-unrestricted-20260927/`.

### Interactive shortcut and output smoke

An isolated UI run changed the three global shortcuts to Ctrl+Shift+F9 (save clip), Ctrl+Shift+F10
(start/stop recording), and Ctrl+Shift+F11 (show/hide). The choices persisted after closing and
reopening the app. The custom show/hide chord hid the window and brought it back from another
foreground app. On a real WGC primary-display source, the custom record chord started recording,
the clip chord saved a replay while recording, and the record chord stopped it; the log confirms
the writer drained before publishing the recording. `ffmpeg` fully decoded both files; `ffprobe`
reported H.264 1920x1080/60 plus stereo AAC 48 kHz. Clip duration was 30.421 s and recording
duration was 24.641 s; no partial files remained. All settings and media were isolated under
`work/universal-ui-smoke-20260927/`, leaving the user's normal configuration untouched. This
validates these three custom chords on one Windows 11 system, not every possible modifier/key,
other applications' global-shortcut conflicts, or all supported GPU vendors.

### Runtime encoder recovery while recording

A test-only NVENC fault injector forced two consecutive runtime submission failures during an active
recording on Windows 11 Home build 26200 (Ryzen 5 7600X, RTX 3060 driver `32.0.15.9595`, Radeon
Graphics driver `32.0.21030.2001`). Klip retried NVENC once, rejected it after the second failure,
attempted the configured hardware compatibility backend, then selected `h264_mf_software`. This
exposed a confirmed bug: recovery used the packet router's default timeline reset, which also
finalized the user's recording. Recovery now clears replay history but preserves the active
recording; the reopened H.264 encoder starts a fresh keyframe sequence. The final rerun completed
both the requested 10-second recording and a replay clip, and FFmpeg decoded both files. The
recording was 10.00 s with 589 decoded frames; the concurrent clip was 2.78 s with 158 decoded
frames. Both had monotonic presentation timestamps, zero consecutive repeats, and a maximum frame
interval of 183.33 ms during fallback. The clip starts after replay history was cleared at the
failure, so it is naturally shorter than a normal replay window. This is a deterministic
state-machine test, not a real driver failure; the one-time visible hitch also remains a release
limitation. Artifacts are under `work/universal-runtime-nvenc-failover-final/`. The fault injector
is guarded by `KLIP_ENABLE_TEST_HOOKS`, which defaults OFF; dedicated validation builds enable it
explicitly. A Release build with the option OFF passed both test suites, and its executable
contained neither the fault-injection command-line switch nor the injected-error message.
The test uses Klip's default encoder order: on this NVIDIA D3D device, AMF failed quickly with
“No such device,” Media Foundation hardware open took about 142 ms before failing, then Media
Foundation software opened. That backend-probing delay accounts for most of the measured 183 ms
presentation gap. Skipping a possible hardware fallback would be a compatibility tradeoff, so the
current implementation retains the attempt rather than optimizing one adapter at the expense of
others.

Repeat with:

```powershell
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -RuntimeEncoderFailure -Name universal-runtime-nvenc-failover-final
& tools/build-validation.ps1 -Configuration Release -EnableTestHooks $false
```

Two follow-up checks clarify the failure's user-visible cost. With a 1080p60 FFplay audiovisual
fixture, the baseline NVENC recording measured 50.42 changed fixture IDs/s, 15.97% repeats, and a
16.67 ms maximum PTS interval. With the default-order injected failure and software fallback, the
recording measured 49.70 changed IDs/s, 15.48% repeats, and a single 183.33 ms gap; the concurrent
clip measured 44.19 changed IDs/s and 21.66% repeats over 2.78 seconds. All 10 recording and 3 clip
flash/beep pairs were within the ±40 ms gate (median audio-minus-flash +2.67 ms for recording,
-0.67 ms for clip). The source/compositor-limited fixture is not a game-motion benchmark. Klip's
sampled peak working set/private memory was 405.6/678.6 MiB and CPU time 5.03 s over the 10-second
test; the matching NVENC baseline was 179.7/240.1 MiB and 0.95 s CPU. These are short single-host
process samples, not minimum-requirement guarantees. Artifacts: `work/universal-runtime-failover-av-baseline/`
and `work/universal-runtime-failover-default-av/`.

With the moving native scene, 720p Performance mode, and Klip pinned to one logical processor,
default-order injected runtime recovery passed both media outputs: the 10-second recording decoded
589 frames, 58.70 changed IDs/s, 0.17% repeats; the 2.77-second clip decoded 157 frames, 56.02
changed IDs/s, 0.64% repeats. Both had monotonic timestamps and one 183.33 ms fallback gap.
Sampled peak working set/private memory was 242.7/384.7 MiB and CPU time was 2.58 s over 10 seconds
of wall time. This indicates the reduced-resolution fallback can sustain near-60 motion on this
Ryzen core under the synthetic scene, not that every low-end CPU will. Artifact:
`work/universal-runtime-failover-default-onecore/`.

To separate resolution from encoder quality, a 1080p60 Performance-quality run with the same
one-core limit decoded at 58.70 changed IDs/s with no consecutive repeats and a 200 ms maximum gap
(floating-point representation was 200.00000000000017 ms). The 720p Performance run had a similar
motion rate but a slightly smaller 183.33 ms max gap. A 1080p60 Balanced-quality run under the same
one-core stress did not pass the cadence gate: the recording reached 46.98 changed IDs/s, 17.95%
repeats, and a 300 ms max interval; its clip reached 38.78 changed IDs/s, 30.26% repeats, and a
216.67 ms max interval. Peak private memory was 924 MiB. This single-core simulation on a modern CPU
does not equal a physical low-end PC, but it is evidence that Balanced software encoding at 1080p
can lose motion under a constrained CPU budget. Klip now makes the tested Performance mode / lower
resolution recommendation visible in the software-fallback tooltip; it does not silently alter
the user's chosen quality or output size. Artifacts are under
`work/universal-runtime-failover-1080p-performance-onecore/` and
`work/universal-runtime-failover-default-balanced-onecore/`.

Reproduce the motion/resource and audiovisual recovery checks with:

```powershell
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwarePerformance -ProcessorAffinityMask 1 -RuntimeEncoderFailure -Name universal-runtime-failover-default-onecore
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -ProcessorAffinityMask 1 -RuntimeEncoderFailure -BalancedQuality -Name universal-runtime-failover-default-balanced-onecore
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -ProcessorAffinityMask 1 -RuntimeEncoderFailure -Name universal-runtime-failover-1080p-performance-onecore
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -RuntimeEncoderFailure -RequireAvMarkers -Name universal-runtime-failover-default-av
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -RequireAvMarkers -Name universal-runtime-failover-av-baseline
```

### Media Foundation software timestamp and pacing correction

The default Balanced software fallback previously requested two B-frames from the Media Foundation
software transform. A one-core 1080p run during runtime failover produced duplicate decoded
presentation timestamps (two invalid intervals in the recording), 17.95% repeated frames, and a
300 ms maximum gap; the clip had a 216.67 ms gap. The acceptance harness previously missed the
recording's invalid timestamps because it stopped on the clip's cadence failure. It now checks
monotonic presentation timestamps for every saved clip and recording before applying other gates.

The software fallback now disables B-frames and enables low-delay mode while retaining the user's
selected quality scenario/quality value, output dimensions, and bitrate. Core regression coverage
checks this tuning. After the change, a 1080p60 Balanced software-only run pinned to one logical
processor produced a 10.02-second recording and 4.83-second concurrent clip with 60 changed native
scene IDs/s, zero repeats, 16.667 ms maximum intervals, and no duplicate/nonmonotonic presentation
timestamps. A separate audiovisual software-only run decoded both files with no invalid timestamps;
all 10 recording and 5 clip flash/beep pairs met ±40 ms (median offsets -9.33 ms and +12.33 ms).
The FFplay fixture itself showed about 50 unique changes/s and 15–16% repeats, so that run proves
timestamp and sync handling, not 60 unique source motion. Artifacts are under
`work/universal-balanced-software-no-bframes/` and
`work/universal-balanced-software-av-no-bframes/`.

The post-fix one-core runtime-failure run had no invalid timestamps or repeated frames, but the
recording still showed a single 300 ms interval while the replay clip peaked at 200 ms. This is a
remaining one-time fallback hitch under the severe affinity limit; the software-only steady-state
test passed. Runtime encoder failure remains visibly reported and must not be described as seamless.
Artifact: `work/universal-balanced-failover-mfb0-onecore/`.

Reproduce the steady-state and audio checks with:

```powershell
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwareFallback -BalancedQuality -ProcessorAffinityMask 1 -Name universal-balanced-software-no-bframes
& tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -SoftwareFallback -BalancedQuality -RequireAvMarkers -Name universal-balanced-software-av-no-bframes
```

After these changes, the production-style Release build with `KLIP_ENABLE_TEST_HOOKS=OFF` also
passed an actual WGC/NVENC display-capture run: the 10.02-second recording decoded 602 frames and
the 5.02-second concurrent clip decoded 302. Both were 1920x1080 H.264/60 plus stereo AAC/48 kHz,
with 60 changed native-scene IDs/s, zero repeats, monotonic PTS, and a 16.667 ms maximum interval.
The binary was checked to contain neither test-only fault-injection command-line switch nor its
message. This was a portable LLVM-MinGW Release build, not the packaged MSVC/Inno installer.
Artifact: `work/universal-production-nohooks-final/`.

## Broader Windows software-fallback smoke matrix (2026-09-27)

After the software fallback B-frame/timestamp fix, the production-style Release binary
was rebuilt with test hooks disabled and exercised at 30, 60, and 120 FPS using the
Media Foundation software encoder at 720p Performance mode. Each run saved a clip while
recording, then decoded both outputs and analyzed the synthetic scene IDs and presentation
timestamps.

| Target | Recording | Concurrent clip | Decoded cadence |
|---|---|---|---|
| 30 FPS | 10.07 s, 302 frames | 4.67 s, 140 frames | 30.0 changes/s; zero repeats; monotonic PTS; max interval 33.334 ms |
| 60 FPS | 10.05 s, 603 frames | 4.85 s, 291 frames | 60.0 changes/s; zero repeats; monotonic PTS; max interval 16.667 ms |
| 120 FPS | 10.03 s, 1,203 frames | 4.94 s, 592 frames | 119.8 / 119.6 changes/s; one repeat in each file; monotonic PTS; max interval 16.667 ms |

This is useful evidence that the tested software encode path can handle all three output
rates on the available Ryzen 5 7600X. It remains one high-performance machine, still using
D3D11/WGC for capture; it neither proves operation without a working D3D11 adapter nor
predicts performance on low-end CPUs. Artifacts, decoded analysis, and resource samples are
under `work/universal-followup-mf-software-{30,60,120}/`. The comparison 1080p60 NVENC
capture is under `work/universal-followup-nvenc-60/`; its recording and clip each decoded
at 60 unique transitions/s with zero repeats and regular 16.667 ms presentation intervals.

In the same 10-second process-sampling window, Klip accumulated 0.89 CPU seconds with 1080p60
NVENC (about 9% of one logical processor), compared with 1.56/2.84/5.91 CPU seconds using 720p
software encoding at 30/60/120 FPS (about 16/28/59% of one logical processor). Software-mode
peak working set was about 227–229 MiB and private memory about 343–347 MiB; NVENC peaked at
about 180 MiB working set and 241 MiB private memory. Sampling is one-second Windows process
sampling on this Ryzen 5 7600X; no GPU utilization measure or low-end comparison is implied.

The compatibility scope and next hardware/OS lab matrix are maintained in
[`universal-windows-compatibility.md`](universal-windows-compatibility.md). These runs do
not close the known gaps for Intel, AMD-native displays, integrated-only/low-end systems,
older Windows versions, or physical audio endpoint removal/recovery.

## Recording queue overload prefix regression

The Windows regression suite previously verified a zero-capacity queue rejects the first
packet and separately verified that an ordinary one-packet recording decodes. It now also
uses a four-packet queue and bursts independent MPEG-4 keyframes until the real writer hits
backpressure. The test requires Klip to stop accepting recording packets, report that it is
saving the accepted portion, finalize a saved file, and decode at least one frame from that
file. This protects the intended policy of stopping on compressed-packet loss rather than
continuing with a broken reference chain.

The updated Debug and production-style Release builds passed both CTest suites. The Release
Windows regression executable passed 20 consecutive repetitions, including the overflow case.
This is a deterministic in-process queue stress check; it does not emulate a physically full
disk, filesystem failure, or slow external drive. Code: `tests/windows_tests.cpp`,
`TestRecordingOverloadSavesAcceptedPrefix`.

## Encoder input surface ownership on failures

The 64-surface bound made early-return ownership important: runtime NVENC failure injection,
encoder setup failure, and GPU synchronization failure can happen before FFmpeg's normal
per-frame release callback owns the texture. Added a scoped return guard so unsubmitted surfaces
go back to the reusable pool; when GPU completion is uncertain, the surface is retired and its
allocation slot is released instead of being reused. This prevents repeated failures from
silently consuming the bounded pool. Retained hardware-encoder registrations remain alive until
codec teardown as before.

After the change, the final-source injected two-failure NVENC-to-software runtime recovery test
passed.
The 10-second recording decoded 589 frames and the concurrent clip 159 frames; both had no
repeated frames or timestamp errors. Each still has one 183.33 ms recovery gap, so fallback is
not seamless. A production-style hooks-off NVENC WGC run also passed: 602 recording frames and
302 clip frames, 60 unique scene transitions/s, no repeats or timestamp errors, and max interval
16.667 ms. Both outputs were decoded and cadence-analyzed. Artifacts: `work/universal-poolguard-runtime-final/`
and `work/universal-poolguard-production-final/`. The final Debug, Release-with-hooks, and
production-style Release-with-hooks-off builds each passed both CTest suites; the Release Windows
regression executable also passed 20 repeated runs after the final source change. The production
binary hash was `4EBBE135B5688B6029358221BBE8C347B2A14936C9E241BC4B719CEB53118BDD`.

Reproduce the final verification sequence from PowerShell:

```powershell
.\tools\build-validation.ps1 -Configuration Debug
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $true
& .\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -RuntimeEncoderFailure -Name universal-poolguard-runtime-final
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $false
& .\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name universal-poolguard-production-final
& .\tools\run-cadence-test.ps1 -Configuration Release -Fps 30 -SoftwareFallback -RequireAvMarkers -Name universal-poolguard-sw30av-current
& .\tools\run-cadence-test.ps1 -Configuration Release -Fps 30 -SoftwareFallback -RequireAvMarkers -Name universal-poolguard-sw30av-repeat
```

## Final compatibility follow-up (2026-09-27)

Additional production-style Release tests exercised a selected WGC window with NVENC, a pinned
AMD AMF cross-adapter capture, the video-only setting, and the combined feature-level-10.0 plus
D3D11 event-query synchronization fallback with A/V markers. In all three moving-scene tests,
both the concurrent replay and recording decoded with zero non-monotonic PTS intervals, no repeats,
60 unique scene transitions/s, and a 16.667 ms max frame interval. AMD accepted the explicit
`h264_amf` requirement. This remains one Windows 11 system with an NVIDIA-driven display; AMD
results are cross-adapter only.

The feature-level-10.0/event-query run decoded both outputs and matched 5 clip / 10 recording
audio-visual markers, with offsets from -21.3 to +13.7 ms and no timestamp errors. Its FFplay-based
source showed about 49 unique transitions/s despite regular 60 fps output timestamps; therefore the
result validates the older D3D/synchronization fallback and tested A/V tolerance, not 60 fps source
motion. Video-only clips and recordings decoded successfully; their retained AAC tracks measured
-91 dBFS, confirming silence rather than unintended desktop capture. This does not emulate a
physically absent or removed audio endpoint.

The validation runner now accepts `-VideoOnly` and verifies decoded audio is silent if the muxer
includes an AAC track. The dashboard's runtime audio toggle requires the silent track to remain
available; this preserves in-recording audio toggles. New artifacts are under
`work/universal-final-window60/`, `work/universal-final-amd60/`,
`work/universal-final-video-only60-repeat/`, and
`work/universal-final-fl10-eventquery-av/`. The production-style binary hash remained
`4EBBE135B5688B6029358221BBE8C347B2A14936C9E241BC4B719CEB53118BDD`. Debug and Release test
suites each passed 2/2 CTest cases; `git diff --check` passed after the runner and documentation
updates.

## Hotkey action-path acceptance (2026-09-27)

The Windows test suite now checks more than `RegisterHotKey` success: a hidden real Klip window
receives save-clip, record, and show/hide `WM_HOTKEY` messages and invokes the three expected
callbacks. That registration/message-routing regression passed in Debug and production-style
Release, including 20 repeated Release Windows-test runs.

A separate real capture acceptance mode then registered isolated Ctrl+Alt+Shift+F22/F23/F24
shortcuts and sent the matching `WM_HOTKEY` messages through the actual Klip window while WGC was
capturing the moving D3D11 scene. Save-clip and start/stop-record actions all routed; Klip finalized
a 10.02-second recording and saved a concurrent 5.02-second clip. Both decoded at 60 unique scene
transitions/s, with zero repeated frames, zero invalid PTS intervals, and 16.667 ms maximum frame
spacing. Artifact: `work/rc-hotkey-actions-20260927/`. The test invokes the window messages internally
instead of synthesizing global keyboard input, so it does not prove that a user's configured key
chord reaches Klip while Fortnite or another game has focus.

The later `-HotkeyInput` acceptance path sends isolated Ctrl+Alt+Shift+F22/F23/F24 chords using
`SendInput`, letting Windows deliver the registered global `WM_HOTKEY` events. With Klip hidden,
the test toggled its window, started/stopped recording, and saved a concurrent clip while capturing
a separate moving test window. The 601-frame recording and 302-frame clip decoded at ~60 unique
transitions/s, with 0.17%/0% repeated frames, zero invalid PTS, and 16.667 ms maximum spacing.
Debug and Release each passed both CTest targets; Release had `KLIP_ENABLE_TEST_HOOKS=OFF`. Artifact:
`work/rc-global-hotkeys-all-actions-20260927/`. Binary SHA-256:
`455061FF78AD31998A4CBB22B6F53D90C4C2026BB2AD3C78DC92FEB3A1648927`. This verifies OS key delivery
while Klip is hidden, but is not physical keyboard or Fortnite-focused confirmation.

The resulting production-style Release binary SHA-256 is
`0D82ABB3738D10B760966AE4CDE91F4D11117C6415912633C99B5A971A8D9D4F`. Debug and Release each passed
both CTest targets after the change.

The same final binary passed another production NVENC 120 FPS WGC run: the 10.01-second recording
decoded 1,202 frames and the 5.03-second concurrent clip 605 frames. Both had 120 unique scene
transitions/s, zero repeated images, zero invalid PTS intervals, and 8.334 ms maximum spacing.
Artifact: `work/rc-final-native120-20260927-recheck/`.

The 60 FPS audiovisual fixture test also passed with five matched markers in the clip and ten in
the recording. Offsets were -14.7 to +2.0 ms for the clip and -14.0 to +2.7 ms for the recording,
with monotonic timestamps and 16.667 ms max spacing. This FFplay/compositor fixture itself produced
only 45.1/45.6 unique image transitions/s and about 24% repeated frames; its motion result is not
evidence of a Klip encoder drop. Artifact: `work/rc-final-av60-20260927-recheck/`.

After running the test-hook-enabled runtime failover acceptance, the production Release build was
reconfigured with hooks off and both CTest suites passed. That final binary was exercised again:
the 120 FPS native-scene run decoded 1,203 recording frames and 604 clip frames, both with 120
unique transitions/s, no repeats, zero invalid PTS intervals, and 8.334 ms maximum spacing. The
60 FPS hotkey action-path run decoded both clip and recording at 60 transitions/s with no repeats
or irregular timestamps. The 60 FPS A/V marker run decoded 5 clip and 10 recording markers, with
offsets -24.7..+8.7 ms and -23.0..+10.3 ms respectively; its FFplay fixture showed only 50.0/50.2
unique transitions/s and 16% repeats. Artifacts are under `work/rc-final-production120-20260927/`,
`work/rc-final-production-hotkeys-20260927/`, and `work/rc-final-production-av-20260927/`.

The final production-style Release executable SHA-256 is
`DA2C6A15D81153FA16B41FC22FD5C4B21E520BBE572AC258CA7E3154F3E180CC`. A fresh test-hook-enabled
Release run also exercised two injected NVENC submission failures, recovered to Media Foundation
software encoding, and produced decodable clip/record outputs. It retained a one-time 183.33 ms PTS
gap during transition, so runtime failover is functional but not seamless. Artifact:
`work/rc-final-runtime-failover-20260927/`.

## Repeatable toolchain and cross-backend rerun (2026-09-27)

The local validation bootstrap previously resolved FFmpeg from a mutable `latest` release. It now
pins LLVM-MinGW 20260922, CMake 4.4.3, Ninja 1.13.2, FFmpeg n8.1.2-267, Windows SDK C++
10.0.28000.2705, and Dear ImGui 1.92.8 by direct versioned URL and SHA-256. Cached source archives
are reverified on each bootstrap run. Debug and Release test suites each passed 2/2 with the pinned
dependencies; the final Release cache confirms `KLIP_ENABLE_TEST_HOOKS=OFF`.

Fresh production-style WGC tests on the pinned n8.1.2-line build passed NVIDIA NVENC at 720p120,
AMD AMF cross-adapter at 1080p60, and Media Foundation software at 720p60 Performance. Both replay
clips and recordings were decoded and frame-analyzed. The 60 FPS audio/video run matched 5/5 clip
and 10/10 recording markers at -9.00 ms and -7.33 ms respectively. A two-fault injected NVENC
runtime-recovery test switched to Media Foundation software and finalized both files, but retained
a single 183.33 ms frame interval during restart; fallback is not seamless. The local FFmpeg package
is BtbN `n8.1.2-267-gb2f422d306`; it shares the release workflow's upstream n8.1.2 line, but is not
the exact patched vcpkg build. Artifacts and detailed frame counts are listed in
`docs/universal-windows-compatibility.md`.

The current production-style Release executable, including display-to-adapter diagnostics, is
SHA-256 `5D5BF6C6E08854F4CA02280FD52D9056D27C6A381FAD30EA27F89FF13F1BC3D5`. The currently available
machine remains one Windows 11 desktop with RTX 3060 plus an AMD iGPU; Intel, AMD-native display,
low-end integrated-only, and older-Windows lab coverage remain outstanding. This is not a
widespread-deployment certification.

## Local compatibility and support-report UI (2026-09-27)

Settings now reports Windows build/architecture, D3D feature level, current capture activity,
encoder actually opened for capture, desktop audio signal state, microphone endpoint/activity,
same- vs cross-adapter display routing, and available clip storage. It offers an explicit five-
second local test recording; it requires live capture/encoder and at least 512 MiB free at both
clip and recording destinations, stops automatically, and reports success only after the completed
recording appears in state. A visible
UI run completed and saved a 5.05-second 1280x720 H.264 + 48 kHz stereo AAC MP4 (304 decoded frames,
0 invalid presentation intervals, 16.667 ms maximum interval). This verifies initialization,
encoding, mux/finalization, and file writing on this host; the short check does not certify audible
quality or long-run A/V drift. The file remains in the user's configured Recordings folder.

Support diagnostics now open as an in-settings preview; nothing is transmitted. Adapter/device
names, capture-source labels, output directory, microphone names, and full error text are omitted by
default. The user can opt to include them, review the report, then explicitly choose “copy reviewed
report.” Both default-redacted and opt-in preview states were inspected in the live UI; no report
was copied during verification. The compatibility page showed Windows 11 build 26200, x64, D3D
11.1, active WGC/NVENC, and same-adapter routing on the current machine. Its desktop-audio row
correctly prompted for signal because there was no live desktop audio at that moment.

A fresh Release 60 FPS capture acceptance after GPU-routing state was wired through also passed:
clip and recording were finalized with NVENC; logs reported display/WGC on the same RTX 3060.
Debug and Release CTest each passed 2/2, the Release configuration has test hooks off, and
`git diff --check` is clean. Current test-hook-free Release SHA-256:
`BD976F58077A322884487757F27E5A04E503EA373A13B8100D1219FDD805FDE9`.

Display-routing diagnostics were verified end-to-end for NVIDIA same-adapter display capture, AMD
cross-adapter display capture, and NVIDIA-selected-window capture. Each acceptance run still
finalized and decoded its clip and recording; the AMD log explicitly identifies the NVIDIA display
adapter and AMD WGC device rather than leaving the routing relationship implicit.

## Long-run A/V sync and high-refresh display comparison (2026-09-27)

Added `-LongAvSync` to the repeatable acceptance runner. It generates a 70-second 60 FPS / 48 kHz
source with paired one-second visual flashes and audio beeps, captures a 60-second recording plus
concurrent replay clip, decodes every frame, pairs markers, and computes linear A/V offset drift.
The recording contained 3,602 decoded frames over 60.017 seconds (60.02 encoded FPS), 60/60 paired
markers, median offset -3.00 ms, and fitted drift +0.66 ms/min. Marker offsets were -3 ms for 56
markers and -19.667 ms for the final four; no marker exceeded 40 ms. The clip contained 55 paired
markers. This verifies a sustained one-minute audio path on this endpoint, not multi-hour drift or
physical microphone hot-plug behavior. Artifact: `work/rc-long-av-drift-20260927/`.

This run also shows why encoded FPS is not enough: the decoded recording had 23.60% repeated
images and 45.84 unique transitions/s, while the original 60 FPS fixture decoded with 60 unique
transitions/s and no repeats. A separate same-machine 60 FPS native moving-scene run yielded 601
recording frames at 60.00 transitions/s with no repeats and 16.667 ms maximum spacing; its
concurrent clip yielded 302 frames at 60 transitions/s, also with no repeats. Thus the test points
to a playback/compositor fixture cadence problem on this 239 Hz display (or another input-side
interaction), not an encoder/timestamp failure. It is not yet isolated enough to call a Klip bug or
to dismiss reports from real games. The long-run A/V acceptance records this warning rather than
using nominal 60 FPS as a smoothness claim. Artifacts: `work/rc-long-av-drift-20260927/` and
`work/rc-long-av-comparison-native60-20260927/`.

The host was Windows 11, Ryzen 5 7600X (6 cores/12 threads), NVIDIA GeForce RTX 3060 (driver
32.0.15.9595) plus AMD Radeon integrated graphics, 1920x1080 at 239 Hz. Capture used the NVIDIA
adapter with WGC on the same adapter and NVENC; D3D feature level 11.1. Intel, AMD-native display,
low-end PCs, earlier Windows, and physical game-focused tests remain open. Debug and Release CTest
each passed 2/2 with test hooks disabled. Current test-hook-free Release SHA-256:
`4D5A30B57F994C0902AA40A365D62F15D457869718EC76DD21F9F67AEFE1DFF2`.

Reproduction:

```powershell
.\tools\build-validation.ps1 -Configuration Debug -EnableTestHooks $false
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $false
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -SourceFps 60 -LongAvSync -RequireAvMarkers -Name rc-long-av-drift
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name rc-native-display-60
```
