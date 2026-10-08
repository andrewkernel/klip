# System requirements and compatibility

## Compatibility tiers

- **Baseline target:** x64 Windows 10 build 18362 (1903) or newer, or Windows 11, with a working
  Windows Graphics Capture path and D3D11 feature level 10.0 or newer. This identifies the API
  floor; it is not a claim that every GPU/driver on those systems has been tested.
- **Recommended setup:** current Windows and GPU drivers, 8 GB or more of RAM, a hardware H.264
  encoder, and at least 1 GiB free in the clip/recording destination before testing. An encoder is
  only considered usable after Klip successfully opens it on the active D3D device.
- **Validated configurations:** only hardware/Windows/driver combinations listed as tested in
  `docs/universal-windows-compatibility.md`. Untested Intel, AMD-native-display, low-end, hybrid,
  Windows N, or older-Windows rows must remain “not yet validated,” even if they meet the baseline.
- **Not currently supported:** x86 Windows, ARM64 Windows, Windows builds older than 1903, and
  machines without a usable WGC/D3D11 hardware-capture path. Software video encoding does not
  remove Klip's D3D11/WGC requirement.

## Supported baseline

- 64-bit Windows 10 version 1903 (build 18362) or newer, or Windows 11.
- An x64 processor and a Windows Graphics Capture-compatible D3D11 adapter. Device creation
  requests feature levels 11.1 and 11.0 first, then falls back through 10.1 to 10.0.
- GPU work synchronization uses D3D11 fences when available and falls back to bounded event-query
  synchronization on drivers without the D3D11.4 fence interfaces.
- A current GPU driver. Klip prefers the available hardware H.264 encoder (NVIDIA NVENC, AMD AMF,
  or Windows Media Foundation hardware encoding) and includes a tested Windows Media Foundation
  software fallback.
- If FFmpeg cannot bind a hardware encoder context to the selected D3D11 device, Klip now keeps
  the capture pipeline alive and tries Media Foundation software encoding rather than failing
  startup. This is a compatibility path, not a promise of hardware-equivalent frame pacing:
  software encoding can use significantly more CPU, especially at high resolution/FPS. If the
  fallback is active, the 720p Performance preset is the tested lower-load profile; users should
  lower output FPS/resolution further if the machine cannot sustain the selected workload. Klip
  reports the fallback in the dashboard and log rather than presenting it as a hardware encoder.
  A one-logical-core stress run on the available Ryzen 5 7600X showed that 1080p Balanced-quality
  software encoding can have substantial cadence gaps after fallback; the dashboard recommends
  Performance mode instead of silently changing the user's quality or output size.
- On systems with more than one GPU, Klip first attempts Windows' default hardware adapter and
  then retries other installed hardware adapters if device initialization fails. Adapter choice
  can affect capture compatibility and performance, especially on hybrid-graphics laptops.
- At least 4 GB RAM; 8 GB or more is recommended while gaming.
- Enough free storage for the selected bitrate. At the 12 Mbps default, video uses about
  90 MB per minute before audio and container overhead.
- WGC capture targets must not exceed 8192 pixels in either dimension. Source-sized output rounds
  odd window dimensions down by one pixel to satisfy NV12 encoding requirements.
- A Windows audio output device for desktop audio. A microphone is optional.
- WASAPI 16-bit PCM, packed 24-bit PCM, 24 valid bits in a 32-bit PCM container, 32-bit PCM,
  and 32-bit float formats are handled; unusual vendor-specific encodings may still be rejected.

Application-specific desktop-audio exclusion uses Windows process loopback where available.
Microsoft documents that API from Windows build 20348; on earlier Windows 10 builds or when that
activation is unavailable, Klip falls back to ordinary endpoint loopback, so desktop audio remains
available but the selected application's audio cannot be excluded. See Microsoft's
[process-loopback requirements](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params).

Packed 24-bit PCM is unpacked to 32-bit samples before resampling. This path has deterministic
regression coverage but still needs validation with a physical 24-bit device. Klip can also start
and record video when no Windows audio output endpoint is available. Desktop
audio remains silent until a usable endpoint is enabled; Klip reports endpoint errors without
blocking capture. Use video-only mode for headless/virtual PCs or systems without playback audio.

