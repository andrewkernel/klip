# In-app updates and lossless trimming

2.0.2 beta implementation, October 11, 2026. Users on 2.0.1 and legacy builds
need one manual installation to get the updater. Do not overwrite existing
immutable tags or assets. See release-2.0.2-results.md for publication checks
and outstanding clean-machine/hardware limitations.

Follow-up test results and exact isolation/remaining release gates:
[`updater-verification-2026-10-11.md`](updater-verification-2026-10-11.md).

## User experience

Settings → Updates checks a signed manifest on launch (can be disabled).
Downloads require approval. Installed copies offer **download update**, then
**update & restart**. Portable/developer copies offer version checking but must
be replaced manually. The installer remains per-user, with no elevation. Active
recording, finalization, accepted replay saves, and trims block restart. The
helper waits for the exact parent process to exit before running Inno Setup.
The existing user settings/media directories are not removed.

Settings → Storage offers **open trimmer after each saved clip**, **trim latest
clip**, and **choose a clip to trim**. A saved-clip notification also offers
trimming. Auto-open does not foreground Klip or steal game focus. The editor
has an inline still-frame scrub preview and start/end sliders, not an integrated
audio/video player. **Watch original** opens the system player.

Exports preserve all encoded video/audio streams and save a distinct
`-trimmed-N.mkv` copy. Both boundaries snap outward to keyframes (usually up to
two extra seconds per edge). This is not frame-exact Twitch-style transcoding;
the actual range is shown before export. The original is never overwritten.
Frame-exact export would require a separately tested re-encoding path.

## Update trust and publishing

The current-account CNG key `Klip.ReleaseSigning.v1` was created with user
approval. Its P-256 private key is non-exportable and never written to the
repository, website, release assets, environment variables, or logs. The
public key blob is in `include/klip/update/public_key.h`.

The manifest is a canonical LF-only UTF-8 file with these seven lines:

1. `klip-update-v1`
2. numeric `MAJOR.MINOR.PATCH`
3. exact first-party HTTPS installer URL matching that version
4. lowercase SHA-256
5. byte count (64 KiB–256 MiB)
6. Unix expiry timestamp
7. lowercase P-256 signature (64-byte IEEE P1363, SHA-256 of the first six lines,
   each including its newline)

WinHTTP validates HTTPS normally; redirects are refused. Responses are bounded;
timeouts and cancellation limit work. The client checks the signature, expiry,
version ordering, exact origin/path, hash, and size. The helper independently
checks these, verifies the installed parent executable, and holds the installer
open without write/delete sharing throughout launch. No arbitrary command or
script is accepted from the server. Unsigned installers may still trigger
Windows reputation warnings; manifest signing is **not** Authenticode signing.

After a new version is approved and its exact installer tested:

```powershell
./tools/sign-update.ps1 -Installer artifacts/<release>/Klip-<version>-win64-setup.exe `
  -Version <version> -Output work/<fresh-name>.manifest
```

Publish that manifest as `website/public/updates/windows.manifest` **only after**
the matching immutable installer is reachable at its signed URL. Feed refresh
is necessary before expiry (default 14 days, maximum 21); old expired manifests
are rejected. Version comparison prevents downgrade. Keep beta-channel
semantics explicit when creating the first updater release.

There is no signing secret in CI. Build/package in CI and sign the verified
artifact on this PC. Losing this Windows account/key requires an approved key
rotation or one manual migration release. Never generate a new production key
silently, disable signature verification, or export private material as a
workaround.

Inno's failed-install recovery is not a full application-directory rollback.
The helper retains its installer log and attempts to reopen the installed
application; clean-machine upgrade/failure testing is still a release gate.
Completed installer/cache files are retained; partial downloads are removed.

## Trim implementation and bounds

FFmpeg headers/imports are pinned to the same official OBS dependency archive
used by OBS 32.1.2. No second FFmpeg DLL build or full ffmpeg executable is added
to the package. Preview uses one decoder thread and a ≤640-pixel-wide RGBA image;
only the latest image is uploaded by the UI thread. A single cancellable worker
streams packets at below-normal CPU priority; no full clip is decoded into RAM.
Inputs must be finalized, ≤4 GiB, ≤602 seconds, ≤2 million packets. A source read
handle excludes concurrent writers. Preview seek decoding is capped at 1,500
frames. Files publish only after trailer/close succeeds, with no replacement.

Matroska sometimes omits leading decode timestamps after seeking. The trim
worker reconstructs only a bounded leading prefix from the next known DTS and
stream frame cadence. Packet presentation times and payloads remain preserved.

## Verification commands and release gates

```powershell
./tools/build-obs.ps1 -Configuration Debug -BuildSuffix updater-trimmer
./tools/build-obs.ps1 -Configuration Release -BuildSuffix updater-trimmer
./build-obs-Release-updater-trimmer/bin/64bit/klip_trim_integration_tests.exe <isolated-copy.mkv>
./tools/run-obs-test.ps1 -BuildSuffix updater-trimmer -GlobalHotkeys -AvMarkers `
  -SeparateAudioTracks -Name <fresh-name>
```

Four CTest suites include version/manifest parsing, CNG signature tampering,
installer hash/size rejection, config persistence, and prior core/output/UI-tab
regressions. Signature fixtures are public, fixed-time test data, not executable
updates or private keys. Integration export tests must additionally decode and
compare packet payloads, cadence, audio tracks, and A/V markers.

Local results on October 11:

- Debug/Release: 4/4 suites passed. The helper links only Windows/UCRT libraries
  and a static C++ runtime, so it can continue after the main app exits.
- Export cancellation, existing partial-file preservation, and refusal to read
  an input that is still open for writing passed in both Debug and Release.
- Real release 2.0.1 MKV: requested 5.2–11.3 s, actual video range 4.0–11.999 s;
  480 decoded frames, ~60.003 unique transitions/s, zero repeats, monotonic PTS.
  All 480 video and 375 audio compressed packet payloads exactly matched the
  selected portion of the original.
- Three-audio-track AV fixture: all three AAC tracks retained, 480 video frames,
  eight matched markers, median audio-minus-video offset +5 ms, monotonic PTS.
- User-provided Fortnite MKV (October 7, isolated copy): preview/export passed
  and trimmed output decoded without errors. This does not constitute a new
  live Fortnite capture run or a game-performance benchmark.
- MP4 input remux/export also passed; output format is MKV.
- Native Windows interactive QA harness: start/end scrubbing, preview updates,
  save-copy action, and output confirmation worked. Layout review corrected
  inherited zero padding and clipped slider labels. The Updates tab performed
  a real WinHTTP request and correctly reported that no signed feed exists yet.
  This harness uses the actual panel, trim worker, updater, and D3D preview but
  intentionally does not initialize a capture engine or register hotkeys.

Evidence: `work/trim-tests-20261011/`, and interactive harness
`tests/feature_ui_preview.cpp`. Production capture was not restarted because
the user's installed Klip remains running. The outstanding tray-quit request
must be satisfied before running the changed binary's capture acceptance.

Before publication: test the actual changed capture binary, install the
updater-enabled bootstrap release on a clean Windows VM, upgrade to a second
signed higher version, test cancellation/network loss/bad signature/truncated
download/occupied files/installer failure/restart/settings preservation, and
verify both installed and portable package contents. Nothing is silently
published by these scripts.
