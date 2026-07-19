# Win64 release process

## Automated release

1. Update the version in `CMakeLists.txt`, `vcpkg.json`, the installer fallback, and the
   packaging script fallback.
2. Review `EULA.txt` and `PRIVACY.md`; update their effective dates when terms or data
   handling change.
3. Push a tag such as `v0.3.0`.
4. The `Win64 release` workflow bootstraps a pinned vcpkg commit, builds the dynamic LGPL
   FFmpeg SDK with NVENC/AMF/Media Foundation, restores the static-runtime ImGui dependency,
   builds Release x64, and runs `klip_tests`.
5. The packaging script stages `Klip.exe`, all FFmpeg DLLs, documentation, and license
   notices. It creates a per-user Inno Setup installer with mandatory EULA acceptance and a
   portable ZIP, then emits SHA-256 checksums.
6. The workflow verifies the upstream FFmpeg source with the vcpkg port's SHA-512 and bundles
   it with the pinned port recipe and complete patch set. All three artifacts are published
   together.

Manual `workflow_dispatch` runs produce downloadable workflow artifacts without creating
a GitHub Release. Tag runs create the Release automatically.

Tagged releases are blocked unless the repository has both
`WINDOWS_SIGNING_CERTIFICATE_BASE64` (a base64-encoded PFX) and
`WINDOWS_SIGNING_CERTIFICATE_PASSWORD` secrets. The workflow signs and timestamp-verifies
both `Klip.exe` and the installer. Manual workflow artifacts may remain unsigned for testing.

## Release gate

Do not call a build production-ready until all automated checks pass and the Windows
hardware checklist in `verification.md` has been completed on NVIDIA, AMD, and Intel
systems intended for support. A code-signing certificate is required for tagged releases;
unsigned builds can trigger Windows SmartScreen reputation warnings and are not intended for
broad public distribution.

Have qualified counsel review `EULA.txt` and `PRIVACY.md` before broad commercial distribution.
