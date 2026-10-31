"""Analyze type-based enrollment separately from affine-consumer evidence."""
import argparse,json,re,collections
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('run');ap.add_argument('--out',required=True);a=ap.parse_args()
lines=[line for file in Path(a.run).glob('circle-*.log') for line in file.read_text(errors='replace').splitlines()]
registered={};pairs={};corrections=[]
for line in lines:
 m=re.search(r'motor-world-type: registered kind (\d+) model (\S+) asset (.*?) instance (.*)',line)
 if m:registered[m[2]]={'kind':int(m[1]),'model':m[2],'asset':m[3],'instance':m[4]}
 m=re.search(r'motor-world-type: right-pair kind (\d+) model (\S+) token (\d+) frame (\d+) equal (\d+) asset (.*?) instance (.*)',line)
 if m:pairs[m[2]]={'kind':int(m[1]),'model':m[2],'token':int(m[3]),'frame':int(m[4]),'equal':bool(int(m[5])),'asset':m[6],'instance':m[7]}
 if 'motor-world-pair: aligned' in line:corrections.append(line)
rows=list(registered.values())
result={'run':a.run,'registered':len(rows),'byKind':dict(collections.Counter(r['kind'] for r in rows)),
 'animals':[r for r in rows if r['asset'].startswith('md6def/animals/')],
 'vehicles':[r for r in rows if '/vehicles/' in r['asset']],
 'rightPairs':list(pairs.values()),'correctionLogs':corrections,
 'capacityRejections':sum('motor-world-registry: full' in line for line in lines),'registrations':rows,
 'limits':'Enrollment is not visibility or movement proof; affine selection is not final GPU vertex or headset validation.'}
Path(a.out).write_text(json.dumps(result,indent=2))
print(json.dumps({k:len(v) if isinstance(v,list) else v for k,v in result.items() if k!='registrations'},indent=2))
