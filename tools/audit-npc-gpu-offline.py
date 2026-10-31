"""Offline integrity and consumer audit of existing NPC GPU captures."""
import json,struct,hashlib,collections,bisect,argparse
from pathlib import Path

def fnv(data):
 h=14695981039346656037
 for v in data:h=((h^v)*1099511628211)&0xffffffffffffffff
 return h

def audit(run):
 rows=[];done={};updates=collections.defaultdict(list);submits=collections.defaultdict(list);draws=collections.defaultdict(list);sessions=collections.defaultdict(collections.Counter)
 with (run/'gpu-bindings.jsonl').open(encoding='utf-8-sig') as f:
  for line in f:
   r=json.loads(line);sessions[r.get('session')][r['event']]+=1
   if r['event']=='gpu_complete':done[r['id']]=r
   if r['event']=='descriptor_update':updates[r['set']].append(r['serial'])
   if r['event']=='submit':submits[(r['cb'],r['commandGeneration'])].append(r)
   if r.get('bindings'):draws[(r['cb'],r['commandGeneration'],r['pass'])].append(r)
   if r['event'] not in ('buffer_address','descriptor_update','begin','push','joint_selection','render_attachments'):rows.append(r)
 snapshots=[];payload={}
 for c in [r for r in rows if r['event']=='gpu_copy' and r.get('status')=='recorded']:
  issues=[];d=done.get(c['id']);data=b''
  if not d:issues.append('missing_completion')
  else:
   path=run/d['file']
   if not path.exists():issues.append('missing_payload')
   else:
    data=path.read_bytes()
    if len(data)!=c['copyBytes'] or len(data)!=d['bytes']:issues.append('size_mismatch')
    if fnv(data)!=int(d['hash'],16):issues.append('hash_mismatch')
    if d.get('captureSession')!=c['session']:issues.append('completion_session_mismatch')
  ss=submits[(c['cb'],c['commandGeneration'])]
  if not ss:issues.append('missing_submit')
  if any(s.get('submitNumber',1)!=1 for s in ss):issues.append('resubmitted')
  if any(s.get('descriptorChanged') or s.get('tagMismatch') for s in ss):issues.append('unstable_submission')
  consumers=[x for x in draws[(c['cb'],c['commandGeneration'],c['pass'])] if x['serial']<c['serial']]
  update_sets=[]
  for draw in consumers:
   for b in draw['bindings']:
    serials=updates[b['set']];start=bisect.bisect_right(serials,draw['serial'])
    if d and start<len(serials) and serials[start]<d['serial']:update_sets.append(b['set'])
  if update_sets:issues.append('descriptor_updated_before_completion_observation')
  payload[c['id']]=data
  snapshots.append(dict(**{k:c.get(k) for k in ('id','session','eye','token','backend','cb','commandGeneration','pass','phase','buffer','generation','copyOffset','copyBytes')},file=d.get('file') if d else None,sha256=hashlib.sha256(data).hexdigest(),issues=issues,consumerCount=len(consumers)))
 executed=[]
 for ds in draws.values():
  for draw in ds:
   if draw.get('event')!='CmdDrawIndexedIndirectCount':continue
   def get(buf,offset,size):
    matches=[c for c in snapshots if c['session']==draw['session'] and c['cb']==draw['cb'] and c['commandGeneration']==draw['commandGeneration'] and c['pass']==draw['pass'] and c['phase']=='after_render_pass' and c['buffer']==buf and c['copyOffset']==offset and c['copyBytes']==size]
    return matches[0] if len(matches)==1 else None
   count=get(draw['countBuffer'],draw['countOffset'],4);args=get(draw['indirect'],draw['offset'],draw['maxCount']*20)
   table=next((b for b in draw['bindings'] if b.get('setNumber')==0 and b.get('binding')==8 and b.get('element')==0),None)
   tab=get(table['buffer'],table['offset'],65520) if table else None
   if not count or not args:continue
   n=struct.unpack('<I',payload[count['id']])[0];rec=[];outside=[]
   if n<=draw['maxCount'] and tab:
    for i in range(n):
     command=struct.unpack_from('<IIIiI',payload[args['id']],i*20);index=command[4]&0xffffff
     if index*56+56>len(payload[tab['id']]):outside.append(index);continue
     entry=struct.unpack_from('<4i2Q6I',payload[tab['id']],index*56)
     rec.append(dict(index=index,command=command,record=entry))
   executed.append(dict(session=draw['session'],eye=draw['eye'],token=draw['token'],backend=draw['backend'],passNumber=draw['pass'],pipeline=draw['pipeline'],cb=draw['cb'],commandGeneration=draw['commandGeneration'],drawNumber=draw['drawNumber'],count=n,maxCount=draw['maxCount'],records=rec,outsideTablePrefix=outside,issues=sorted(set(count['issues']+args['issues']+(tab['issues'] if tab else ['missing_table'])))))
 return dict(run=str(run),sessions={k:dict(v) for k,v in sessions.items()},snapshots=snapshots,executed=executed,cpu=[r for r in rows if r['event'] in ('npc','npc_placement')],integrityIssues=dict(collections.Counter(e for s in snapshots for e in s['issues'])))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('runs',nargs='+',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 for run in a.runs:
  result=audit(run);name=run.parent.parent.name;(a.out/(name+'.json')).write_text(json.dumps(result,indent=2));print(name,len(result['snapshots']),'snapshots',len(result['executed']),'decoded consumers',result['integrityIssues'],flush=True)
