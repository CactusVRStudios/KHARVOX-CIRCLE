"""Summarize actual native world registry events without conflating registration and draw validation."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('--out',required=True);a=p.parse_args()
lines=[l for f in Path(a.run).glob('circle-*.log') for l in f.read_text(errors='replace').splitlines()]
registered={};validated={}
for l in lines:
 if 'motor-world-registry: door registered' in l:
  m=re.search(r'model (\S+) committed (\S+) manager (\S+) index (\d+) asset (.*?) instance (.*)',l)
  if m:registered[m[1]]={'model':m[1],'committed':m[2],'manager':m[3],'index':int(m[4]),'asset':m[5],'instance':m[6]}
 if 'motor-world-registry: door right-pair validated' in l:
  m=re.search(r'model (\S+).*equal (\d+) asset (.*?) instance (.*)',l)
  if m:validated[m[1]]={'model':m[1],'equal':bool(int(m[2])),'asset':m[3],'instance':m[4]}
r={'run':a.run,'registered':len(registered),'registeredMD6':sum(v['asset'].endswith('.md6') for v in registered.values()),'registeredModules':sum(v['asset'].startswith('models/modules/') for v in registered.values()),'rightPairValidated':len(validated),'capacityRejectEvents':sum('motor-world-registry: full' in l for l in lines),'registrations':list(registered.values()),'pairValidations':list(validated.values()),'limits':'Registration proves live registry enrollment. Pair validation proves affine selection only; neither proves GPU output or an opening-motion visual fix.'}
Path(a.out).write_text(json.dumps(r,indent=2));print(json.dumps({k:v for k,v in r.items() if k not in ('registrations','pairValidations')},indent=2))
