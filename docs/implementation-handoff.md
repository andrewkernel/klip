# Klip release-candidate implementation handoff

Updated: 2026-09-27. Baseline HEAD: `a4b377d`. All current fixes are uncommitted.

Latest verification and release checklist: [`release-candidate-results.md`](release-candidate-results.md).

## Roles and authority

- **Astra is the orchestrator:** maintain the acceptance criteria, prioritize evidence-backed work, review implementation and test evidence, and report release risk. Do not continue production implementation in that role.
- **The implementation model:** inspect the actual working tree, finish targeted fixes, build, exercise the changed executable, and return a reviewable diff with reproducible evidence.
- **The user:** authorizes publication, deployment, version changes, and any material expansion of scope. None is currently authorized.
- This is a local handoff, not an instruction to create or message another chat automatically.

## Mission

Deliver a reliable Windows clipping release candidate, prioritizing video that advertises 60/120 FPS but looks choppy. Preserve and critically review the existing frame-pacing changes. Do not replace this task with a UI redesign, metadata-only correction, or a broad rewrite.

Read `docs/rc-system-design.md` with this document. Existing `docs/architecture.md`, `docs/verification.md`, `docs/frame-pacing.md`, and release documents contain useful context but have stale statements. Code and actual measurements take precedence.

## First actions

1. Read repository instructions, `git status`, and the complete current diff, including untracked files under `tools/`, `cmake/`, and `tests/`.
2. Inspect live processes before rerunning tests. At handoff inspection no Klip, cadence scene, ffplay, CMake, Ninja, or Clang test process was running. Recheck; do not rely on this historical observation.
3. Read existing test logs and JSON under `work/`. Preserve them as baseline evidence. Do not overwrite a named run.
4. Rebuild the current source in both configurations. Final Debug and Release builds and both test suites passed after the latest changes.
5. Repeat the latest 120 FPS experiment with a source that stays alive throughout capture and analysis. **Completed:** the final native-scene 120 FPS outputs are valid; source lifetime and decoded results are in `release-candidate-results.md`.

## Confirmed evidence versus hypotheses

| Finding | Evidence | Current status |
|---|---|---|
| Arrival-time jitter can discard compositor frames even after relaxing the gate | On the same RTX 3060 host, the old callback clock admitted 745/1,172 frames at 60 FPS and 1,374/2,274 at 120 FPS; compositor-time gating admitted 1,164/1,164 and 2,288/2,288. Decoded native-scene transition rates rose from 59.3 to 60.0/s and 119.9 to 120.0/s, respectively | WGC `SystemRelativeTime` is now the session admission clock when present; callback arrival is an explicit logged fallback. See the A/B section in `release-candidate-results.md` |
| Nominal 120 FPS can contain only about 60 unique updates/sec | Before WGC interval request, native D3D scene at ~239.7 presents/sec yielded about 59.95 changed IDs/sec and 50.04% repeats; final 120 output is about 118.8 changed IDs/sec and about 1% repeats | WGC update-interval request implemented; reproduced before/after on the same physical host |
| NVENC sometimes stops a recording with ENOMEM | Real RTX 3060 capture, FFmpeg stderr: `EncodePicture failed!: out of memory (10)` | Dedicated encoder texture pool and retained registration lifetimes implemented; 60-second NVENC stress recording passed, but multi-minute/device breadth remains untested |
| Queue wait can lose notification | Producer published outside the condition-variable wait mutex | Publication coordinated with wait mutex, stop-aware waits, stress regression added |
| One occupied shortcut disables unrelated shortcuts | Original `Hotkeys::Register` unregistered everything on partial failure | Successful registrations preserved; actual Win32 registration-conflict regression passes |
| 24 valid PCM bits in a 32-bit container rejected | Resolver replaced container width with valid-bit precision | Container-width resolution and malformed-extensible checks added; format tests pass |
| Recording overload continues after dropping compressed packets | Original writer dropped reference packets and carried on | Stop accepting on first loss, drain accepted prefix, report overload; zero-capacity fault-injection test passes |
| Switching a valid WGC target stopped an in-progress recording | New window → display → window acceptance run finalized the MP4 at ~2.1 seconds; call chain showed `ResetTimeline()` stopped the recording writer | Replay reset now preserves the recording writer; encoder clock continues and the last complete image repeats over capture startup. Unit test decodes packets across reset; real 10-second NVENC and cross-adapter AMF switch/clip recordings now pass. |
| Old capture smoke test could pass after an encoder reset | An MP4 existed, so test returned success despite early recording termination | 10-second ordinary and 60-second stress recordings require simultaneous clip, valid file, expected encoder, no early stop, and unchanged monotonic error generation |

