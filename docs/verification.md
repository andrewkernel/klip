# Verification checklist

## Automated

Run both configurations from a Visual Studio developer shell:

```powershell
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DFFMPEG_ROOT=C:/ffmpeg
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

CTest runs `klip_core_tests` and `klip_windows_tests`. Together they cover configuration and settings,
bounded-queue overflow/closure, video timeline and clip selection, timestamp rebasing, configured
encoder priority, coherent concurrent state snapshots, WAV storage-format conversion (including
packed 24-bit PCM), partial global-hotkey conflicts, and recording-writer failure behavior.

For local LLVM-MinGW validation without Visual Studio, `tools/bootstrap-validation.ps1` installs a
locked toolset: LLVM-MinGW 20260922, CMake 4.4.3, Ninja 1.13.2, FFmpeg n8.1.2-267, Windows SDK C++
10.0.28000.2705, and Dear ImGui 1.92.8. Every archive is SHA-256 checked before extraction and
cached archives are checked on every run; this avoids a moving `latest` FFmpeg build invalidating
test-to-test comparisons. The FFmpeg package is pinned to a Windows LGPL shared build at commit
`gb2f422d306`, close to but not byte-identical with the release workflow's patched upstream n8.1.2
vcpkg build. `tools/build-validation.ps1` accepts `-EnableTestHooks $false` for a
production-style Release binary.

The Win64 release workflow also runs `Klip.exe --package-smoke-test` from both a silent
per-user installation and an extracted portable ZIP on a clean GitHub Windows runner. This
verifies Windows loader/DLL resolution and isolated first-run settings creation before the
installer is uninstalled. It intentionally does not claim hardware capture validation on the
hosted VM; vendor GPU, WGC, WASAPI, and media-quality coverage remains in the checklist below.

Set `KLIP_DATA_ROOT` to an isolated directory for smoke tests that must not touch the normal
LocalAppData/Videos locations. The override affects settings, logs, clips, and recordings only.

### Hardware capture acceptance mode

Run this from an installed or portable release directory on each physical GPU:

```powershell
$env:KLIP_DATA_ROOT = Join-Path $PWD "capture-acceptance"
.\Klip.exe --capture-smoke-test
$LASTEXITCODE
Get-Content "$env:KLIP_DATA_ROOT\klip.log"
```

Use `--video-only-smoke-test` on a clean data root to confirm WGC capture can start with the
desktop-audio endpoint intentionally unopened. This verifies the video-only configuration path;
it does not emulate missing or defective WASAPI drivers. Klip now reports and retries unexpected
audio-worker/endpoint failures, but physical device removal, reconnection, and selected-microphone
recovery remain separate hardware checks.

For the automated moving-scene variant, run
`tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -VideoOnly -Name <unique-name>`.
It decodes both outputs and, if the MP4 contains the intentionally retained AAC track, verifies
that the track is silent (at or below -80 dBFS). The current muxer keeps this silent track so users
can toggle audio capture while recording; that behavior is distinct from capture on a machine with
no physical audio endpoint.

To verify shortcut registration and route the clip/record actions through Klip's actual window
message handler during real WGC capture, run:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -HotkeyActions -Name hotkey-actions-run1
```

The test registers isolated Ctrl+Alt+Shift+F22/F23/F24 shortcuts, dispatches their corresponding
`WM_HOTKEY` messages through the app window, and requires both a concurrent replay clip and
finalized recording to decode. It does not synthesize global keyboard input. For the Windows
registered-hotkey path, run:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -WindowCapture -HotkeyInput -Name global-hotkey-input
```

This sends Ctrl+Alt+Shift+F22/F23/F24 through `SendInput` while Klip starts hidden and captures a
separate moving test window. It verifies show/hide, save-clip, and record start/stop via OS hotkey
delivery and decodes both media outputs. It is stronger than posting `WM_HOTKEY` directly, but
still does not replace physical keyboard confirmation with Fortnite focused.

On a hybrid system with an AMD adapter, run `--amd-hardware-smoke-test` to pin the hidden acceptance
run to the first hardware DXGI adapter with AMD vendor ID `0x1002` and require `h264_amf`. The test
must fail rather than silently select NVIDIA or software. Confirm the log names the AMD adapter,
the selected encoder is `h264_amf`, and both the replay and recording decode. This is a test-only
adapter override; regular Klip startup still uses automatic adapter selection.

On an Intel graphics system, run the matching Media Foundation hardware path:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -NativeScene -Intel -Fps 60 -Name intel-native60
```

