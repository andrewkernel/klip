# Frame pacing and capture quality

Klip has several distinct rates. **Source arrival FPS** counts WGC frames delivered;
**accepted FPS** counts frames admitted to conversion; **submission FPS** counts
frames passed to the encoder; **output FPS** counts packets returned by
`avcodec_receive_packet`. The output clock can repeat the latest source frame to
keep the MP4 at the requested constant frame rate. A 120 FPS MP4 can therefore
show only 60 unique motion updates per second if the source supplies only 60.
Decoded frame IDs and timestamp spacing are the final motion checks.

## Changes in this revision

- The capture callback now tolerates normal arrival jitter. Previously it
  rejected every frame arriving even slightly sooner than exactly `1 / FPS`
  after the previous accepted frame. Alternating early/late 60 Hz arrivals
  could cause Klip to accept only every other frame.
- Frame admission now uses WGC `SystemRelativeTime` (the compositor's frame
  time) instead of callback dispatch time whenever that timestamp is available.
  If unavailable, the session uses callback time and logs the fallback. On the
  RTX 3060 test host, the callback clock admitted 745/1,172 delivered frames at
  a 60 FPS target and 1,374/2,274 at 120 FPS. Compositor-time gating admitted
  all 1,164 and 2,288 delivered frames in corresponding runs. Decoded native-
  scene recordings changed from 59.3 to 60.0 unique transitions/s at 60 FPS and
  119.9 to 120.0/s at 120 FPS. This is one WGC/driver environment, not a
  universal hardware guarantee.
- The encoder now drains the converted-frame queue to the newest frame before
  waiting on GPU completion. It no longer waits on frames that it will discard.
  D3D11.4 fences are preferred; drivers without fence interfaces use a bounded
  event-query wait instead.
- On a valid capture-target change, the fixed-rate output clock continues by
  repeating its last synchronized frame while WGC starts the new target. Replay
  history is cleared to avoid mixing sources; an active recording is preserved.
- The dashboard distinguishes source FPS from output FPS and counts stale
  converted frames as skipped. A low source rate is highlighted.
- Output FPS now uses packets emitted by FFmpeg, not successful frame submissions;
  diagnostics report submissions and emitted packets separately.
- WGC is asked for an update interval of half the requested output frame period
  (twice target FPS) when the runtime supports it. On the RTX 3060 239 Hz setup,
  decoded 120 FPS unique transitions increased from about 60/s before this request
  to about 119/s after it. A source still cannot be forced to produce motion.

OBS is useful as an architecture reference, but its Game Capture path uses a
game hook, while Klip uses Windows Graphics Capture for both selected windows
and displays. OBS's video core advances a fixed output clock and counts missed
intervals as lagged frames. OBS Game Capture separately shares a frame interval
with its injected hook; its source code describes this as limiting very fast
games to avoid excessive capture work, with an option to capture at twice the
configured output rate. That is not the same input/pacing model as Klip's WGC
compositor timestamps, so the two capture backends are not directly equivalent.
These OBS details are reference points, not evidence that Klip matches OBS's
capture behavior or performance.

Source references:

- https://github.com/obsproject/obs-studio/blob/master/libobs/obs-video.c#L3424-L3462
- https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/game-capture.c#L3402-L3427

## Release acceptance on physical hardware

For each NVIDIA, AMD, and Intel configuration intended for release, capture the
same moving scene for at least two minutes in Klip and OBS with the same
resolution, FPS, codec, bitrate, and source type where possible. Include a
selected window and a display test; do not compare Klip's WGC window capture
against OBS's hooked Game Capture as though they were the same source path.

1. Record the dashboard's source FPS, output FPS, skipped count, and encoder.
2. Save a replay clip while a continuous recording is active; verify both MP4s
   decode, contain audio, and have no visible cadence stalls or A/V drift.
3. Use `ffprobe` to inspect average frame rate, duration, and packet timing.
   Nominal 60 FPS alone is insufficient: inspect the motion in the frames.
4. Repeat with the game focused, unfocused, borderless, and exclusive fullscreen
   where supported. Note if WGC supplies fewer unique frames in a mode.
5. Capture CPU, GPU encode, GPU 3D, RAM, and dropped/skipped counters alongside
   the clips. Compare motion smoothness as well as resource usage.

If source FPS is low, investigate WGC/game/display behavior and target selection.
If source FPS is healthy but output FPS is low, investigate conversion, GPU fence/event-query
waits, encoder latency, and queue saturation. If both rates look healthy but
motion stutters, inspect decoded frame uniqueness and timestamp spacing.