The NVENC ownership explanation is supported by code inspection and improved short tests, but do not present it as an exhaustively isolated driver-level root cause. Continue investigating if it recurs.

## Measurements already collected

Hardware: NVIDIA GeForce RTX 3060, driver `32.0.15.9595`; primary display 1920x1080, reported 239 Hz. AMD Radeon(TM) Graphics is enumerated, driver `32.0.21030.2001`, and has now completed 60/120 FPS AMF clip-plus-record tests, including a 60 FPS audiovisual marker run. Its adapter owns no active display output, so these are cross-adapter tests; AMD-native display cadence remains unverified. No Intel GPU is installed in the test environment; the strict Intel/MF hardware acceptance mode is implemented but has not been run.

| Run directory under `work/` | Interpretation |
|---|---|
| `cadence-release-60` | Before dedicated encoder pool: NVENC reset around nine seconds, recording stopped early. Decoded output had regular 16.667 ms timestamps but 12.56% repeats. Source was ffplay, not a game benchmark |
| `cadence-resource-fix-60` | Dedicated pool: short recording and simultaneous clip completed without reset. 13.51% repeats remained in player-driven source. Do not claim a motion improvement from the pool change |
| `cadence-trace-60` | Diagnostics show player-driven WGC source below 60 updates/sec; output scheduler remained at 60 |
| `cadence-60-source60` | Player-source repeat rate ~20%; not an appropriate isolated capture ceiling test |
| `cadence-native-60` | Native source ~239.68 presents/sec. Recording: 482 decoded frames, 60 unique transitions/sec, 0% consecutive repeats, regular timestamps. Short test only; native scene contains no sync audio |
| `cadence-native-120` | Before WGC interval configuration: 1,202 decoded frames, ~59.95 unique transitions/sec, 50.04% repeats, regular ~8.333 ms timestamps |
| `cadence-native-120-interval` | New WGC interval setting logs 240 Hz input headroom and substantially higher source arrival counts. However source exited after ~10.66 sec and capture continued, so whole-file 19.73% repeats / ~96.32 unique transitions/sec is contaminated. Repeat; do not use as final before/after result |

The final-binary FFplay flash/beep recording and clip had median detected audio-minus-flash offsets of +1.667 ms; individual samples included -15 ms. These include player/compositor offset, are short samples, and do not prove long-term sync, microphone behavior, or all-device audio quality.

The final stress run sampled CPU/private memory and confirmed the moving source stayed
alive for every one-second sample. The final AV fixture's median audio-minus-flash offset
was +13 ms in both recording and clip; this includes playback/compositor offset and does
not establish long-duration A/V sync. See `release-candidate-results.md` for exact files.

## Existing implementation changes to review

