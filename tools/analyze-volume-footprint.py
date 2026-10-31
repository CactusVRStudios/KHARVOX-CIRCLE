"""Reconstruct verified 15d88/76321 volume accesses from one GPU eye pair.
Fails on missing sampler evidence, changed constants, or a mismatched producer.
"""
import argparse,json
from pathlib import Path
import numpy as np
import cv2

def sample2(a,u,v,linear,reduction=0):
    h,w=a.shape;x=u*w-.5;y=v*h-.5
    if not linear:return a[np.clip(np.floor(v*h).astype(int),0,h-1),np.clip(np.floor(u*w).astype(int),0,w-1)]
    ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=x-ix;fy=y-iy
    if reduction:
        samples=[np.where((fx if dx else 1-fx)*(fy if dy else 1-fy)>0,
            a[np.clip(iy+dy,0,h-1),np.clip(ix+dx,0,w-1)],np.inf if reduction==1 else -np.inf)
            for dy in (0,1) for dx in (0,1)]
        if reduction==1:return np.minimum.reduce(samples)
        if reduction==2:return np.maximum.reduce(samples)
        raise ValueError('Unknown sampler reduction mode')
    return sum(a[np.clip(iy+dy,0,h-1),np.clip(ix+dx,0,w-1)]*(fx if dx else 1-fx)*(fy if dy else 1-fy) for dy in (0,1) for dx in (0,1))

