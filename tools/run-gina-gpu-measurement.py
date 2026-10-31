"""r216 automatic geometry/MVP pilot and bounded five-minute Gina GPU measurement."""
import argparse,ctypes,hashlib,importlib.util,json,math,shutil,time
from pathlib import Path
s=importlib.util.spec_from_file_location('pilot',Path(__file__).with_name('paired-npc-pilot.py'));p=importlib.util.module_from_spec(s);s.loader.exec_module(p)
FILES=['run-gina-gpu-measurement.py','paired-npc-pilot.py','gpu-capture-data.py','capture-gina-geometry.py','skinning-jobs.py','request-gpu-bindings.ps1','gina-projection.py','analyze-gina-raster.py']
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def active_process(run):
 launch=json.loads((run/'launch.json').read_text(encoding='utf-8-sig'));pid=launch['pid']
 k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.restype=ctypes.c_void_p;k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32];k.GetExitCodeProcess.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)];k.CloseHandle.argtypes=[ctypes.c_void_p]
 handle=k.OpenProcess(0x1000,False,pid)
 if not handle:raise ValueError('Diagnostic process has exited')
 try:
  code=ctypes.c_uint32()
  if not k.GetExitCodeProcess(handle,ctypes.byref(code)) or code.value!=259:raise ValueError('Diagnostic process is no longer active')
 finally:k.CloseHandle(handle)
 return pid

def identity(run):
 pid=active_process(run)
 root=Path(__file__).resolve().parent.parent
 return dict(pid=pid,launch=digest(run/'launch.json'),plan=digest(run/'paired-plan.json'),dll=digest(root/'bin/KharvoxCircle.dll'),tools={name:digest(root/'tools'/name) for name in FILES})
def complete(result):
 return result.get('status')=='complete' and (not result.get('projectionRequested') or result.get('projectionVerified') is True) and all(result.get(k) is True for k in ('identityVerified','vertexIndexCoverageVerified','writerLinked')) and {h['eye'] for h in result.get('hits',[])}=={0,1}
def motion(rows,token):
 cpus=[r for r in rows if r['event']=='npc' and r['token']==token and 'gina' in r['asset']]
 deltas=[]
 for c in cpus:
  a=p.struct.unpack('<16f',bytes.fromhex(c['cpuMatrix']));b=p.struct.unpack('<16f',bytes.fromhex(c['cpuPreviousMatrix']));deltas.append(math.sqrt(sum((a[i]-b[i])**2 for i in (3,7,11))))
 return dict(rootStep=max(deltas,default=0),classification='moving-root' if max(deltas,default=0)>1e-5 else 'stationary-root')
