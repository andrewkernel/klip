# Klip 2.0 beta release evidence — October 7, 2026

Native version: 2.0.0. Capture engine: official libobs 32.1.2. GPL-3.0-or-later.
Release build: `build-obs-Release-release-2/bin/64bit/Klip.exe`, SHA-256
`1dc70441263cae668a92466360e56475f1f1aa20b7bf306b083d9cdea5042cd5`.
Diagnostic hooks are disabled; the legacy media engine is not initialized.

Debug and Release each passed the three deterministic test suites. In
`work/release-2-functional`, real configured global shortcuts, three saves
while recording, separate AAC tracks and clean shutdown passed. All four
files decoded with zero repeated frames and approximately 60 distinct
updates/s from a 240 FPS controlled source. These are not Fortnite overhead
benchmarks or live microphone quality tests.

The portable ZIP was extracted to a fresh directory and all manifest hashes
verified. File/product version was 2.0.0. The root compatibility launcher
passed its thin startup smoke check; that check alone does not initialize or
test capture. A separate actual capture regression used the **extracted**
`bin/64bit/Klip.exe` in `work/release-2-extracted-capture`: all four saved files
decoded with zero repeats, clips 59.60–59.64 distinct/s and recording 60/s.
Clip median A/V marker offset was +18 ms. The physical microphone was disabled.

The Inno installer was compiled from the same staged payload and uses the root
launcher for old shortcut compatibility. Microsoft Visual C++ x64 runtime is
an explicit system prerequisite with an actionable launcher message. A clean
Windows VM installer run, missing-runtime branch interaction and broader
AMD/Intel/game testing have **not** been performed. This is an unsigned public
beta, not stable or universally compatible release certification.

## Exact distribution artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Klip-2.0.0-win64-setup.exe | 18261308 | 9b4cc691a088601159e4e0936841da7cff10a604bbd3253a025d602273c31366 |
| Klip-2.0.0-win64-portable.zip | 25963198 | 791ef517e7921af3ddd62eb29f3f51ac862aa5a8539647f7f67ef8e5598ebf80 |
| Klip-2.0.0-corresponding-sources.zip | 373857746 | 8b06c2698e716e91719fd6d82a6a65cdbd5c68833ac960b3d1151adfb0ba11de |

The source bundle contains Klip source commit `2afd718`, the unmodified OBS
source, 25 verified upstream dependency source archives with source revisions
and hashes, official dependency build/patch recipes and ImGui source. Later
website-only validation changes do not change the native binary. Complete
current source is also available at the release tag. Microsoft system runtime
and GPU drivers are not bundled/relicensed. Font, LLVM, OBS and other component
notices are included in the payload and source archives.

## Website verification

The 2.0 page preserves the existing design, removes legacy engine/fallback
claims, discloses MKV output/Performance tradeoffs/beta limitations, and links
source archives and checksums. An out-of-sync dependency lockfile was repaired.
Strict TypeScript validation and seven rendered/route/artifact tests passed.
Unknown, null and prototype-named download queries now return 400; ordinary
download requests preserve the existing aggregate Redis/D1 counter keys.

Before deployment the public statistics endpoint returned total 348
(installer 289, portable 59). No counters were reset or migrated. Actual
post-deployment HTML, download redirect/hash and counter checks are still
required; the successful local build is not evidence of a live deployment.
