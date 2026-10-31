"""Validate a bounded pre/post Streamline capture; export raw HDR, depth and motion maps.
Requires numpy and Pillow. Cross-eye pixel differences alone are NOT a defect:
the cameras see different surfaces. Crops are identified in full-image coordinates.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image

FORMATS = {9:('u1',1),10:('i1',1),13:('u1',1),16:('u1',2),17:('i1',2),
           70:('<u2',1),71:('<i2',1),74:('<u2',1),76:('<f2',1),
           77:('<u2',2),78:('<i2',2),81:('<u2',2),98:('<u4',1),
           37: ('u1',4),38:('i1',4),43:('u1',4),44:('u1',4),50:('u1',4),
           83:('<f2',2),97:('<f2',4),100:('<f4',1),103:('<f4',2),126:('<f4',1),130:('<f4',1),129:('<u4',1),122:('<u4',1),58:('<u4',1),64:('<u4',1)}

def validate(rows, session):
    rows = [r for r in rows if r.get('session') == session]
    images = [r for r in rows if r['event']=='temporal_image']
    cameras = {r['eye']:r for r in rows if r['event']=='temporal_camera'}
    complete = {r['id']:r for r in rows if r['event']=='gpu_complete' and r.get('captureSession')==session}
    if set(cameras)!={0,1}: raise ValueError('Both camera identities required')
    l,r=cameras[0],cameras[1]
    if not l['token'] or l['token']!=r['token'] or l['backend']+1!=r['backend']:
        raise ValueError('Not one adjacent stereo pair')
    records=[]
    for eye in (0,1):
        evaluation=[r for r in rows if r['event']=='temporal_evaluate' and r['eye']==eye]
        if len(evaluation)!=1 or evaluation[0]['result']!=0: raise ValueError('Evaluation failed or ambiguous')
        identities=set()
        for kind in ('depth','motion','input','output'):
            found=[r for r in images if r['eye']==eye and r['kind']==kind]
            if len(found)!=1 or found[0]['status']!='recorded': raise ValueError(f'{eye}/{kind} missing: {found}')
            x=found[0]
            if x['token']!=l['token'] or x['backend']!=cameras[eye]['backend']:raise ValueError('Mixed render identities')
            if x['after'] != (kind=='output'): raise ValueError('Incorrect capture stage')
            identities.add((x['cb'],x['commandGeneration']))
            done=complete.get(x['id'])
            if not done or done['bytes']!=x['bytes']:raise ValueError('GPU copy incomplete')
            if done['file']!=f"gpu-buffer-{x['id']}.bin":raise ValueError('Unexpected capture path')
            records.append((x,done))
        if len(identities)!=1:raise ValueError('Stages use different command recordings')
        cb,generation=next(iter(identities))
        submits=[x for x in rows if x['event']=='submit' and x.get('cb')==cb and x.get('commandGeneration')==generation]
        if len(submits)!=1:raise ValueError('Expected exactly one observed command submission')
    return cameras,records

def unpack_pixels(a,fmt):
    if fmt in (13,74,81,98): return a.copy()  # Preserve every UINT mask bit.
    if fmt in (9,16,37,43,44,50): return a.astype(np.float32)/255
    if fmt in (10,17,38): return np.maximum(a.astype(np.float32)/127,-1)
    if fmt in (70,77): return a.astype(np.float32)/65535
    if fmt in (71,78): return np.maximum(a.astype(np.float32)/32767,-1)
    if fmt==129: return (a & 16777215).astype(np.float32)/16777215
    if fmt in (58,64):
        v=a[...,0]; shifts=(0,10,20) if fmt==64 else (20,10,0)
        return np.stack([(v>>shift)&1023 for shift in shifts],axis=-1).astype(np.float32)/1023
    if fmt==122:
        v=a[...,0];channels=[]
        for shift,bits in ((0,6),(11,6),(22,5)):
            channel=(v>>shift)&((1<<(bits+5))-1);exponent=channel>>bits;mantissa=channel&((1<<bits)-1)
            value=np.ldexp(1+mantissa.astype(np.float32)/(1<<bits),exponent.astype(np.int32)-15)
            value=np.where(exponent==0,np.ldexp(mantissa.astype(np.float32),-14-bits),value)
            value=np.where(exponent==31,np.where(mantissa==0,np.inf,np.nan),value);channels.append(value)
        return np.stack(channels,axis=-1)
    return a.astype(np.float32)

def decode_bc4(raw,width,height):
    blocks=np.frombuffer(raw,dtype=np.uint8).reshape((height+3)//4,(width+3)//4,8)
    result=np.empty((((height+3)//4)*4,((width+3)//4)*4,1),dtype=np.float32)
    for by in range(blocks.shape[0]):
        for bx in range(blocks.shape[1]):
            b=blocks[by,bx];a,c=int(b[0]),int(b[1])
            palette=[a,c]+([( (7-i)*a+i*c)/7 for i in range(1,7)] if a>c else [((5-i)*a+i*c)/5 for i in range(1,5)]+[0,255])
            bits=int.from_bytes(b[2:].tobytes(),'little')
            for i in range(16):result[by*4+i//4,bx*4+i%4,0]=palette[(bits>>(3*i))&7]/255
    return result[:height,:width]

def decode(run,x,done):
    fmt=x['format']
    if fmt==139:
        raw=(run/done['file']).read_bytes()
        expected=((x['cropWidth']+3)//4)*((x['cropHeight']+3)//4)*8
        if x.get('cropDepth',1)!=1 or len(raw)!=expected or len(raw)!=x['bytes']:raise ValueError('Invalid BC4 payload')
        return decode_bc4(raw,x['cropWidth'],x['cropHeight'])
    if fmt not in FORMATS:raise ValueError(f'Unsupported VkFormat {fmt}')
    dtype,channels=FORMATS[fmt]
    path=run/done['file']
    if path.stat().st_size!=x['bytes']:raise ValueError('Truncated GPU file')
    a=np.fromfile(path,dtype=dtype)
    depth=x.get('cropDepth',1)
    if a.size!=x['cropWidth']*x['cropHeight']*depth*channels:raise ValueError('Wrong crop stride')
    shape=(x['cropHeight'],x['cropWidth'],channels)
    if depth>1:shape=(depth,)+shape
    a=unpack_pixels(a.reshape(shape),fmt)
    if fmt in (44,50):a=a[...,[2,1,0,3]]
    return a

def analyze(run,session,out):
    rows=[json.loads(l) for l in (run/'gpu-bindings.jsonl').read_text().splitlines()]
    cameras,records=validate(rows,session)
    out.mkdir(parents=True,exist_ok=True)
    report={'session':session,'cameras':cameras,'images':[],
        'limits':'Pre/post Streamline boundary, not individual internal temporal shader dispatches. No direct L/R subtraction: stereo disparity and crop resolutions differ. Preview tone mapping is diagnostic, not the game tonemapper.'}
    arrays=[(x,decode(run,x,done)) for x,done in records]
    colors=[a[:,:,:3][np.isfinite(a[:,:,:3])] for x,a in arrays if x['kind'] in ('input','output')]
    white=max(.001,float(np.percentile(np.concatenate(colors),99.5)))
    motions=[a[:,:,:2]*np.array(cameras[x['eye']]['motionScale']) for x,a in arrays if x['kind']=='motion']
    finite_motion=np.concatenate([a[np.isfinite(a)] for a in motions])
    motion_bound=max(.00001,float(np.percentile(np.abs(finite_motion),99))) if finite_motion.size else 1.
    depths=np.concatenate([a[np.isfinite(a)] for x,a in arrays if x['kind']=='depth'])
    dlo,dhi=np.percentile(depths,[1,99]) if depths.size else (0,1)
    for x,a in arrays:
        vals=a[np.isfinite(a)]
        item=dict(x);item['nonfinite']=int((~np.isfinite(a)).sum())
        item['percentiles_1_50_99']=np.percentile(vals,[1,50,99]).tolist() if vals.size else None
        if x['kind']=='motion':
            mv=a[:,:,:2]*np.array(cameras[x['eye']]['motionScale'])
            preview=np.full((*a.shape[:2],3),.5);preview[:,:,:2]=mv/(2*motion_bound)+.5
            item['scaledMotionHalfRange']=motion_bound
        elif x['kind']=='depth':
            preview=np.repeat((a[:,:,:1]-dlo)/max(float(dhi-dlo),1e-10),3,axis=2)
            item['depthPreviewRange']=[float(dlo),float(dhi)]
        else:
            preview=np.maximum(a[:,:,:3],0)/white
            preview=np.power(preview/(1+preview),1/2.2);item['sharedHdrWhite']=white
        preview=np.uint8(np.clip(np.nan_to_num(preview),0,1)*255)
        path=out/f"{'L' if x['eye']==0 else 'R'}-{x['kind']}.png"
        Image.fromarray(preview).save(path);item['preview']=str(path.resolve());report['images'].append(item)
    (out/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False))
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('run',type=Path);ap.add_argument('--session',required=True,type=int);ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args();analyze(args.run,args.session,args.out);print(args.out/'report.json')
