# Klip libobs migration

## Audit and migration plan

Klip is a C++20 Win32 application with Dear ImGui/D3D11 UI, native global
hotkeys, and an INI settings store. `KlipApplication` initializes and shuts down
the media components. The existing media engine is WGC → D3D11 NV12 conversion
→ FFmpeg hardware H.264 encoding. WASAPI/resampling/mixing feeds FFmpeg AAC.
`PacketRouter` shares compressed packets between `RollingMediaBuffer`,
`ClipWriter`, and `RecordingWriter`; it does not encode twice for recording.
Dependencies include FFmpeg, Media Foundation, NVENC/AMF, WinRT, DXGI, and D3D11.

The custom fixed output clock, compositor capture path, admission gating, GPU
handoff, and mixer are quality-sensitive boundaries. Recent checks proved that
60 FPS metadata can coexist with repeated or irregular source motion. OBS Game
Capture uses its established hook; Klip's previous "Game / Window" path was WGC.
Changing bitrate cannot recover movement absent from captured frames.

Preserve the UI, settings store, native window, global shortcut registration,
folder actions, and shared application state. Introduce `ObsEngine` as the sole
media owner in the libobs build. Do not start the legacy capture/audio/encoders
alongside it. Keep the previous engine available in an explicit legacy build
during migration so its existing uncommitted changes are preserved.

1. Pin official OBS Studio/libobs 32.1.2, its source headers, and Windows x64
   runtime by release SHA-256. Package only required modules and dependencies.
2. Initialize libobs, D3D11 video (NV12, Rec.709, limited), stereo 48 kHz audio,
   and a minimal internal scene. Load win-capture, win-wasapi, OBS hardware
   encoders, obs-x264 fallback, and obs-outputs/obs-ffmpeg output/AAC support.
3. Use OBS Game Capture or Display Capture and the actual encoder property
   identifiers. Start OBS replay_buffer; save through its procedure and saved
   signal. Reuse the same encoder for continuous recording.
4. Wire dashboard source selection, volumes, device choice, presets, shortcuts,
   recording, and saved-file state to the new engine. Record explicitly any
   existing features not yet migrated; never silently drop enabled settings.
5. Verify standalone packaged startup/shutdown, real saved media, consecutive
   saves, concurrent recording, A/V sync, and bounded replay memory. Add a
   reproducible identical-settings OBS comparison and measured report.
6. Complete device-loss/game-close/settings/tray/idle behavior and broad vendor
   testing. Keep the goal active until the acceptance criteria are proven.

Balanced defaults: 1080p60, NVENC H.264, CQP 18, P5, HQ tuning, quarter-resolution
multipass, High profile, AQ on, lookahead off, two B-frames. Apply only properties
and values actually exposed by the pinned encoder. Explicit encoder selection
fails visibly if unavailable; a default automatic selection reports its choice.

No OBS frontend, Qt UI, browser source, streaming service, WebSocket, scripting,
virtual camera, or preview is part of Klip's runtime. OBS replay stores compressed
packets and remuxes saved clips; use MKV for crash-resistant continuous recording
and a supported replay container without re-encoding.

## License and redistribution

OBS Studio/libobs is GPL-2.0-or-later. Distribution of a linked Klip build must
meet the applicable GPL requirements for Klip and the included OBS components:
include license notices and provide corresponding source/build instructions.
The migration is local; do not publish binaries until source/license obligations
and runtime packaging have been verified. Retain OBS module data, game capture
helpers, encoder test executables, and muxer dependencies where required.

Official references:

- https://github.com/obsproject/obs-studio/tree/32.1.2
- https://github.com/obsproject/obs-studio/releases/tag/32.1.2
- https://docs.obsproject.com/reference-core
- https://docs.obsproject.com/reference-outputs

## Phase evidence

Repository audit and the first working capture-to-replay slice are complete.
Klip's libobs build uses the original dashboard/settings/hotkeys and does not
initialize the legacy media components. The build is currently explicit via
`-DKLIP_USE_LIBOBS=ON`; the normal legacy build remains available during migration.
See `libobs-results.md` for actual decoded-media evidence and the outstanding
acceptance gates. The complete migration remains in progress.