- `graphics_capture.cpp`: jitter-tolerant gate using WGC compositor timestamps with callback-arrival fallback; newest-frame selection before GPU completion wait; dedicated encoder input pool bounded at 64 allocations per dimensions; cadence diagnostics; WGC `MinUpdateInterval` request at twice target FPS with older-SDK/runtime fallback; D3D11 event-query completion fallback when fences are unavailable. Forced event-query fallback passes two real A/V acceptance runs after removing a per-frame Flush regression.
- `video_encoder.cpp`: retain references to registered hardware input textures until codec teardown; cap retained textures at 64. The capture pool is separate from conversion/coalescing now.
- `bounded_queue.h`: wait synchronization and cancellation changes.
- `hotkeys.cpp`: retry missing bindings without discarding successful bindings; configuration equality tracks changes.
- `audio_pipeline.cpp`: extensible PCM storage format resolver, packed-24 normalization, and bounded WASAPI endpoint recovery; no broad mixer rewrite.
- `recording_writer.cpp`: abort further packet acceptance on overload; retain accepted prefix; CPU-only below-normal priority instead of background I/O priority.
- `application.cpp`: stronger capture acceptance mode and clipping while recording.
- `main_panel.cpp`: source versus output labels from the earlier pacing change. Do not redesign the dashboard.
- `tests/windows_tests.cpp`: audio format, actual Win32 registration conflict, and deterministic recording-queue overflow tests.
- `rolling_media_buffer.cpp` and `packet_router.cpp`: propagate clone/retention failures, refuse partial snapshots, reset replay history, and ignore dependent packets until a new keyframe.
- `application_state.*`: monotonic error generation plus component-specific clearing; capture acceptance sees transient failures.
- `video_encoder.*`, `graphics_capture.*`: output FPS counts encoder-emitted packets; submitted frames remain separate in cadence logs.

## Priority work packages

Current overall verification state: see `release-candidate-results.md`. The portable
RTX 3060 and cross-adapter AMD evidence pass the stated clip/record acceptance gates. Intel hardware,
AMD-native display cadence, packaged MSVC output, game-focused hotkeys, and long audio/device tests
remain open.

### P0 — finish motion and encoder reliability

1. Reproduce 120 FPS with the native scene alive for the entire test. Measure actual decoded source IDs, not only submission counters. Test 60 FPS after the WGC interval change too.
2. The source-clock A/B change has passed on the current RTX 3060 host at 60/120 FPS. Repeat it on physical AMD-native and Intel systems and lower-performance hardware; keep unique-frame and timestamp analysis rather than relying on metadata.
3. The RTX 3060 passes window → display → window switch during a 10-second recording with a concurrent replay clip and continuous 60/1 output timestamps on NVENC; cross-adapter AMF also passes. An injected NVENC failure twice now proves that the active recording continues through runtime software fallback on the RTX host, with a measured maximum PTS gap of 183.33 ms; see `release-candidate-results.md`. This state-machine test does not replace real driver-failure tests. Repeat on AMD-native and Intel hardware, different source dimensions, long recording, presets with B-frames/lookahead, and physical runtime failures. Continue auditing pool exhaustion and separate true backpressure from fatal backend errors.
4. Encoder-input ownership now has an RAII return guard for failures before FFmpeg accepts a frame. GPU synchronization and failed software readback retire, rather than recycle, surfaces whose completion cannot be proven, and release their bounded-pool slot. Runtime-failure and ordinary capture were rerun after this change. Full artificial 64-surface exhaustion and device-removal stress remain untested.
5. Strengthen acceptance with monotonic error/reset counters; polling `last_error` can miss a failure cleared between polls. Successful file creation is a smoke check, not smoothness acceptance.

### P1 — data integrity, audio, shortcuts

1. The Windows tests now force a four-packet recording queue to overflow after accepting media, then require the finalized accepted prefix to exist and decode. The overload regression passed Debug/Release CTest and 20 repeated Release runs. Keep this as a regression gate; real slow-disk/full-disk behavior still requires physical fault testing.
2. Audit writer completion/error state races and global `ClearError()` behavior; do not erase another component's failure. Review normal stop versus overload versus codec-reset finalization.
3. Inspect keyframe alignment, B-frame PTS/DTS, clip rebasing, AAC priming, audio tail, and packet-clone failures. Never silently omit compressed packets from a replay snapshot.
4. Verify microphone/desktop gain, mute, no microphone, device switch/removal/recovery, 44.1/48 kHz, 16/24/32-bit PCM and float storage formats, and sustained A/V drift. Packed 24-bit PCM has deterministic regression coverage but still needs a physical endpoint test. WASAPI endpoint supervision now retries unexpected capture-worker exits; verify actual unplug/replug behavior before claiming device-change recovery.
5. Hotkey persistence, rollback, registration conflicts, and callback routing now have automated coverage. The `-HotkeyActions` real-capture acceptance mode drives save/record actions through the app's actual `WM_HOTKEY` window handler, and the clip/record outputs decode. It posts those messages internally rather than generating physical keyboard input; verify the user's configured chord while a game has focus before claiming full hotkey acceptance.

