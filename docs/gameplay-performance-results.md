# Gameplay performance work — 2026-10-07

## Implemented

The mapped process is in `gameplay-performance-plan.md`. This pass removes
dashboard-only work without changing capture quality:

- `Win32Window::DashboardActive()` separates actual drawable activity from
  close-to-tray visibility. Minimized and hidden dashboards do not submit UI
  renders, request animation frames, or keep active-widget redraw timers running.
- `ObsEngine::SetDashboardActive()` destroys only visualization meters while
  inactive and reattaches them on show/restore. WASAPI sources, volumes, AAC
  tracks, video/encoder settings, replay and recording remain running.
- Settings → Capture exposes an **opt-in**, default-off stock OBS game capture
  copy limiter. It changes `limit_framerate`, reducing nominal hook copy cadence
  from twice clip FPS to clip FPS. It does not limit the game's own FPS, reduce
  resolution/CQP, or replace OBS internals. Its smoothness warning is explicit.
- Persisting the limiter requires media reconfiguration, respecting existing
  recording/save restrictions. Unit tests cover default-off, round-trip and
  reconfiguration. Native UI interaction verified enabling and Save & Apply in
  an isolated test profile; the user's normal settings were not changed.
- New read-only thread profiling and stronger resource-sampler identity checks
  support diagnosis. Idle sampling no longer tries to read the System Idle
  process as a gameplay source. Test/build scripts accept a separate binary or
  build suffix to avoid replacing a running user's app.

## Builds and functional evidence

The following hashes describe the earlier build, not the final rebuild below.

Release: `build-obs-Release-gameplay/bin/64bit/Klip.exe`, SHA-256
`E4C216C442C3805021160B0FF4064E27C4EC68B426B25DCA35CB7D8D876CCBFE`.
Debug: `build-obs-Debug-gameplay/bin/64bit/Klip.exe`, SHA-256
`C7659FFE8905B533A856CB5E4AAA596FD4F3513A78265FFBA0D940073E3A8274`.
Both builds passed core, output-reservation and event-driven-tab test suites.

`work/gameplay-feature-regression` tested minimize/restore, repeated dashboard
activity transitions, close-to-tray capture, real global hotkeys, three saves
while recording, three AAC tracks, and clean shutdown. All four media files
decoded with zero repeated frames and monotonic PTS. Clip A/V offsets were
+14 ms; recording offset −7 ms. Source 240 FPS, output 60 FPS. The separate
microphone track was configured but its physical input was disabled; this does
not replace a live-microphone acceptance test.

`work/gameplay-limiter-120-regression` tested limiter on with a 240 FPS source
and 120 FPS output, three saves plus recording. All four decoded with zero
repeats, 119.60–119.81 distinct updates/s and monotonic PTS. Clip median A/V
offset +6 ms; recording −15 ms. This differs from earlier 120 FPS-source runs
and must not be used to claim those previously observed repeats were fixed.

`work/gameplay-limiter-matched-120` then tested 120 FPS source and 120 FPS
output with the limiter enabled. Clips measured 118.68–119.03 distinct updates/s
with 0.79–0.83% repeats; recording 119.50/s with 0.42% repeats. All PTS were
monotonic and median A/V offsets were +6 ms/−15 ms. This passed the 97% gate,
not a zero-repeat gate. It reinforces keeping the limiter opt-in; it is not a
fix for every asynchronous-cadence case.

`work/gameplay-ui-validation` verified the actual settings checkbox, saved INI,
minimize and restore. With replay disabled and dashboard minimized, a short
sample measured 0.014% mean process CPU (one input-transition spike, most
samples zero). This is not enabled-capture overhead or an in-game FPS result.

## First high-source-FPS comparison — no performance win proven

Same controlled 1080p source, 240 FPS capped Present(0), 60 FPS output, NVENC
CQP18/P5/HQ/qres/AQ, default desktop AAC192 and hidden dashboard. No app quality
settings changed. One sequential run per condition; approximately 20-second
resource samples, same warm-up procedure. All artifacts are retained.

