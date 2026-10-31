"""Summarize read-only lighter captures; snapshots are not atomic render pairs."""
import argparse
import json
import math
import statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('capture')
parser.add_argument('--out', required=True)
args = parser.parse_args()
rows = json.loads(Path(args.capture).read_text())
offsets, distances, freezes = [], [], []
prior = run = None
for row in rows:
    models = [m for m in row['models'] if m['identityStable']]
    parent = next((m for m in models if m['asset'].endswith('.md6')), None)
    flame = next((m for m in models if m['asset'].endswith('.prt') and parent
                  and m['name'] == parent['name'] + '-flame'), None)
    if not parent or not flame:
        prior = run = None
        continue
    a, b, t = parent['native'], flame['native'], row['time']
    distance = math.dist(a[:3], b[:3])
    distances.append(distance)
    if distance < .06 and max(abs(a[k]-b[k]) for k in range(3, 12)) < 1e-6:
        offsets.append([sum(a[3+3*i+k]*(b[k]-a[k]) for k in range(3)) for i in range(3)])
    if prior and b == prior[2] and flame['model'] == prior[3]:
        if run is None:
            run = [prior[0], prior[1], t, a[:3]]
        else:
            run[2:] = [t, a[:3]]
    elif run:
        freezes.append(dict(seconds=run[2]-run[0], parentTravelMeters=math.dist(run[1], run[3])))
        run = None
    prior = (t, a[:3], b, flame['model'])
if run:
    freezes.append(dict(seconds=run[2]-run[0], parentTravelMeters=math.dist(run[1], run[3])))
result = dict(samples=len(rows), durationSeconds=rows[-1]['time']-rows[0]['time'],
              pairedSamples=len(distances), coherentSamples=len(offsets),
              maxNativeSeparationMeters=max(distances, default=0),
              samplesBeyondOld2mGate=sum(d > 2 for d in distances),
              longestFrozenRuns=sorted(freezes, key=lambda r:r['seconds'], reverse=True)[:8],
              limits='Unsynchronized external reads; no final-image or engine scheduling attribution.')
if offsets:
    for name, operation in [('Min', min), ('Max', max), ('Median', statistics.median)]:
        result['coherentLocalOffset'+name] = [operation(v[k] for v in offsets) for k in range(3)]
Path(args.out).write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
