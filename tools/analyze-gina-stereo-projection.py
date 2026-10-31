"""Compare same-local-vertex binocular disparity, not raw L/R screen equality."""
import argparse,json,math
from pathlib import Path

def analyze(result,allow_partial_writer=False):
 if result.get('status') not in (('complete','partial-writer-coverage') if allow_partial_writer else ('complete',)) or not all(result.get(k) is True for k in ('identityVerified','vertexIndexCoverageVerified','projectionVerified')) or (not allow_partial_writer and result.get('writerLinked') is not True):raise ValueError('Incomplete projection evidence')
 maps={}
 for h in result['hits']:
  q=h['projection'];key=(h['eye'],h['record'][5],*h['record'][6:8]);vertices=maps.setdefault(key,{})
  for offset,xyz,actual,expected in zip(h['vertexOffsets'],h['positions'],q['actualNdc'],q['expectedNdc']):
   vertex=offset//12-(h['record'][1]-2);item=(xyz,actual,expected)
   if vertex in vertices and vertices[vertex]!=item:raise ValueError('Conflicting projection for same surface vertex')
   vertices[vertex]=item
 surfaces=[]
 for key,left in maps.items():
  if key[0]!=0:continue
  right=maps.get((1,*key[1:]),{});common=left.keys()&right.keys();residuals=[];xyzError=[]
  for vertex in common:
   l,r=left[vertex],right[vertex];xyzError.append(math.dist(l[0],r[0]));residuals.append([(l[1][i]-r[1][i])-(l[2][i]-r[2][i]) for i in range(3)])
  surfaces.append(dict(surface=key[1],source=list(key[2:]),leftVertices=len(left),rightVertices=len(right),commonVertices=len(common),leftOnly=len(left.keys()-right.keys()),rightOnly=len(right.keys()-left.keys()),maximumLocalXyzDifference=max(xyzError,default=None),maximumDisparityResidual=[max((abs(v[i]) for v in residuals),default=None) for i in range(3)]))
 if not surfaces or not any(s['commonVertices'] for s in surfaces):raise ValueError('No common surface vertices for stereo comparison')
 return dict(token=result['token'],writerCoverageComplete=result['writerLinked'],draws=len(result['hits']),indexedVertices=sum(h['uniqueVertices'] for h in result['hits']),maximumMvpError=max(h['projection']['currentMvpMaximumError'] for h in result['hits']),maximumNdcError=max(h['projection']['maximumNdcError'] for h in result['hits']),surfaces=surfaces)

if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('series',type=Path);args=a.parse_args();series=json.loads(args.series.read_text());run=args.series.parent
 report=dict(series=args.series.name,status=series['status'],elapsed=series.get('elapsed'),plannedCaptures=series.get('plannedCaptures',6),validCaptures=len(series['samples']),gaps=series['gaps'],invalidCaptures=series.get('invalidCaptures',[]),samples=[],partialProjectionSamples=[],limitations=['GPU inputs captured after the pass; no direct vertex-output or final-image readback.','Complete indexed coverage applies to the selected pass and identified surfaces, not separate hair, depth, or motion passes.','Different per-eye vertex sets can result from legitimate visibility/culling; difference alone is not a defect.'])
 for s in series['samples']:
  r=analyze(json.loads((run/(s['label']+'-result.json')).read_text()));r.update(motion=s['motion'],elapsed=s['elapsed'],label=s['label']);report['samples'].append(r)
 for s in report['invalidCaptures']:
  path=run/(s['label']+'-projection-only.json')
  if path.exists():
   r=analyze(json.loads(path.read_text()),allow_partial_writer=True);r['label']=s['label'];report['partialProjectionSamples'].append(r)
 out=args.series.with_name(args.series.stem+'-analysis.json');out.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
