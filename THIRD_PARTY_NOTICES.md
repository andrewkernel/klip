# Third-party notices

Klip's Win64 distribution includes the following third-party components.

## FFmpeg

Klip dynamically links to FFmpeg libraries built by the release workflow from a pinned
FFmpeg release and pinned Microsoft vcpkg port. FFmpeg is licensed under the GNU Lesser
General Public License, version 2.1 or later. The distribution includes the upstream
license and build/version configuration. The original source, vcpkg build recipe, and
complete applied patch set are published beside each Klip installer and portable ZIP.

- Project: https://ffmpeg.org/
- Source: https://github.com/FFmpeg/FFmpeg
- Build recipe: https://github.com/microsoft/vcpkg/tree/master/ports/ffmpeg

The FFmpeg DLLs remain replaceable: they are ordinary files beside `Klip.exe`.

## Dear ImGui

Klip statically links Dear ImGui and its official Win32 and DirectX 11 backends. Dear
ImGui is licensed under the MIT License. The packaged distribution includes the license
text installed by vcpkg.

- Project and source: https://github.com/ocornut/imgui

This notice is informational and is not legal advice. Klip itself is distributed under
the End User License Agreement in `EULA.txt`; that agreement does not replace or restrict
the licenses that apply to the third-party components listed above.