| Condition | Parent CPU % | Source CPU % | Parent 3D engine % | Source 3D engine % | Clip repeats | Sampled RGB PSNR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Preserved pre-change binary | 0.56 | 0.99 | 5.07 | 0.80 | 0.116% | 48.60 dB |
| New binary, limiter off | 0.97 | 1.58 | 5.50 | 0.50 | 0% | 48.34 dB |
| New binary, limiter on | 1.24 | 1.77 | 9.00 | 0.63 | 0% | 48.68 dB |

These samples **do not demonstrate reduced total CPU/GPU usage**. Do not
advertise the nominal copy reduction as a measured game-FPS benefit. The source
was capped, very light, and not Fortnite; its p99 presentation intervals ranged
4.82–5.03 ms. No uncapped gameplay impact or GPU-saturated baseline was measured.
GPU percentages are per-process busiest-engine counters, not whole-system GPU
load. Clock/power-state data and repeated randomized-order runs are still needed.
PSNR uses the simple synthetic raster, not detailed game imagery.

The new thread profiler identifies OBS's graphics thread as the dominant CPU
consumer (0.80% machine CPU in one profile), with audio IO around 0.10% and
housekeeping much smaller. Pinned OBS's
`libobs/util/platform-windows.c::os_sleepto_ns` sleeps to just before the frame
deadline then spins on QueryPerformanceCounter/YieldProcessor. This can account
for CPU time outside useful rendering; it does **not** prove how much each
sample spent spinning. No private-memory access, binary patch, runtime hook,
thread affinity, game FPS cap, driver setting or system security change was used.

The baseline thread-name reader originally treated only HRESULT zero as success;
its preserved profile lacks names. It now accepts nonnegative success HRESULTs
and the next profile correctly resolved OBS's public thread descriptions.

## GPU-bound follow-up: explicit encoder tuning

Hardware: Ryzen 5 7600X (12 logical processors), RTX 3060, NVIDIA driver
32.0.15.9595, Windows 11 build 26200.8655, 32 GiB RAM, 1080p/240 Hz display.
The bounded uncapped D3D11 shader fixture reached 99–100% GPU utilization.
PresentMon 2.4.1 observed its dominant swap chain without input tracking.
Measured phases were source-only before, source-with-capture and source-only
after, with warm-up; the baseline is the mean of the two source-only rates.
These are submission/Present rates, **not displayed FPS or Fortnite results**.
The fixture used `Composed: Copy with GPU GDI`, unlike many games' flip modes.

NVENC Performance now explicitly selects P3, disabled multipass and AQ off.
Balanced remains P5/qres/AQ on; Maximum remains P7/fullres/AQ on. Resolution,
FPS and CQ are unchanged by this encoder-policy change. The settings explain
that image quality and file size may differ. AQ and multipass can use CUDA
resources; presets trade speed against compression/quality, as documented in
the [NVIDIA encoder guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html).
The combined policy was tested, so no result is attributed to AQ alone.

| Workload / condition | Bracketed source rate | With capture | Throughput loss | Klip encode-engine busy |
| --- | ---: | ---: | ---: | ---: |
| Smooth GPU-bound / Balanced | 315.52/s | 296.18/s | 6.13% | 27.18% |
| Smooth GPU-bound / Performance | 313.73/s | 297.40/s | 5.20% | 8.83% |

One pair in `work/gpu-bound-smooth-etw`, 1080p60/NVENC CQ18, desktop AAC192,
256 MiB replay budget, hidden dashboard. Both saved clips decoded at 59.85
distinct updates/s with zero repeated frames, monotonic PTS and median A/V
marker offsets +18/+24 ms. Duration was 13.367 s, sizes 17,735,914/9,315,052
bytes. Equal CQ does not establish equal visual quality across presets.
The ~0.93 percentage-point difference is modest, not a universal guarantee.

