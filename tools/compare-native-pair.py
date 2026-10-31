"""Scene-patch correspondence check. Never alters the eye images sent to XR."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageFilter,ImageDraw
ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);a=ap.parse_args()
originals=[Image.open(a.run/f'native-{eye}.png').convert('RGB') for eye in ('left','right')]
images=[np.array(im.convert('L').filter(ImageFilter.GaussianBlur(2)),dtype=np.float32) for im in originals]
# Named scene content, excluding overlays and featureless sky. Locations use the
# captured 1920x1080 frame; all coordinates and scores remain in the JSON report.
points=[('near person shoulder',1215,646),('near person torso',1240,726),
        ('near person leg',1220,864),('near tree edge',634,646),('near tree base',668,884),
        ('distant trunk',874,370),('distant canopy',914,490),('distant rock',1005,735),
        ('path rock',1350,928),('right foliage',1630,710)]
radius=28;dxLimit=48;dyLimit=5
def match(source,target,x,y):
    patch=source[y-radius:y+radius,x-radius:x+radius]
    search=target[y-radius-dyLimit:y+radius+dyLimit,x-radius-dxLimit:x+radius+dxLimit]
    windows=np.lib.stride_tricks.sliding_window_view(search,patch.shape)
    p=patch-patch.mean();w=windows-windows.mean(axis=(-2,-1),keepdims=True)
    scores=(w*p).sum(axis=(-2,-1))/np.sqrt((w*w).sum(axis=(-2,-1))*(p*p).sum()+1e-12)
    iy,ix=np.unravel_index(scores.argmax(),scores.shape)
    return x+int(ix)-dxLimit,y+int(iy)-dyLimit,float(scores[iy,ix])
records=[]
for label,x,y in points:
    rx,ry,score=match(images[0],images[1],x,y)
    backX,backY,reverse=match(images[1],images[0],rx,ry)
    record={'scene_content':label,'left_xy':[x,y],'right_xy':[rx,ry],
            'horizontal_disparity_px':x-rx,'vertical_disparity_px':y-ry,
            'ncc':score,'reverse_ncc':reverse,'reverse_error_px':[backX-x,backY-y]}
    record['consistent']=score>0.95 and reverse>0.95 and abs(backX-x)<=2 and abs(backY-y)<=2
    records.append(record)
report={'method':'bidirectional normalized correlation of 56px scene patches; Gaussian filtering only for correspondence analysis',
        'correspondences':records,'note':'Varying positive scene disparity is the geometric check. No completed image was shifted to create stereo.'}
(a.run/'native-disparity.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
# Original, unfiltered eye crops with matching content marked for human review.
canvas=Image.new('RGB',(800,220*len(records)),(24,24,24));draw=ImageDraw.Draw(canvas)
for i,record in enumerate(records):
    x,y=record['left_xy'];rx,ry=record['right_xy'];top=i*220
    for eye in (0,1):
        crop=originals[eye].crop((x-170,y-90,x+170,y+90))
        canvas.paste(crop,(eye*400+30,top+30))
        px=(x if eye==0 else rx)-(x-170)+eye*400+30;py=(y if eye==0 else ry)-(y-90)+top+30
        draw.ellipse((px-5,py-5,px+5,py+5),outline=(255,210,0),width=2)
    draw.text((30,top+8),f"{record['scene_content']} | disparity {x-rx}px, vertical {y-ry}px, NCC {record['ncc']:.4f}, reverse {record['reverse_error_px']}",fill=(240,240,240))
canvas.save(a.run/'native-disparity-review.png')
print(json.dumps(report,indent=2))
