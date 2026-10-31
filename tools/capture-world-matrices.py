import argparse,math
ap=argparse.ArgumentParser(description="Bounded GPU root-matrix snapshots; target visibility is NOT inferred.");ap.add_argument("run");ap.add_argument("--pipeline",required=True);ap.add_argument("--writer");ap.add_argument("--sample",type=int,required=True);ap.add_argument("--pass",dest="pass_number",type=int,default=2);args=ap.parse_args();number=args.sample
import json,importlib.util,struct
from pathlib import Path
run=Path(args.run);s=importlib.util.spec_from_file_location('gpu',Path('tools/gpu-capture-data.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
rows=json.load(open(run/'world-consumers-inventory.json'));motion=json.load(open(run/'world-motion.json'));targets=[r for r in motion['models'] if r['maxAffineChange']>1e-5 or '/dog/' in r['asset'] or '/gizeh_dog/' in r['asset']];pipeline=args.pipeline;draws=[r for r in rows if r.get('pipeline')==pipeline and r['event']=='CmdDrawIndexedIndirectCount'];buffers={(b['buffer'],b['generation'],b['offset']) for d in draws for b in d['bindings'] if b.get('binding')==16};ranges=[]
for b,g,o in buffers:
 for target in targets:ranges.append((b,o+target['index']*64,64,g))
# Merge adjacent/near entries into bounded matrix-only spans.
merged=[]
for b,o,n,g in sorted(ranges):
 if merged and merged[-1][0]==b and merged[-1][3]==g and o+n-merged[-1][1]<=65536:
  prev=merged[-1];merged[-1]=(b,prev[1],o+n-prev[1],g)
 else:merged.append((b,o,n,g))
f=m.ranges_file(run/'world-matrix-ranges.txt',merged);print('ranges',merged,flush=True)
extra=['-Pass',str(args.pass_number)]
if args.writer:extra+=['-SkinningInputs','-SkinningBatch','9','-PairedNpcConsumers','-RenderWidth','928','-RenderHeight','992']
r=m.capture.capture(run,args.writer or pipeline,f,extra);(run/f'world-matrix-capture-{number}.json').write_text(json.dumps(r));cap=m.Capture(run,r)
ds=[d for d in r if d.get('event')=='CmdDrawIndexedIndirectCount'];result=[]
assert len({d['token'] for d in ds})==1 and {d['eye'] for d in ds}=={0,1}
leftFrames={d['backend'] for d in ds if d['eye']==0};rightFrames={d['backend'] for d in ds if d['eye']==1}
assert len(leftFrames)==len(rightFrames)==1 and next(iter(rightFrames))==next(iter(leftFrames))+1
assert not any(d.get('tagMismatch') for d in ds)
for target in targets:
 values={}
 for d in ds:
  b=m.bindings(d).get((0,16))
  if not b or not b.get('resolved',True) or b.get('buffer')=='0x0' or b.get('size')!=6291456:continue
  data=cap.read(d,b['buffer'],b['offset']+target['index']*64,64,b['generation']);values[d['eye']]=data
 if set(values)!={0,1}:raise ValueError('Missing eye pair')
 left=struct.unpack('<16f',values[0]);right=struct.unpack('<16f',values[1]);assert all(math.isfinite(v) for v in left+right);result.append({'model':target['model'],'index':target['index'],'asset':target['asset'],'instance':target['instance'],'movingCPU':target['maxAffineChange']>1e-5,'gpuLeft':list(left),'gpuRight':list(right),'validAffine':all(abs(v)<1e30 for v in left+right) and abs(left[15]-1)<1e-5 and abs(right[15]-1)<1e-5,'equal':values[0]==values[1],'maxDelta':max(abs(a-b) for a,b in zip(left,right))})
out={'pipeline':pipeline,'token':ds[0]['token'],'backendFrames':sorted({d['backend'] for d in ds}),'copies':len(cap.copies),'models':result,'limits':'GPU matrix entries captured at a real geometry consumer, but target draw/visibility association is not proven.'};(run/f'world-matrix-result-{number}.json').write_text(json.dumps(out,indent=2));print('targets',len(result),'equal',sum(x['equal'] for x in result),'maxDelta',max(x['maxDelta'] for x in result))
