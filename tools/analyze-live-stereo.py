"""Summarize measured native alternating-eye frames. No synthetic images or same-state claims."""
import argparse, csv, ctypes, json, re
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

parser=argparse.ArgumentParser()
parser.add_argument('run',type=Path)
a=parser.parse_args()
rows=list(csv.DictReader((a.run/'live-frames.csv').read_text().splitlines()))
rows=[r for r in rows if None not in r.values()]
valid=[r for r in rows if int(r['camera'])>0]
if not valid: raise SystemExit('No verified native eye frames')
freq=ctypes.c_longlong();ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
qpc=all(int(r.get('copy_qpc') or 0)>0 for r in valid)
def when(r): return int(r['copy_qpc'])/freq.value if qpc else int(r['ticks'])/1000
duration=when(valid[-1])-when(valid[0])
best=run=0;prev=None;offsets=[]
for r in rows:
    c=int(r['camera'])
    continuous=prev and c and c==int(prev['camera'])+1 and int(r['present'])==int(prev['present'])+1
    run=run+1 if continuous else int(c>0);best=max(best,run)
    if continuous and r['pair']=='1':offsets.append((when(r)-when(prev))*1000)
    prev=r
log='\n'.join(p.read_text(errors='replace') for p in a.run.glob('*.log'))
xr=re.findall(r'live-xr: frame (\d+) view (\d+) predicted (\d+) result (-?\d+) pose_age_ms ([\d.]+)',log)
def stats(values):
    return dict(mean=float(np.mean(values)),p50=float(np.median(values)),p95=float(np.percentile(values,95)),max=float(max(values))) if values else None
report={'method':'Native alternate-eye game frames; rolling pairs have a temporal offset, NOT same-state stereo.',
    'recorded_presents':len(rows),'verified_eyes':len(valid),'rolling_pairs':sum(r['pair']=='1' for r in rows),
    'longest_consecutive_game_and_camera_frames':best,'seconds':duration,
    'verified_eyes_per_second':len(valid)/duration,'rolling_pair_publications_per_second':sum(r['pair']=='1' for r in rows)/duration,
    'left_eye_per_second':sum(r['eye']=='0' for r in valid)/duration,'right_eye_per_second':sum(r['eye']=='1' for r in valid)/duration,
    'eye_offset_ms':stats(offsets),'timestamp_source':'QueryPerformanceCounter' if qpc else 'GetTickCount64 (coarse)',
    'xr_logged_submission_results':dict((k,sum(x[3]==k for x in xr)) for k in set(x[3] for x in xr)),
    'xr_pose_age_ms':stats([float(x[4]) for x in xr]),
    'latency_limit':'Pose age at XR submission, not measured motion-to-photon latency.',
    'native_pause_values':sorted(set(r.get('paused','not recorded') for r in valid)),
    'rt_build_and_trace_observed':'motor workload: first vkCmdBuildAccelerationStructuresKHR' in log and 'motor workload: first vkCmdTraceRaysKHR' in log,
    'captures':[]}
xr_rows=[]
for path in a.run.glob('circle-vr-frames-*.csv'):
    for row in csv.DictReader(path.read_text().splitlines()):
        if None not in row.values() and int(row['eye0_frame'])>0 and int(row['eye1_frame'])>0:
            xr_rows.append(row)
if xr_rows:
    report['xr_all_stereo_frame_pose_age_ms']=stats([float(r['pose_age_ms']) for r in xr_rows])
    report['xr_recorded_stereo_frames']=len(xr_rows)
    report['xr_distinct_native_pairs']=len(set((r['eye0_frame'],r['eye1_frame']) for r in xr_rows))
    report['xr_repeat_fraction']=1-report['xr_distinct_native_pairs']/len(xr_rows)
previous=None
by_camera={int(r['camera']):r for r in valid}
for left in sorted((a.run/'captures').glob('*-L.png')):
    right=left.with_name(left.name.replace('-L.png','-R.png'))
    if not right.exists():continue
    l=np.array(Image.open(left).convert('RGB'),dtype=np.float32)
    r=np.array(Image.open(right).convert('RGB'),dtype=np.float32)
    # Exclude the native performance overlay: image changes must occur in scene geometry.
    region=(slice(400,1000),slice(100,1250))
    record={'left':left.name,'right':right.name,'native_size':[l.shape[1],l.shape[0]],
            'eye_scene_mean_absolute_difference':float(np.abs(l[region]-r[region]).mean())}
    seq=int(re.search(r'-t(\d+)',left.name)[1]);poses=[by_camera.get(seq),by_camera.get(seq+1)]
    if all(poses):
        axes=[np.array([float(p[k]) for k in ('fx','fy','fz','lx','ly','lz','ux','uy','uz')]) for p in poses]
        record['camera_sequences']=[seq,seq+1]
        record['max_axis_change_between_eyes']=float(np.max(np.abs(axes[0]-axes[1])))
        # Rotation between temporally offset eyes can reverse raw disparity.
        # Only check sign with essentially identical rendered camera axes.
        if record['max_axis_change_between_eyes']<0.0001:
            gray=[np.array(Image.open(p).convert('L').filter(ImageFilter.GaussianBlur(2)),dtype=np.float32) for p in (left,right)]
            matches=[];radius=20;limit=64
            for x,y in [(400,300),(750,300),(1100,300),(400,550),(750,550),(1100,550),(400,800),(750,800)]:
                patch=gray[0][y-radius:y+radius,x-radius:x+radius];patch=patch-patch.mean()
                if patch.std()<5:continue
                search=gray[1][y-radius-3:y+radius+3,x-radius-limit:x+radius+limit]
                windows=np.lib.stride_tricks.sliding_window_view(search,patch.shape)
                w=windows-windows.mean(axis=(-2,-1),keepdims=True)
                scores=(w*patch).sum(axis=(-2,-1))/np.sqrt((w*w).sum(axis=(-2,-1))*(patch*patch).sum()+1e-12)
                iy,ix=np.unravel_index(scores.argmax(),scores.shape)
                if scores[iy,ix]>.96:matches.append({'xy':[x,y],'L_minus_R_px':int(limit-ix),'vertical_px':int(3-iy),'ncc':float(scores[iy,ix])})
            record['parallel_camera_scene_correspondences']=matches
    if previous is not None:record['left_scene_change_since_previous_capture']=float(np.abs(l[region]-previous[region]).mean())
    previous=l;report['captures'].append(record)
(a.run/'live-analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