def summarize(result,rows):
 maps={}
 for h in result['hits']:
  key=(h['eye'],tuple(h['record'][6:8]));v=maps.setdefault(key,{})
  for off,xyz in zip(h['vertexOffsets'],h['positions']):v[off//12-(h['record'][1]-2)]=xyz
 comparisons=[]
 for (eye,key),a in maps.items():
  if eye or (1,key) not in maps:continue
  b=maps[(1,key)];shared=a.keys()&b.keys();dist=[math.dist(a[i],b[i]) for i in shared]
  comparisons.append(dict(source=list(key),commonVertices=len(shared),maximumObjectSpaceDistance=max(dist,default=0),meanObjectSpaceDistance=sum(dist)/len(dist) if dist else None))
 projection=[dict(eye=h['eye'],recordIndex=h['recordIndex'],**{k:v for k,v in h['projection'].items() if k not in ('actualNdc','expectedNdc')}) for h in result['hits'] if 'projection' in h]
 return dict(token=result['token'],copies=result['copies'],draws=len(result['hits']),vertices=sum(h['uniqueVertices'] for h in result['hits']),motion=motion(rows,result['token']),comparisons=comparisons,writerLinks=result['writerLinks'],projection=projection)
def sample(run,plan,label):
 rows=p.acquire(run,plan);(run/f'{label}-capture.json').write_text(json.dumps(rows));result=p.analyze(run,rows,plan);(run/f'{label}-result.json').write_text(json.dumps(result))
 if not complete(result):raise ValueError('Incomplete pilot geometry or writer linkage')
 if plan.get('rasterAudit'):
  catalog=p.m.capture.read(run/'shaders/pipelines.jsonl');raster=p.load('gina_raster','analyze-gina-raster.py').analyze(rows,result,catalog)
  (run/f'{label}-raster.json').write_text(json.dumps(raster,indent=2))
 summary=summarize(result,rows);summary['label']=label;start=next(r for r in rows if r['event']=='start');summary['remainingSlots']=start['snapshotSlotsRemaining']-result['copies'];summary['bytes']=sum(r['copyBytes'] for r in rows if r['event']=='gpu_copy');summary['remainingBytes']=start['remainingBudget']-summary['bytes'];return summary

def budget(samples,count):
 peak=max(s['copies'] for s in samples);byte_peak=max(s['bytes'] for s in samples);last=samples[-1]
 if last['remainingSlots']<math.ceil(peak*1.25)*count+3072:raise ValueError('Insufficient immutable snapshot slots for the full series')
 if last['remainingBytes']<math.ceil(byte_peak*1.25)*count:raise ValueError('Insufficient native byte budget for the full series')

def pilot(run,projection=False):
 readiness=run/'gina-measurement-ready.json';readiness.write_text(json.dumps(dict(readyForUserTest=False,status='piloting')))
 active_process(run)
 for discovery in range(3 if projection else 1):
  try:p.prepare(run,projection=projection);break
  except ValueError as e:
   if not projection or str(e) not in ('Discovery found no executed Gina consumer in both eyes','Scene not ready: no character consumers/Gina') or discovery==2:raise
   print(f'Discovery {discovery+1}/3: waiting for visible Gina geometry.',flush=True);time.sleep(2)
 plan=json.loads((run/'paired-plan.json').read_text());samples=[];pilotId=time.time_ns()
 for i in range(6 if projection else 3):
  try:sample_result=sample(run,plan,f'pilot-{pilotId}-{i+1}')
  except ValueError as e:
   if str(e)=='Incomplete pilot geometry or writer linkage':
    label=f'pilot-{pilotId}-{i+1}';rows=json.loads((run/f'{label}-capture.json').read_text());partial=json.loads((run/f'{label}-result.json').read_text())
    if partial.get('status')!='discovery':raise
    p.refine_coverage(run,plan,rows,partial)
    print(f'Pilot attempt {i+1}: incomplete coverage; next request refined, failed pair not counted.',flush=True)
    continue
   if not projection or str(e)!='No executed draw independently identifies Gina':raise
   (run/f'pilot-{pilotId}-{i+1}-skipped.json').write_text(json.dumps(dict(reason=str(e),valid=False)))
   print(f'Pilot attempt {i+1}/6: Gina absent from executed draws; no valid sample counted.',flush=True)
   continue
  samples.append(sample_result);print(f'Pilot {i+1}/{6 if projection else 3}: {sample_result["draws"]} Gina draws, {sample_result["vertices"]} indexed vertices, writer linked.',flush=True)
  if len(samples)>=3 and (not projection or any(s['motion']['classification']=='moving-root' for s in samples)):break
  time.sleep(2)
 if len({s['token'] for s in samples})!=len(samples):raise ValueError('Pilot repeated a stale eye pair')
 if len(samples)<3:raise ValueError('Fewer than three complete Gina pairs in bounded pilot')
 if projection and not any(s['motion']['classification']=='moving-root' for s in samples):raise ValueError('No moving Gina pair in bounded MVP pilot; no user test requested')
 budget(samples,6)
 if shutil.disk_usage(run).free<1024**3:raise ValueError('Less than 1 GiB disk space available')
 result=dict(readyForUserTest=True,status='ready',profile='mvp' if projection else 'geometry',runtime=json.loads((run/'launch.json').read_text(encoding='utf-8-sig')).get('runtime'),identity=identity(run),samples=samples,duration=300,sampleCount=6,scope='Complete indexed Gina geometry and linked skinning writers; MVP profile additionally requires captured GPU matrices, jitter and same-frame CPU projection reference, including a moving pair; no rendering-fix claim')
 readiness.write_text(json.dumps(result,indent=2));return result

def require_ready(run,projection=False):
 ready=json.loads((run/'gina-measurement-ready.json').read_text())
 if projection and ready.get('profile')!='mvp':raise ValueError('A geometry-only pilot cannot authorize an MVP measurement')
 if ready.get('readyForUserTest') is not True or ready.get('identity')!=identity(run):raise ValueError('No current verified pilot for this process, plan and build')
 budget(ready['samples'],ready['sampleCount']);return ready

def measure(run,projection=False):
 ready=require_ready(run,projection=projection);plan=json.loads((run/'paired-plan.json').read_text());started=time.monotonic();result=dict(status='recording',duration=ready['duration'],samples=[],gaps=[]);out=run/f'gina-measurement-{time.strftime("%Y%m%d-%H%M%S")}.json';ready['readyForUserTest']=False;ready['status']='consumed';(run/'gina-measurement-ready.json').write_text(json.dumps(ready,indent=2))
 out.write_text(json.dumps(result,indent=2));print(f'MEASUREMENT STARTED: {ready["duration"]} seconds, {ready["sampleCount"]} paired captures.',flush=True)
 try:
  for i in range(ready['sampleCount']):
   target=started+i*ready['duration']/(ready['sampleCount']-1)
   while time.monotonic()<target:
    if (run/'gina-measurement.cancel').exists():raise ValueError('Measurement cancelled')
    time.sleep(min(1,target-time.monotonic()))
   if (run/'gina-measurement.cancel').exists():raise ValueError('Measurement cancelled')
   active_process(run)
   try:
    item=sample(run,plan,f'measurement-{i+1}');item['elapsed']=time.monotonic()-started;result['samples'].append(item);print(f'Sample {i+1}/{ready["sampleCount"]}: {item["motion"]["classification"]}, {item["vertices"]} indexed vertices.',flush=True)
   except ValueError as e:
    if str(e)!='No executed draw independently identifies Gina':raise
    result['gaps'].append(dict(attempt=i+1,elapsed=time.monotonic()-started,reason=str(e)));print(f'Sample {i+1}/{ready["sampleCount"]}: no executed Gina draw; gap recorded.',flush=True)
   out.write_text(json.dumps(result,indent=2))
  result['status']='partial' if result['gaps'] else 'complete'
 except Exception as e:result['status']='failed';result['reason']=str(e);raise
 finally:result['elapsed']=time.monotonic()-started;out.write_text(json.dumps(result,indent=2))
 return result

if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);a.add_argument('--pilot',action='store_true');a.add_argument('--mvp',action='store_true');args=a.parse_args();run=args.run.resolve();lock=run/'gina-measurement.lock'
 with lock.open('x') as f:f.write(str(time.time()))
 try:
  try:
   result=pilot(run,projection=args.mvp) if args.pilot else measure(run,projection=args.mvp);print(result['status'],flush=True)
  except Exception as e:
   if args.pilot:(run/'gina-measurement-ready.json').write_text(json.dumps(dict(status='blocked',readyForUserTest=False,reason=str(e)),indent=2))
   raise
 finally:lock.unlink()
