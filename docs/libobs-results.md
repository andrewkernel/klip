# libobs migration evidence — 2026-10-06

## Working slice

`tools/build-obs.ps1` builds the existing Klip application with an alternate
media owner (`ObsEngine`). The runtime is official OBS 32.1.2, pinned by release
SHA-256 and source commit. The public libobs API controls video, audio, source
graph, hardware encoder, replay output, and recording output. Neither the legacy
capture/encoder/mixer nor the OBS frontend runs in this build.

The original native UI and hotkey/settings implementations remain. Video uses
NV12, Rec.709 limited, 1080p60 (or the first live source's native resolution).
Balanced NVENC settings observed in the actual encoder log are CQP 18, P5, HQ,
quarter-resolution multipass, High profile, AQ enabled, lookahead disabled, and
two B-frames. Performance selects P3/single pass; maximum quality selects
P7/full-resolution multipass. Resolution, FPS, and CQ are not silently reduced.
An explicit unavailable encoder fails visibly; the settings window stays usable.

Game Capture uses the official `game_capture` implementation and packaged OBS
hooks/helpers. Display Capture uses `monitor_capture`; automatic selection
reports a WGC fallback if DXGI produces no frames. OBS WASAPI sources handle
desktop/microphone volume and device capture. Optional audio tracks are mixed,
desktop-only, and microphone-only. Audio meters use libobs volume meters.

OBS replay and continuous recording share the same encoder objects. Replay
starts once the capture source is ready, avoiding black startup history. MKV is
used for clips and recordings. Saving remuxes compressed packets. A bounded save
queue and reserved unique filenames prevent rapid saves from overwriting each
other (stock replay overwrites a repeated filename). Closing the dashboard hides
it to the tray; Quit performs engine shutdown. Hidden UI does not render.

## Current evidence

### Overlay and event-driven UI integration

Static images now use stock `image_source`; live window/handcam overlays use
stock `window_capture` with WGC, exact-title matching, client-area capture,
cursor off and audio capture off. Overlay position/size is normalized against
the output canvas and applies to both replay and recording before encoding.
Capture-source replacement moves the new capture below the overlay. A missing
live-overlay window waits and is acquired when it appears; its window list
comes from OBS window-capture properties, not the game-capture whitelist.

Only enabled overlays load their extra modules: `image-source` for static
images and `obs-filters` when opacity is below 100%. Opacity uses pinned OBS's
`color_filter_v2` float `opacity` property, checked against its public property
range. Other filters are not instantiated. Scene bounds are updated when canvas
size changes rather than redundantly on every metrics tick. No preview, browser
source, scene editor or frontend is introduced.

`work/obs-overlay-static-half`, `obs-overlay-static-opaque`, and
`obs-overlay-static-transparent` passed decoded position/opacity checks in
three clips plus a concurrent recording each. The 50% white fixture measured
luma 187 against background 25; 100% was opaque and 0% matched the background.
These tests also passed the decoded-motion gate and had no repeated frame IDs.
`work/obs-overlay-live-late` verified acquisition of a window opened after Klip
and alternating captured content in all saved outputs. Its recording measured
59.6 distinct transitions/s and 0.67% repeats; its replays had 1.20–1.33%
repeats. It passed the current 97% motion gate, but is not evidence of zero
repeats or OBS parity. A Debug build ran concurrently during that functional
test; it is not a controlled resource benchmark.

Final media checks on the hashes below:

- `work/obs-overlay-shutdown-debug-final`: 50% static overlay, native resolution,
  three queued saves on immediate shutdown, concurrent recording, three AAC
  tracks, decoded overlay bounds/opacity, motion and A/V gates all passed.
  All four outputs had 0% repeated IDs.
- `work/obs-overlay-live-release-final`: real global hotkeys, close-to-tray,
  delayed live-overlay window and three audio tracks all passed. Both fixture
  windows actually presented about 60 FPS. Replays measured 59.21–59.46 distinct
  transitions/s with 0.66–0.95% repeats and +1 ms A/V offset; recording was 59.1/s,
  1.5% repeats and -20 ms offset. Live-overlay cadence still needs comparison
  against equivalent OBS settings; do not describe it as perfect or zero-repeat.

Computer Use inspected the native dashboard and settings with buffering off
and an old `capture_preview_enabled=true` setting. No preview window appeared.
The UI exposed an idle-input defect: stock ImGui tabs require a preceding
hovered frame because of overlap handling, so a batched move+click could miss
selection. A public-API tab selection helper and bounded four-frame redraw
burst fix it without a continuously active idle render loop. The actual Capture
and Audio tabs switched on the first click after idle; Audio displayed
`42 seconds / custom`. Temporary mouse tracing was removed after diagnosis.
`klip_event_driven_tabs_tests` reproduces the missed click with stock tabs and
checks that the fixed helper selects the requested tab within the redraw burst.

Current Debug/Release builds pass three suites: core tests, output-reservation
tests, and event-driven tab tests. Final hashes for this integration pass:

- Release: `425B3D13F5FD90F8B5A620F4715562D6D7B23E818D69874608225D51E76B25A6`
- Debug: `A34DE6E0D59EBD6643C25B5BDDB98F6C457C4FB0563742E008E901E70FB4E2F5`

`work/obs-destination-loss-recovery` moved only its verified-empty, isolated
test Clips directory aside, requested a save, and restored the directory. The
real failed reservation surfaced Windows error 3, transitioned to Failed and
rejected further requests. Disabling/re-enabling buffering through application
settings recovered without restart. Three subsequent clips and a recording
passed decoded-motion, three-track and A/V gates: recording 60.0 distinct/s,
replays 59.80–59.82/s, all 0% repeats, recording A/V -17 ms. This tests a missing
destination path, not a physical drive disconnect, disk-full or ACL-denial case.

### Necessary behavior changes

- The lightweight build deliberately has no capture preview. Old preview
  settings remain stored for legacy compatibility but cannot open a preview.
- Stock OBS capture sources expose no capture-border suppression property.
  Windows manages WGC indicators; the ineffective highlight toggle is removed
  from this build, with an explanation in Capture settings.
- Stock OBS desktop audio has no process-tree exclusion setting. Its process
  loopback implementation hardcodes `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`
  (`plugins/win-wasapi/win-wasapi.cpp` at the pinned source), which does not
  implement Klip's previous exclusion mode. Old exclusions therefore produce a
  clear startup error. Audio settings provide an explicit clear-exclusion action
  instead of silently recording an application the user chose to exclude.
- Dashboard/compatibility descriptions distinguish OBS render FPS from unique
  source cadence and dashboard D3D11 from the OBS video engine. Disabled buffering
  shows no encoder running rather than an indefinite "warming up" state.

### Save/shutdown hardening

The engine now drains accepted clip requests before stopping its shared outputs.
The drain has a 30-second request-processing deadline; this is not a guarantee
that a blocked stock OBS mux-thread join will finish within 30 seconds. Save
timeouts reject further saves until buffering is restarted, rather than issuing
another mux request against potentially unfinished work. Source changes are
rejected while saves are pending. Recording names use the same collision-safe
reservation policy as replay clips.

Empty-file cleanup occurs only after the output is released and its writer has
joined. Windows file identity, creation time, file size and sharing checks
prevent removing a replacement file, an active writer's file or partial media.
Replay/recording callbacks now only publish atomic events; path/state/log work
runs on the application thread. The OBS log callback catches C++ exceptions.

`work/obs-save-shutdown-debug-final` verified three queued saves followed
immediately by engine shutdown, with a live concurrent recording. A deliberately
constant filename produced `shutdown_collision.mkv`, `_2.mkv`, and `_3.mkv`
without overwriting. All four decoded files had **0% repeated frame IDs**, three
AAC tracks and monotonic presentation timestamps. Replays measured 59.64–60.00
distinct transitions/s with +8 ms beep/flash offset; the recording measured
59.82/s with -13 ms offset. This is a short synthetic test, not Fortnite parity.

Both Debug and Release pass two test suites: core regression tests (including
legacy encoder-setting migration) and Windows output-reservation tests covering
collisions, an active writer, partial-file preservation and replacement-file
identity. Old single encoder selections map to actual OBS IDs; old automatic
fallback lists remain automatic, and an explicit new OBS selection always wins.
The previous Intel Media Foundation preference maps to OBS Quick Sync, which is
a different implementation; unavailable selections fail visibly instead of
silently switching. The legacy media engine itself is unchanged.

Verified binaries for this hardening pass:

- Release SHA-256: `52E591AABD375E333F3B54E92C5B0F876EDF18E5456C7FB3447E7773899F24B3`
- Debug SHA-256: `1C08E27E4C4162BCF5E35612AF82DBF679DDCF7B35FD5B6031A178B290F3AEA6`

Real low-disk, denied-folder, disconnected-drive and mux-process failure tests
remain outstanding. File-safety unit tests do not replace those fault tests.

`work/obs-save-safety-release-final` also passed on the Release hash above:
delayed game source, native resolution, real global hotkeys, close-to-tray,
three saves during recording and three audio tracks. Recording was 60.00
distinct transitions/s; replays were 59.60–60.00/s; all had 0% repeated IDs.
Replay A/V offsets were +1 to +11 ms and recording offsets -20 to -10 ms.
No Klip, OBS frontend, muxer or cadence-fixture process remained at the final
process checkpoint. `git diff --check` passed.

Earlier media test: `work/obs-current-regression` (RTX 3060 / Windows host).
Native D3D11 source at 60 FPS; game opens after Klip; native resolution; actual
global key chords; close-to-tray; three consecutive saves while recording;
three AAC tracks; device-positioned beep/flash markers.

| Output | Decoded distinct updates/s | Repeated source IDs | A/V marker offset |
| --- | ---: | ---: | ---: |
| Recording | 60.00 | 0% | -17 ms |
| Replay 1 | 59.60 | 0% | +4 ms |
| Replay 2 | 59.62 | 0% | +4 ms |
| Replay 3 | 59.64 | 0% | +4 ms |

The replay rate calculation divides transitions by container duration, which
includes the end frame; a short clip can therefore report slightly below 60/s
without repeated frame IDs. The analyzer checks actual decoded IDs, timestamp
monotonicity, three audio streams, and marker coverage/sync within 40 ms. These
are synthetic-source results, not a Fortnite or cross-vendor quality benchmark.

`work/obs-game-av-device-clock` independently passed the same three-save,
global-key, separate-track, decoded-motion and sync checks. The earlier marker
fixture flashed at sound submission and exposed MME output latency; the fixture
now flashes when the sound device reports playback, so the fixture does not
invent that offset. Game capture activation/startup and native-resolution
locking are logged in `work/obs-final-delayed-native-tray`.

Explicit OBS WGC display capture (`work/obs-slice-wgc-final`) produced 59.9
distinct updates/s in its recording and 59.6/s in its clip. Automatic display
fallback (`work/obs-slice-auto-final`) saved valid media but its recording
**failed** the 97% distinct-motion gate (58.0/s). Do not call that gate passed.
Investigate this path against an identical OBS display configuration.

Idle measurement: `work/obs-idle-check/idle-measurement.json`, a 10.0015-second
hidden-window sample with replay disabled. Process CPU time did not measurably
increase; private bytes were 84,836,352 (~80.9 MiB), working set 42,766,336
(~40.8 MiB). This is one short idle sample, not a claim about capture overhead.
The libobs video/audio engine was not started during that sample.

Release and Debug builds and core tests passed during this migration. Repeat
the Debug build after subsequent edits; previous hashes/tests do not validate a
changed binary. Successful capture runs exited cleanly; no Klip, muxer, or
capture-helper process remained at the inspected checkpoints.

## Reproduction

```powershell
# On a clean workspace, obtain the existing pinned validation toolchain first.
& tools/bootstrap-validation.ps1
& tools/prepare-obs.ps1
& tools/build-obs.ps1 -Configuration Debug
& tools/build-obs.ps1 -Configuration Release
& tools/run-obs-test.ps1 -Mode game -GlobalHotkeys -AvMarkers -SeparateAudioTracks -NativeResolution -StartSourceLate -Name obs-new-regression
& tools/run-obs-test.ps1 -Mode display -DisplayMethod 2 -Name obs-new-wgc
& tools/run-obs-test.ps1 -Configuration Debug -Mode game -SaveOnShutdown -AvMarkers -SeparateAudioTracks -Name obs-new-shutdown
& tools/run-obs-test.ps1 -Mode game -Overlay static -OverlayOpacity 0.5 -AvMarkers -Name obs-new-static-overlay
& tools/run-obs-test.ps1 -Mode game -Overlay live -OverlaySourceLate -AvMarkers -Name obs-new-live-overlay
& tools/run-obs-test.ps1 -Mode game -DestinationLoss -AvMarkers -SeparateAudioTracks -Name obs-new-destination-recovery
& tools/open-obs-ui-test.ps1 -Name obs-new-ui-check
```

Use fresh test names: the harness preserves previous media and evidence. Run
the executable from `build-obs-Release/bin/64bit/Klip.exe` with its sibling
runtime/data directories intact. `--obs-idle-smoke-test` requires an isolated
`KLIP_DATA_ROOT/settings.ini` containing `obs_replay_enabled=false`.

## Runtime and licensing

Loaded modules: `win-capture`, `win-wasapi`, `obs-ffmpeg`, `obs-nvenc`,
`obs-qsv11`, `obs-x264`; enabled overlays additionally load `image-source` and/or
`obs-filters` as described above. The prepared SDK stages both overlay modules.
OBS source is pinned to
`fb4d98bf88fae5fc85cb11fc57f7c5e309282194`; the Windows x64 archive digest is
`8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6`.

Packaging includes the PE dependency closure, core shader data, module locale/
data, 32/64-bit game-capture helpers/hooks/manifests, hardware probe executables,
and the OBS muxer. Qt, browser/CEF, OBS frontend API, scripting, WebSocket,
streaming services, transitions, and virtual camera are not loaded. Microsoft
VC runtime provisioning, third-party notices/corresponding source, installer
integration, and a clean-machine loader test still need release verification.
The bundled OBS GPL notice does not by itself complete Klip's GPL distribution
obligations. No release has been published.

## Remaining scope — goal still active

- Gameplay-performance follow-up is in `gameplay-performance-plan.md` and
  `gameplay-performance-results.md`. Hidden/minimized dashboard work and audio
  visualization processing are now suspended, with real lifecycle/audio/clip
  regression tests on separate `*-gameplay` builds. An explicit default-off OBS
  copy limiter is added to Capture settings. Initial enabled-capture samples
  do not prove a CPU/GPU or game-FPS win; do not reuse old hashes/results as proof
  for the modified binary or advertise its benefit before gameplay measurement.
- Initial matched 60 FPS OBS Studio comparison is now measured in
  `libobs-parity-benchmark.md`: both replays had zero repeated frames; Klip's
  memory was lower but CPU higher in short samples. The additional 120 FPS Klip
  test passed the 97% gate, with 0.5–0.66% repeats, not zero. These are synthetic
  source tests, not Fortnite or release sign-off. Complete the remaining benchmark:
  quality, bitrate/file size, cadence, drops, audio sync, CPU, GPU/encode GPU,
  RAM, gameplay frametimes, replay payload memory, and save latency.
- Test focused Fortnite, compatible fullscreen/windowed games, 120 FPS, game
  closure/reopen, source switching, resolution changes, consecutive lifecycle
  starts/stops, memory stability, and fault recovery. Test AMD/Intel where actual
  hardware is available; record unsupported hardware explicitly.
- Test live overlay window closure/reopen, source changes under overlays and
  actual camera windows. Static/live sources and opacity are implemented and
  tested with fixtures; necessary preview/border/audio-exclusion changes are
  documented above, not silently substituted.
- Compare the live-overlay path's observed 0.66–1.5% repeated frames against
  identical OBS settings before claiming motion parity or optimizing that path.
- Finish remaining legacy/UI setting migration, native UI verification, actual
  disk/save fault tests, slow-mux shutdown behavior, and device disconnect/reconnect
  tests. Separate-track creation is tested; a live microphone still needs a
  physical-device acceptance test.
- Complete deployment packaging, GPL/source and third-party redistribution
  requirements, VC runtime prerequisites, clean-machine verification, CI, and
  changing the shipping default to libobs. The migration is not release-ready.
