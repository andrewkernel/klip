# Klip release-candidate system design

Status: target invariants for the implementation model, not a claim that the current code satisfies all of them. Astra owns orchestration and acceptance review; see `implementation-handoff.md` for current evidence and execution order.

## Design intent

Keep the native Windows/D3D11/WGC/FFmpeg architecture. One capture and video-encoding pipeline must serve replay buffering and continuous recording. Reliability comes from explicit clocks, ownership, bounded work, and truthful diagnostics—not a larger framework or a second encoder per output.

## Pipeline and ownership

```text
WGC source frames + original timestamps
    -> bounded raw-frame queue
    -> newest-useful-frame selection
    -> D3D11 conversion/scaling -> completion fence (or event-query fallback)
    -> bounded converted-frame queue
    -> fixed output scheduler -> dedicated encoder input surfaces
    -> FFmpeg hardware encoder -> timestamped compressed packets
                                      |-> time/byte-bounded replay buffer
                                      |       -> keyframe-safe snapshot -> clip MP4
                                      |-> bounded recording queue -> recording MP4

WASAPI desktop + microphone
    -> format conversion/resampling -> bounded time-aligned mix
    -> AAC packets --------------------> same output routing
```

### Capture boundary

- WGC owns captured surfaces until the frame is released. Make that lifetime explicit across asynchronous GPU work and all early exits.
- Prefer the source's timestamp for identifying a source frame; callback arrival time measures scheduling delay, not when its pixels were captured.
- Request an appropriate WGC minimum update interval when supported; older Windows must degrade transparently without crashing. Requested 120 FPS cannot guarantee 120 unique source frames.
- Keep a small bounded queue. When behind, select current content instead of building latency by processing obsolete frames.
- Count source arrivals, intentional rate limiting, queue overflow, and coalescing separately.

### GPU conversion and synchronization

- Conversion and copy commands must use compatible dimensions/formats and a documented context synchronization strategy.
- Fence values must be issued monotonically in the same ordering domain as GPU work. Wait for the selected useful frame, not every stale frame.
- Do not recycle a texture while GPU operations or the encoder still reference it.
- Conversion surfaces and encoder-registered input surfaces have different lifetimes. A backend may cache a registration after releasing an individual input frame; hold registered resources until unregister/codec teardown.
- Bound total allocations, not just the free-list length. Reconfiguration must safely release old-dimension resources.

### Video scheduler and encoder

- Output frame index defines the desired presentation timeline. Use rational/integer arithmetic; avoid cumulative rounded-frame-duration drift.
- A repeat is allowed when the source has not changed. It must remain observable as a repeat, not be presented as fresh motion.
- When late, use an explicit bounded policy for missed slots. Do not silently squeeze elapsed real time into consecutive timestamps.
- Distinguish accepted input submissions from emitted packets. Lookahead and B-frame reordering make them different measurements.
- Drain/retry transient backpressure with bounded work. Fatal failures must produce an explicit recovery/fallback transition and preserve or finalize affected outputs safely.
- Report the actual selected encoder and why fallback occurred. Do not promise identical resource use or quality across hardware/software fallback.

### Audio timeline

- Preserve storage format, channel layout, sample rate, device timestamps, and gain semantics through resampling/mixing.
- Treat discontinuity and invalid-timestamp flags explicitly. Silence insertion and dropped audio must be measurable.
- Use sample-count/rational clocks and a defined synchronization origin with video. Account for AAC delay and clip boundary handling.
- Verify perceptual sync with a known stimulus and long-run drift; stream start times alone are insufficient.

### Buffering and files

- Replay retention is bounded by time and bytes. Snapshot under a short lock, perform disk I/O outside it.
- Start clips/recordings on a decodable keyframe. Preserve decode order and valid PTS/DTS relationships for reordered video.
- Packet allocation/snapshot failure must fail the operation explicitly, not produce a subtly incomplete dependency chain.
- Recording overflow stops further acceptance and finalizes the valid accepted prefix where possible. Never continue after silently discarding reference packets.
- Finalize to a temporary file, then publish atomically. A final filename must not imply success before mux finalization succeeds.

### Commands and state

- Hotkeys and buttons call the same application actions. UI, capture, and muxing work must not block the global shortcut message loop unnecessarily.
- A conflicting shortcut must not disable unrelated registered shortcuts. Settings changes need coherent rollback and persistence.
- Component failures must not be erased by an unrelated success. Track failure/recovery counters for acceptance tests, not just one transient error string.
- Shutdown order must stop producers, drain/stop consumers, release backend registrations, then release shared GPU resources. Test repeated start/stop and failed startup rollback.

## Observability contract

Measure and label these separately:

| Metric | Meaning |
|---|---|
| Source arrival FPS | Frames delivered by WGC; not necessarily different pixels |
| Accepted/conversion FPS | Frames admitted to conversion after intentional gating |
| Unique source submissions | Distinct source identities selected for encode; not a pixel-uniqueness proof |
| Encoded output FPS | Video packets actually emitted by the encoder, with input submission rate separately available |
| Repeat count | Reuse of the same source identity at another output tick |
| Skipped output slots | Missed scheduled output opportunities |
| Queue drops/coalescing | Separate saturation loss from deliberate freshness selection |
| Decoded motion cadence | Frame IDs/pixels observed after decoding the saved file |
| Timestamp irregularity | Nonmonotonic times, gaps, and interval distribution |
| A/V offset and drift | Known audio/visual markers and change in their offset over time |

Static content naturally repeats. Do not show a low WGC update rate on a static desktop as proof of an error. Conversely, 120 encoded frames per second is not proof of 120 unique motion updates.

## OBS comparison boundary

Compare scheduling, frame ownership, texture reuse, and output handling with OBS's open-source video code. Compare WGC with WGC where possible. OBS Game Capture hooks a game's graphics path and is not equivalent to Klip's WGC window/display capture. Do not copy GPL implementation into the project without an explicit license review and authorization.

Relevant sources already consulted:

- https://github.com/obsproject/obs-studio/blob/master/libobs/obs-video.c
- https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/game-capture.c
- https://github.com/FFmpeg/FFmpeg/blob/n8.1/libavcodec/nvenc.c
- https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.minupdateinterval
- https://github.com/LizardByte/Sunshine/blob/master/src/platform/windows/display_wgc.cpp

Pin revisions for the final report. Independent implementation and API use are preferable to transplanting code.

## Verification and release gates

1. Final Debug and Release builds pass deterministic and Windows-specific regression tests.
2. Changed binaries run actual WGC capture, clip saving, recording, and simultaneous clipping/recording. Verify the final packaged binary separately from developer builds.
3. Use a source with independently verified sufficient cadence. Inspect decoded frame identities, repeats, timing gaps, decode errors, and audio. Compare identical before/after workloads.
4. Exercise 60 and 120 FPS, supported quality presets, window/display sources, source changes, recovery/fallback, and long sessions. Record untested cases explicitly.
5. Test NVIDIA, AMD, and Intel paths separately where hardware is available. Availability detection is not successful encoding coverage.
6. Verify queue/memory bounds and overload behavior under realistic load. Report CPU/RAM/GPU measurements with methodology and hardware, not universal claims.
7. Deliver the root-cause report, artifacts, exact reproduction commands, limitations, and release checklist to the orchestrator.
8. No widespread-deployment readiness claim while a changed path lacks real capture evidence or a known release-blocking failure remains. The user separately decides whether to publish.
