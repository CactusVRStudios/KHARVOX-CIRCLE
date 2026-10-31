"""Offline r207 Gina stereo localization. No game memory or renderer changes.
All image comparisons retain pair tags and full-image crop coordinates.
Static-world controls independently check the disparity predicted by depth/IPD.
"""
import argparse,importlib.util,json,struct
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('temporal',ROOT/'tools/analyze-temporal-images.py')
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
# Manually reviewed INPUT-image coordinates. Head point and interior torso box.
REGIONS={6:((145,190),(134,213,153,247),(85,170,185,340)),13:((348,203),(338,233,357,276),(285,185,380,380)),14:((269,157),(254,182,278,228),(235,140,292,315)),18:((189,214),(176,243,196,281),(128,197,214,380)),19:((248,209),(232,239,259,282),(212,190,279,375)),28:((233,195),(216,222,240,273),(173,177,291,378)),31:((326,164),(305,201,332,250),(293,145,351,345))}
def corr(p,q):
 p=p-p.mean();q=q-q.mean();return float((p*q).sum()/max(1e-16,np.linalg.norm(p)*np.linalg.norm(q)))
def match(l,r,x,y,dx0,spread,dyspread=2,rad=5):
 p=l[y-rad:y+rad+1,x-rad:x+rad+1]
 if p.std()<1e-5:return None
 best=(-2,0,0)
 for dy in range(-dyspread,dyspread+1):
  yy=y+dy
  if yy-rad<0 or yy+rad>=r.shape[0]:continue
  for dx in range(int(round(dx0))-spread,int(round(dx0))+spread+1):
   xx=x+dx
   if xx-rad<0 or xx+rad>=r.shape[1]:continue
   c=corr(p,r[yy-rad:yy+rad+1,xx-rad:xx+rad+1])
   if c>best[0]:best=(c,dx,dy)
 c,dx,dy=best
 vals=[corr(p,r[y+dy-rad:y+dy+rad+1,x+dx+i-rad:x+dx+i+rad+1]) if 0<=x+dx+i-rad and x+dx+i+rad<r.shape[1] else -2 for i in [-1,0,1]]
 den=vals[0]-2*vals[1]+vals[2];sub=.5*(vals[0]-vals[2])/den if den<-.00001 else 0
 return c,float(dx+np.clip(sub,-.5,.5)),dy,dx

def robust_fit(points):
 a=np.array(points);X=np.column_stack([a[:,0],np.ones(len(a))]);keep=np.ones(len(a),bool)
 for _ in range(4):
  coef=np.linalg.lstsq(X[keep],a[keep,1],rcond=None)[0];err=a[:,1]-X@coef;keep=abs(err)<.7
 return coef.tolist(),int(keep.sum()),float(np.median(abs(err[keep])))
