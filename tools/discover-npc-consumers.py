"""Discover executed NPC consumers from complete indirect-command windows."""
import importlib.util,json,struct,argparse
from pathlib import Path
s=importlib.util.spec_from_file_location('data',Path(__file__).with_name('gpu-capture-data.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def chunks(b,offset,size):return [(b['buffer'],offset+i,min(65536,size-i),b['generation']) for i in range(0,size,65536)]
def discover(run,pass_number=None):
 inv=m.capture.capture(run,extra=['-NpcConsumers']);(run/'npc-inventory.json').write_text(json.dumps(inv))
 draws=[d for d in inv if d.get('event')=='CmdDrawIndexedIndirectCount'];cat={x['pipeline']:x for x in m.capture.read(run/'shaders/pipelines.jsonl')}
 # The last substantive scene pass containing the verified skin shader.
 candidates=[d for d in draws if any(s['module']=='59d9a748b6815ee5' for s in cat[d['pipeline']]['stages'])]
 if not candidates:raise ValueError('Verified consumer absent')
 anchor=max([d for d in candidates if pass_number is None or d['pass']==pass_number],key=lambda d:d['pass']);render_pass=anchor['pass'];ds=[d for d in draws if d['pass']==render_pass and d['indirect']==anchor['indirect'] and d['countBuffer']==anchor['countBuffer']]
 b=m.bindings(anchor);args=anchor['argumentBindings'][0];counts=anchor['argumentBindings'][1]
 lo=min(d['offset'] for d in ds)&~3;hi=max(d['offset']+d['maxCount']*20 for d in ds);clo=min(d['countOffset'] for d in ds)&~3;chi=max(d['countOffset']+4 for d in ds)
 ranges=chunks(args,lo,hi-lo)+chunks(counts,clo,chi-clo)+chunks(b[(0,8)],b[(0,8)]['offset'],65520)
 rows=m.capture.capture(run,anchor['pipeline'],m.ranges_file(run/'consumer-discovery-ranges.txt',ranges),['-Pass',str(render_pass)])
 (run/'consumer-discovery.json').write_text(json.dumps(rows));cap=m.Capture(run,rows);executed=[]
 for a in [d for d in rows if d.get('bindings') and d.get('pass')==render_pass]:
  table=m.bindings(a)[(0,8)]
  for d in ds:
   if d['eye']!=a['eye']:continue
   count=struct.unpack('<I',cap.read(a,d['countBuffer'],d['countOffset'],4))[0]
   if count>d['maxCount']:raise ValueError('Indirect count exceeds inventoried capacity')
   if not count:continue
   raw=cap.read(a,d['indirect'],d['offset'],count*20)
   for n in range(count):
    command=struct.unpack_from('<IIIiI',raw,n*20);idx=command[4]&0xffffff
    try:record=struct.unpack('<4i2Q6I',cap.read(a,table['buffer'],table['offset']+idx*56,56,table['generation']))
    except ValueError:continue
    executed.append(dict(eye=a['eye'],pipeline=d['pipeline'],shader=cat[d['pipeline']]['stages'][0]['module'],recordIndex=idx,command=list(command),record=list(record),index=d['index']))
 result=dict(passNumber=render_pass,anchor=anchor['pipeline'],executed=executed,ranges=ranges,limits='Pipeline windows are discovery candidates until same-capture draw identities are verified')
 (run/'consumer-executed-candidates.json').write_text(json.dumps(result,indent=2));return result
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('run',type=Path);a.add_argument('--pass-number',type=int);args=a.parse_args();result=discover(args.run,args.pass_number);print('Executed candidates:',len(result['executed']));print('pipelines',sorted(set(x['pipeline'] for x in result['executed'])))
