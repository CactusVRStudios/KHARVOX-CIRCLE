"""Verify actual native selector traces, not a simulated stereo implementation."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
pattern=re.compile(r'motor-temporal: selector (\d) eye (\d) eyeSeq (\d+) backend (\d+) index (\d) target ([0-9A-Fa-f]+)')
rows=[]
for file in a.run.glob('*.log'):
    for m in pattern.finditer(file.read_text(errors='replace')):
        k,eye,seq,backend,index=map(int,m.groups()[:5])
        rows.append(dict(kind=k,eye=eye,sequence=seq,backend=backend,index=index,target=m[6]))
issues=[];comparisons=0;families=[]
for output,history in [(0,1),(3,4)]:
    sets=[{r['target'] for r in rows if r['kind'] in (output,history) and r['eye']==eye} for eye in (0,1)]
    if any(len(s)!=2 for s in sets):issues.append(f'family {output}: need two recorded targets per eye')
    if sets[0]&sets[1]:issues.append(f'family {output}: eyes alias images')
    written={(r['eye'],r['sequence']):r['target'] for r in rows if r['kind']==output}
    for r in rows:
        if r['kind'] not in (output,history):continue
        expected=(r['sequence']&1)^(r['kind']==output)
        if r['index']!=expected:issues.append('incorrect per-eye index')
        if r['kind']==history and (r['eye'],r['sequence']-1) in written:
            comparisons+=1
            if r['target']!=written[r['eye'],r['sequence']-1]:issues.append('history does not equal own previous output')
    families.append({'output_selector':output,'eye_targets':[sorted(s) for s in sets]})
if comparisons<2:issues.append('insufficient adjacent-eye-frame evidence')
report={'pass':not issues,'issues':issues,'selector_calls':len(rows),'previous_output_comparisons':comparisons,'families':families,
        'limit':'Native TAA targets only. Does not validate vendor upscaler, GI/RT or object motion histories.'}
(a.run/'motor-temporal-analysis.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
raise SystemExit(0 if report['pass'] else 1)
