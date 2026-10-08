"""Decode the binary frame-ID fixture; timestamps alone cannot prove motion.

Usage: python tools/analyze-cadence.py FILE --tools FFMPEG_BIN_DIRECTORY
The source must be tools/new-cadence-source.ps1 played full-screen at 16:9.
"""
import argparse
import array
import collections
import json
import math
import pathlib
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('file')
parser.add_argument('--tools', required=True)
parser.add_argument('--overlay-mode', choices=('static', 'live'))
parser.add_argument('--overlay-opacity', type=float, default=0.5)
args = parser.parse_args()
ffmpeg = str(pathlib.Path(args.tools) / 'ffmpeg.exe')
ffprobe = str(pathlib.Path(args.tools) / 'ffprobe.exe')
probe = json.loads(subprocess.check_output([
    ffprobe, '-v', 'error', '-show_streams', '-show_frames',
    '-show_entries', 'stream=codec_type,width,height,start_time,duration,r_frame_rate,avg_frame_rate:frame=media_type,best_effort_timestamp_time',
    '-of', 'json', args.file]))
pts = [float(f['best_effort_timestamp_time']) for f in probe['frames']
       if f['media_type'] == 'video' and 'best_effort_timestamp_time' in f]
process = subprocess.Popen([ffmpeg, '-v', 'error', '-i', args.file, '-map', '0:v:0',
    '-vf', 'scale=1280:720,format=gray', '-fps_mode', 'passthrough',
    '-f', 'rawvideo', 'pipe:1'], stdout=subprocess.PIPE)
ids, flashes, overlay_inside, overlay_outside, overlay_bottom = [], [], [], [], []
previous_flash = False
while True:
    frame = process.stdout.read(1280 * 720)
    if not frame:
        break
    if len(frame) != 1280 * 720:
        raise RuntimeError('Incomplete decoded frame')
    ids.append(sum((frame[40 * 1280 + 32 + 80 * bit] > 160) << bit for bit in range(12)))
    if args.overlay_mode:
        # Fixture rectangle: x=.72, y=.14, width=.20, height=.06. These samples
        # deliberately avoid the source's frame-ID strip and moving white bar.
        overlay_inside.append(frame[int(.17 * 720) * 1280 + int(.82 * 1280)])
        overlay_outside.append(frame[int(.17 * 720) * 1280 + int(.65 * 1280)])
        overlay_bottom.append(frame[int(.205 * 720) * 1280 + int(.82 * 1280)])
    flash = all(frame[680 * 1280 + x] > 220 for x in (100, 640, 1100))
    if flash and not previous_flash and len(ids) > 1:
        flashes.append(pts[len(ids) - 1])
    previous_flash = flash
if process.wait() != 0 or len(ids) != len(pts):
    raise RuntimeError('Decode failed or decoded frame/PTS count mismatch')
deltas = [b-a for a,b in zip(pts, pts[1:])]
steps = [(b-a) % 4096 for a,b in zip(ids, ids[1:])]
duration = pts[-1] - pts[0] if len(pts) > 1 else 0
audio_stream = next((s for s in probe['streams'] if s['codec_type']=='audio'), None)
beeps = []
if audio_stream:
    samples = array.array('f', subprocess.check_output([ffmpeg, '-v', 'error', '-i', args.file,
        '-map', '0:a:0', '-ac', '1', '-ar', '48000', '-f', 'f32le', 'pipe:1']))
    audio_start = float(audio_stream.get('start_time', 0))
    # The fixture uses a 1 kHz tone, 50 ms long once per second. A generic
    # loudness gate mistakes unrelated game audio for markers. Measure the
    # tone's amplitude in non-overlapping 10 ms windows instead.
    window = 480
    frequency = 1000
    omega = 2 * math.pi * frequency / 48000
    tone_windows = []
    for index in range(0, len(samples) - window + 1, window):
        block = samples[index:index + window]
        real = sum(value * math.cos(omega * n) for n, value in enumerate(block))
        imag = sum(value * math.sin(omega * n) for n, value in enumerate(block))
        amplitude = 2 * math.hypot(real, imag) / window
        rms = math.sqrt(sum(value * value for value in block) / window)
        tone_windows.append(amplitude > .025 and amplitude > rms * .45)
    # Join adjacent tone windows, tolerate one quiet window due to capture
    # mixing, and only accept marker-sized runs (20-100 ms).
    runs = []
    run_start = None
    quiet_windows = 0
    for window_index, present in enumerate(tone_windows + [False]):
        if present:
            if run_start is None:
                run_start = window_index
            quiet_windows = 0
        elif run_start is not None:
            quiet_windows += 1
            if quiet_windows > 1 or window_index == len(tone_windows):
                run_end = window_index - quiet_windows + 1
                run_duration = run_end - run_start
                if 2 <= run_duration <= 10:
                    runs.append(run_start)
                run_start = None
                quiet_windows = 0
    beeps = [audio_start + start * window / 48000 for start in runs]
