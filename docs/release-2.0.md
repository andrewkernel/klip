# Klip 2.0 public beta (native version 2.0.0)

This is a named new-engine edition, not an upgrade-order claim relative to the
previous legacy 3.0.x builds. There is no automatic updater. Install the complete
new package; older shortcuts are handled by the root launcher. The legacy
engine remains in source behind `KLIP_USE_LIBOBS=OFF`, but is not shipped.

## Build from source on Windows x64

Prerequisites: Git, PowerShell 7, Windows 10/11 x64, Microsoft Visual C++
2015–2022 x64 runtime, and Inno Setup 6 (for installer creation). A supported
GPU/driver is required for hardware capture verification. The bootstrap pins
LLVM/MinGW, CMake, Ninja, Windows SDK and ImGui, validating archive hashes.

```powershell
& tools/bootstrap-validation.ps1
& tools/build-obs.ps1 -Configuration Release -BuildSuffix release-2
& tools/build-obs.ps1 -Configuration Debug -BuildSuffix release-2
& tools/run-obs-test.ps1 -Mode game -BuildSuffix release-2 -SourceFps 240 -GlobalHotkeys -AvMarkers -SeparateAudioTracks -Name release-check-new
& packaging/prepare-obs-sources.ps1
& packaging/package-obs-win64.ps1 -BuildDirectory build-obs-Release-release-2 -SourcesDirectory work/release-sources-2.0.0
```

Run from the unpacked source root. Use fresh evidence/output names; do not
overwrite existing verified releases. Diagnostic benchmark hooks must be OFF
in distributed builds. A benchmark build is separate and never distributed.

## Corresponding upstream sources

The release's corresponding-sources ZIP contains Klip source/build scripts,
official OBS 32.1.2 source, ImGui 1.92.8, upstream dependency source archives,
and obs-deps build recipes/patches. `dependency-sources.json` records upstream
URLs, revisions and SHA-256 hashes. OBS uses its official CMake/build scripts;
see its INSTALL, docs and CMakePresets.json. Its Windows dependency build
recipes are obs-deps 2025-08-23 at
`21e25b2b508598ce8239de9ecd68400e45559399`. Unpack the source archives when
building those components and apply the recorded upstream recipe patches.
Klip stages unmodified official OBS binary modules; its adapter builds an
import library from official exports for the pinned LLVM toolchain.

Klip is GPL-3.0-or-later. OBS is GPL-2.0-or-later; its FFmpeg dependency build
enables GPL/version3. Component licenses retain their own terms. Shared LLVM
runtime sources/build provenance are llvm-mingw 20260922 (Apache-2.0 with LLVM
exceptions), not a modified LLVM fork. Windows/GPU drivers and the Microsoft
VC++ runtime are separately installed system prerequisites.

## Verification and scope

Before publication, verify the exact packaged executable and portable ZIP,
manifest/checksums, consecutive saves while recording, real hotkeys and decoded
cadence/audio markers. Installer entry points must target the new engine, and
the old EULA must not ship. Do not claim clean-machine/hardware-matrix tests
that have not actually been performed. This is an unsigned public beta, not
stable release certification; see gameplay-performance-results.md and
libobs-results.md for measured results and remaining limitations. Never
disable security software to install it.

The download endpoint preserves existing aggregate counters and redirects to
the exact versioned files. Source archives/checksums are available alongside
the installer and portable ZIP. A fresh production deployment must be tested
before promotion; do not change the current live site on a failed build.