The 30 FPS software-fallback audiovisual fixture previously failed the ±40 ms marker threshold,
but two later runs passed it with all matched markers inside the limit (recording medians about
-29 to -31 ms; clips about -27 to -28 ms). Since FFplay source/compositor/audio-endpoint timing
is not independently isolated and historical results varied, keep 30 FPS fallback sync as a
compatibility check rather than treating these runs as proof for all hardware.

Klip does not require administrator access. The installer is per-user and installs under
`%LOCALAPPDATA%\Programs\Klip`.

If a desktop-audio or microphone WASAPI worker exits unexpectedly, Klip reports the endpoint
failure and retries discovery/reopening in the background. A removed microphone is reselected by
the configured name when it returns, or falls back to the first active input. This recovery logic
is code-reviewed and build-tested; physical unplug/replug behavior still needs confirmation.

## Current hardware evidence

The current build has been exercised on Windows with an NVIDIA GeForce RTX 3060 using NVENC,
1920×1080/60 and 120 FPS display capture, 60/120 FPS window capture, and 60-second recording.
Replay clips and continuous recordings produced H.264 video plus 48 kHz AAC audio. The forced
`h264_mf_software` path also produced a
complete recording at 1920×1080, with decoded 60 FPS marker motion and no repeated marker frames in
the short run. A Radeon(TM) Graphics adapter completed Klip WGC/AMF clip-and-record tests at 60
and 120 FPS; an AMF GOP configuration defect was fixed and the 120 FPS file passed duration,
periodic-keyframe, and full-decode checks. However, DXGI reports that this AMD adapter owns no
active display on the test PC (both active monitors are driven by NVIDIA). Forced AMD capture was
therefore cross-adapter, and its 120 FPS unique-motion results varied substantially. AMD-native
display cadence is not verified. This evidence is not a low-end benchmark or broad driver
compatibility claim. Intel hardware encoding, a physical no-audio-device test, and multi-adapter
fallback after default-device failure remain untested; see the checklist in
[verification.md](verification.md).

For compatibility stress, the 720p software encoder also passed native moving-window clip-plus-
record runs at 60 and 120 FPS while the Klip process was pinned to one logical processor on that
same modern Ryzen system. A similar 30 FPS one-core run had 100–133 ms presentation gaps and did
not pass the cadence gate. Processor affinity is only a rough stress aid; these results do not
establish minimum CPU requirements or predictable performance on older/low-power processors.

The D3D feature-level fallback was forced to 10.1 and 10.0 on the RTX 3060 test host; both completed
WGC capture, NVENC clip-plus-record acceptance, and full media decode. This proves that Klip's
pipeline works when the device is created at those lower feature levels, but not that a physical
feature-level-10 GPU or its driver will expose all required WGC, video-processor, fence, and encoder
interfaces. Actual low-end adapter coverage remains outstanding.

## Expected limitations

- When output dimensions are left at **Source**, Klip locks the output size to the first captured
  target's dimensions for the lifetime of the media pipeline. Later window/display size changes
  are scaled into that stable output so an active MP4 recording is not silently split or
  truncated. Odd source dimensions are rounded down by one pixel for NV12 compatibility. A brief
  capture cadence dip during resize is still possible.

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
If no H.264 encoder can be opened, the error now lists the attempted backends and native failure
details (within a bounded message) and calls out the Windows N Media Feature Pack and GPU driver as
common things to check. A machine without any usable encoder was not available for physical UI
verification; the formatter is covered by deterministic tests.
If startup or a runtime encoder failure causes a fallback, the dashboard keeps a visible notice;
the copyable diagnostics include Windows build, CPU architecture/core count, total RAM, active
adapter/vendor, selected encoder, rejection/failure reason, current capture target, output settings,
and frame-drop/latency counters. Review device and target names before sharing.
If a hardware encoder ignores a quality-tuning option, the log now names the option and notes
that its backend default was used; this helps explain vendor/driver-specific quality differences.
UTF-8 paths containing accented and CJK characters have passed an end-to-end Windows clip and
recording test, including settings persistence and MP4 decoding.

Klip's Windows compatibility scope, the meaning of hardware versus software fallback,
the current tested-device matrix, and untested Windows/GPU classes are tracked in
[the Windows compatibility plan](universal-windows-compatibility.md). “Universal” here
means improving coverage across supported x64 Windows PCs; it does not mean all
operating systems or all PC hardware have been certified.
