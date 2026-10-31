"""Validate named Gina body/wear CPU uploads alongside completed temporal GPU images.

These matrices are native CPU upload observations, not GPU vertex readbacks.
Visibility and pixel correspondence are assessed separately by the image analyzer.
"""
import argparse, importlib.util, json, math, struct
from pathlib import Path

s=importlib.util.spec_from_file_location('temporal',Path(__file__).with_name('analyze-temporal-images.py'))
t=importlib.util.module_from_spec(s);s.loader.exec_module(t)

def analyze(run):
    rows=[json.loads(line) for line in (run/'gpu-bindings.jsonl').read_text().splitlines()]
    sessions=sorted({r['session'] for r in rows if r['event']=='temporal_start'})
    samples=[]
    required={'HEADWEAR','UPPER_BODY','LOWER_BODY','BACKPACK','FOOTWEAR'}
    for session in sessions:
        cameras,images=t.validate(rows,session)
        for descriptor,done in images:
            if (run/done['file']).stat().st_size!=descriptor['bytes']:
                raise ValueError('Incomplete temporal payload')
        token=cameras[0]['token']
        cpus=[r for r in rows if r['event']=='npc' and r['session']==session and r['token']==token]
        roots=[r for r in cpus if r['eye']==0 and r['asset']=='md6def/characters/standard/gina/gina_vatican.md6']
        if len(roots)!=1:raise ValueError('Ambiguous Gina root')
        root=roots[0];owner=root['instance'];parts=[];slots=set()
        for left in cpus:
            if left['eye']!=0:continue
            if left['model']!=root['model'] and not (left['asset'].startswith('models/wear/gina/') and left['instance'].startswith(owner+':wear[')):
                continue
            right=[r for r in cpus if r['eye']==1 and r['model']==left['model']]
            if len(right)!=1:raise ValueError('Missing or ambiguous paired part')
            right=right[0]
            if any(left[k]!=right[k] for k in ('model','committed','index','asset','instance')):
                raise ValueError('Part identity changed between eyes')
            a=struct.unpack('<16f',bytes.fromhex(left['cpuMatrix']))
            b=struct.unpack('<16f',bytes.fromhex(right['cpuMatrix']))
            if not all(math.isfinite(x) for x in (*a,*b)):raise ValueError('Nonfinite matrix')
            difference=max(abs(x-y) for x,y in zip(a,b))
            parts.append(dict(asset=left['asset'],instance=left['instance'],model=left['model'],maximumMatrixDifference=difference))
            if ':wear[' in left['instance']:slots.add(left['instance'].split(':wear[')[1].removesuffix(']'))
        if slots!=required or len(parts)!=6:raise ValueError('Incomplete Gina wear coverage')
        current=struct.unpack('<16f',bytes.fromhex(root['cpuMatrix']))
        previous=struct.unpack('<16f',bytes.fromhex(root['cpuPreviousMatrix']))
        samples.append(dict(session=session,token=token,rootStep=math.dist(current[3:12:4],previous[3:12:4]),parts=parts))
    if not samples:raise ValueError('No temporal samples')
    return dict(samples=samples,pairs=len(samples),partPairs=sum(len(s['parts']) for s in samples),
                movingPairs=sum(s['rootStep']>1e-5 for s in samples),
                maximumMatrixDifference=max(p['maximumMatrixDifference'] for s in samples for p in s['parts']),
                scope='Named CPU upload matrices plus complete paired pre/post temporal GPU images; not a vertex-output or visibility guarantee.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    report=analyze(a.run);a.out.write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
