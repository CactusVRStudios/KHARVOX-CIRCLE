"""Validate paired hair model-table payloads against independently named Gina CPU records."""
import argparse, importlib.util, json, math, struct
from pathlib import Path
s=importlib.util.spec_from_file_location('capture_data',Path(__file__).with_name('gpu-capture-data.py'))
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

def analyze(run,rows):
    cap=m.Capture(run,rows)
    draws=[r for r in rows if r['event']=='draw_indexed']
    if not draws or {d['eye'] for d in draws}!={0,1}:raise ValueError('Missing paired hair draws')
    token=draws[0]['token'];left=next(d['backend'] for d in draws if d['eye']==0)
    catalog={p['pipeline']:p for p in m.capture.read(run/'shaders/pipelines.jsonl')}
    tables={};counts=[0,0]
    for d in draws:
        if d['token']!=token or d['backend']!=left+d['eye'] or d.get('tagMismatch'):
            raise ValueError('Mixed or mismatched hair pair')
        p=catalog[d['pipeline']]
        if not any(s['module']=='a39cad8addc3c423' for s in p['stages']):raise ValueError('Unverified hair layout')
        if d['count']!=6 or d['instances']!=1:raise ValueError('Unexpected direct hair draw')
        b=m.bindings(d).get((0,4))
        if not b or not b['resolved'] or b['size']!=128*416:raise ValueError('Unknown hair model table')
        data=cap.read(d,b['buffer'],b['offset'],b['size'],b['generation'])
        if d['eye'] in tables and tables[d['eye']]!=data:raise ValueError('Multiple differing model tables within eye')
        tables[d['eye']]=data;counts[d['eye']]+=1
    cpu={}
    for e in (0,1):
        matches=[r for r in rows if r['event']=='npc' and r['token']==token and r['eye']==e and '/gina/' in r['asset']]
        if len(matches)!=1:raise ValueError('Ambiguous independently named Gina record')
        cpu[e]=matches[0]
    if any(cpu[0][k]!=cpu[1][k] for k in ('model','committed','index','asset','instance')):
        raise ValueError('Changed Gina CPU identity')
    matrices={e:struct.unpack('<16f',bytes.fromhex(cpu[e]['cpuMatrix'])) for e in (0,1)}
    previous=struct.unpack('<16f',bytes.fromhex(cpu[0]['cpuPreviousMatrix']))
    position=lambda matrix:[matrix[i] for i in (3,7,11)]
    rootStep=math.dist(position(matrices[0]),position(previous))
    models=[]
    for index in range(128):
        chunks={e:tables[e][index*416:(index+1)*416] for e in (0,1)}
        if not any(chunks[0]) and not any(chunks[1]):continue
        world={e:struct.unpack_from('<16f',chunks[e],176) for e in (0,1)}
        if not all(math.isfinite(x) for w in world.values() for x in w):raise ValueError('Nonfinite hair world matrix')
        models.append(dict(index=index,leftPosition=position(world[0]),rightPosition=position(world[1]),
            positionDistance=math.dist(position(world[0]),position(world[1])),
            worldMaximumEyeDifference=max(abs(x-y) for x,y in zip(world[0],world[1])),
            leftCpuWorldMaximumError=max(abs(x-y) for x,y in zip(world[0],matrices[0])),
            rightCpuWorldMaximumError=max(abs(x-y) for x,y in zip(world[1],matrices[1])),
            rightBodyPositionDistance=math.dist(position(world[1]),position(matrices[1]))))
    return dict(token=token,backends=[left,left+1],gpuPayloadVerified=True,directDrawCounts=counts,
        ginaAsset=cpu[0]['asset'],ginaCpuIndex=cpu[0]['index'],rootStep=rootStep,
        motion='moving-root' if rootStep>1e-5 else 'stationary-root',
        cpuBodyWorldMaximumEyeDifference=max(abs(x-y) for x,y in zip(matrices[0],matrices[1])),
        modelRows=models,limitations=[
            'Bound model-table bytes captured after render pass, not shader outputs or pixel coverage.',
            'Row association uses full left-world matrix correspondence with independently named Gina. Exact per-draw selected hair index is not captured.',
            'Extra right-eye draws are not proven: this pair has the direct counts reported above.',
            'Ordinary eye projection differences are not used as evidence; comparison uses model-to-world matrix at byte 176 of verified 416-byte record.'])

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('capture',type=Path);args=a.parse_args()
    report=analyze(args.capture.parent,json.loads(args.capture.read_text()))
    out=args.capture.with_name(args.capture.stem+'-analysis.json');out.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
