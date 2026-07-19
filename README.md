# Klip

Klip is a focused Win64 game recorder: keep a rolling replay buffer, save the moment
that just happened, or record a full session. There is deliberately no streaming stack,
scene compositor, browser source, or plugin host competing for system resources.

## What it does

- Captures a selected game/window or an entire display with Windows Graphics Capture.
- Saves the last 15–300 seconds without interrupting capture.
- Starts and stops continuous MP4 recording from the same encoded packet stream.
- Mixes Windows desktop audio with an optional, selectable microphone.
- Uses GPU H.264 encoding through NVIDIA NVENC, AMD AMF, or Windows Media Foundation
  hardware encoding (the Intel-friendly fallback).
- Exposes frame rate, resolution, bitrate, encoder, load preset, replay length, audio
  bitrate, cursor capture, microphone, and storage settings in the app.
- Keeps every hot queue and replay buffer bounded; overload drops frames instead of
  allowing RAM use or latency to grow without limit.

Klip uses a compact capture deck rather than an OBS-style scene editor. Its replay ring
is the main status signal: once it fills, `Alt+C` can preserve what just happened.

## Download and install

Tagged builds publish two self-contained x64 artifacts on the
[GitHub Releases page](https://github.com/andrewkernel/klip/releases):

- `Klip-<version>-win64-setup.exe` — per-user installer; no administrator access needed.
- `Klip-<version>-win64-portable.zip` — unzip and run `Klip.exe`.

The package includes the required FFmpeg DLLs and uses the static Microsoft C/C++
runtime. Windows 10 version 1903 or newer, or Windows 11, is required. A current GPU
driver and a supported hardware H.264 encoder are strongly recommended.

The installer shows the [End User License Agreement](EULA.txt) and requires explicit
acceptance before installation. See [system requirements](docs/system-requirements.md) for
the supported baseline and currently verified hardware. SHA-256 checksums are published with
every release.

## Use

1. Choose **Game / Window** or **Display**, then select the exact source.
2. Leave **Desktop audio** on for game/system sound. Enable **Microphone** for voice or
   physical keyboard clicks; desktop loopback cannot hear sounds in the room.
3. Wait for the replay ring to become ready.
4. Press **Save last … seconds** or `Alt+C` for a replay clip.
5. Press **Start recording** or `Alt+R` for a full recording.

`Alt+X` hides or restores the UI. Source, microphone, and mode selections are remembered.
Capture mode/source and microphone changes apply immediately; encoder and media format
changes apply after restarting Klip.

Klip's game/window mode is a low-overhead Windows Graphics Capture path, not an injected
DirectX/OpenGL hook. It is safer and much smaller than OBS's dedicated Game Capture
hook, but protected video and a few exclusive-fullscreen games may still require
**Display** mode.

## Files and privacy

- Clips: `%USERPROFILE%\Videos\Klip\Clips`
- Recordings: `%USERPROFILE%\Videos\Klip\Recordings`
- Settings: `%LOCALAPPDATA%\Klip\settings.ini`
- Diagnostics: `%LOCALAPPDATA%\Klip\klip.log`

Klip records locally and contains no upload, telemetry, account, or streaming feature.
Completed media is written as `.partial` and becomes visible as `.mp4` only after the
MP4 trailer is finalized. Recordings start at a video keyframe so each file is independently
decodable.

See the full [Privacy Notice](PRIVACY.md). Uninstalling Klip leaves user clips, recordings,
settings, and logs in place so an application update cannot silently delete personal media.

## Resource model

The default 60 FPS / 12 Mbps configuration uses one WGC session, one D3D11 BGRA-to-NV12
conversion path, one hardware video encoder, and one AAC encoder. The replay and recording
writers share reference-counted encoded packets—starting a recording does not start a second
screen capture or video encode.

Defaults cap each raw/converted video queue at four frames, the recording backlog at 512
packets, and the encoded replay buffer at 192 MiB. Capture is throttled before conversion to
the selected FPS. The most recent converted texture is reused with GPU-to-GPU copies on a fixed
frame clock, so static windows still produce valid constant-rate media without CPU pixel copies.
The audio mixer waits for a complete AAC-sized sample window before encoding, preventing short
WASAPI packets from being padded into a repeating click/static pattern.
The dashboard reports encoded FPS, selected encoder, replay RAM, and dropped frames so load is
visible rather than hidden.

## Build from source

Prerequisites:

- Visual Studio 2022 with Desktop development with C++ and a current Windows SDK
- CMake 3.24+
- vcpkg
- Inno Setup 6 for the installer (use `-SkipInstaller` only when a portable ZIP is intended)
- An x64 shared FFmpeg SDK with headers, MSVC import libraries, runtime DLLs, and the
  required hardware encoders

The release workflow builds FFmpeg 8.1.2 from its checksum-verified source with a pinned
vcpkg port, enabling NVENC, AMF, and Windows Media Foundation. To prepare the same dynamic
FFmpeg SDK locally, then build Klip with a static app runtime:

```powershell
vcpkg install `
  --x-manifest-root=packaging/ffmpeg-vcpkg `
  --x-install-root=dependencies/ffmpeg-installed `
  --overlay-triplets=packaging/triplets `
  --triplet=x64-windows-klip
cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DFFMPEG_ROOT=dependencies/ffmpeg-installed/x64-windows-klip
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

To assemble the portable ZIP and installer after building:

```powershell
.\packaging\package-win64.ps1 `
  -BuildDirectory .\build\Release `
  -FfmpegRoot .\dependencies\ffmpeg-installed\x64-windows-klip `
  -Version 0.3.0 `
  -VcpkgInstalledDirectory .\build\vcpkg_installed
```

`packaging/package-win64.ps1` validates the executable and required runtime DLLs before
publishing artifacts. The GitHub Actions workflow builds, runs tests, creates both packages,
and attaches the checksum-verified original FFmpeg source plus the pinned vcpkg recipe and
complete patch set used to build it for LGPL compliance.

## Engineering notes

- [Architecture and threading](docs/architecture.md)
- [Verification checklist](docs/verification.md)
- [Release process](docs/release.md)
- [System requirements and compatibility](docs/system-requirements.md)
- [Design and capture research](docs/research.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

Klip is distributed under its [End User License Agreement](EULA.txt). The repository is
source-available for inspection and development; no broader right to redistribute Klip's own
source or binaries is granted unless the EULA or a separate written agreement says otherwise.
Third-party components retain their own licenses.
