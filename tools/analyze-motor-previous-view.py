"""Check captured native per-eye history after the Motor CC8820 adapter."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import numpy as np
ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);args=ap.parse_args()
rows=[]
for path in sorted(args.run.glob('motor-previous-eye*.bin')):
    data=path.read_bytes();eye=int(path.name.split('eye')[1][0])
    def matrix(offset):return np.array(struct.unpack_from('<16f',data,offset-0x18790)).reshape(4,4)
    p,v,vp=matrix(0x18810),matrix(0x18890),matrix(0x18950)
    view,vp2,ivp=matrix(0x189d0),matrix(0x18a10),matrix(0x18a50)
    checks={
        'previous_vp_product':float(np.max(np.abs(p@v-vp))),
        'previous_unlocked_vp_product':float(np.max(np.abs(p@view-vp2))),
        'previous_inverse_vp_product':float(np.max(np.abs(vp2@ivp-np.eye(4)))),
    }
    # This run's VDXR FOV is known from actual XR layer records. Read it from
    # native primary projection captures, rather than hardcoding Quest optics.
    reference=args.run/f'motor-projections-eye{eye}-1.bin'
    expected=struct.unpack_from('<f',reference.read_bytes(),8)[0] if reference.exists() else None
    own=expected is not None and abs(float(p[0,2])-expected)<1e-4
    rows.append({'file':path.name,'eye':eye,'previous_p02':float(p[0,2]),
                 'expected_own_eye_p02':expected,'own_eye_optics':own,
                 'matrix_errors':checks,'pass':own and all(np.isfinite(x) and x<0.01 for x in checks.values())})
report={'snapshots':rows,'pass':bool(rows) and all(r['pass'] for r in rows),
        'limit':'Validates camera history only. Does not establish per-eye RT/DLSS image histories or corrected object motion.'}
(args.run/'motor-previous-view-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));sys.exit(0 if report['pass'] else 1)
