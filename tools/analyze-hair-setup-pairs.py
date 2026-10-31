"""Compare CPU hair setup placement by exact model identity and adjacent eye frames."""
import argparse, collections, importlib.util, json, math, struct
from pathlib import Path
s=importlib.util.spec_from_file_location('view_history',Path(__file__).with_name('analyze-view-history.py'))
v=importlib.util.module_from_spec(s);s.loader.exec_module(v)

def analyze(rows):
    groups=collections.defaultdict(lambda:collections.defaultdict(list))
    for r in rows:
        if r['stage']==2:groups[(r['token'],r['model'],r['asset'],r['view'])][r['eye']].append(r)
    pairs=[];rejected=0
    for (token,model,asset,view),eyes in groups.items():
        if set(eyes)!={0,1} or any(len(x)!=1 for x in eyes.values()):rejected+=1;continue
        l,r=eyes[0][0],eyes[1][0]
        if ((r['frame']-l['frame'])&0xffffffff)!=1:rejected+=1;continue
        a,b=(struct.unpack_from('<15f',x['model_data']) for x in (l,r))
        if not all(math.isfinite(x) for x in (*a,*b)):raise ValueError('Nonfinite hair placement')
        d=[y-x for x,y in zip(a,b)]
        pairs.append(dict(token=token,model=hex(model),view=hex(view),asset=asset,leftFrame=l['frame'],rightFrame=r['frame'],
            positionDistance=math.sqrt(sum(x*x for x in d[:3])),positionDelta=d[:3],
            axisMaximum=max(abs(x) for x in d[3:12]),scaleMaximum=max(abs(x) for x in d[12:15])))
    return dict(validAdjacentSameModelPairs=len(pairs),rejectedGroups=rejected,
        nonzeroPositionPairs=sum(p['positionDistance']>1e-6 for p in pairs),
        maximumPositionDistance=max((p['positionDistance'] for p in pairs),default=None),
        maximumAxisDifference=max((p['axisMaximum'] for p in pairs),default=None),pairs=pairs,
        scope='CPU model+0x120 hair setup inputs; exact token/model/asset/view, unique eye records, adjacent frames. Frontend identity, not independent GPU ownership or pixel coverage.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);a=p.parse_args()
    result=analyze(v.read(a.capture));out=a.capture.with_suffix('.hair-pairs.json');out.write_text(json.dumps(result,indent=2))
    print(out);print(json.dumps({k:x for k,x in result.items() if k!='pairs'}))
