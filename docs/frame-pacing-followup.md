# Fortnite frame pacing follow-up (2026-09-27)

The current Release executable is `build-validation-Release/Klip.exe` (SHA-256
`71f6834e35f90f3b4f2384ca3f4dc5084572d521e9e8e9b71dd01f55f6035fa3`).
It has not passed a same-scene, focused Fortnite comparison against OBS. Do not
describe its motion as equivalent to OBS or publish it as a frame-pacing fix.

## What changed

The encoding worker now waits on a per-thread high-resolution Windows timer and
selects completed WGC frames at the output deadline. It keeps a bounded frame of
lookahead, uses the WGC source timestamp on the audio clock, and anchors startup
to a fresh source frame after encoder initialization. Similar-rate sources keep
frames through normal arrival jitter; faster sources are downsampled. Source
changes reset the cadence classifier. The queues remain bounded. OBS's fixed
video clock and WGC latest-texture handoff were studied as references; no OBS
code was copied. OBS Game Capture uses a different hooked input path.

## Measured on this host

Windows 11, RTX 3060, Ryzen 5 7600X, 239 Hz monitor, H.264 NVENC. Native WGC
test window presented at the stated input FPS; Klip saved a 10-second recording
and a replay clip during that recording. The analyzer decodes a frame ID from
each image, so these values measure distinct motion, not MP4 header FPS.

| Input/output | Earlier build recording | Current recording | Current clip |
| --- | ---: | ---: | ---: |
| 60/60 FPS | 48.1 distinct updates/s, 19.83% repeats | 57.8/s, 3.67% repeats | 60.0/s, 0% repeats |
| 120/120 FPS | 103.7/s, 13.56% repeats | 118.7/s, 1.08% repeats | 118.8/s, 1.0% repeats |

With a 120 FPS WGC input and 60 FPS output, the current build produced 59.7
distinct updates/s in the recording and 59.8/s in the clip. The fast-source
downsampling path passed its 58/s motion gate.

The current 60 FPS recording **failed** the new minimum 58 distinct updates/s
motion gate. Do not round this up to a passing 60 FPS test. The audiovisual
marker test passed its ±40 ms sync gate, but its decoded FFplay display motion
was only 48.4 distinct updates/s in the recording. The source-switch test
passed. Debug and Release both passed the two core/Windows test suites.

Fortnite WGC window capture with a hidden acceptance process saved a recording
and concurrent clip using NVENC, with no encode failures and approximately
1.1–1.3 ms average encode time. For roughly the first four seconds the WGC
callback supplied about 120 updates/s; after that it supplied about 30/s and
Klip repeated frames to fill a 60 FPS file. The game may have lost focus during
this run. This test proves that source starvation can cause the reported symptom
but does **not** establish that it happened in the user's focused-game clip.

The user's recent 20-second Fortnite clip is 60 FPS in metadata and has no
large run of identical full frames. Gameplay motion can still be irregular;
animated UI and scenery make identical-frame counting insufficient to prove
smooth camera movement. A fresh matched OBS clip was unavailable for direct
frame-by-frame comparison.

## Remaining release gate

Record the same continuous Fortnite camera movement with the game focused in
Klip and OBS, at the same output resolution/FPS/codec, while logging WGC source
FPS, accepted frames, unique encoder submissions, repeats, and skipped slots.
Inspect decoded camera-motion cadence and A/V sync. If WGC stops delivering
smooth source frames while OBS's hooked Game Capture remains smooth, an encoder,
bitrate, or MP4 change cannot restore the missing movement; the capture backend
needs to change. Test that backend on Fortnite and anti-cheat protected games
before calling it a supported solution.

## Repeatable checks

```powershell
& tools/build-validation.ps1 -Configuration Debug -EnableTestHooks $false
& tools/build-validation.ps1 -Configuration Release -EnableTestHooks $false
& tools/run-cadence-test.ps1 -NativeScene -WindowCapture -NativeSourceFps 60 -MinimumUniqueFps 58 -Name pacing-final-native60
& tools/run-cadence-test.ps1 -NativeScene -WindowCapture -Fps 120 -NativeSourceFps 120 -MinimumUniqueFps 116 -Name pacing-final-native120
& tools/run-cadence-test.ps1 -NativeScene -WindowCapture -NativeSourceFps 120 -Fps 60 -MinimumUniqueFps 58 -Name pacing-final-fastsource60
& tools/run-cadence-test.ps1 -SourceFps 60 -RequireAvMarkers -Name pacing-final-av60
& tools/run-cadence-test.ps1 -NativeScene -SourceSwitch -Name pacing-final-switch
```

Detailed logs and decoded analyses are under `work/pacing-final-*` and
`work/pacing-fortnite-window`. The old baseline is under
`work/pacing-before-native60` and `work/pacing-before-native120`.

OBS references:

- https://github.com/obsproject/obs-studio/blob/master/libobs/obs-video.c
- https://github.com/obsproject/obs-studio/blob/master/libobs-winrt/winrt-capture.cpp
- https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/game-capture.c
