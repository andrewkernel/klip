# Controlled libobs comparison — 2026-10-07

## Outcome

Both saved 60 FPS replays decoded to **60 distinct updates/second, zero repeated
frames, and monotonic presentation timestamps** in this controlled D3D11 test.
Klip's parent process used less memory, but **more CPU** in these initial short
samples. This is not evidence that Klip is universally faster than OBS, nor a
Fortnite release sign-off. No game benchmark or deployment was performed.

## Hardware and matched configuration

- Windows 11 build 26200.8655, Ryzen 5 7600X, 12 logical processors, RTX 3060,
  NVIDIA driver 32.0.15.9595. Primary display 1920×1080 at 240 Hz.
- Official OBS Studio/libobs 32.1.2, source
  `fb4d98bf88fae5fc85cb11fc57f7c5e309282194`, official archive SHA-256
  `8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6`.
- OBS Game Capture of the same native D3D11 fixture; cursor off, 1080p60,
  NV12/Rec.709/limited, bicubic; NVENC `obs_nvenc_h264_tex`, CQP18/P5/HQ,
  quarter-resolution multipass, High profile, two B-frames, AQ on/lookahead off,
  two-second keyframe interval. Confirmed in both encoder logs.
- Default desktop audio only, unity volume, stereo 48 kHz AAC192; microphone
  and overlays off. Replay duration 15 seconds, 256 MiB maximum. MKV outputs.
- OBS minimized with preview disabled; Klip dashboard hidden. One application
  at a time. No compilation or media analysis during resource sampling.
- Klip Release binary SHA-256
  `425B3D13F5FD90F8B5A620F4715562D6D7B23E818D69874608225D51E76B25A6`.

## Measured resource averages

CPU is process CPU time divided by elapsed time and all 12 logical processors.
RAM distinguishes committed private bytes from resident working set. GPU values
are Windows formatted **busiest matching per-process engine counters**, not
total GPU load or a sum of engines. Helper processes are excluded.

| Phase | App | CPU, machine % | Private MiB | Working set MiB | 3D/compute % | Video encode % |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Replay only | OBS | 0.85 | 434.1 | 247.5 | 7.88 | 26.00 |
| Replay only | Klip | 1.21 | 418.1 | 197.0 | 8.56 | 26.00 |
| Replay + recording | OBS | 0.83 | 435.2 | 249.2 | 10.80 | 26.13 |
| Replay + recording | Klip | 1.11 | 418.6 | 197.6 | 9.75 | 26.25 |

Each phase has approximately 20–21 seconds of samples. On this run, Klip's
working set was about 20–21% lower, private memory about 4% lower, and CPU
0.29–0.37 percentage points higher. Do not attribute the CPU difference to a
confirmed defect without profiling and repeated matched runs. Warm-up times,
recording durations and application order differed; allocator high-water marks,
background activity and sampling noise can affect the results.

Raw samples and full min/median/max/count statistics are preserved in
`work/obs-parity-timed`, `work/klip-parity-timed`, and
`work/parity-first-summary.json`. The OBS replay-only sample predates the sampler's
explicit validity field; source lifetime was separately confirmed by its process
observations, continuous frame trace and logs. The sampler now rejects missing
or reused process IDs, including an ended capture process.

## Saved media

| Metric | OBS replay | Klip replay |
| --- | ---: | ---: |
| Distinct updates/s | 60.000 | 60.000 |
| Repeated frames | 0 | 0 |
| Nonmonotonic PTS | 0 | 0 |
| Median audio minus video | +18 ms | +21 ms |
| File size | 703,156 B | 690,080 B |
| Container average bitrate | 374,567 bit/s | 369,694 bit/s |
| Compressed packet payload | 691,562 B | 678,531 B |
| Mean sampled RGB PSNR | 48.29 dB | 48.73 dB |

Image samples were aligned by decoded frame ID **and** moving-bar position to
the fixture's source trace. Reference colors/geometry were checked against its
actual startup GPU RGB readback, not against another lossy recording. Samples
are every 60 decoded frames. Different replay source phases/durations affect the
sample mix; the small PSNR difference is not a claim of superior visual quality.
This simple raster is easily compressed and cannot represent detailed gameplay.
Packet payload is not allocated replay-buffer RAM.

The full OBS recording (18,163 decoded frames) had 0.011% repeated frames,
59.993 distinct updates/s, monotonic PTS and median A/V offset −22 ms. Klip's
shorter concurrent recording (2,032 decoded frames) had zero repeats, 60 distinct
updates/s, monotonic PTS and median offset −9 ms. These different durations do
not establish a recording reliability/performance advantage.

