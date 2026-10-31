"""Arm the verified scatter producer and composition consumer in one eye pair."""
import argparse,json,importlib.util
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
s=importlib.util.spec_from_file_location('lighting',Path(__file__).with_name('lighting-images.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
catalog=list(m.rows(a.run/'shaders/pipelines.jsonl'))
aux=[r['pipeline'] for r in catalog if r.get('kind')=='compute' and [x['module'] for x in r.get('stages',[])]==['15d88f905dd05544']]
if len(set(aux))!=1:raise ValueError('Producer pipeline not unique')
observed=set()
for r in m.rows(a.run/'gpu-bindings.jsonl'):
    if r.get('pipeline')==aux[0] and r.get('event')=='dispatch':
        observed.update((b['setNumber'],b['binding'],b['element']) for b in r.get('bindings',[]) if b.get('resolved') and b.get('imageViewLive'))
if not {(0,4,0),(1,3,0),(1,4,0)}<=observed:raise ValueError('Producer inputs/outputs absent from live inventory')
path=a.run/'lighting-auxiliary.pipeline'
if path.exists():raise ValueError('Auxiliary request pending')
path.write_text(aux[0]+'\n',encoding='ascii')
try:m.request(a.run,'76321d417fac921a',['0:8:0','0:28:0','0:30:0','0:31:0','1:4:0'],.5,.8)
except BaseException:
    path.unlink(missing_ok=True);raise
