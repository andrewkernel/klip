# Reduce in-game performance loss

## Map and decision process

Game Present → OBS hook texture copy → OBS scene/color conversion → hardware
encoder → compressed replay packets → muxer/disk. Alongside that path, Klip
runs its dashboard, audio meters, source/device discovery and housekeeping.

| Contention point | Intervention | Guardrail/evidence |
| --- | --- | --- |
| Game-side texture copies | Expose stock OBS `limit_framerate`: at 60 FPS output, limit copies to approximately 60 rather than the stock 120 | Opt-in initially; test decoded cadence at high source FPS and keep encoder settings unchanged |
| Dashboard graphics work | Do not render a minimized or hidden dashboard | Visibility-state regression and real minimize/restore tests |
| Audio-meter CPU | Detach meter-only callbacks while dashboard is inactive; restore on show | Do not disable WASAPI, change gain, audio tracks or AAC; decode A/V markers and exercise show/hide |
| Main-thread overhead | Profile per-thread CPU before changing polling/scheduling | Keep source-loss detection, save signals and bounded queues responsive |
| Encoding load | Reuse the same encoder for replay and recording; explicit NVENC Performance uses P3, one pass and AQ off | Balanced remains P5/qres/AQ on. Do not silently change quality; Performance is a disclosed image/compression tradeoff, not equal-quality proof |
| GPU saturation | Benchmark with source-only baseline and capture enabled; recommend headroom only from measurements | Do not force a game's FPS cap, affinity, GPU driver settings or security changes |
| Clip saves | Remux compressed packets off the game thread | Preserve collision safety, bounded save queue and concurrent capture |

OBS 32.1.2 `plugins/win-capture/game-capture.c::reset_frame_interval` limits
the hook to twice output FPS by default and once output FPS when
`limit_framerate` is selected. The hook's `graphics-hook.h::frame_ready` applies
that interval before its capture copy. This reduces redundant **capture copies**,
not the game's rendering rate. Asynchronous source/output clocks can still
produce repeated frames; it must not be sold as a guaranteed smoothness fix.

Confirmed from Klip code: `WM_SIZE/SIZE_MINIMIZED` previously left `Visible()`
true, and the application rendered visible dashboards during every 250 ms
engine tick. Hidden audio meters remained attached to WASAPI sources; stock
libobs calculates their per-channel magnitude/peak for every audio block.
These are UI-only computations, separate from the actual audio pipeline.

## Verification before acceptance

1. Preserve the pre-change binary/runtime for real A/B tests, and record hashes.
2. Profile background threads; match source, encoder, output, duration and
   warm-up. Repeat in alternating order rather than relying on one CPU sample.
3. Build Debug/Release and add tests for minimized state, settings persistence
   and capture reconfiguration. Run real hotkey, concurrent recording, separate
   audio, cadence and synchronization tests after changing the binary.
4. Measure limiter on/off at high source FPS; reject silent cadence degradation.
5. Compare game/source baseline frametimes with capture, not just Klip process
   utilization. A synthetic source is diagnostic, not a Fortnite benchmark.
6. Record benefit, tradeoffs and remaining hardware/game gaps. Do not claim a
   universal FPS improvement or publish without the requested release authority.

Real-game measurement uses `tools/measure-gameplay.ps1`, with the already
running game's exact PID and title, not automated gameplay or a synthetic
replacement. It refuses other running Klip/OBS instances, isolates app settings
and media, checks source identity, records source-only/capture/source-only QPC
phases and uses a unique timed PresentMon observer without input tracking.
`tools/analyze-gameplay.py` rejects failed/incomplete trials, absent target
frames, short traces and uncovered phases. Repeat the same replay segment in
alternating Balanced/Performance order, and inspect decoded clips separately:
Present rates do not establish clip smoothness or displayed FPS. Record the
game renderer, resolution, FPS cap and scene in `-SceneDescription`. This
harness does not change them and never stops the game.

Status: background-work fixes and the opt-in capture-copy limiter are implemented
and functionally tested; NVENC Performance tuning was measured on GPU-bound
fixtures, and replay-duration labels no longer pretend uptime is retained
history. See `gameplay-performance-results.md`. Actual in-game
performance benefit is still unproven and further measurement is required. The earlier short
comparison in `libobs-parity-benchmark.md` is a baseline, not evidence for the
new changes.
