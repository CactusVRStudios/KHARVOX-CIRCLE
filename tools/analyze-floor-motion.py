"""Local motion continuity at depth-separated hands. Offline diagnostic, not shader attribution."""
import argparse,importlib.util,json
from pathlib import Path
import numpy as np
import cv2
spec=importlib.util.spec_from_file_location('temporal',Path(__file__).with_name('analyze-temporal-images.py'))
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);ap.add_argument('--sessions',type=int,nargs='+',required=True);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
rows=[json.loads(l) for l in (args.run/'gpu-bindings.jsonl').read_text().splitlines()];results=[]
for session in args.sessions:
 cams,recs=t.validate(rows,session)
 for eye in (0,1):
  pairs={x['kind']:(x,t.decode(args.run,x,d)) for x,d in recs if x['eye']==eye}
  meta,z=pairs['depth'];z=z[:,:,0];mv=pairs['motion'][1][:,:,:2]*np.array(cams[eye]['motionScale'])*[meta['width'],meta['height']]
  # Reviewed near hands in baseline sessions4-7. Preserve an unclassified depth gap.
  hand=(z>.12).astype('uint8');bg=(z>.005)&(z<.10)
  distance=cv2.distanceTransform(1-hand,cv2.DIST_L2,5)
  yy,xx=np.indices(z.shape);u=xx/512;v=yy/512
  X=np.stack([u*0+1,u,v,z,u*u,u*v,v*v,u*z,v*z,z*z],axis=-1)
  controls=bg&(distance>16)&(xx>15)&(xx<497)&(yy>15)&(yy<497)
  edge=bg&(distance>=2)&(distance<=8)&(xx>15)&(xx<497)&(yy>15)&(yy<497)
  if controls.sum()<1000 or edge.sum()<20:continue
  coef=np.linalg.lstsq(X[controls],mv[controls],rcond=None)[0]
  residual=np.linalg.norm(mv-X@coef,axis=2)
  results.append(dict(session=session,eye=eye,edgePixels=int(edge.sum()),controlPixels=int(controls.sum()),
    edgeResidualPixels=np.percentile(residual[edge],[50,95,99]).tolist(),
    controlResidualPixels=np.percentile(residual[controls],[50,95,99]).tolist(),
    handMotionPixels=np.percentile(np.linalg.norm(mv[hand.astype(bool)],axis=1),[50,95]).tolist()))
report={'results':results,'scope':'Depth-separated background 2-8 pixels from near geometry vs smooth fit of static background motion. Units are input pixels. Not a previous-frame correspondence proof; excludes mixed boundary pixels and cannot validate lighting motion or hidden internal GI buffers.'}
args.out.write_text(json.dumps(report,indent=2));print(json.dumps(report))
