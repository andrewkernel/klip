# Klip product scope and universal compatibility roadmap

Updated: 2026-09-27. This is a product/architecture assessment, not a support or certification
claim.

## What Klip is today

Klip is a single-user Windows game/window/display capture utility. The capture and media core is
native C++20: Windows Graphics Capture feeds D3D11 conversion and FFmpeg encoding; WASAPI supplies
desktop and microphone audio; a bounded encoded-packet replay buffer supports instant clips while
the same encoder can feed a continuous MP4 recording. ImGui/Win32 is the desktop interface.

The architecture already has several sound reliability choices: explicit startup rollback and
ordered shutdown, bounded frame/audio/packet queues, a fixed output timestamp clock, separate
source and emitted-frame telemetry, software-encoder fallback, failure-aware MP4 finalization,
and a deterministic core test target. That is a solid foundation. It does not yet make the binary
universal across all PCs, nor does “enterprise software” mean that a web account/backend is needed.

## Define “universal” before promising it

Recommended first support statement: supported x64 Windows 10 1903+ and Windows 11, with a
WGC-capable D3D11 adapter and software H.264 fallback where Windows Media Foundation is present.
Hardware encoding is opportunistic and must be confirmed at runtime. The app cannot guarantee
equal quality/performance on every driver, and protected or anti-cheat-restricted sources may not
be capturable. ARM64, other operating systems, and systems without usable D3D11/WGC remain out of
scope unless a separate capture backend is designed and tested.

Use “universal” as a measurable coverage goal, not “runs on every computer.” Each supported
hardware/OS row needs launch, source selection, clip, recording, clip-during-recording, decoded
motion cadence, audio behavior, and recovery checks. Keep results by hardware/driver build and
binary hash. See [the compatibility matrix](universal-windows-compatibility.md).

## Recommended changes, ordered by value

1. **Compatibility and recovery first.** Finish the real hardware matrix: Intel iGPU/Arc, AMD-owned
   display, lower-tier integrated graphics/CPU, older Windows versions, and Windows N without the
   Media Feature Pack. Exercise actual driver reset/removal and audio endpoint changes. Address the
   measured 183 ms encoder restart gap; do not mask it with a nominal-FPS counter. Keep existing
   bounded queues and explicit fallback notices.
2. **First-run diagnostics and self-check.** Add a short, user-invoked “check my setup” flow that
   reports OS build, D3D feature level, capture support, adapter/monitor routing, encoders that
   actually open, audio endpoints, disk space, and a tiny local test recording. Explain failures in
   plain language and offer a redacted support bundle. Device names and file paths must be previewed
   before sharing; no telemetry upload by default.
3. **Reliable release operations.** Use signed, reproducible tagged builds; test installer upgrade,
   repair, uninstall, portable extraction, and rollback on clean Windows images. Provide stable and
   beta channels only when a rollback and compatibility report exist. Keep updates opt-in initially.
4. **Managed deployment only if real organizations need it.** Add documented command-line install
   switches, silent per-user install, clean uninstall, policy-controlled settings, and exportable
   configuration before considering accounts, cloud storage, teams, or a service. Enterprise
   additions should not require sending video/audio to a server; local-first capture is a useful
   privacy boundary.
5. **Accessibility and device variability.** Test DPI scaling, high contrast, keyboard-only flows,
   localized paths, multi-monitor/hybrid GPU routing, unusual refresh rates, and audio endpoint
   changes. Keep 30/60/120 choices conditional on measured source and machine capability rather
   than presenting encoder FPS as captured motion.

## Avoid for now

- A cross-platform rewrite: capture, graphics, audio, hotkeys, and window APIs are Windows-specific;
  a portable UI alone would not port the media pipeline.
- A cloud account or upload service: neither is necessary for local clipping and each adds privacy,
  security, cost, and operational obligations.
- Automatic quality changes without user consent. A clearly explained Performance preset is safer
  than silently lowering resolution, FPS, or bitrate.
- Claims of superiority to OBS/ShadowPlay based on a single desktop. Comparisons require identical
  sources, settings, hardware, and decoded frame/audio analysis.

## Current design gaps and next gates

- Settings now has a local compatibility check for Windows build/architecture, D3D feature level,
  live WGC capture and encoder selection, desktop/microphone state, same- vs cross-adapter routing,
  and free storage at both clip and recording destinations. A user-triggered five-second local
  recording verifies the encoder/muxer/output path without uploading anything; it requires at least
  512 MiB free at each destination. The check is a preflight aid, not a substitute for game-motion,
  audio-listening, or hardware-matrix acceptance.
- Support diagnostics now have an in-app review screen; device names, output path, and detailed
  error text are excluded by default and can be explicitly included. Nothing is transmitted by
  Klip. This still needs UI regression coverage and a privacy review of any future report fields.
- Physical Intel encoder, AMD-native display, low-end PC, older Windows, Windows N, device hot-plug,
  and actual game-focused global hotkeys remain unverified; see the compatibility matrix.
- Runtime encoder fallback now avoids attempting NVENC/AMF on known mismatched adapter vendors. In
  the latest fault-injection run this skipped AMF, but fallback still produced a 183.333 ms max
  timestamp gap. Treat recovery as functional but visibly discontinuous until a real driver-fault
  test and bounded recovery improvement pass.
- The current app has logs and a copyable diagnostics path; a structured, privacy-previewed support
  bundle and automated local preflight would reduce support effort as the tester population grows.
- Do not call the product enterprise-ready or universally compatible until its packaging, update,
  security/privacy, support, and hardware matrix gates have evidence—not just a broad feature list.

## Suggested release sequence

| Stage | Gate | Deliverable |
|---|---|---|
| Windows RC | Current architecture and changed paths pass real Windows capture tests; no known data-loss bug | Signed installer candidate, compatibility report, known-limitations page |
| Public beta | Representative GPU/CPU/audio/OS matrix, support bundle, reproducible update/rollback | Opt-in beta, privacy notice, issue intake template |
| Managed rollout | Silent install/upgrade/uninstall, policy config, offline package and rollback | Admin deployment guide and tested enterprise package |
| Broader platforms | New capture/audio backends and hardware matrix per platform | Separate platform-specific products/releases |
