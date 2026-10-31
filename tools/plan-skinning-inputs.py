"""Plan bounded inputs for the verified skinning writer; no GPU requests are issued."""
import argparse,json
from pathlib import Path
SHADER='2d357153fa50c442'
def plan(rows,stages,output):
 if SHADER not in stages:raise ValueError('Unverified skinning writer')
 draws=[r for r in rows if r.get('event') in ('dispatch','dispatch_indirect')]
 if not draws or {r.get('eye') for r in draws}!={0,1}:raise ValueError('Both eyes required')
 left=next(r for r in draws if r['eye']==0);ranges=set();output_bindings=set()
 for d in draws:
  if d.get('tagMismatch') or not d.get('token') or d['token']!=left['token'] or d['backend']!=left['backend']+d['eye']:raise ValueError('Mixed eye pair')
  b={(x['setNumber'],x['binding']):x for x in d['bindings'] if x.get('element')==0}
  out=b[(1,11)]
  if (out['buffer'],out['generation'])!=tuple(output):raise ValueError('Writer output is not the verified vertex resource')
  output_bindings.add((out['buffer'],out['generation']))
  for key in [(0,0),(1,1),(1,2)]:
   x=b[key]
   if not x.get('resolved') or not x.get('generation'):raise ValueError('Unresolved input')
   size=min(x['size'],65536);size-=size%4
   if size<=0 or x['offset']%4:raise ValueError('Invalid bounded input')
   ranges.add((x['buffer'],x['offset'],size,x['generation']))
 if len(ranges)>16:raise ValueError('Too many input ranges')
 return dict(shader=SHADER,ranges=sorted(ranges),output=list(output),coverage='bounded prefixes; decode dispatch job list and 64-byte job records before claiming complete coverage',readyForUserTest=False)
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);a.add_argument('--session',type=int,required=True);a.add_argument('--pipeline',required=True);a.add_argument('--output-buffer',required=True);a.add_argument('--output-generation',type=int,required=True);a.add_argument('--out',type=Path,required=True);args=a.parse_args()
 cat=[json.loads(l) for l in (args.run/'shaders/pipelines.jsonl').read_text().splitlines()];stages={s['module'] for r in cat if r.get('pipeline')==args.pipeline for s in r['stages']}
 rows=[r for l in (args.run/'gpu-bindings.jsonl').read_text().splitlines() if (r:=json.loads(l)).get('session')==args.session and r.get('pipeline')==args.pipeline]
 result=plan(rows,stages,(args.output_buffer,args.output_generation));args.out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
