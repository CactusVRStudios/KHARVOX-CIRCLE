"""Inspect r186 CPU view snapshots. No GPU-consumption or visual-success claim."""
import argparse
import collections
import json
import math
from pathlib import Path
import struct

BASE = 0x18790
SIZE = 2752
# Verified CC8820 current -> previous destinations.
FIELDS = [(0x18790, 0x18810, 16), (0x18850, 0x18890, 16),
          (0x18910, 0x18950, 16), (0x19068, 0x18e98, 3)]


def values(row, at, count):
    return struct.unpack_from('<' + 'f' * count, row['data'], at - BASE)


def delta(a, b, previous=False):
    errors = []
    for current, old, count in FIELDS:
        x, y = values(a, old if previous else current, count), values(b, current, count)
        if not all(math.isfinite(v) for v in (*x, *y)):
            return None
        errors.extend(abs(v - w) for v, w in zip(x, y))
    return max(errors)


def previous_delta(a,b):
    errors = [abs(x-y) for _,old,count in FIELDS
              for x,y in zip(values(a,old,count),values(b,old,count))]
    return max(errors) if all(math.isfinite(e) for e in errors) else None


def read(path):
    raw = path.read_bytes()
    if len(raw) < 16 or raw[:8] != b'CVIEW001':
        raise ValueError('Unknown view-audit format')
    size, count = struct.unpack_from('<II', raw, 8)
    if size != SIZE or len(raw) != 16 + size * count:
        raise ValueError('Truncated or incompatible view-audit records')
    rows = []
    for i in range(count):
        at = 16 + i * size
        keys = struct.unpack_from('<8Q', raw, at)
        stage, eye, frame, sequence, token, view, model, qpc = keys
        if stage > 2 or eye > 1 or not token:
            raise ValueError('Invalid record identity')
        rows.append(dict(stage=stage, eye=eye, frame=frame, sequence=sequence,
                         token=token, view=view, model=model, qpc=qpc,
                         data=raw[at+64:at+64+0x980],
                         model_data=raw[at+64+0x980:at+64+0x980+128],
                         asset=raw[at+size-128:at+size].split(b'\0')[0].decode('utf-8', 'replace')))
    return rows


def analyze(rows):
    latch = collections.defaultdict(list)
    for r in rows:
        if r['stage'] == 0:
            latch[(r['token'], r['eye'], r['view'])].append(r)
    evidence = []
    previous = {}
    for r in sorted(rows, key=lambda r: r['qpc']):
        if r['stage'] == 0:
            key = r['view'], r['eye']
            prev = previous.get(key)
            # A gap, duplicate, mono pass or reset is unproven continuity.
            if prev and ((r['frame']-prev['frame']) & 0xffffffff) == 2 and r['token'] != prev['token']:
                evidence.append(dict(kind='previous_same_eye', eye=r['eye'], frame=r['frame'],
                                     error=delta(r, prev, previous=True)))
            previous[key] = r
        else:
            candidates = latch.get((r['token'], r['eye'], r['view']), [])
            # Do not silently choose one latch if the same simulation was rendered twice.
            if len(candidates) == 1:
                match = candidates[0]
                evidence.append(dict(kind='material_bind' if r['stage'] == 1 else 'hair_setup',
                                     eye=r['eye'], frame=r['frame'], latch_frame=match['frame'],
                                     asset=r['asset'], error=delta(r, match),
                                     previous_error=previous_delta(r,match)))
    groups = {}
    for kind in ('previous_same_eye', 'material_bind', 'hair_setup'):
        items = [e for e in evidence if e['kind'] == kind]
        errs = [e['error'] for e in items if e['error'] is not None]
        groups[kind] = dict(matched=len(items), nonfinite=sum(e['error'] is None for e in items),
                            max_error=max(errs, default=None),
                            changed=sum(e > 0.0001 for e in errs))
        old = [e['previous_error'] for e in items if e.get('previous_error') is not None]
        if old:
            groups[kind]['previous_max_error'] = max(old)
            groups[kind]['previous_changed'] = sum(e > .0001 for e in old)
        groups[kind]['previous_nonfinite'] = sum(
            'previous_error' in e and e['previous_error'] is None for e in items)
    return dict(records=len(rows), stages=dict(collections.Counter(r['stage'] for r in rows)),
                groups=groups, evidence=evidence,
                limit='CPU snapshots only. Hair ownership uses frontend chain/frame; material uses existing '
                      'in-flight tag. These are observations to compare, not independent proof of job ownership, '
                      'GPU uniform lifetime, final vertices or corrected visuals.')


def self_test():
    def row(stage, eye, frame, token, current, old, qpc):
        data = bytearray(0x980)
        for c, p, n in FIELDS:
            struct.pack_into('<'+'f'*n, data, c-BASE, *([current]*n))
            struct.pack_into('<'+'f'*n, data, p-BASE, *([old]*n))
        return dict(stage=stage, eye=eye, frame=frame, sequence=frame//2, token=token,
                    view=123, model=0, qpc=qpc, asset='', data=data)
    left = row(0,0,100,1,10,0,1)
    right = row(0,1,101,1,20,0,2)
    next_left = row(0,0,102,2,30,10,3)
    wrong_right = row(0,1,103,2,40,30,4)  # current L instead of previous R
    corrupt_bind = row(1,0,102,2,40,10,5)  # overwritten by R before bind
    report = analyze([left,right,next_left,wrong_right,corrupt_bind])
    assert report['groups']['previous_same_eye']['matched'] == 2
    assert report['groups']['previous_same_eye']['changed'] == 1
    assert report['groups']['material_bind']['changed'] == 1
    gap = row(0,0,106,3,50,999,6)
    assert analyze([next_left,gap])['groups']['previous_same_eye']['matched'] == 0
    assert analyze([next_left,next_left,corrupt_bind])['groups']['material_bind']['matched'] == 0
    invalid = dict(corrupt_bind, data=bytearray(corrupt_bind['data']))
    struct.pack_into('<f', invalid['data'],0,float('nan'))
    assert analyze([next_left,invalid])['groups']['material_bind']['nonfinite'] == 1
    print('View audit: wrong-eye history, overwritten consumer, gaps, ambiguous latches and nonfinite data detected.')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('capture', type=Path, nargs='?')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.capture:
            ap.error('capture is required')
        report = analyze(read(args.capture))
        target = args.out or args.capture.with_suffix('.json')
        target.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({k:v for k,v in report.items() if k!='evidence'},indent=2))