This pins Direct3D to Intel vendor ID `0x8086`, restricts encoding to `h264_mf` with hardware
encoding requested, and requires the acceptance log to report that encoder. It must fail instead
of passing on another vendor or the software fallback. Save the adapter name, feature level, driver,
and both decoded media outputs with the test record. The test is ready to run, but no Intel GPU has
been exercised in the current lab. To include audio/video marker checks, omit `-NativeScene` and
add `-RequireAvMarkers`; the FFplay fixture supplies both streams.

For moving-source cadence, use the native marker and analyze the decoded files on both AMD output
rates:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -NativeScene -Amd -Fps 60 -Name amd-native60
.\tools\run-cadence-test.ps1 -Configuration Release -NativeScene -Amd -Fps 120 -Name amd-native120
```

To force the device-creation path to Direct3D feature level 10.0 on the local adapter and validate
WGC, encoder startup, clip saving during recording, and final file decode:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -FeatureLevel10 -Name feature-level-10-0
```

Passing this forced-level test on a newer GPU is not equivalent to testing a physical older GPU or
its driver. Record its actual adapter and feature level from `klip.log`.

To exercise the lower-version GPU synchronization fallback even when the current adapter supports
D3D11.4 fences:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -EventQuerySync -RequireAvMarkers -Name event-query-sync-av
```

Require `GPU synchronization: forced D3D11 event-query fallback`, valid decoded clip and recording,
and successful A/V marker checks. This verifies the fallback implementation on the current driver;
it does not substitute for testing an older physical GPU/driver. Combine `-FeatureLevel10` with
`-EventQuerySync` to exercise both compatibility fallbacks together at feature level 10.0.

At 120 FPS, repeat the measurement: adapter/source cadence can vary, so one successful run is not
enough to rule out repeated frames. `-Amd` pins to the first AMD hardware adapter and requires AMF.
The current lab PC's AMD adapter owns no active display output, so these commands exercise a
cross-adapter WGC path rather than AMD-native display capture. To certify AMD motion cadence, move
the test display connection to the AMD adapter, verify the DXGI output-to-adapter mapping, then
repeat the 60/120 tests. Do not treat the current variable 120 FPS marker results as an encoder
failure or as proof of AMD-native behavior.

The local validation toolchain and pinned FFmpeg build were rechecked with NVIDIA 120 FPS, AMD
cross-adapter 60 FPS, software-fallback 720p60, and A/V-marker runs on 2026-09-27. Their decoded
outputs and measurements are recorded in `docs/universal-windows-compatibility.md`; these add
repeatability evidence but do not expand the physical hardware matrix.

For release-version-line coverage using the locked FFmpeg n8.1.2 build:

```powershell
.\tools\bootstrap-validation.ps1
.\tools\build-validation.ps1 -Configuration Debug
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $false
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -Name release-ffmpeg812-nvenc120
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Amd -Name release-ffmpeg812-amd60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwarePerformance -Name release-ffmpeg812-software60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -RequireAvMarkers -Name release-ffmpeg812-av60
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $true
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -RuntimeEncoderFailure -Name release-ffmpeg812-runtime-recovery
.\tools\build-validation.ps1 -Configuration Release -EnableTestHooks $false
```

Use a fresh name if any artifact directory already exists. Runtime fault injection requires hooks ON;
the final Release configuration must be restored with hooks OFF after the injected-failure run.

To verify actual WGC HWND capture separately from monitor capture, run the same marker fixture with
`-WindowCapture` at both output rates:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -WindowCapture -Name window-native60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -WindowCapture -Name window-native120
```

The fixture window must be selected by its exact title/executable label in `klip.log`; the analyzer
then checks decoded frame IDs and timestamps for both replay and recording. This verifies the WGC
window path, not game-specific capture restrictions, anti-cheat policy, or exclusive-fullscreen.

