"""Bounded simulator validation of moving Gina's paired GPU hair model placement."""
import argparse,hashlib,importlib.util,json,time
from pathlib import Path
s=importlib.util.spec_from_file_location('hair_payload',Path(__file__).with_name('analyze-hair-model-payload.py'))
a=importlib.util.module_from_spec(s);s.loader.exec_module(a);m=a.m

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--attempts',type=int,default=12);args=p.parse_args();run=args.run
    stamp=time.time_ns();result=dict(status='recording',samples=[],gaps=[],movingPassed=0)
    out=run/f'hair-pair-validation-{stamp}.json'
    catalog=m.capture.read(run/'shaders/pipelines.jsonl')
    pipelines=sorted({p['pipeline'] for p in catalog if any(x['module']=='a39cad8addc3c423' for x in p['stages'])})
    chosen=None
    for pipeline in pipelines:
        rows=m.capture.capture(run,pipeline);(run/f'hair-discovery-{stamp}-{pipeline}.json').write_text(json.dumps(rows))
        draws=[d for d in rows if d['event']=='draw_indexed']
        if {d['eye'] for d in draws}!={0,1}:continue
        ranges=set()
        for d in draws:
            b=m.bindings(d).get((0,4))
            if b and b['resolved'] and b['size']==53248:ranges.add((b['buffer'],b['offset'],b['size'],b['generation']))
        if ranges:chosen=pipeline;break
    if chosen is None:raise ValueError('No live paired verified hair pipeline')
    f=m.ranges_file(run/f'hair-ranges-{stamp}.txt',ranges)
    try:
        for i in range(args.attempts):
            rows=m.capture.capture(run,chosen,f);label=f'hair-validation-{stamp}-{i+1}'
            (run/(label+'-capture.json')).write_text(json.dumps(rows))
            if {d['eye'] for d in rows if d['event']=='draw_indexed'}!={0,1}:
                result['gaps'].append(dict(label=label,reason='No paired hair payload'));print('Visibility/payload gap',i+1,flush=True);continue
            report=a.analyze(run,rows);report['label']=label
            (run/(label+'-analysis.json')).write_text(json.dumps(report,indent=2))
            if not report['modelRows'] or any(x['leftCpuWorldMaximumError']>1e-4 for x in report['modelRows']):
                raise ValueError('Hair model rows do not all correspond to independently named Gina')
            maximum=max(x['positionDistance'] for x in report['modelRows'])
            matrixMaximum=max(x['worldMaximumEyeDifference'] for x in report['modelRows'])
            result['samples'].append(report)
            print(f'Pair {i+1}: {report["motion"]}, root step {report["rootStep"]:.8f}, hair position difference {maximum:.9g}',flush=True)
            if maximum>1e-5:raise ValueError('Hair placement still diverges between eyes')
            if matrixMaximum>1e-5:raise ValueError('Hair world matrix still diverges between eyes')
            if report['motion']=='moving-root':result['movingPassed']+=1
            out.write_text(json.dumps(result,indent=2))
            if result['movingPassed']>=3:break
            time.sleep(2)
        if result['movingPassed']<3:raise ValueError('Fewer than three verified moving pairs')
        result['status']='passed';result['dllSha256']=hashlib.sha256(Path('bin/KharvoxCircle.dll').read_bytes()).hexdigest()
    except Exception as e:result['status']='failed';result['reason']=str(e);raise
    finally:out.write_text(json.dumps(result,indent=2))
    print(out,flush=True)
