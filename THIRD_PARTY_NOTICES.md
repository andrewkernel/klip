# Third-party notices — Klip 2.0

Klip 2.0 is distributed under GPL-3.0-or-later; see LICENSE. Individual
third-party components retain their own copyrights and licenses. Microsoft
Windows, GPU drivers and the Microsoft Visual C++ runtime are system
prerequisites, not relicensed as GPL components.

## OBS Studio / libobs 32.1.2

The runtime uses the unmodified official OBS Windows x64 libraries and required
capture, WASAPI, encoding, output and overlay modules. OBS is GPL-2.0-or-later.
Source commit: fb4d98bf88fae5fc85cb11fc57f7c5e309282194.
Official runtime SHA-256: 8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6.
Upstream: https://github.com/obsproject/obs-studio/tree/32.1.2
OBS license text is in licenses/OBS-GPL-2.0.txt.

## OBS runtime dependencies

These are the official OBS dependency builds, not Klip's former LGPL-only
FFmpeg 8 SDK. The official FFmpeg 7.1.1 runtime enables GPL and version3
(including x264 and other codecs); the distributed combination uses GPLv3
compatible terms. The release includes source archives, license texts and the
complete upstream build/patch recipes from obs-deps 2025-08-23 (commit
21e25b2b508598ce8239de9ecd68400e45559399). Archive hashes and revisions are
in dependency-sources.json in the corresponding-sources download.
See https://github.com/obsproject/obs-deps/tree/2025-08-23.
OBS's bundled pthreads, JSON, graphics and audio utility sources are included
in the OBS source archive. Open-source dependency sources and notices include
FFmpeg, x264, Opus, Ogg/Vorbis/Theora, libvpx, SVT-AV1, AOM, LAME, mbedTLS,
SRT, librist, zlib, curl, Jansson, rnnoise, SpeexDSP, FreeType, Detours,
Intel libvpl, WIL, zstd, NV codec headers and AMF headers.

## Dear ImGui 1.92.8

Klip statically links ImGui with its Win32/D3D11 backends under the MIT license.
Source archive and license are provided. https://github.com/ocornut/imgui

## LLVM runtime

libc++ and libunwind from llvm-mingw 20260922 are unmodified shared libraries.
The LLVM Apache-2.0 license with LLVM exceptions is in licenses/LLVM-runtime.txt.
Source/build provenance: https://github.com/mstorsjo/llvm-mingw/tree/20260922

## Inter

Copyright 2016 The Inter Project Authors. SIL Open Font License 1.1; complete
license is in bin/64bit/fonts/LICENSE-Inter.txt. https://rsms.me/inter/

Complete Klip source and build scripts are published at the release tag,
and upstream source/build archives accompany the installer/portable download:
https://github.com/andrewkernel/klip/releases/tag/v2.0.0

This inventory is not a legal opinion or a hardware-compatibility guarantee.