def analyze(run,folder,session):
    rows=[json.loads(x) for x in (run/'gpu-bindings.jsonl').read_text().splitlines()];rows=[x for x in rows if x.get('session')==session]
    start=next(x for x in rows if x['event']=='temporal_start');report=json.loads((folder/'report.json').read_text());results=[]
    def sampler(pipe,eye,binding):
        found=[x for x in rows if x['event']=='lighting_sampler' and x['pipeline']==pipe and x['eye']==eye and x['set']==0 and x['binding']==binding and x['element']==0]
        if len(found)!=2:raise ValueError('Missing sampler boundaries')
        keys=('sampler','minFilter','magFilter','addressU','addressV','addressW','unnormalized','anisotropy','reduction')
        if any(found[0][k]!=found[1][k] for k in keys):raise ValueError('Sampler changed')
        s=found[0]
        if s['unnormalized'] or s['anisotropy'] or s['minFilter']!=s['magFilter']:raise ValueError('Unsupported sampler configuration')
        if s['addressU']!=2 or s['addressV']!=2:raise ValueError('Expected clamp-to-edge sampling')
        return s
    for eye in (0,1):
        def arr(stage,binding,phase='before',setnum=0):return np.load(folder/f'{stage}-eye{eye}-lighting-s{setnum}-b{binding}-e0-{phase}.npy')
        def constants(stage,kind='s0-b0'):
            before=(folder/f'{stage}-eye{eye}-{kind}-before.bin').read_bytes();after=(folder/f'{stage}-eye{eye}-{kind}-after.bin').read_bytes()
            if before!=after:raise ValueError('Constants changed within dispatch')
            return np.frombuffer(before,dtype='<f4')
        cu=constants('consumer');pu=constants('producer');push=constants('consumer','push-reference-prefix')
        if pu[36]!=0:raise ValueError('Producer uses alternate depth source')
        main=start['lightingPipeline'];aux=start['auxiliaryPipeline'];sa=sampler(aux,eye,5);sb=sampler(aux,eye,6);sc=sampler(main,eye,27)
        if sc['addressW']!=2 or sc['minFilter']!=1 or sc['reduction']:raise ValueError('Expected trilinear volume sampler')
        radiance=arr('producer',3,'after',1);volume=arr('consumer',30)
        if not np.array_equal(radiance,volume) or not np.array_equal(arr('producer',4,'after',1),arr('consumer',31)):raise ValueError('Producer and consumer differ')
        nz,ny,nx=volume.shape[:3]
        if not np.array_equal(pu[32:35],[nx,ny,nz-1]):raise ValueError('Volume dimension constants mismatch')
        low=arr('producer',4)[...,0];yy,xx=np.indices((ny,nx));u=(xx+.5)/nx;v=(yy+.5)/ny
        samples=[sample2(low,u+dx*pu[42],v+dy*pu[43],sa['minFilter'] if i<2 else sb['minFilter'],sa['reduction'] if i<2 else sb['reduction']) for i,(dx,dy) in enumerate(((-.5,.5),(.5,-.5),(1,1),(-1,-1)))]
        depth=1-np.minimum.reduce(samples);distance=pu[7]/(np.clip(depth,0,1)+pu[6])
        cutoff=np.minimum(nz-1,(np.sqrt(np.maximum(0,distance-.12)/(pu[37]-.12))*(nz-1)).astype(int)+3)
        changed=np.any(arr('producer',3,'before',1)!=radiance,axis=-1)
        outside=np.arange(nz)[:,None,None]>cutoff
        if np.any(changed&outside):raise ValueError('Observed writes outside reconstructed cutoff; model rejected')
        z=arr('consumer',8)[...,0];meta=next(x for x in report['records'] if x.get('stage')=='consumer' and x['eye']==eye and x['kind']=='lighting-s0-b8-e0-before')
        y,x=np.indices(z.shape);x=x+meta['x'];y=y+meta['y']
        if not np.allclose(push[6:8],[1/meta['width'],1/meta['height']],rtol=1e-6):raise ValueError('UV constants do not match depth extent')
        uvx=x*push[6];uvy=y*push[7];noise=arr('consumer',28)[...,0]
        if noise.shape!=(512,512):raise ValueError('Unexpected noise texture')
        if cu[90]+cu[91]>0:
            phase=int(cu[12])%32
            shift=np.floor(np.mod(np.array([phase*12664745,phase*9560333],dtype=np.float32)/np.float32(16777216),1)*np.float32(512)).astype(int)
            uvx=uvx+(noise[(y+shift[1])&511,(x+shift[0])&511]*2-1)*cu[92]
            uvy=uvy+(noise[(y+shift[1]+151)&511,(x+shift[0]+257)&511]*2-1)*cu[93]
        distance=cu[7]/(np.clip(1-z,0,1)+cu[6]);vz=nz*(.5*cu[94]+np.sqrt(np.maximum(0,distance-.12)*cu[99])*cu[95])-.5
        vx=uvx*nx-.5;vy=uvy*ny-.5;ix=np.floor(vx).astype(int);iy=np.floor(vy).astype(int);iz=np.floor(vz).astype(int)
        fx=vx-ix;fy=vy-iy;fz=vz-iz;stale=np.zeros(z.shape)
        for dz in (0,1):
            for dy in (0,1):
                for dx in (0,1):
                    cx=np.clip(ix+dx,0,nx-1);cy=np.clip(iy+dy,0,ny-1);cz=np.clip(iz+dz,0,nz-1)
                    weight=(fx if dx else 1-fx)*(fy if dy else 1-fy)*(fz if dz else 1-fz)
                    stale+=weight*(cz>cutoff[cy,cx])
        hand=(z>.12).astype('uint8');edge=cv2.distanceTransform(1-hand,cv2.DIST_L2,5);bg=(z>.005)&(z<.10);ring=bg&(edge>=2)&(edge<=16)
        hits=(stale>1e-5)&bg
        result=dict(eye=eye,backgroundPixels=int(bg.sum()),staleFootprintPixels=int(hits.sum()),handRingPixels=int(ring.sum()),staleHandRingPixels=int((hits&ring).sum()),maximumStaleWeight=float(stale[bg].max()),cutoffWriteValidation=True)
        results.append(result);np.save(folder/f'eye{eye}-actual-stale-weight.npy',stale)
    return {'results':results,'limits':'Reconstructed explicit LOD0 sampling. BC4 interpolation and floating-point boundary precision may differ slightly from GPU. Not a claim that stale contribution explains the visible broad smear.'}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('folder',type=Path);p.add_argument('--session',type=int,required=True);a=p.parse_args();r=analyze(a.run,a.folder,a.session);(a.folder/'actual-footprint.json').write_text(json.dumps(r,indent=2));print(json.dumps(r))
