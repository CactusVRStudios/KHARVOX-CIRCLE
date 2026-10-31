"""Reconstruct the verified Gina VS clip path from same-pair GPU payloads."""
import math,struct
SHADERS={'59d9a748b6815ee5','3c66ada7bae85bd0','75bc4db5a97bfb90'}
LAYOUTS={'59d9a748b6815ee5':(15,656),'3c66ada7bae85bd0':(18,672),'75bc4db5a97bfb90':(18,672)}
def matmul(a,b):return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]
def transform(a,v):return [sum(a[r*4+k]*v[k] for k in range(4)) for r in range(4)]
def maximum(a,b):return max(abs(x-y) for x,y in zip(a,b))
def ndc(matrix,xyz,jitter):
 c=transform(matrix,[*xyz,1.0])
 if not all(math.isfinite(x) for x in c) or abs(c[3])<1e-8:raise ValueError('Unprojectable vertex')
 return [c[0]/c[3]+jitter[0],-(c[1]/c[3]+jitter[1]),c[2]/c[3]]
def decode(cap,draw,hit,cpu,rows,plan,bindings):
 if plan['vertexShaders'].get(draw['pipeline']) not in SHADERS:raise ValueError('Unverified Gina vertex shader projection layout')
 index=hit['record'][0]
 if index!=plan['mvpIndex']:raise ValueError('Executed Gina MVP index changed since independent discovery')
 binding,uniformSize=LAYOUTS[plan['vertexShaders'][draw['pipeline']]]
 b=bindings.get((0,binding));u=bindings.get((0,0))
 if not b or not u or b['size']!=9699328 or u['size']!=uniformSize:raise ValueError('Unverified or uncaptured MVP/view descriptor layout')
 matrix=list(struct.unpack('<16f',cap.read(draw,b['buffer'],b['offset']+index*64,64,b['generation'])))
 uniform=cap.read(draw,u['buffer'],u['offset'],u['size'],u['generation']);jitter=list(struct.unpack_from('<2f',uniform,128))
 refs={x['bytes'] for x in rows if x['event']=='cpu_view_projection' and x['token']==draw['token'] and x['eye']==draw['eye'] and x['frame']==draw['backend']}
 if len(refs)!=1:raise ValueError('Missing or ambiguous same-frame CPU projection reference')
 raw=bytes.fromhex(refs.pop())
 if len(raw)!=0x980:raise ValueError('Truncated CPU projection reference')
 def m(at):return list(struct.unpack_from('<16f',raw,at-0x18790))
 projection,view,vp=m(0x18790),m(0x18850),m(0x18910)
 inverseView,centeredReverse=m(0x188D0),m(0x18BD0)
 world=list(struct.unpack('<16f',bytes.fromhex(cpu['cpuMatrix'])));previous=list(struct.unpack('<16f',bytes.fromhex(cpu['cpuPreviousMatrix'])))
 if not all(math.isfinite(x) for a in (matrix,jitter,projection,view,vp,inverseView,centeredReverse,world,previous) for x in a):raise ValueError('Nonfinite projection input')
 referenceError=maximum(matmul(projection,view),vp)
 if referenceError>1e-4*max(1,max(abs(x) for x in vp)):raise ValueError('CPU reference P*V consistency check failed')
 identity=[float(i//4==i%4) for i in range(16)]
 if maximum(matmul(view,inverseView),identity)>1e-4:raise ValueError('CPU inverse-view consistency check failed')
 if max(abs(centeredReverse[r*4+c]-vp[r*4+c]) for r in (0,1,3) for c in range(3))>1e-4:raise ValueError('CPU centered projection orientation mismatch')
 # Native raster matrices use the independently captured camera-relative,
 # reversed-depth projection. The ordinary VP above uses forward depth.
 relative=identity.copy()
 for i in (3,7,11):relative[i]=-inverseView[i]
 rasterVp=matmul(centeredReverse,relative)
 expected=matmul(rasterVp,world);old=matmul(rasterVp,previous)
 actualNdc=[ndc(matrix,x,jitter) for x in hit['positions']];expectedNdc=[ndc(expected,x,jitter) for x in hit['positions']]
 return dict(mvpIndex=index,gpuMvp=matrix,viewConstantsHex=uniform.hex(),jitter=jitter,projection=projection,view=view,viewProjection=vp,inverseView=inverseView,centeredReverseProjection=centeredReverse,rasterViewProjection=rasterVp,world=world,previousWorld=previous,
  forwardDepthMvpMaximumError=maximum(matrix,matmul(vp,world)),
  referenceCompositionError=referenceError,currentMvpMaximumError=maximum(matrix,expected),previousModelMvpMaximumError=maximum(matrix,old),
  actualNdc=actualNdc,expectedNdc=expectedNdc,maximumNdcError=max(maximum(a,b) for a,b in zip(actualNdc,expectedNdc)),
  interpretation='Reconstructed VS output from captured inputs, including jitter and Y inversion; not a GPU vertex-output readback or final-image proof')
