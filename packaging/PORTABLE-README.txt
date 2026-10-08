Klip 2.0 public beta

Extract the complete ZIP, then open Klip.exe. Keep bin, data,
obs-plugins and licenses together. Do not copy only the executable.

Close older Klip instances before starting. Settings and saved media remain
in your Windows user folders unless KLIP_DATA_ROOT is explicitly configured.
The window close button hides Klip; use the tray menu's Quit Klip to exit.

This beta is unsigned. Check the published SHA256SUMS before running it.
Do not disable antivirus or other Windows security features to install Klip.
Requires Microsoft's Visual C++ 2015-2022 x64 runtime (also required by OBS).
If missing, install it from https://aka.ms/vs/17/release/vc_redist.x64.exe.

New engine: stock OBS/libobs Game Capture, audio, encoding and replay output.
Saved clips/recordings use MKV. Replay memory limits may shorten clips.
Performance mode trades encoder tuning/quality for lower overhead; it does
not silently change your selected resolution, frame rate or CQ.

Compatibility and performance vary with the game, hardware and drivers.
No universal OBS-parity or lower-overhead guarantee is made. Read LICENSE,
PRIVACY.md and THIRD_PARTY_NOTICES.md before redistribution.
