"""Align sampled decoded frames to the actual cadence source trace and RGB raster.

This deliberately simple source tests configuration parity, not game-image quality.
No reference is inferred from a lossy OBS or Klip recording.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics
import subprocess

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('file', type=pathlib.Path)
parser.add_argument('--root', required=True, type=pathlib.Path)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args = parser.parse_args()
ffmpeg = str(args.tools / 'ffmpeg.exe')
ffprobe = str(args.tools / 'ffprobe.exe')
probe = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-show_streams',
    '-show_format', '-show_packets', '-show_entries',
    'stream=codec_type,width,height:format=duration,size:packet=stream_index,size',
    '-of', 'json', str(args.file)]))
video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
width, height = video['width'], video['height']
with (args.root / 'source-first.ppm').open('rb') as file:
    if file.readline().strip() != b'P6':
        raise RuntimeError('Expected the actual startup RGB readback')
    source_width, source_height = map(int, file.readline().split())
    if file.readline().strip() != b'255':
        raise RuntimeError('Unsupported readback depth')
    reference_first = np.frombuffer(file.read(), dtype=np.uint8).reshape(source_height, source_width, 3)
if (source_width, source_height) != (width, height):
    raise RuntimeError('This parity test requires native-size output, not approximate rescaling')

def point(x, y):
    return reference_first[y * height // 720, x * width // 1280].copy()

colors = {'blue': point(1100, 300), 'black': point(100, 40), 'white': point(30, 300)}
with (args.root / 'source-frames.csv').open(newline='') as file:
    trace = list(csv.DictReader(file))
by_id = {}
for row in trace:
    index = int(row['source_index'])
    by_id.setdefault(index % 4096, []).append((index, row['flash'] == '1'))

def expected(index, flash):
    raster = np.empty((height, width, 3), dtype=np.uint8)
    raster[:] = colors['blue']
    def rectangle(x, y, w, h, color):
        raster[y * height // 720:(y+h) * height // 720,
               x * width // 1280:(x+w) * width // 1280] = color
    rectangle(0, 0, 1280, 96, colors['black'])
    for bit in range(12):
        if index & (1 << bit):
            rectangle(bit * 80, 8, 64, 64, colors['white'])
    rectangle((index * 7) % 1200, 150, 80, 450, colors['white'])
    if flash:
        rectangle(0, 640, 1280, 80, colors['white'])
    return raster

if not np.array_equal(expected(0, trace[0]['flash'] == '1'), reference_first):
    raise RuntimeError('Reconstructed reference does not match the actual GPU startup readback')

process = subprocess.Popen([ffmpeg, '-v', 'error', '-i', str(args.file), '-map', '0:v:0',
    '-vf', r'select=not(mod(n\,60)),format=rgb24', '-fps_mode', 'passthrough',
    '-f', 'rawvideo', 'pipe:1'], stdout=subprocess.PIPE)
samples = []
while True:
    data = process.stdout.read(width * height * 3)
    if not data:
        break
    if len(data) != width * height * 3:
        raise RuntimeError('Incomplete decoded RGB frame')
    frame = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 3)
    frame_id = sum((int(frame[40 * height // 720, (32+80*bit) * width // 1280].mean()) > 160) << bit
                   for bit in range(12))
    bar = np.flatnonzero(frame[300 * height // 720].mean(axis=1) > 220)
    if len(bar) == 0:
        raise RuntimeError('Source moving bar is absent')
    bar_left = int(bar[0])
    flash = all(frame[680 * height // 720, x * width // 1280].mean() > 220 for x in (100, 640, 1100))
    candidates = [(index, marker) for index, marker in by_id.get(frame_id, [])
                  if abs(((index * 7) % 1200) * width // 1280 - bar_left) <= 2 and marker == flash]
    if len(candidates) != 1:
        raise RuntimeError(f'Unresolved source-frame alignment: id={frame_id}, bar={bar_left}, matches={len(candidates)}')
    index, marker = candidates[0]
    difference = frame.astype(np.float64) - expected(index, marker).astype(np.float64)
    mse = float(np.mean(difference * difference))
    samples.append({'decoded_index': len(samples) * 60, 'source_index': index,
                    'rgb_mse': mse, 'rgb_psnr_db': 10 * math.log10(255 * 255 / mse) if mse else None})
if process.wait() or not samples:
    raise RuntimeError('RGB decode failed')
durations = float(probe['format']['duration'])
packet_bytes = sum(int(p['size']) for p in probe['packets'])
trace_intervals = [(int(b['present_end_ns']) - int(a['present_end_ns'])) / 1e6
                   for a, b in zip(trace, trace[1:])]
steady_intervals = trace_intervals[60:]
result = {'file': str(args.file.resolve()), 'sample_count': len(samples),
    'mean_rgb_psnr_db': statistics.mean(s['rgb_psnr_db'] for s in samples),
    'samples': samples, 'source_colors_rgb': {k: v.tolist() for k,v in colors.items()},
    'duration_seconds': durations, 'file_bytes': int(probe['format']['size']),
    'average_container_bitrate_bps': int(probe['format']['size']) * 8 / durations,
    'compressed_packet_payload_bytes': packet_bytes,
    'source_present_interval_median_ms': statistics.median(steady_intervals),
    'source_present_interval_p95_ms': float(np.percentile(steady_intervals, 95)),
    'source_present_interval_p99_ms': float(np.percentile(steady_intervals, 99)),
    'limitations': 'One simple raster source, sampled every 60 encoded frames. RGB PSNR includes color conversion and codec loss; not perceptual game quality. Source intervals are capped synthetic Present cadence, not gameplay FPS impact. Packet payload is not allocated replay-buffer memory.'}
print(json.dumps(result, indent=2))