Source presentation intervals after startup were similar: median 16.631 ms,
p95 17.128 ms, p99 17.611 ms for OBS's run; 16.635/17.132/17.618 ms for Klip's.
The source was capped at 60 FPS. This is **not** an uncapped-game frametime or
gameplay FPS-impact measurement.

Observed controller-request-to-writer-log times were approximately 318 ms for
OBS's Save Replay button and 282 ms for Klip's global shortcut. Different
controls and UI dispatch make these rough observations, not a fair latency win;
they do not measure disk durability. Short injected chords targeted at the
source were not observed by OBS's polling hotkeys, so its real Save Replay
button finalized the verified clip. Do not infer an OBS user-hotkey defect.

## Additional 120 FPS check

`work/obs-120-current-parity-followup` tested the same Klip Release binary with a
120 FPS native source (vsynced Present, unlike the 60 FPS parity fixture's
Present(0)). Three consecutive global-hotkey saves while recording all decoded
successfully. Clips measured 118.87–119.03 distinct updates/s with 0.63–0.66%
repeats; the recording measured 119.40/s with 0.50% repeats. PTS were monotonic;
median A/V offsets were +9 ms for clips and −12 ms for recording. This passes
the documented 97% motion gate, **not** a zero-repeat gate. A matched OBS 120 FPS
run is still required before claiming parity at 120 FPS.

Release and Debug builds/core, output-reservation and event-driven-tab tests
must remain green. No app runtime changes were made for these measurements.
Test capture/helper processes were absent at the final process inspection.
Klip's manual benchmark instance was terminated only after recording finalized
and source loss stopped replay; that teardown is not a graceful-shutdown test.
Earlier dedicated shutdown tests remain separately documented.

## Exact reproduction

Use fresh names; scripts refuse to overwrite evidence. The portable OBS profile
does not alter an installed OBS profile. Control outputs only after capture is
confirmed ready; save while the source is still running.

```powershell
& tools/build-obs.ps1 -Configuration Debug
& tools/build-obs.ps1 -Configuration Release
& tools/prepare-obs-parity.ps1 -Name obs-parity-new -Launch
# Sample OBS replay-only while minimized, then start recording and sample again.
& tools/sample-capture-process.ps1 -CapturePid <OBS_PID> -SourcePid <SOURCE_PID> -EvidenceRoot work/obs-parity-new -Phase replay
& tools/sample-capture-process.ps1 -CapturePid <OBS_PID> -SourcePid <SOURCE_PID> -EvidenceRoot work/obs-parity-new -Phase replay-recording
# Save replay, stop recording/replay, close portable OBS, close fixture to flush trace.
& tools/prepare-klip-parity.ps1 -Name klip-parity-new -Launch
# Ctrl+Alt+Shift+F10 toggles recording, F12 saves, F11 shows/hides the dashboard.
& tools/sample-capture-process.ps1 -CapturePid <KLIP_PID> -SourcePid <SOURCE_PID> -EvidenceRoot work/klip-parity-new -Phase replay
& tools/sample-capture-process.ps1 -CapturePid <KLIP_PID> -SourcePid <SOURCE_PID> -EvidenceRoot work/klip-parity-new -Phase replay-recording
python tools/analyze-cadence.py <MEDIA_FILE> --tools <PINNED_FFMPEG_BIN>
python tools/analyze-parity-quality.py <REPLAY_FILE> --root <RUN_ROOT> --tools <PINNED_FFMPEG_BIN>
# Save analyses as replay-analysis.json, recording-analysis.json, quality-analysis.json.
python tools/summarize-parity.py --obs-root <OBS_RUN_ROOT> --klip-root <KLIP_RUN_ROOT>
& tools/run-obs-test.ps1 -Mode game -Fps 120 -SourceFps 120 -GlobalHotkeys -AvMarkers -Name obs-120-new
```

## Remaining acceptance gates

Repeat measurements in randomized order with matched warm-up and recording
durations; profile the extra CPU before optimizing. Add representative Fortnite
replays, uncapped frametimes/baseline, 120 FPS OBS parity, live-overlay parity,
save-phase peaks and replay-memory isolation. AMD hardware is present on this
host and needs a separate real path test; no Intel GPU was observed. Complete
the lifecycle/fault/audio-device/packaging/license/clean-machine gates in
`libobs-results.md`. The migration is **not yet release-ready**.
