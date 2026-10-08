"""Compare within-source before/capture/after submission cadence, not game FPS."""
import argparse
import csv
import json
import pathlib
import re
import statistics
import math
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('root',type=pathlib.Path)
args=parser.parse_args()
trials=[]
for path in sorted(args.root.glob('*/phases.json')):
    data=json.loads(path.read_text(encoding='utf-8-sig'))
    if not data['valid']: raise RuntimeError('Invalid trial')
    root=path.parent
    log=(root/'source.log').read_text(encoding='utf-8-sig')
    clock=re.search(r'trace_start_qpc=(\d+) qpc_frequency=(\d+)',log)
    if not clock: raise RuntimeError('Missing clock anchor')
    anchor,frequency=map(int,clock.groups())
    with (root/'source-frames.csv').open(newline='') as file:
        rows=list(csv.DictReader(file))
    begins=np.array([int(row['begin_ns']) for row in rows],dtype=np.int64)
    ends=np.array([int(row['present_end_ns']) for row in rows],dtype=np.int64)
    phases={}
    for phase in data['phases']:
        start=(phase['start_qpc']-anchor)*1e9/frequency
        end=(phase['end_qpc']-anchor)*1e9/frequency
        selected=ends[(begins>=start)&(ends<=end)]
        if len(selected)<100: raise RuntimeError('Insufficient steady-state frames')
        intervals=np.diff(selected)/1e6
        phases[phase['name']]={'frames':len(selected),'observed_span_seconds':float((selected[-1]-selected[0])/1e9),
            'submit_fps':float((len(selected)-1)*1e9/(selected[-1]-selected[0])),
            'median_interval_ms':float(np.median(intervals)),
            'p95_interval_ms':float(np.percentile(intervals,95)),
            'p99_interval_ms':float(np.percentile(intervals,99))}
    baseline=statistics.mean(phases[name]['submit_fps'] for name in ('source-before','source-after'))
    captured=phases['source-with-capture']['submit_fps']
    metrics=json.loads((root/'replay-resources.json').read_text(encoding='utf-8-sig'))
    if metrics.get('valid') is not True: raise RuntimeError('Invalid resource measurement')
    app=[r for s in metrics['samples'] for r in s['processes'] if r['pid']==data['capture_pid']]
    def average(key):
        values=[r[key] for r in app if r.get(key) is not None]
        return statistics.mean(values) if values else None
    etw={}
    if (root/'presentmon.csv').exists():
        with (root/'presentmon.csv').open(encoding='utf-8-sig',newline='') as file:
            events=[row for row in csv.DictReader(file) if int(row['ProcessID'])==data['source_pid']]
        chains={row['SwapChainAddress'] for row in events}
        if not chains: raise RuntimeError('ETW file contains no target-process frames')
        chain=max(chains,key=lambda c:sum(row['SwapChainAddress']==c for row in events))
        events=[row for row in events if row['SwapChainAddress']==chain]
        def numeric(row,key):
            try:
                value=float(row[key])
                return value if math.isfinite(value) and value>=0 else None
            except (KeyError,ValueError): return None
        for phase in data['phases']:
            selected=[row for row in events if phase['start_qpc']<=int(row['CPUStartQPC'])<phase['end_qpc']]
            if len(selected)<100: raise RuntimeError('Insufficient ETW frames in a measured phase')
            times=sorted(int(row['TimeInQPC']) for row in selected)
            result={'frames':len(selected),'swap_chain':chain,
                    'present_fps':(len(times)-1)*frequency/(times[-1]-times[0])}
            for key in ('MsBetweenPresents','MsInPresentAPI','MsGPUBusy','MsGPULatency','MsRenderPresentLatency','MsUntilDisplayed'):
                values=[v for row in selected if (v:=numeric(row,key)) is not None]
                result[key]={'count':len(values),'mean':statistics.mean(values),'p95':float(np.percentile(values,95)),
                             'p99':float(np.percentile(values,99))} if values else None
            result['present_modes']=sorted({row['PresentMode'] for row in selected})
            etw[phase['name']]=result
    telemetry=[]
    with (root/'gpu.csv').open(newline='') as file:
        for row in csv.reader(file):
            if len(row)!=10: continue
            try: telemetry.append({'clock_mhz':float(row[4]),'temperature_c':float(row[7]),'util_percent':float(row[8]),'pstate':row[3].strip()})
            except ValueError: continue
    gpu={'clock_range_mhz':[min(r['clock_mhz'] for r in telemetry),max(r['clock_mhz'] for r in telemetry)],
         'temperature_range_c':[min(r['temperature_c'] for r in telemetry),max(r['temperature_c'] for r in telemetry)],
         'max_util_percent':max(r['util_percent'] for r in telemetry),
         'pstates':sorted({r['pstate'] for r in telemetry})} if telemetry else None
    trials.append({'trial':str(root),'condition':data['condition'],'phases':phases,'etw_phases':etw,'whole_trial_gpu_telemetry':gpu,
        'bracketed_baseline_submit_fps':baseline,'captured_submit_fps':captured,
        'submit_throughput_loss_percent':100*(baseline-captured)/baseline,
        'parent_cpu_machine_percent':average('cpu_machine_percent'),
        'parent_gpu_video_encode_max_percent':average('gpu_video_encode_max_percent')})
if not trials: raise RuntimeError('No completed trials')
print(json.dumps({'trials':trials,'limitations':[
    'Uncapped synthetic shader workload, not Fortnite. Submission throughput/intervals are not displayed frames, GPU completion time or latency. ETW fields are reported separately when collected.',
    'Same-source bracketed baselines mitigate drift but do not eliminate thermal/clock or competing-process effects.',
    'Consult raw gpu.csv for power state, clock, temperature and saturation; results from different quality presets are not quality-equivalent.',
    'Short resource samples exclude helper-process peaks; test-only normal-loop controller adds four status checks per second.']},indent=2))