Noise-heavy runs in `work/gpu-bound-encoder-etw` and
`work/gpu-bound-repeat-etw` provide three observations per preset. Mean loss
was 6.73% Balanced versus 6.15% Performance; mean encode-engine busy was
43.22% versus 36.94% (about 14.5% lower). Samples overlap and are short; no
statistical significance is claimed. The separate initial limiter-only runs
gave 6.80%/6.02%/6.32% loss for Balanced/limited/Balanced, insufficient for a
firm limiter-benefit claim. Raw phases, ETW CSV, GPU telemetry, settings and
media are retained under each evidence root.

The noise fixture is intentionally hard to compress. Its hundreds-of-Mbit/s
stream exhausted the bounded replay memory and retained only ~3.35 seconds,
not the configured 15-second limit. This exposed misleading UI duration copy:
libobs dashboard now says **save up to N seconds** and **memory cap may shorten
clips**, rather than presenting uptime as actual buffered history. It does not
increase memory without consent or inspect libobs private structures.

Telemetry showed P0, 1845–1882 MHz and 55–79 °C over these trials. These are
whole-trial ranges, not time-joined phase averages. Resource percentages are
the parent process's busiest encode engine, not whole-GPU utilization. There
were no clock, priority, affinity, security or game-settings changes. The
test-hook controller uses the normal hidden 250 ms loop, with four additional
status reads/s. Firmware, heat, other programs and sample order remain possible
confounders. [OBS's GPU troubleshooting guidance](https://obsproject.com/kb/encoding-performance-troubleshooting)
also describes capture/render contention; the user must approve any game/driver
settings change rather than having it silently applied.

Diagnostic hashes: noise encoder/repeat executable
`0DBF60B7E2C247A515C7D959B59EAB9CA941D81E94564D0DA7A57EC80A0D1FD3`;
smooth executable
`59E2C27796ED46E54A52EB3B0CCCA4EE2231DBB5F04FCB295D946609EEC95642`.
These builds enable test hooks; do not distribute them as the user test build.

## Final rebuild and regression

Release `build-obs-Release-gameplay/bin/64bit/Klip.exe` SHA-256:
`3467731B2417AB42F89CC59D1C78FB3860C482BE05B3CD2655BD31B0DCF71D54`.
Debug `build-obs-Debug-gameplay/bin/64bit/Klip.exe` SHA-256:
`4D10FD27C0E7051CF7338F0E5D84CF23DB3F580020CA9E58CFFF62D474290E9B`.
Both rebuilt with benchmark hooks disabled and passed all three test suites.
The ordinary Release binary rejected the test-only benchmark flag with exit 2
before app startup. Only deprecated libobs data-path API warnings remained.

`work/gameplay-final-balanced`: actual global hotkeys, minimize/restore,
close-to-tray, three replay saves during recording, separate audio tracks and
clean shutdown passed on the final Release. All four files decoded with zero
repeats, 59.81–60.00 distinct updates/s, monotonic PTS; clip A/V +14 ms and
recording median −7 ms. As before, the physical microphone was disabled; this
does not establish live-microphone quality.

`work/gameplay-final-performance` repeated the same acceptance flow on the
final Release. All four files had zero repeats, 59.62–60.00 distinct updates/s
and monotonic PTS; clip A/V +4 ms, recording median −17 ms. Logs verified real
NVENC `p3`, `disabled` multipass and `aq: false`, versus Balanced's unchanged
`p5`, `qres`, `aq: true`. Native UI inspection with an isolated replay-disabled
profile (`work/gameplay-final-ui`) confirmed the duration labels and readable
Performance tradeoff text. The installed legacy application is not the updated
test build; verify the executable path before testing. Normal user settings
were not edited by these isolated tests.

## Reproduce and continue

### Live-source ETW workflow validation

`work/gameplay-etw-workflow-validation` exercised the new external-source
workflow end-to-end: an already running cadence source at 240 FPS was targeted
by exact PID/title; three timed phases, parent resource samples, normal-loop
Performance capture, a saved replay and normal timed PresentMon shutdown all
completed. The source was left running and ended at its own bounded lifetime.
The analyzer's known-value tests verify process filtering, 100→50/s loss
arithmetic, percentile interpolation, and rejection of failed, absent-process
and phase-truncated data. Invalid source PID was rejected before capture or
evidence creation. PS syntax and diff-whitespace checks passed.

Observed source Present rates: 239.963/s before, 239.890/s during and 239.958/s
after; p99 intervals 4.606/4.680/4.628 ms. This capped light source is a
**workflow check, not a game-overhead benchmark**. Saved clip: 801 decoded
distinct frames, zero repeats, 59.925 updates/s, monotonic PTS, +18 ms median
A/V marker offset. One final interval (frame 800, 13.317→13.350 s) was 33 ms;
remaining intervals were 16/17 ms. Zero repeats is not proof that every frame
deadline was met. Real-game media needs independent inspection.

The harness targets stock OBS Game Capture only and never silently substitutes
Display capture. It refuses ambiguous source windows/other capture instances,
retains partial failed evidence, isolates settings/media, validates source
identity and recorded resource validity, and owns a uniquely named ETW session.
It does not automate gameplay, change game settings, stop the game, collect
input events or upload desktop audio/media. The user must provide a repeatable
scene plus renderer/FPS-cap information; Fortnite was not running at this check.

```powershell
# Start the same repeatable game/replay scene for each condition first.
& tools/measure-gameplay.ps1 -GamePid <GAME_PID> -WindowTitle '<EXACT_TITLE>' -Name fortnite-balanced-1 -Quality balanced -SceneDescription '<renderer, game resolution, FPS cap, replay segment>'
python tools/analyze-gameplay.py work/fortnite-balanced-1
& tools/measure-gameplay.ps1 -GamePid <GAME_PID> -WindowTitle '<EXACT_TITLE>' -Name fortnite-performance-1 -Quality performance -SceneDescription '<same conditions and segment>'
python tools/analyze-gameplay.py work/fortnite-performance-1
python tools/test_gameplay_metrics.py
```

```powershell
& tools/build-obs.ps1 -Configuration Release -BuildSuffix gameplay
& tools/build-obs.ps1 -Configuration Debug -BuildSuffix gameplay
& tools/run-obs-test.ps1 -Mode game -BuildSuffix gameplay -SourceFps 240 -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name gameplay-new
& tools/run-obs-test.ps1 -Mode game -BuildSuffix gameplay -Fps 120 -SourceFps 240 -LimitGameCapture -GlobalHotkeys -AvMarkers -Name gameplay-limit-new
& tools/run-obs-test.ps1 -Mode game -BuildSuffix gameplay -Fps 120 -SourceFps 120 -LimitGameCapture -GlobalHotkeys -AvMarkers -Name gameplay-equal-new
& tools/run-obs-test.ps1 -Mode game -BuildSuffix gameplay -SourceFps 240 -EncoderQuality performance -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name gameplay-performance-new
& tools/build-obs.ps1 -Configuration Release -BuildSuffix gpu-bench -EnableTestHooks
& tools/run-gpu-bound-benchmark.ps1 -Name gpu-new -Conditions balanced,performance -SmoothWorkload -PresentMon
python tools/analyze-gpu-bound.py work/gpu-new
& tools/prepare-klip-parity.ps1 -Name gameplay-profile-new -SourceFps 240 -Executable build-obs-Release-gameplay/bin/64bit/Klip.exe -Launch
& tools/profile-capture-threads.ps1 -CapturePid <PID> -EvidenceRoot <RUN_ROOT> -Seconds 20
& tools/sample-capture-process.ps1 -CapturePid <PID> -SourcePid <SOURCE_PID> -EvidenceRoot <RUN_ROOT> -Phase replay
```

Next: characterize limiter tradeoffs near source/output FPS equality and
noninteger ratios, repeat randomized A/B, measure uncapped/GPU-bound source
baseline and Fortnite frametimes, distinguish OBS render work from timing spin,
then make only justified further changes. The goal remains active: automatic
unnecessary work is removed and explicit controls have diagnostic evidence, but
lower **Fortnite performance loss** has not yet been demonstrated. The new
libobs engine remains opt-in at build time; AMD/Intel paths, a clean-machine
installer and real-game quality acceptance are still open gates. Nothing was
published.
