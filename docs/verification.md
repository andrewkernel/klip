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

`klip_tests` covers default/invalid configuration, persisted-settings round trips, bounded queue overflow, queue closure, complete audio-frame coverage, keyframe-aware clip selection, timestamp rebasing, configured encoder priority, and coherent concurrent state snapshots.

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

Verify on each supported GPU/vendor and intended Windows release:

1. `Klip.exe` starts and the ImGui window renders.
2. Game/window and display source lists populate; each listed target can be selected and is remembered after reopening Klip.
3. Targets can be resized and closed without a hang or stale texture use.
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
