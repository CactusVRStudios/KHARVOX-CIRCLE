"""Continue a visibility-only aborted r216 series without counting absent Gina as evidence."""
import argparse,importlib.util,json,time
from pathlib import Path
s=importlib.util.spec_from_file_location('measurement',Path(__file__).with_name('run-gina-gpu-measurement.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
ABSENT='No executed draw independently identifies Gina'

def run_series(run):
 ready=json.loads((run/'gina-measurement-ready.json').read_text())
 if ready.get('profile')!='mvp' or ready.get('status')!='consumed' or ready.get('identity')!=m.identity(run):raise ValueError('No unchanged consumed MVP pilot')
 if 'runtime=OpenXR Simulator Runtime' not in (run/'circle-status.txt').read_text():raise ValueError('This continuation requires the simulator')
 failed=max(run.glob('gina-measurement-20*.json'),key=lambda p:p.stat().st_mtime)
 failure=json.loads(failed.read_text())
 if failure.get('status')!='failed' or failure.get('samples') or failure.get('reason')!=ABSENT:raise ValueError('Only the zero-sample visibility failure is recoverable here')
 rows=json.loads((run/'measurement-1-capture.json').read_text());m.p.m.Capture(run,rows)
 start=next(r for r in rows if r['event']=='start');copies=[r for r in rows if r['event']=='gpu_copy']
 with (run/'gpu-bindings.jsonl').open('rb') as f:
  f.seek(max(0,f.seek(0,2)-262144));tail=f.read().splitlines()[1:]
 sessions={json.loads(line).get('session') for line in tail if line.strip()}
 if sessions!={start['session']}:raise ValueError('Intervening capture invalidates remaining budget')
 last=dict(ready['samples'][-1]);last.update(copies=len(copies),bytes=sum(r['copyBytes'] for r in copies),remainingSlots=start['snapshotSlotsRemaining']-len(copies),remainingBytes=start['remainingBudget']-sum(r['copyBytes'] for r in copies))
 m.budget([*ready['samples'],last],6)
 if list(run.glob('gpu-binding-*.request')):raise ValueError('Another GPU request is pending')
 marker=run/'gina-simulator-continuation-used.json'
 with marker.open('x') as f:json.dump(dict(priorFailure=failed.name,identity=ready['identity'],scriptSha256=m.digest(__file__)),f)
 plan=json.loads((run/'paired-plan.json').read_text());began=time.monotonic();stamp=time.strftime('%Y%m%d-%H%M%S');out=run/f'gina-simulator-series-{stamp}.json'
 result=dict(status='recording',duration=300,plannedCaptures=6,priorFailure=failed.name,samples=[],gaps=[])
 out.write_text(json.dumps(result,indent=2));print('SIMULATOR SERIES STARTED: 300 seconds, six scheduled capture attempts.',flush=True)
 try:
  for i in range(6):
   target=began+i*60
   while time.monotonic()<target:
    if (run/'gina-measurement.cancel').exists():raise ValueError('Measurement cancelled')
    time.sleep(min(1,target-time.monotonic()))
   if (run/'gina-measurement.cancel').exists():raise ValueError('Measurement cancelled')
   if m.identity(run)!=ready['identity']:raise ValueError('Pilot identity changed during series')
   label=f'simulator-series-{stamp}-{i+1}'
   try:
    item=m.sample(run,plan,label);item.update(elapsed=time.monotonic()-began,label=label);result['samples'].append(item)
    print(f'Capture {i+1}/6 valid: {item["motion"]["classification"]}, {item["vertices"]} vertices.',flush=True)
   except ValueError as e:
    if str(e)!=ABSENT:raise
    result['gaps'].append(dict(attempt=i+1,elapsed=time.monotonic()-began,label=label,reason=str(e)))
    print(f'Capture {i+1}/6: visibility gap, not counted as a valid Gina sample.',flush=True)
   out.write_text(json.dumps(result,indent=2))
  result['status']='complete' if len(result['samples'])==6 else 'partial'
 except Exception as e:result.update(status='failed',reason=str(e));raise
 finally:
  result['elapsed']=time.monotonic()-began;out.write_text(json.dumps(result,indent=2));print(str(out),flush=True)
 return result

if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);args=a.parse_args();run=args.run.resolve();lock=run/'gina-measurement.lock'
 with lock.open('x') as f:f.write(str(time.time()))
 try:print(run_series(run)['status'],flush=True)
 finally:lock.unlink()
