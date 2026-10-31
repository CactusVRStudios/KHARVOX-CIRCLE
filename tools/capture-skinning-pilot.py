"""r214 bounded skinning-input pilot. Never starts a user measurement."""
import argparse,importlib.util,json,struct,subprocess,time
from pathlib import Path

def module(name,file):
 s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
c=module('capture','capture-gina-geometry.py');j=module('jobs','skinning-jobs.py')
SHADER='2d357153fa50c442'

def verify(run,rows,pipeline):
 if any(r['event'] in ('truncated','capture_rejected','submit_error','copy_skipped') or (r['event']=='gpu_copy' and r.get('status')!='recorded') for r in rows):raise ValueError('Incomplete native capture')
 draws=[r for r in rows if r.get('pipeline')==pipeline and r['event'] in ('dispatch','dispatch_indirect')]
 if not draws or {d['eye'] for d in draws}!={0,1}:raise ValueError('Both eye dispatches required')
 left=next(d for d in draws if d['eye']==0)
 if any(d['session']!=left['session'] or d['token']!=left['token'] or d['backend']!=left['backend']+d['eye'] or d.get('tagMismatch') for d in draws):raise ValueError('Mixed dispatch pair')
 copies=[r for r in rows if r['event']=='gpu_copy'];done=[r for r in rows if r['event']=='gpu_complete'];blobs={}
 if len({r['id'] for r in copies})!=len(copies) or len({r['id'] for r in done})!=len(done):raise ValueError('Duplicate copy/completion')
 for copy in copies:
  completions=[d for d in done if d['id']==copy['id']]
  if len(completions)!=1:raise ValueError('Missing completion')
  end=completions[0];path=(run/end['file']).resolve()
  if path.parent!=run.resolve() or end.get('captureSession')!=copy['session']:raise ValueError('Invalid completion identity')
  data=path.read_bytes()
  if len(data)!=copy['copyBytes'] or len(data)!=end['bytes'] or c.fnv64(data)!=int(end['hash'],16):raise ValueError('Corrupt payload')
  submits=[r for r in rows if r['event']=='submit' and r.get('cb')==copy['cb'] and r.get('commandGeneration')==copy['commandGeneration']]
  if len(submits)!=1 or submits[0].get('submitNumber')!=1 or submits[0].get('descriptorChanged') or submits[0].get('tagMismatch'):raise ValueError('Unstable submission')
  blobs[copy['id']]=data
 def payload(d,b,phase):
  hits=sorted([x for x in copies if all(x.get(k)==d.get(k) for k in ('session','cb','commandGeneration','drawNumber','eye','token','backend')) and x['phase']==phase and x['buffer']==b['buffer'] and x['generation']==b['generation'] and b['offset']<=x['copyOffset']<b['offset']+b['size']],key=lambda x:x['copyOffset'])
  cursor=b['offset'];result=b''
  for x in hits:
   if x['copyOffset']!=cursor:raise ValueError('Missing/overlapping per-dispatch payload')
   result+=blobs[x['id']];cursor+=x['copyBytes']
  if not result:raise ValueError('Missing per-dispatch payload')
  return result

 results=[]
 for d in draws:
  bindings={(b['setNumber'],b['binding']):b for b in d['bindings'] if b.get('element')==0}
  if not d.get('skinningHeader'):raise ValueError('Physical batch header not captured')
  ins=[d['skinningHeader'],bindings[(1,1)],bindings[(1,2)]]
  before=[payload(d,b,'before_dispatch') for b in ins];after=[payload(d,b,'after_dispatch') for b in ins]
  if before!=after:raise ValueError('Job inputs changed during dispatch')
  groups=d.get('groups')
  if groups is None:
   args=d['argumentBindings'][0];raw=payload(d,args,'before_dispatch')
   if raw!=payload(d,args,'after_dispatch') or len(raw)!=12:raise ValueError('Unstable indirect dispatch arguments')
   groups=list(struct.unpack('<3I',raw))
  jobs=j.decode_jobs(bytes.fromhex(d['computePush']),bytes.fromhex(d['computePushValid']),*before,groups)
  results.append(dict(eye=d['eye'],drawNumber=d['drawNumber'],jobs=jobs))
 return dict(status='writer_inputs_verified',readyForUserTest=False,reason='Writer jobs decoded; Gina identity and complete indexed output coverage still required',dispatches=results)

def pilot(run):
 cat=c.read(run/'shaders/pipelines.jsonl');pipelines={x['pipeline'] for x in cat if any(s.get('module')==SHADER for s in x.get('stages',[]))}
 if len(pipelines)!=1:raise ValueError('Skinning pipeline not uniquely loaded; scene preflight unavailable')
 rows=c.capture(run,next(iter(pipelines)))
 draws=[r for r in rows if r.get('pipeline') in pipelines and r.get('event') in ('dispatch','dispatch_indirect')]
 live={d['pipeline'] for d in draws}
 if len(live)!=1:raise ValueError('No unique skinning dispatch in a tagged stereo eye pair; loaded gameplay scene required. No payload or long measurement started.')
 pipeline=live.pop();ranges=set()
 for d in draws:
  b=next(b for b in d['bindings'] if b.get('setNumber')==1 and b.get('binding')==11 and b.get('element')==0)
  ranges.add((b['buffer'],b['offset'],min(b['size'],65532),b['generation']))
 path=run/'skinning-pilot-ranges.txt';path.write_text(''.join(f'{b} {o} {s} {g}\n' for b,o,s,g in sorted(ranges)))
 log=run/'gpu-bindings.jsonl';offset=log.stat().st_size
 subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(Path(__file__).with_name('request-gpu-bindings.ps1')),'-RunDirectory',str(run),'-Pipeline',pipeline,'-RangesFile',str(path),'-SkinningInputs'],check=True)
 deadline=time.monotonic()+25
 while time.monotonic()<deadline:
  time.sleep(.25);rows=c.read(log,offset)
  if any(r['event']=='capture_rejected' for r in rows):raise ValueError('Native preflight rejected capture')
  if any(r['event']=='stop' for r in rows):
   ids={r['id'] for r in rows if r['event']=='gpu_copy' and r.get('status')=='recorded'}
   if ids<={r['id'] for r in rows if r['event']=='gpu_complete'}:return verify(run,rows,pipeline)
 raise ValueError('Pilot timed out after 25 seconds; no long measurement started')

if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);args=a.parse_args();run=args.run.resolve();lock=run/'skinning-pilot.lock'
 try:
  with lock.open('x') as f:f.write(str(time.time()))
 except FileExistsError:raise SystemExit('Pilot already locked; no second request issued')
 try:
  try:result=pilot(run)
  except Exception as e:result=dict(status='blocked',readyForUserTest=False,reason=str(e))
  (run/'skinning-pilot.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
 finally:lock.unlink()
 raise SystemExit(0 if result['status']=='writer_inputs_verified' else 1)
