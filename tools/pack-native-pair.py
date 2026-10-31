"""Pack completed native L/R captures without shifting or synthesizing pixels."""
import argparse,json,math,struct
from pathlib import Path
from PIL import Image
import numpy as np

ap=argparse.ArgumentParser()
ap.add_argument('--left',required=True,type=Path)
ap.add_argument('--right',required=True,type=Path)
ap.add_argument('--left-rig',required=True,type=Path)
ap.add_argument('--right-rig',required=True,type=Path)
ap.add_argument('--out',required=True,type=Path)
a=ap.parse_args()
l=json.loads(a.left_rig.read_text(encoding='utf-8-sig'))
r=json.loads(a.right_rig.read_text(encoding='utf-8-sig'))
for record in (l,r):
    assert record['native_pause'] and record['mismatches']==0,'pause/copy integrity failed'
for key in ('latched_owner','display_time','eye_positions','eye_orientations','base_camera','fov_x','fov_y'):
    assert l[key]==r[key],f'the capture epochs differ: {key}'
assert l['verified_eye_copies'][0]>0 and r['verified_eye_copies'][1]>0,'each eye must complete a native copy'
assert np.linalg.norm(np.array(r['eye_cameras'][1][:3])-l['eye_cameras'][0][:3])>0.01,'native camera positions must differ'
li=Image.open(a.left).convert('RGBA');ri=Image.open(a.right).convert('RGBA')
assert li.size==ri.size
left=np.array(li);right=np.array(ri)
assert not np.array_equal(left,right),'duplicated mono images are rejected'
w,h=li.size
assert w*h*8<=64*1024*1024
header=struct.pack('<8sIII2f6f8f',b'CIRSTP01',w,h,1,l['fov_x'],l['fov_y'],
                   *sum(l['eye_positions'],[]),*sum(l['eye_orientations'],[]))
pixels=np.concatenate((left,right),axis=1)
temporary=a.out.with_suffix('.tmp')
temporary.write_bytes(header+pixels.tobytes());temporary.replace(a.out)
# Keep provenance separate from donor's mono transport sidecars: each source is one eye.
delta=np.abs(left[:,:,:3].astype(float)-right[:,:,:3])
report={'left':str(a.left.resolve()),'right':str(a.right.resolve()),'width':w,'height':h,
        'native_pause':True,'same_capture_epoch':l['display_time'],'native_left_camera':l['eye_cameras'][0],
        'native_right_camera':r['eye_cameras'][1],'actual_xr_ipd_m':float(np.linalg.norm(np.array(l['eye_positions'][1])-l['eye_positions'][0])),
        'different_pixels_fraction':float(np.any(delta!=0,axis=2).mean()),'mean_absolute_rgb_difference':float(delta.mean()),
        'note':'Pixel differences alone do not prove disparity. Scene correspondence and human depth checks are separate.',
        'scale_status':l['scale_status']}
a.out.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
Image.fromarray(pixels).save(a.out.with_suffix('.png'))
print(json.dumps(report,indent=2))
