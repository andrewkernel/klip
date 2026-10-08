"""Public PresentMon CSV metrics; presentation rate is not displayed-frame rate."""
import csv
import math
import statistics
from collections import Counter


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize_presentmon(path, protocol):
    if not protocol.get('valid'):
        raise ValueError('Incomplete or failed measurement')
    frequency = protocol['qpc_frequency']
    if frequency <= 0:
        raise ValueError('Invalid QPC frequency')
    with open(path, encoding='utf-8-sig', newline='') as stream:
        events = [row for row in csv.DictReader(stream)
                  if int(row['ProcessID']) == protocol['source_pid']]
    if not events:
        raise ValueError('No frames for the requested process')
    phases = protocol['phases']
    if len(phases) != 3 or len({phase['name'] for phase in phases}) != 3:
        raise ValueError('Expected three unique measured phases')
    for before, after in zip(phases, phases[1:]):
        if before['end_qpc'] > after['start_qpc']:
            raise ValueError('Measurement phases overlap or are out of order')
    chains = Counter(row['SwapChainAddress'] for row in events)
    chain = chains.most_common(1)[0][0]
    events = [row for row in events if row['SwapChainAddress'] == chain]
    result = {}
    for phase in phases:
        if phase['end_qpc'] <= phase['start_qpc']:
            raise ValueError('Invalid phase range')
        rows = [row for row in events
                if phase['start_qpc'] <= int(row['CPUStartQPC']) < phase['end_qpc']]
        if len(rows) < 100:
            raise ValueError('Fewer than 100 frames in a measured phase')
        times = sorted(int(row['TimeInQPC']) for row in rows)
        starts = sorted(int(row['CPUStartQPC']) for row in rows)
        if starts[0] > phase['start_qpc'] + frequency * .5 or starts[-1] < phase['end_qpc'] - frequency * .5:
            raise ValueError('ETW events do not cover the complete measured phase')
        if times[-1] == times[0]:
            raise ValueError('ETW timestamps have no span')
        intervals = [(b - a) * 1000 / frequency for a, b in zip(times, times[1:])]
        result[phase['name']] = {
            'frames': len(rows), 'present_rate': (len(times) - 1) * frequency / (times[-1] - times[0]),
            'p95_present_interval_ms': percentile(intervals, .95),
            'p99_present_interval_ms': percentile(intervals, .99),
            'zero_present_intervals': sum(value == 0 for value in intervals),
            'present_modes': sorted({row['PresentMode'] for row in rows}),
        }
    if len(protocol['phases']) != 3 or set(result) != {'source-before', 'source-with-capture', 'source-after'}:
        raise ValueError('Missing or unexpected measurement phases')
    baseline = statistics.mean(result[name]['present_rate'] for name in ('source-before', 'source-after'))
    capture = result['source-with-capture']['present_rate']
    return {'swap_chain': chain, 'phases': result,
            'bracketed_baseline_present_rate': baseline, 'capture_present_rate': capture,
            'present_rate_loss_percent': 100 * (baseline - capture) / baseline,
            'baseline_drift_percent': 100 * (result['source-after']['present_rate'] / result['source-before']['present_rate'] - 1),
            'limitations': ['Present rate is not displayed FPS or decoded clip cadence.',
                            'A capped or changing scene may hide overhead; repeat matched scenes and alternate order.',
                            'Dominant swap chain only; inspect raw events for swap-chain recreation or mode changes.']}
