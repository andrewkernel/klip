"""Summarize preserved controlled OBS/Klip measurements without inventing gates."""
import argparse
import json
import pathlib
import statistics

parser = argparse.ArgumentParser()
parser.add_argument('--obs-root', type=pathlib.Path, required=True)
parser.add_argument('--klip-root', type=pathlib.Path, required=True)
args = parser.parse_args()

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def summarize(root, app):
    processes = read(root / 'processes.json')
    capture_pid = processes['obs_pid' if app == 'OBS' else 'klip_pid']
    phases = {}
    for phase in ('replay', 'replay-recording'):
        data = read(root / f'{phase}-resources.json')
        if data.get('valid') is False:
            raise RuntimeError(f'Invalid measurement: {root}, {phase}: {data.get("invalid_reason")}')
        rows = [r for s in data['samples'] for r in s['processes'] if r['pid'] == capture_pid]
        if len(rows) < 5:
            raise RuntimeError('Insufficient resource samples')
        def stats(key, divisor=1):
            values = [r[key] / divisor for r in rows if r.get(key) is not None]
            return {'mean': statistics.mean(values), 'median': statistics.median(values),
                    'min': min(values), 'max': max(values), 'count': len(values)} if values else None
        phases[phase] = {'seconds': data['duration_seconds'],
            'sampler_validity_marker': data.get('valid'),
            'cpu_machine_percent': stats('cpu_machine_percent'),
            'private_mib': stats('private_bytes', 1024**2),
            'working_set_mib': stats('working_set_bytes', 1024**2),
            'gpu_3d_or_compute_max_percent': stats('gpu_3d_or_compute_max_percent'),
            'gpu_video_encode_max_percent': stats('gpu_video_encode_max_percent')}
    cadence = {}
    for kind in ('replay', 'recording'):
        data = read(root / f'{kind}-analysis.json')
        keys = ('decoded_frames', 'presentation_span_seconds', 'unique_transitions_per_second',
                'consecutive_repeats', 'repeat_percent', 'source_id_step_histogram',
                'nonmonotonic_presentation_timestamps', 'max_presentation_interval_ms',
                'matched_av_markers', 'median_audio_minus_video_ms', 'av_offset_drift_ms_per_minute')
        cadence[kind] = {k: data.get(k) for k in keys}
    quality = read(root / 'quality-analysis.json')
    quality.pop('samples')
    return {'protocol': read(root / 'protocol.json'), 'phases': phases,
            'cadence': cadence, 'replay_quality': quality}

obs, klip = summarize(args.obs_root, 'OBS'), summarize(args.klip_root, 'Klip')
if obs['protocol']['settings'] != klip['protocol']['settings']:
    raise RuntimeError('Controlled settings differ; do not call this a parity comparison')
print(json.dumps({'OBS': obs, 'Klip': klip, 'limitations': [
    'One sequential run per application on one host; short 20-second samples, not statistical proof.',
    'OBS minimized with preview disabled; Klip dashboard hidden. Both sources/encoders continuously active.',
    'Process CPU normalized across 12 logical processors; memory excludes helper processes.',
    'GPU counter values are busiest matching engines, not total system GPU utilization.',
    'OBS replay-only used an earlier sampler without a validity marker. Source lifetime and continuous media independently confirmed in logs/trace.',
    'Different warm-up and recording durations may affect allocator/high-water memory; repeat in randomized order.',
    'No Fortnite/uncapped-game frametime benchmark, allocated replay-memory isolation, or save-phase resource measurement yet.',
    'Saved replay payload is a compressed-packet estimate, not the allocated memory of the rolling buffer.',
    'OBS save used its UI button after short simulated global chords were not observed. Klip save used its native global shortcut.',
    'Test Klip process was ended only after recording finalized and source loss stopped replay; this is not a graceful-shutdown acceptance test.'
    ]}, indent=2))
