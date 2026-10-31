import json,math,statistics,struct
from pathlib import Path
rows=json.loads(Path('logs/r281-lifecycle.json').read_text());a=[]
for row in rows:
 p=next((m for m in row['models'] if '/indy_lighter_' in m['asset']),None);c=next((m for m in row['models'] if 'particle_prop_lighter' in m['asset']),None)
 if not p or not c or not p.get('identityStable') or not c.get('identityStable'):continue
 mat=p['matrix'];target=[mat[i*4+3]+.008*mat[i*4]+.05*mat[i*4+2] for i in range(3)]
 a.append((row['time'],target,c['particleOrigin']))
report={'samples':len(rows),'validPairs':len(a),'durationSeconds':rows[-1]['time']-rows[0]['time'],'caveat':'Read-only external sequential samples, not a synchronized GPU-frame capture or final-pixel measurement.'}
for bucket in ['still','moving']:
 errors=[];lags=[];align=[];speeds=[]
 for i in range(1,len(a)-1):
  t,p,o=a[i];dt=a[i+1][0]-a[i-1][0]
  if dt<=0 or dt>.3:continue
  vel=[(a[i+1][1][j]-a[i-1][1][j])/dt for j in range(3)];speed=math.dist(vel,[0]*3)
  if (bucket=='still' and speed>.05) or (bucket=='moving' and speed<1):continue
  e=[o[j]-p[j] for j in range(3)];err=math.dist(e,[0]*3);errors.append(err);speeds.append(speed)
  if speed>1e-8 and err>1e-8:align.append(sum(e[j]*vel[j] for j in range(3))/(speed*err));lags.append(-sum(e[j]*vel[j] for j in range(3))/speed**2)
 report[bucket]={'samples':len(errors),'medianErrorMeters':statistics.median(errors),'medianSpeedMetersPerSecond':statistics.median(speeds),'medianDirectionCosine':statistics.median(align),'medianProjectedLagSeconds':statistics.median(lags)}
q=json.loads(Path('logs/r281-gpu-queue.json').read_text());v=[];w=[]
for r in q:
 if not(r.get('headerStable') and r.get('identityStable')):continue
 for x in r['records']:
  b=bytes.fromhex(x['raw']);v.append(math.dist(struct.unpack_from('<3f',b,0x10),[0]*3));w.append(math.dist(struct.unpack_from('<3f',b,0x70),[0]*3))
report['gpuQueue']={'samples':len(q),'stableRecords':len(v),'maxLinearSpeed':max(v),'maxAngularSpeed':max(w)}
Path('logs/r281-motion-analysis.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