def analyze(run,out,sessions=None,control_step=24):
 out.mkdir(exist_ok=True,parents=True);rows=[json.loads(l)for l in (run/'gpu-bindings.jsonl').read_text().splitlines()]
 cpu=[];reports=[]
 for session in (range(2,32) if sessions is None else sessions):
  cams,recs=t.validate(rows,session);token=cams[0]['token']
  n=[x for x in rows if x['session']==session and x['event']=='npc' and x['token']==token and 'gina' in x.get('asset','') and x['asset'].startswith('md6def/characters/')]
  by={x['eye']:x for x in n};assert len(n)==2 and set(by)=={0,1}
  mats={e:np.frombuffer(bytes.fromhex(by[e]['cpuMatrix']),dtype='<f4').astype(float)for e in [0,1]}
  prev={e:np.frombuffer(bytes.fromhex(by[e]['cpuPreviousMatrix']),dtype='<f4').astype(float)for e in [0,1]}
  delta=mats[0][[3,7,11]]-prev[0][[3,7,11]]
  placements=[x for x in rows if x['session']==session and x['event']=='npc_placement' and x['token']==token and x['model']==by[0]['model']]
  item={'session':session,'sample':session-1,'token':token,'rootStep':delta.tolist(),'rootStepLength':float(np.linalg.norm(delta)), 'currentMatrixLRMax':float(abs(mats[0]-mats[1]).max()),'previousMatrixLRMax':float(abs(prev[0]-prev[1]).max()),'jointIndexL':by[0]['joint'],'jointIndexR':by[1]['joint'],'placements':placements}
  cpu.append(item)
  if session not in REGIONS:continue
  arr={(x['eye'],x['kind']):t.decode(run,x,d)for x,d in recs};meta={(x['eye'],x['kind']):x for x,d in recs}
  l=np.log1p(arr[0,'input'][:,:,:3].mean(2));r=np.log1p(arr[1,'input'][:,:,:3].mean(2));depth=arr[0,'depth'][:,:,0]
  hx,hy=REGIONS[session][0];box=REGIONS[session][2];W=meta[0,'input']['width'];H=meta[0,'input']['height']
  baseline=float(np.linalg.norm(np.array(cams[1]['position'])-cams[0]['position']))
  # Recorded perspective matrix, reversed native depth: near / distance.
  # Empirical static-world controls below validate this convention independently.
  near=abs(cams[0]['viewToClip'][14]);k=W*.5*abs(cams[0]['viewToClip'][0])*baseline/near
  controls=[];control_mv=[]
  for y in range(24,488,control_step):
   for x in range(32,480,control_step):
    if box[0]-25<x<box[2]+25 and box[1]-25<y<box[3]+25:continue
    z=depth[y-5:y+6,x-5:x+6];d=float(np.median(z))
    if np.ptp(z)>max(.0001,d*.04):continue
    ma=match(l,r,x,y,-k*d,3)
    if ma is None or ma[0]<.94:continue
    cc,dx,dy,di=ma
    controls.append([d,dx,dy,cc,x,y])
    mv=[]
    for e,xx,yy in [(0,x,y),(1,x+di,y+dy)]:mv.append(np.median(arr[e,'motion'][yy-2:yy+3,xx-2:xx+3,:2],axis=(0,1))*np.array(cams[e]['motionScale'])*[W,H])
    control_mv.append((mv[1]-mv[0]).tolist())
  fit,nfit,fiterr=robust_fit(controls)
  d=float(np.median(depth[hy-2:hy+3,hx-2:hx+3]));expected=-k*d
  # Independent depth-silhouette correspondence: two boundaries across nine head rows.
  edge=[]
  for y in range(hy-4,hy+5):
   bounds=[]
   for e in [0,1]:
    v=arr[e,'depth'][y,max(0,hx-38):min(512,hx+39),0];xx=np.where((v>d*.9)&(v<d*1.1))[0]+max(0,hx-38)
    if len(xx):bounds.append([int(xx.min()),int(xx.max())])
   if len(bounds)==2:edge.append([bounds[1][0]-bounds[0][0],bounds[1][1]-bounds[0][1]])
  depthshift=float(np.median(edge));body=[]
  x0,y0,x1,y1=REGIONS[session][1]
  for y in range(y0,y1,5):
   for x in range(x0,x1,4):
    z=float(depth[y,x]);ma=match(l,r,x,y,-k*z,16,5)
    if ma is None or ma[0]<.85:continue
    cc,dx,dy,di=ma
    # Reject obvious texture aliases inconsistent with the independently observed head shift.
    if abs(dx-depthshift)>4:continue
    mv=[]
    for e,xx,yy in [(0,x,y),(1,x+di,y+dy)]:mv.append(np.median(arr[e,'motion'][yy-1:yy+2,xx-1:xx+2,:2],axis=(0,1))*np.array(cams[e]['motionScale'])*[W,H])
    body.append({'x':x,'y':y,'depth':z,'corr':cc,'dx':dx,'dy':dy,'expectedDx':-k*z,'extraDx':dx+k*z,'mvL':mv[0].tolist(),'mvR':mv[1].tolist(),'mvRminusL':(mv[1]-mv[0]).tolist()})
  # Native torso depth contours, independent of color/temporal processing.
  torso_edges=[]
  for y in range(y0,min(y1,y0+25)):
   bounds=[]
   z0=float(np.median(depth[y0:y1,x0:x1]))
   for e in [0,1]:
    start=max(0,x0-25);end=min(512,x1+25)
    v=arr[e,'depth'][y,start:end,0];xs=np.where((v>z0*.92)&(v<z0*1.08))[0]+start
    if len(xs):bounds.append([int(xs.min()),int(xs.max())])
   if len(bounds)==2:torso_edges.append([bounds[1][0]-bounds[0][0],bounds[1][1]-bounds[0][1]])
  # Output uses twice the resolution in this run; compare corresponding UVs.
  ol=np.log1p(arr[0,'output'][:,:,:3].mean(2));orr=np.log1p(arr[1,'output'][:,:,:3].mean(2));outputs=[]
  scale=meta[0,'output']['width']/W
  for bp in body:
   ox=round((bp['x']+meta[0,'input']['x'])*scale-meta[0,'output']['x'])
   oy=round((bp['y']+meta[0,'input']['y'])*scale-meta[0,'output']['y'])
   if not(8<ox<504 and 8<oy<504):continue
   mm=match(ol,orr,ox,oy,bp['dx']*scale,6,6,6)
   if mm and mm[0]>.9:
    outputs.append({'corr':mm[0],'dx':mm[1],'dy':mm[2],'extraInputPixels':mm[1]/scale-bp['expectedDx']})
  rep={**item,'torsoDepthBoundaryShifts':torso_edges,'outputPoints':outputs,'outputMedianExtraInInputPixels':float(np.median([p['extraInputPixels']for p in outputs])) if outputs else None,'baseline':baseline,'theoryDisparityPerDepth':-k,'staticFit':fit,'staticInliers':nfit,'staticFitMedianErrorPixels':fiterr,'staticMVdifferenceMedian':np.median(control_mv,axis=0).tolist(),'headDepth':d,'headExpectedDx':expected,'headDepthBoundaryDx':depthshift,'headDepthExtraDx':depthshift-expected,'torsoPoints':body,'torsoCount':len(body),'torsoMedianExtraDx':float(np.median([p['extraDx']for p in body])) if body else None,'torsoMedianMVdifference':np.median([p['mvRminusL']for p in body],axis=0).tolist() if body else None,'controls':controls}
  reports.append(rep)
  print(session,'step',round(item['rootStepLength'],4),'static',nfit,np.round(fit,2),'head expected/actual',round(expected,2),depthshift,'body',len(body),rep['torsoMedianExtraDx'],rep['torsoMedianMVdifference'])
  # Depth masks at original input coordinates, R mapped to L using only expected stereo shift.
  mask=[]
  for e in [0,1]:
   z=arr[e,'depth'][:,:,0];m=(z>d*.82)&(z<d*1.2);roi=np.zeros_like(m);roi[max(0,box[1]-5):min(512,box[3]+5),max(0,box[0]-30):min(512,box[2]+30)]=True;mask.append(m&roi)
  yy,xx=np.indices(depth.shape);xr=np.clip(np.rint(xx-k*depth).astype(int),0,511)
  overlay=np.zeros((512,512,3),np.uint8);overlay[:,:,0]=mask[0]*255;overlay[:,:,1]=mask[1][yy,xr]*255;overlay[:,:,2]=overlay[:,:,1]
  Image.fromarray(overlay).save(out/f'session-{session:02d}-depth-stereo-overlay.png')
 result={'method':'Raw input pixels. Expected stereo from recorded eye baseline, projection and reversed depth, checked against static-world correspondences. Manual visible torso ROIs; NCC texture matches and independent head depth boundaries. No camera-motion-free assumption. CPU identity joined by token and model.', 'limits':'Region selection manual; matches are image correspondences, not vertex IDs. Horizontal depth reprojection assumes parallel eyes; static controls quantify its validity. Eye-specific shading/occlusion and subpixel uncertainty remain. No final GPU vertex payload or source shader instruction capture.', 'cpuPairs':cpu,'comparisons':reports}
 (out/'comparison.json').write_text(json.dumps(result,indent=2,allow_nan=False))
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);ap.add_argument('--out',type=Path,required=True)
 ap.add_argument('--regions',type=Path,help='Reviewed input-pixel regions keyed by session: [head XY, torso XYXY, exclusion XYXY]')
 ap.add_argument('--control-step',type=int,choices=range(8,33),default=24)
 a=ap.parse_args()
 if a.regions:REGIONS={int(k):v for k,v in json.loads(a.regions.read_text()).items()}
 analyze(a.run,a.out,sorted(REGIONS) if a.regions else None,a.control_step)
