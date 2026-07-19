# System requirements and compatibility

## Supported baseline

- 64-bit Windows 10 version 1903 (build 18362) or newer, or Windows 11.
- An x64 processor and a DirectX 11-capable GPU.
- A current GPU driver with a hardware H.264 encoder exposed through NVIDIA NVENC, AMD AMF,
  Intel Quick Sync, or Windows Media Foundation.
- At least 4 GB RAM; 8 GB or more is recommended while gaming.
- Enough free storage for the selected bitrate. At the 12 Mbps default, video uses about
  90 MB per minute before audio and container overhead.
- A Windows audio output device for desktop audio. A microphone is optional.

Klip does not require administrator access. The installer is per-user and installs under
`%LOCALAPPDATA%\Programs\Klip`.

## Current hardware evidence

The current build has been exercised on Windows with an NVIDIA GeForce RTX 3060 using NVENC,
at 1920×1080 and 60 FPS. Replay clips and continuous recordings produced H.264 video plus
48 kHz AAC audio. AMD and Intel paths are implemented but must pass the full hardware checklist
in [verification.md](verification.md) before being advertised as verified configurations.

## Expected limitations

- Protected video and some anti-cheat or exclusive-fullscreen titles may not permit window
  capture. Use Display mode when allowed by the game's rules.
- Outdated or vendor-modified GPU drivers can omit the required encoder interface.
- Windows N editions may require Microsoft's Media Feature Pack for Media Foundation fallback.
- HDR sources are currently captured into the standard SDR/NV12 H.264 pipeline; color may not
  match a native HDR recording.
- An unsigned build can trigger Microsoft Defender SmartScreen. Public tagged releases are
  intentionally gated on a configured code-signing certificate.

When startup cannot find a compatible encoder or capture device, Klip reports the failure in
the dashboard and `%LOCALAPPDATA%\Klip\klip.log` rather than silently creating unusable media.