sync_ms = []
av_pairs = []
unmatched_flashes = set(range(len(flashes)))
unmatched_beeps = set(range(len(beeps)))
while unmatched_flashes and unmatched_beeps:
    candidates = [(abs(beeps[bi] - flashes[fi]), fi, bi)
                  for fi in unmatched_flashes for bi in unmatched_beeps]
    distance, flash_index, beep_index = min(candidates)
    # Markers occur together; distant detections are not meaningful pairs.
    if distance > .25:
        break
    offset_ms = (beeps[beep_index] - flashes[flash_index]) * 1000
    sync_ms.append(offset_ms)
    av_pairs.append({'video_flash_seconds': flashes[flash_index],
                     'audio_tone_seconds': beeps[beep_index],
                     'audio_minus_video_ms': offset_ms})
    unmatched_flashes.remove(flash_index)
    unmatched_beeps.remove(beep_index)
if len(av_pairs) > 1:
    xs = [pair['video_flash_seconds'] for pair in av_pairs]
    ys = [pair['audio_minus_video_ms'] for pair in av_pairs]
    x_mean = statistics.mean(xs)
    y_mean = statistics.mean(ys)
    x_variance = sum((x - x_mean) ** 2 for x in xs)
    av_offset_drift_ms_per_minute = (
        60 * sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) / x_variance
        if x_variance > 0 else 0.0)
else:
    av_offset_drift_ms_per_minute = None
result = {
    'file': str(pathlib.Path(args.file).resolve()),
    'streams': probe['streams'], 'decoded_frames': len(ids),
    'presentation_span_seconds': duration,
    'unique_frame_ids': len(set(ids)),
    'unique_transitions_per_second': sum(step != 0 for step in steps) / duration if duration else 0,
    'consecutive_repeats': steps.count(0),
    'repeat_percent': 100 * steps.count(0)/len(steps) if steps else 0,
    'source_id_step_histogram': dict(collections.Counter(steps)),
    'presentation_interval_ms_histogram': dict(collections.Counter(round(d*1000, 3) for d in deltas)),
    'nonmonotonic_presentation_timestamps': sum(d <= 0 for d in deltas),
    'max_presentation_interval_ms': max(deltas, default=0)*1000,
    'flash_count': len(flashes), 'beep_count': len(beeps),
    'matched_av_markers': len(sync_ms),
    'unmatched_flashes': len(flashes) - len(sync_ms),
    'unmatched_beeps': len(beeps) - len(sync_ms),
    'audio_minus_video_ms': sync_ms,
    'matched_av_pairs': av_pairs,
    'median_audio_minus_video_ms': statistics.median(sync_ms) if sync_ms else None,
    'av_offset_drift_ms_per_minute': av_offset_drift_ms_per_minute,
    'limitations': 'Includes player/compositor cadence and playback A/V offset. Not a game FPS benchmark. Positive sync offset means audio later than flash.'
}
if args.overlay_mode:
    differences = [inside - outside for inside, outside in zip(overlay_inside, overlay_outside)]
    if args.overlay_opacity == 0:
        valid = max((abs(d) for d in differences), default=999) <= 10
    elif args.overlay_mode == 'static':
        valid = sum(d > 40 for d in differences) >= .95 * len(differences)
        if args.overlay_opacity == 1:
            valid = valid and statistics.median(overlay_inside) >= 240
        else:
            valid = valid and 70 < statistics.median(overlay_inside) < 235
    else:
        # A changing handcam fixture must appear in the encoded media, not just
        # exist as a successfully created OBS source with a frozen first frame.
        valid = sum(d > 40 for d in differences) >= 10 and sum(d <= 10 for d in differences) >= 10
    valid = valid and max((abs(a - b) for a, b in zip(overlay_bottom, overlay_outside)), default=999) <= 15
    result['overlay'] = {'mode': args.overlay_mode, 'opacity': args.overlay_opacity,
                         'inside_min_luma': min(overlay_inside, default=None),
                         'inside_max_luma': max(overlay_inside, default=None),
                         'inside_median_luma': statistics.median(overlay_inside) if overlay_inside else None,
                         'outside_median_luma': statistics.median(overlay_outside) if overlay_outside else None,
                         'passed': valid}
    if not valid:
        print(json.dumps(result, indent=2))
        raise SystemExit('Decoded overlay position, opacity or live-content test failed')
print(json.dumps(result, indent=2))
