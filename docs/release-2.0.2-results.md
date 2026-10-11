# Klip 2.0.2 public beta — October 11, 2026

Adds approval-based signed updates for installed copies and a bounded native
lossless trimmer. The 2.0.1 capture/duration-first policy remains unchanged.
Older versions require one manual installation to bootstrap the updater.
Portable copies still update manually. Executables remain unsigned; manifest
signing is not Authenticode and does not remove Windows reputation warnings.

## Final application verification

Debug and Release passed 4/4 deterministic CTest suites. The ten-case isolated
updater matrix passed again on 2.0.2, including real Inno install/restart,
installer-failure recovery, cancellation, tampering, truncated/corrupt delivery,
unwritable cache and settings/media retention. Test-only version 9.0.0 keeps the
fixture higher than the application; no fixture is distributed or published.
Evidence: `work/release-202-updater-20261011/results.json`.

Final Release executable SHA256:
`5cc097ee5f15bbd6fe00404058a6428287702f24a95e8524c5efcf57582c1a1f`.
Production helper SHA256:
`ec968fa19635ee13c6b7da5c3ce19e006e23abf2faabe8c1d478b03449d390a6`.

After the installed instance was quit, the final executable passed actual
NVENC/OBS game capture at 1080p60 and 1080p120, three consecutive replay saves
while recording, real global shortcut input and three AAC tracks. Decoded
60 FPS media measured 59.81–60.00 distinct transitions/sec with zero repeated
source IDs. At 120 FPS, clips measured 119.62–120.00 distinct transitions/sec
with zero repeated IDs; the recording measured 119.60/sec with 0.334% repeats.
All files passed monotonic PTS and the 40 ms A/V marker gate. Evidence:
`work/release-202-capture60-20261011` and
`work/release-202-capture120-20261011`. These are synthetic moving sources,
not Fortnite benchmarks or all-hardware claims.

The final trimmer passed preview/export, invalid range, cancellation, occupied
input and partial-file collision checks on an isolated copy of the submitted
Fortnite MKV. Earlier MKV/MP4 and three-track audio tests are documented in
`updater-verification-2026-10-11.md`, including packet equality and decoded A/V.

## Reproduction

```powershell
./tools/build-obs.ps1 -Configuration Debug -BuildSuffix updater-trimmer
./tools/build-obs.ps1 -Configuration Release -BuildSuffix updater-trimmer
./tools/test-updater.ps1 -Name <fresh-updater-run>
./tools/run-obs-test.ps1 -BuildSuffix updater-trimmer -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name <fresh-60-run>
./tools/run-obs-test.ps1 -BuildSuffix updater-trimmer -Fps 120 -SourceFps 120 -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name <fresh-120-run>
./tools/run-obs-test.ps1 -BuildSuffix updater-trimmer -ReplayDurationTest -NoiseScene -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name <fresh-duration-run>
```

## Remaining beta limitations

Clean-Windows production bootstrap plus a higher signed production-package
upgrade has not been certified. The isolated Inno fixture is not a substitute.
No new AMD/Intel, physical microphone matrix, live Fortnite or Linux capture
coverage is claimed. Failed-install recovery is not full directory rollback.
Trim boundaries snap outward to keyframes rather than being frame-exact.
Signed feeds expire and must be renewed on the publisher's key-owning account
before expiry; the key remains non-exportable and outside Git/CI.

This publication is an unsigned public beta, not a stable or universal release.
