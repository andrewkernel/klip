# Updater verification — October 11, 2026

Status: local development tests passed for the cases below. **No release,
website, or update feed has been published.** These tests are not a universal
Windows/hardware certification or a clean-VM production-package upgrade test.

## Reproducible commands

```powershell
./tools/build-obs.ps1 -Configuration Debug -BuildSuffix updater-trimmer
./tools/build-obs.ps1 -Configuration Release -BuildSuffix updater-trimmer
./tools/test-updater.ps1 -Name <fresh-evidence-name>
./build-obs-Release-updater-trimmer/klip_update_http_tests.exe `
  <fresh-evidence-name>/payload/current.manifest <new-download-output.exe>
./build-obs-Release-updater-trimmer/bin/64bit/klip_trim_integration_tests.exe <isolated-copy.mkv>
```

The installer harness requires the publisher's already approved current-user
CNG signing key, the pinned build tools, Inno Setup 6, and the verified 2.0.1
installer under `artifacts/klip-2.0.1-beta`. The signing secret is never exported.

## Coverage and results

| Case | Result |
| --- | --- |
| Debug and Release core/security/output/tab tests | 4/4 passed in each configuration |
| Current version offered | No download or reinstallation permitted |
| Manifest tampered | Rejected before downloading/executing an installer |
| Download corrupted | Hash check rejected it; partial removed |
| Download truncated | Size/hash check rejected it; partial removed |
| Connection interrupted | Safe error; no installation; partial removed |
| Download cancelled | Safe error; no installation; partial removed |
| Cache not writable | Safe error; existing test settings/media preserved |
| Cached installer changed after verification | Helper rechecked it and refused; parent did not exit for installation |
| Real Inno installer failure | Notice callback ran; unchanged original test executable reopened |
| Real Inno install success | Helper waited for exact parent exit, installer completed, updated test app restarted |
| Settings/media retention | All cases retained byte-for-byte test sentinels |
| Actual WinHTTP HTTPS delivery | Website's real 2.0.1 installer downloaded and matched signed hash/size |
| Actual WinHTTP cancellation / HTTP 404 | Correctly handled without execution |
| Trimming regressions | Real MKV, 3-track MKV, MP4 and isolated Fortnite clip passed preview/export/decode checks |

Evidence for the installer matrix is in
`work/updater-e2e-complete-20261011/results.json` and each case's installer log
and stdout. The later final rerun uses `work/updater-e2e-final-20261011/`.

Final local Release hashes (diagnostic capture hooks disabled):

- `Klip.exe`: `b52b6de34a70e4db90b76c2a7108eb4cdd6aa260a248e352f8054e53269e0046`
- `KlipUpdater.exe`: `19d02e2581b9eb704ca217a4b9a5a818dc413f5e71971a85e3ab163c37f46269`

Capture acceptance preceded the final transport-message refinement/helper test
extraction, so it is not verification of these final binary hashes. Core/security,
installer matrix and HTTPS tests were rerun on the final build. A missing
installer now produces an actionable unavailable-file message instead of
incorrectly saying no update feed exists.

### What isolation means here

The test parent and helper are separate **test-only binaries**, not the shipping
application. They use the actual `Updater` and `RunUpdateHelper` implementation.
The fixture injects file-backed responses to simulate network faults while
retaining the real manifest/signature/hash/size/version checks. The native
HTTPS test separately exercises the unmodified first-party WinHTTP transport.

`RegOverridePredefKey` maps HKCU only within the test parent/helper processes to
a newly created private test namespace. It does not change the user's real
Klip uninstall entry. The real Inno fixture uses its own app identity, writes
only into this run's workspace folder, creates no uninstall registry entry,
and uses no production capture mutex. Test registry namespaces are removed
after the test; evidence files are retained. The production registry metadata
was compared before/after and was unchanged.

The shipping helper has no registry override, test environment switch, failure
notice suppression, endpoint override, or private key. Its default notice is
still a normal Windows error dialog. A test callback records that notice
without blocking the automated failure-recovery check.

### Real capture evidence and limits

`work/updater-capture-60-20261011/` passed the changed application's NVENC game-
capture acceptance with actual global shortcut input, three replay saves during
a continuous recording, three AAC tracks, decoding and A/V marker checks.
Decoded clips had 59.62–59.80 unique transitions/sec, about 0.30–0.33% repeats,
and zero non-monotonic presentation timestamps. Marker median offsets were
+8 ms in clips and -13 ms in the recording (within the existing 40 ms gate).

This was a synthetic moving capture source, **not Fortnite** and not a CPU/GPU
benchmark. The user's installed Klip process was also still running; whether
its capture was active was not established, so no isolated performance claim
is made. Additional final-binary/120 FPS capture runs are deferred until the
user quits that installed instance. Previously submitted Fortnite clips were
only read via isolated copies for trim testing.

## Remaining release gates

- Run the final production bootstrap installer and a higher signed release on
  a clean Windows VM / standard-user account, including actual app reopening.
- Re-run capture on the final binary after the existing installed instance is
  quit; include 120 FPS and replay-duration acceptance. No AMD/Intel hardware
  was available for new capture tests in this session.
- Verify final package includes the production helper but not the test binaries.
- Publish the immutable, tested installer first, then its signed manifest;
  verify the live in-app flow. The 2.0.1 app currently deployed has no updater.
- Continue beta testing physical audio devices, drivers, and game-focused
  shortcuts. Do not infer all-PC compatibility from one host or these tests.
