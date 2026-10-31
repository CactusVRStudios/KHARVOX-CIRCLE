"""Audit Motor's native same-state pair records, XR transport and unmodified captures."""
import argparse
import csv
import ctypes
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

parser = argparse.ArgumentParser()
parser.add_argument('run', type=Path)
args = parser.parse_args()
run = args.run

def rows(path):
    text = path.read_text()
    lines = text.splitlines()
    if text and not text.endswith('\n'):
        lines = lines[:-1]  # writer may be mid-record, or process exit cut off a buffer
    return [r for r in csv.DictReader(lines)
            if None not in r and None not in r.values()]

def stats(values):
    return {'count': len(values), 'median': float(np.median(values)),
            'p95': float(np.percentile(values, 95)), 'max': float(max(values))} if values else None

pairs = rows(run / 'motor-sequential-pairs.csv')
if not pairs:
    raise SystemExit('No completed native stereo records')
by_token = {int(r['token']): r for r in pairs}
log = '\n'.join(p.read_text(errors='replace') for p in run.glob('*.log'))
freq = ctypes.c_longlong()
ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
times = [int(r['rigQpc']) / freq.value for r in pairs]
gaps = np.diff(times)
# Long gaps include deliberate pause, minimisation and lost ownership. Report separately.
segments = np.split(np.arange(len(pairs)), np.flatnonzero(gaps > .1) + 1)
longest = max(segments, key=len)
seconds = times[int(longest[-1])] - times[int(longest[0])]
xr = re.findall(r'motor-seq-xr: submission (\d+) token (\d+) predicted (\d+) result (-?\d+) pose_age_ms ([\d.]+)', log)
gpu = re.findall(r'motor-seq-gpu: pair (\d+) token (\d+) renders (\d+)/(\d+) copy timeline (\d+)', log)
ips = [np.linalg.norm([float(r['left'+c])-float(r['right'+c]) for c in 'XYZ']) for r in pairs]
report = {
    'method': 'CircleVR FrameEnd -> Motor RenderOne; original frame token and selected camera-copy sequence checked before and after native second render. No second camera/simulation-output update allowed.',
    'limitation': 'Broad F1AEF0 entry counter is NOT a per-tick simulation counter. Native TAA histories have a separate AA1-only adapter; this report does not validate it. Vendor, RT/GI and object histories remain unported.',
    'completed_native_pairs': len(pairs),
    'distinct_frame_tokens': len(by_token),
    'consecutive_native_render_ids': sum(int(r['rightRender']) == int(r['leftRender'])+1 for r in pairs),
    'eye_separation_game_units': stats(ips),
    'longest_continuous_segment': {'pairs': len(longest), 'seconds': seconds,
                                  'pairs_per_second': (len(longest)-1)/seconds if seconds else 0},
    'rig_sample_gap_ms': stats([float(x)*1000 for x in gaps if 0 <= x < .1]),
    'logged_gpu_pair_publications': len(gpu),
    'logged_gpu_metadata_mismatches': sum(int(t) not in by_token or
        int(by_token[int(t)]['leftRender']) != int(l) or int(by_token[int(t)]['rightRender']) != int(r)
        for _, t, l, r, _ in gpu),
    'sampled_xr_results': {k: sum(row[3] == k for row in xr) for k in set(row[3] for row in xr)},
    'sampled_projection_pose_age_ms': stats([float(row[4]) for row in xr]),
    'pose_age_limit': 'Pose age at submission; not motion-to-photon latency.',
    'failed_same_state_completions': len(re.findall(r'COMPLETE unchanged 0', log)),
    'private_camera_copy_mismatches': log.count('private camera copy mismatch'),
    'reentry_preflight_rejections': log.count('preflight REJECT'),
    'rt_build_and_trace_observed': 'motor workload: first vkCmdBuildAccelerationStructuresKHR' in log and 'motor workload: first vkCmdTraceRaysKHR' in log,
    'captures': []}
xr_rows = []
for path in run.glob('circle-vr-frames-*.csv'):
    xr_rows += [r for r in rows(path) if int(r['view']) in by_token and
                int(r['eye0_frame']) > 0 and int(r['eye1_frame']) > 0 and 0 <= float(r['pose_age_ms']) < 250]
report['fresh_pair_image_records_in_xr_worker'] = len(xr_rows)
report['distinct_native_pairs_seen_by_xr_worker'] = len({r['view'] for r in xr_rows})
report['xr_image_record_limit'] = 'Image ownership records, not proof every row used a projection layer; actual EndFrame samples are logged separately.'
previous = None
for left in sorted((run/'captures').glob('*-L.png')):
    right = left.with_name(left.name.replace('-L.png', '-R.png'))
    if not right.exists():
        continue
    token = int(re.search(r'-t(\d+)', left.name)[1])
    l, r = [np.asarray(Image.open(p).convert('RGB'), dtype=np.float32) for p in (left, right)]
    region = (slice(350, 850), slice(100, 1250))
    record = {'left': left.name, 'right': right.name, 'frame_token': token,
              'native_pair_metadata': by_token.get(token),
              'native_size': [l.shape[1], l.shape[0]],
              'scene_mean_absolute_eye_difference': float(np.abs(l[region]-r[region]).mean())}
    gray = [np.array(Image.open(p).convert('L').filter(ImageFilter.GaussianBlur(2)), dtype=np.float32) for p in (left, right)]
    matches = []
    radius, limit = 20, 64
    for x, y in [(400,300),(750,300),(1100,300),(400,550),(750,550),(1100,550),(400,800),(750,800)]:
        patch = gray[0][y-radius:y+radius,x-radius:x+radius]
        patch = patch-patch.mean()
        if patch.std() < 5:
            continue
        search = gray[1][y-radius-3:y+radius+3,x-radius-limit:x+radius+limit]
        windows = np.lib.stride_tricks.sliding_window_view(search, patch.shape)
        w = windows-windows.mean(axis=(-2,-1),keepdims=True)
        scores = (w*patch).sum(axis=(-2,-1))/np.sqrt((w*w).sum(axis=(-2,-1))*(patch*patch).sum()+1e-12)
        iy, ix = np.unravel_index(scores.argmax(),scores.shape)
        if scores[iy,ix] > .96:
            matches.append({'xy':[x,y], 'L_minus_R_px':int(limit-ix), 'vertical_px':int(3-iy), 'ncc':float(scores[iy,ix])})
    record['scene_correspondences'] = matches
    if previous is not None:
        record['scene_change_since_previous_capture'] = float(np.abs(l[region]-previous[region]).mean())
    previous = l
    report['captures'].append(record)
(run/'sequential-analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
