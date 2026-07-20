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
2. Game/window and display source lists populate; each listed target can be selected and is remembered after restart.
3. Targets can be resized and closed without a hang or stale texture use.
4. Desktop audio activity and level respond.
5. Microphone capture can be disabled, enabled, and switched.
6. `Alt+X` toggles the window, `Alt+C` requests a clip, and `Alt+R` toggles continuous recording.
7. Start/stop a recording after at least one GOP; confirm the MP4 begins on a keyframe, is published only after `.partial` finalization, and appears in `recordings/`.
8. While recording, request a clip and confirm both outputs continue from the same encoded packet stream without a second capture session.
9. Repeated saves do not stall capture and queue/drop metrics remain credible.
10. Completed MP4 files open, begin on a decodable video keyframe, and retain A/V sync.
11. The expected GPU encoder is selected; removing one encoder from the FFmpeg build exercises fallback.
12. Repeated start/exit cycles do not hang; no worker remains after process exit.
13. Change FPS, resolution, bitrate, encoder, quality, cursor, replay duration, audio bitrate, and storage settings; restart and confirm every saved value is restored.
14. Confirm clips, recordings, settings, and logs land in the documented user folders when launched from the installer and portable package.
15. Install and uninstall as a standard user; verify the installer requests no elevation and does not remove user media/settings.
16. Confirm the interactive installer displays `EULA.txt`, cannot continue until **I accept the agreement** is selected, and installs `EULA.txt`, `PRIVACY.md`, and third-party notices beside the application.
17. Confirm Explorer, the title bar, taskbar, Start menu shortcut, installer, and uninstaller all show the Klip icon and the executable reports the expected product/file version.
18. Run Application Verifier or an equivalent native diagnostic to check handles/heaps, and inspect the D3D debug layer where available.

Release-candidate additions:

- Rebind each action to a different Ctrl/Alt/Shift combination, save, and confirm the new
  shortcuts work globally without a restart; confirm duplicate and unavailable chords are rejected.
- Enable performance mode and confirm it applies 1280x720, 60 FPS, 8 Mbps, automatic encoder
  selection, and the performance encoder preset, then restart and confirm the preset persists.

Do not interpret passing core tests as validation of WGC, WASAPI, GPU drivers, FFmpeg hardware interoperability, or media quality.
