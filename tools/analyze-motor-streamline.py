"""Check sampled real Streamline calls; absence of stereo evidence is not success."""
import argparse, json, re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path)
p.add_argument('--mode',choices=['sr','rr'],default='sr')
p.add_argument('--require-secondary',action='store_true',help='Require RR viewport 1/3 evidence in a scene that exercises it')
a=p.parse_args()
if a.require_secondary and a.mode!='rr':p.error('--require-secondary needs --mode rr')
text='\n'.join(f.read_text(errors='replace') for f in a.run.glob('*.log'))
calls=[]
pattern=r'motor-sl: evaluate eye (\d) viewport (\d+) backend (\d+) token ([0-9A-Fa-f]+) output ([0-9A-Fa-f]+) result (-?\d+) count (\d+)(?: feature (\d+))?'
for m in re.finditer(pattern,text):
    eye,viewport,backend,token,output,result,count,feature=m.groups()
    calls.append(dict(eye=int(eye),viewport=int(viewport),backend=int(backend),token=token,output=output,result=int(result),count=int(count),feature=int(feature or 0)))
issues=[]
feature=1001 if a.mode=='rr' else 0
for native in ([1,2] if a.require_secondary else [1]):
    for eye in (0,1):
        viewport=native+2*eye
        own=[r for r in calls if r['eye']==eye and r['viewport']==viewport and r['feature']==feature and r['result']==0]
        if len(own)<2:issues.append(f'eye {eye} viewport {viewport} feature {feature}: insufficient successful stereo evaluation evidence')
for r in calls:
    allowed=[1+2*r['eye'],2+2*r['eye']] if r['feature']==1001 else [1+2*r['eye']]
    if r['viewport'] not in allowed or r['result']!=0:issues.append('wrong viewport or failed evaluation: '+str(r))
outputs=[{r['output'] for r in calls if r['eye']==e and r['viewport']==1+2*e} for e in (0,1)]
if outputs[0]&outputs[1]:issues.append('left/right output targets alias in sample (check resize lifetime)')
failures=[line for line in text.splitlines() if 'motor-sl:' in line and any(s in line for s in ['rejected','incomplete','no current output ownership'])]
if failures:issues.append('bridge validation/fallback failure recorded')
report=dict(passed=not issues,mode=a.mode,secondaryRequired=a.require_secondary,issues=issues,evaluations=calls,failures=failures,
            limit='Sampled native API/target evidence only; does not establish visual quality, GPU motion vectors or long-run stability.')
(a.run/'motor-streamline-analysis.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2));raise SystemExit(0 if not issues else 1)
