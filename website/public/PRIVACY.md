# Klip Privacy Notice

Effective July 19, 2026

Klip is a local Windows capture application. The current release has no account system,
advertising, analytics, telemetry, crash-report upload, cloud storage, or automatic media
upload.

## Data Klip handles

Klip processes the display or game/window source you select, Windows desktop audio, and—only
when you enable it—the microphone you select. This data is processed on your PC to maintain a
replay buffer and create clips or recordings.

Klip writes the following data locally:

- clips in `%USERPROFILE%\Videos\Klip\Clips`;
- recordings in `%USERPROFILE%\Videos\Klip\Recordings`;
- settings in `%LOCALAPPDATA%\Klip\settings.ini`; and
- diagnostic logs in `%LOCALAPPDATA%\Klip\klip.log`.

The diagnostic log can contain application status, hardware/encoder names, selected capture
source labels, and error details. It does not intentionally contain recorded video or audio.

## Sharing and retention

Klip does not transmit these files to Andrewkernel or any third party. Media and diagnostics
remain until you delete them. Uninstalling Klip does not automatically delete clips,
recordings, settings, or logs.

If you choose to share a clip, recording, or diagnostic log yourself, the destination's terms
and privacy practices apply. Review logs before sharing because window/source labels may reveal
personal information.

## Changes and contact

If a future Klip release introduces network services or materially different data handling,
this notice will be updated before that behavior is released. Contact information will be
published on the official Klip website when public support channels become available.
