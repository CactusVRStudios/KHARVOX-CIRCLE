"""Bounded live lighting series; validates each capture before advancing."""
import argparse,importlib.util,json,time,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--composition',action='store_true');a=p.parse_args()
spec=importlib.util.spec_from_file_location('lighting',Path(__file__).with_name('lighting-images.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
a.out.mkdir(parents=True,exist_ok=True)
plan=[('f2afa1230b5bd152',[f'0:{b}:0' for b in range(25,29)]),
      ('779d78154a8b677e',[f'0:{b}:0' for b in (*range(12,16),*range(17,21))]),
      ('4c1cce85e005cf67',['0:11:0','1:1:0'])]
if a.composition:
    # Both target sets passed complete live pilots. Excludes unprepared 0:30/31.
    plan=[('76321d417fac921a',['0:16:0','0:17:0','0:19:0','1:1:0','1:2:0','1:4:0']),
          ('7d7a322ed6d6e6b2',['0:6:0','1:1:0','1:3:0','1:4:0'])]
report=[]
time.sleep(8)
for repeat in range(2):
    for module,bindings in plan:
        old=max((r.get('session',0) for r in m.rows(a.run/'gpu-bindings.jsonl')),default=0)
        m.request(a.run,module,bindings,.5,.80)
        started=time.monotonic();result=None
        while time.monotonic()-started<25:
            time.sleep(1)
            records=list(m.rows(a.run/'gpu-bindings.jsonl'))
            start=next((r for r in records if r.get('event')=='temporal_start' and r.get('session',0)>old),None)
            if not start:continue
            session=start['session'];out=a.out/f'session-{session}'
            result=subprocess.run([sys.executable,str(Path(__file__).with_name('lighting-images.py')),str(a.run),
                '--session',str(session),'--out',str(out)],capture_output=True,text=True)
            if result.returncode==0:
                report.append(dict(session=session,module=module,bindings=bindings,validated=True,output=str(out)))
                print(result.stdout.strip(),flush=True);break
        else:
            report.append(dict(module=module,validated=False,error=result.stderr if result else 'No acknowledged request'))
            (a.out/'manifest.json').write_text(json.dumps(report,indent=2));raise RuntimeError('Pilot/series failed; stopped without issuing further requests')
        (a.out/'manifest.json').write_text(json.dumps(report,indent=2))
        time.sleep(4)
print(f'{len(report)} captures validated. No renderer defect conclusion inferred automatically.',flush=True)