### P2 — release evidence and compatibility

1. NVIDIA NVENC and AMD AMF have separate pinned acceptance evidence on this host. AMD-native display cadence is still missing because the AMD adapter owns no display. Record Intel unavailable unless tested on Intel hardware; the new pinned Intel acceptance mode requires an actual Intel adapter and `h264_mf` hardware path.
2. Repeat Debug/Release builds and all tests on final code; distinguish portable LLVM-MinGW validation from the release workflow's MSVC/packaged FFmpeg build.
3. Capture long-run CPU, RAM, GPU/encode activity where available, queue bounds, cadence, and sync. Record scene, source method, refresh rate, codec, settings, driver, duration, and binary hash.
4. Update architecture/verification docs, root-cause report, exact commands, results table, remaining limitations, and release checklist. Keep release-blocking failures visible.

## Reproducible local commands

Run from repository root in PowerShell. Portable dependencies are under ignored `work/validation-tools/`.

```powershell
.\tools\bootstrap-validation.ps1
.\tools\build-validation.ps1 -Configuration Debug
.\tools\build-validation.ps1 -Configuration Release

& .\work\validation-tools\llvm\llvm-mingw-20260922-ucrt-x86_64\bin\clang++.exe `
  tools/cadence_scene.cpp -std=c++20 -O2 -static -o work/cadence_scene.exe `
  -ld3d11 -ldxgi -luser32

.\tools\new-cadence-source.ps1 -Fps 120 -Seconds 30
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -NativeScene -Name rc-native60-run1
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 120 -NativeScene -Name rc-native120-run1
.\tools\run-cadence-test.ps1 -Configuration Release -Fps 60 -Name rc-av60-run1
```

Use a fresh `-Name` each time. Tests use `KLIP_DATA_ROOT` to isolate user settings/media and `KLIP_CAPTURE_TRACE=1` for diagnostics. Inspect the logs and decoded `.analysis.json` for both `Clips` and `Recordings`.

The fixture's binary ID wraps after 4,096 frames. The analyzer's global distinct-ID count is therefore not a uniqueness metric for long tests; transitions and repeat runs are more useful. Improve analysis for source resets, invalid barcodes, beep debounce, and baseline fixture validation before using it as an automated release gate. Native-scene runs have no flash/beep sync signal.

The local validation bootstrap now pins dependency URLs and SHA-256 values instead of resolving
`latest`. Its FFmpeg is a BtbN Windows LGPL shared build based on n8.1.2 at commit
`gb2f422d306`, matching the release workflow's upstream version line but not its exact patched
vcpkg source/build configuration. The key NVENC, AMF, software fallback, A/V, and injected runtime
recovery scenarios were rerun with that pinned build; measurements are in
`docs/universal-windows-compatibility.md`.

## Required return to the orchestrator

- Summary of changed files and why each change addresses a demonstrated failure.
- Root causes separated into confirmed, probable, and unresolved.
- Before/after table using comparable valid runs; label contaminated/incomplete runs.
- Exact commands, log/artifact paths, binary hashes, hardware/settings, and test results.
- Remaining defects and unsupported/unavailable hardware or Windows versions.
- A release checklist with passed, failed, and untested entries—not an unsupported “everything works.”

No publishing, deployment, installer replacement, version bump, or broad readiness claim is authorized.