To verify the compatibility fallback independently of the local hardware encoders, run:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwareFallback -Name mf-software-native60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -SoftwarePerformance -Name mf-software-performance720-native60
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -FallbackDiagnostics -Name encoder-fallback-diagnostics
```

Both modes must explicitly log `h264_mf_software`; they must not silently pass via NVENC or AMF.
The fallback-diagnostics mode places a deliberately missing encoder before the software encoder;
require the dashboard/log to name the rejected codec and reason, then verify both media outputs.
`-SoftwarePerformance` selects 1280x720/60 at 8 Mbps. The cadence harness decodes every emitted
MP4, checks that the continuous recording is at least 9.75 seconds (or 59.75 seconds in stress
mode), requires at least two video keyframes, rejects leftover `.partial` files, and requires a
terminal capture-acceptance success in the application log. When a backend is forced, it also
asserts the success log names that exact backend; an acceptance run cannot pass on the wrong GPU or
software encoder.

To run the FFplay flash/beep A/V check while outputting at 120 FPS, add `-RequireAvMarkers`:

```powershell
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -SourceFps 120 -RequireAvMarkers -Name av120
```

This additionally requires at least three matched audio/visual markers per output and rejects
matches farther than 40 ms apart. The FFplay/window-compositor fixture may itself provide fewer
than 120 distinct visual updates; report decoded unique-motion rate separately, and do not count
nominal output FPS as proof of smooth motion.

The mode stays hidden, selects the primary display, disables overlays and microphone capture,
uses the performance preset at 1920x1080/60 FPS, and lets adapter-aware encoder priority choose
NVENC, AMF, or Media Foundation. It records five seconds through the real WGC, WASAPI, encoder,
packet-router, and MP4 muxer path. Exit code `0`, a finalized MP4 of at least 64 KiB, and a
`CAPTURE ACCEPTANCE PASSED` log entry are required. On an AMD machine, `encoder=h264_amf` proves
the native AMF path; a Media Foundation result proves compatibility fallback only and must be
reported separately.

Use `--settings-apply-smoke-test` for the recording-quality gate. It applies the Balanced preset
at 1920x1080/60 FPS, saves a replay clip, then saves a five-second recording. For both MP4 files,
require `r_frame_rate=60/1`, `avg_frame_rate=60/1`, H.264 High profile, BT.709 primaries/transfer/
matrix, TV range, and B-frames when the selected hardware encoder supports them.

### OBS-parity quality smoke (2026-07-21)

- Compared the current Klip pipeline against OBS Studio's recording-oriented NVENC defaults and
  fixed-frame video clock. Balanced NVENC now uses P5/HQ, quarter-resolution multipass, adaptive
  quantization, lookahead, a two-second GOP, and two B-frames.
- On an NVIDIA RTX 3060, the isolated settings-apply acceptance test saved a 90-frame,
  1.500-second replay and a 272-frame, 4.533-second recording. `ffprobe` reported exact 60/1
  nominal and average frame rates for both, 1920x1080 H.264 High, two B-frames, BT.709 TV range,
  and approximately 6.0 Mbps.

### Local implementation smoke (2026-07-17)

- All 32 application translation units compiled and linked for Win64 with LLVM-MinGW using
  `-Wall -Wextra -Wpedantic -Werror`; the current deterministic core tests passed.
- On Windows with an NVIDIA RTX 3060, display capture and an explicitly selected Notepad
  game/window target both started through WGC, selected `h264_nvenc`, emitted an immediate
  keyframe, and exited cleanly.
- A display-capture smoke saved a replay while a continuous recording was active. `ffprobe`
  reported H.264 1920x1080 at about 30 FPS plus stereo AAC at 48 kHz for both outputs: a
  3.867-second clip and a 7.869-second recording. No `.partial` file remained.
- The packaging script produced and validated portable-package fixtures with `-SkipInstaller`,
  including the pinned
  FFmpeg source bundle. The production MSVC/Inno Setup job is defined in the release workflow;
  publishing that job requires pushing a tag and was intentionally not performed locally.

### Audio regression smoke (2026-07-18)

- The reported 60.309-second Fortnite clip was decoded to PCM. Its mixer had encoded only the
  first part of each 1024-sample AAC frame and padded the remainder with zeros; the eight
  128-sample positions had an intra-frame energy ratio effectively equal to zero, repeating at
  21.3 ms and explaining the audible static.
- After the full-frame readiness fix, an 18.731-second controlled desktop recording kept all
  eight positions within 99.8% of one another. A second 30.784-second recording with the Razer
  microphone and desktop loopback mixed together retained 90.2% uniformity despite naturally
  changing input. Both contained H.264 plus stereo 48 kHz AAC and left no `.partial` files.

## Windows hardware

For frame-pacing regressions, also follow `docs/frame-pacing.md`. Record **source FPS**
and **output FPS** separately. A nominal 60 FPS MP4 can contain repeated source
frames, so `ffprobe` frame-rate metadata alone is not evidence of smooth motion.

Verify on each supported GPU/vendor and intended Windows release:

1. `Klip.exe` starts and the ImGui window renders.
2. Game/window and display source lists populate; each listed target can be selected and is remembered after reopening Klip.
3. Targets can be resized and closed without a hang or stale texture use.
   The automated moving-window resize check is available as
   `tools/run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -ResizeSource`; it
   verifies that a recording and concurrent clip remain decodable at the size latched when the
   capture pipeline began. It does not guarantee no transient frame loss during resize.
4. Desktop audio activity and level respond.
5. Microphone capture can be disabled, enabled, and switched.
6. `Alt+X` toggles the window, `Alt+C` requests a clip, and `Alt+R` toggles continuous recording.
7. Start/stop a recording after at least one GOP; confirm the MP4 begins on a keyframe, is published only after `.partial` finalization, and appears in `recordings/`.
8. While recording, request a clip and confirm both outputs continue from the same encoded packet stream without a second capture session.
9. Repeated saves do not stall capture and queue/drop metrics remain credible.
10. Completed MP4 files open, begin on a decodable video keyframe, and retain A/V sync.
11. The expected GPU encoder is selected; removing one encoder from the FFmpeg build exercises fallback.
    Run `--capture-smoke-test` and `--software-fallback-smoke-test`, then retain their logs
    and MP4s as acceptance evidence.
12. Repeated start/exit cycles do not hang; no worker remains after process exit.
13. Change FPS, resolution, bitrate, encoder, quality, cursor, replay duration, audio bitrate,
    and storage settings; press **Save & Apply** and confirm capture resumes without an app
    restart. Reopen Klip afterward and confirm every saved value is restored.
14. Confirm clips, recordings, settings, and logs land in the documented user folders when launched from the installer and portable package.
15. Install and uninstall as a standard user; verify the installer requests no elevation and does not remove user media/settings.
16. Confirm the interactive installer displays `EULA.txt`, cannot continue until **I accept the agreement** is selected, and installs `EULA.txt`, `PRIVACY.md`, and third-party notices beside the application.
17. Confirm Explorer, the title bar, taskbar, Start menu shortcut, installer, and uninstaller all show the Klip icon and the executable reports the expected product/file version.
18. Run Application Verifier or an equivalent native diagnostic to check handles/heaps, and inspect the D3D debug layer where available.

Release-candidate additions:

- Rebind each action to a different Ctrl/Alt/Shift combination, save, and confirm the new
  shortcuts work globally without a restart; confirm duplicate and unavailable chords are rejected.
- Enable performance mode and confirm it applies 1280x720, 60 FPS, 8 Mbps, automatic encoder
  selection, and the performance encoder preset without restarting, then reopen and confirm
  the preset persists.

Do not interpret passing core tests as validation of WGC, WASAPI, GPU drivers, FFmpeg hardware interoperability, or media quality.

## Current release-candidate evidence (2026-09-27)

The portable LLVM-MinGW validation setup and controlled 60/120 FPS capture results are
recorded in [release-candidate-results.md](release-candidate-results.md). The tests exercise WGC,
NVIDIA NVENC, AMD AMF on a cross-adapter setup, Media Foundation software fallback, replay saving
during continuous recording, a 60-second stress recording, and decoded marker motion cadence.
They do not validate the packaged MSVC build, an actual game-hook comparison, Intel hardware,
long-term microphone behavior, AMD-native display cadence, physical driver-induced hardware encoder
failure, or broad Windows/GPU configurations. A separate test-injected NVENC failure now exercises
the runtime retry/fallback state machine and proves recording continues through a software fallback;
it is not a substitute for inducing a real driver/backend failure.

The repeatable runner also has a decoded one-minute A/V drift test. On the available 239 Hz
Windows 11 display, the recording's 60 visual/audio marker pairs measured +0.66 ms/min drift and
-3 ms median audio offset. Its FFplay fixture playback had 23.6% repeated decoded images despite
clean 60 FPS source media; a same-machine native motion test had 60 unique transitions/s and no
repeats. Validate the actual source/compositor cadence independently of encoded FPS, then test with
game content before treating that fixture's repeated images as a Klip encoding defect. See the
complete measurements and machine details in `docs/release-candidate-results.md`.

Run the repository's portable checks from PowerShell with
`tools/build-validation.ps1 -Configuration Debug` and `tools/build-validation.ps1
-Configuration Release`. Those scripts build Klip and run both `klip_core_tests` and
`klip_windows_tests` via CTest.
