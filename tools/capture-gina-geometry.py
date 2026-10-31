"""r212 bounded Gina GPU payload series. Requires a running diagnostic release."""
import argparse,json,struct,subprocess,time,hashlib
from pathlib import Path
SHADER='59d9a748b6815ee5'
def read(path,offset=0):
    if not path.exists():return []
    with path.open(encoding='utf-8-sig') as f:f.seek(offset);text=f.read()
    return [json.loads(l) for l in text.split('\n')[:-1] if l]
def request(run,pipeline=None,ranges=None,extra=None):
    cmd=['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(Path(__file__).with_name('request-gpu-bindings.ps1')),'-RunDirectory',str(run)]
    cmd+=['-RangesFile',str(ranges)] if ranges else ['-Inventory']
    if pipeline:cmd+=['-Pipeline',pipeline]
    subprocess.run(cmd+(extra or []),check=True,stdout=subprocess.DEVNULL)
def capture(run,pipeline=None,ranges=None,extra=None):
    log=run/'gpu-bindings.jsonl';offset=log.stat().st_size if log.exists() else 0
    request(run,pipeline,ranges,extra);deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        time.sleep(.25);rows=read(log,offset)
        if any(r['event'] in ('truncated','capture_rejected','submit_error') for r in rows):raise RuntimeError('Capture rejected/truncated/submission error')
        copies=[r for r in rows if r['event']=='gpu_copy' and r.get('status')=='recorded'];done={r['id']:r for r in rows if r['event']=='gpu_complete'}
        if any(r['event']=='stop' for r in rows) and all(c['id'] in done for c in copies):return rows
    raise RuntimeError('No completed capture in 25 seconds')
def fnv64(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def validate(run,rows,plan):
    if any(r['event'] in ('truncated','capture_rejected','submit_error') for r in rows):raise RuntimeError('Incomplete capture log')
    sessions={r['session'] for r in rows if r.get('bindings') and r.get('pipeline')==plan['pipeline']}
    if len(sessions)!=1:raise RuntimeError('Mixed capture sessions')
    session=sessions.pop()
    copies=[r for r in rows if r['event']=='gpu_copy' and r.get('status')=='recorded'];done={r['id']:r for r in rows if r['event']=='gpu_complete'}
    draws=[r for r in rows if r.get('pipeline')==plan['pipeline'] and r.get('bindings')]
    if not copies or not draws:raise RuntimeError('No consumer payloads')
    if len(done)!=len([r for r in rows if r['event']=='gpu_complete']):raise RuntimeError('Duplicate completions')
    if len(copies)!=len({c['id'] for c in copies}):raise RuntimeError('Duplicate copies')
    token=draws[0]['token'];left=next((d['backend'] for d in draws if d['eye']==0),None)
    if left is None or any(d['token']!=token or d['backend']!=left+d['eye'] or d.get('tagMismatch') for d in draws):raise RuntimeError('Mixed eye pair')
    blobs={}
    for c in copies:
        completion=done.get(c['id']);path=run/completion['file'] if completion else None
        if c.get('session')!=session or c.get('phase')!='after_render_pass':raise RuntimeError('Wrong capture session/phase')
        if c.get('token')!=token or c.get('eye') not in (0,1) or c.get('backend')!=left+c['eye']:raise RuntimeError('Copy eye/token differs from consumer')
        if not c.get('generation') or not completion or completion.get('captureSession')!=session:raise RuntimeError('Missing generation/completion identity')
        if path.resolve().parent!=run.resolve():raise RuntimeError('Payload outside capture directory')
        if not path or not path.exists():raise RuntimeError('Missing completed GPU file')
        data=path.read_bytes()
        if len(data)!=c['copyBytes'] or len(data)!=completion['bytes']:raise RuntimeError('GPU payload size mismatch')
        if fnv64(data)!=int(completion.get('hash','0'),16):raise RuntimeError('GPU payload hash mismatch')
        blobs[c['id']]=data
        submissions=[s for s in rows if s['event']=='submit' and s.get('cb')==c['cb'] and s.get('commandGeneration')==c['commandGeneration']]
        if len(submissions)!=1 or submissions[0].get('submitNumber')!=1:raise RuntimeError('Missing, duplicate or repeated submission')
        if not any(s['event']=='submit' and s.get('cb')==c['cb'] and s.get('commandGeneration')==c['commandGeneration'] and not s.get('descriptorChanged') and not s.get('tagMismatch') for s in rows):raise RuntimeError('Missing stable submission')
    def blob(draw,buffer,offset,size,generation=None):
        found=[c for c in copies if c['cb']==draw['cb'] and c['commandGeneration']==draw['commandGeneration'] and c['pass']==draw['pass'] and c['buffer']==buffer and c['copyOffset']==offset and c['copyBytes']==size]
        if len(found)!=1:raise RuntimeError('Missing/ambiguous consumer range')
        if generation is not None and found[0]['generation']!=generation:raise RuntimeError('Stale buffer generation')
        if found[0]['eye']!=draw['eye'] or found[0]['token']!=draw['token']:raise RuntimeError('Copy consumer identity mismatch')
        return blobs[found[0]['id']]
    hits=[]
    for draw in draws:
        args=blob(draw,draw['indirect'],draw['offset'],draw['maxCount']*20)
        count=struct.unpack('<I',blob(draw,draw['countBuffer'],draw['countOffset'],4))[0]
        if count>draw['maxCount']:raise RuntimeError('Indirect count exceeds maximum')
        table=blob(draw,*plan['table'])
        for n in range(count):
            arg=struct.unpack_from('<IIIiI',args,n*20);index=arg[4]&0xffffff
            if index*56+56>len(table):continue
            record=struct.unpack_from('<4i2Q6I',table,index*56)
            if record[0]!=plan['instance']:continue
            for b,o,size,g in plan['ranges']:blob(draw,b,o,size,g)
            if not 2<=record[1] or (record[1]-2)*12>=720720:raise RuntimeError('Gina deformation selector outside captured prefix')
            hits.append(dict(eye=draw['eye'],passNumber=draw['pass'],recordIndex=index,record=list(record),drawNumber=draw['drawNumber']))
    if {h['eye'] for h in hits}!={0,1}:raise RuntimeError('Gina instance not identified in both eyes of captured table prefix')
    cpu=[r for r in rows if r['event']=='npc' and r.get('index')==plan['cpuIndex'] and 'gina' in r.get('asset','') and r.get('token')==token]
    if {r['eye'] for r in cpu}!={0,1}:raise RuntimeError('Missing same-pair Gina CPU identity')
    return dict(status='candidate_payload_complete',identityVerified=False,vertexIndexCoverageVerified=False,session=draws[0]['session'],token=token,copies=len(copies),hits=hits,files=[dict(file=done[c['id']]['file'],sha256=hashlib.sha256(blobs[c['id']]).hexdigest()) for c in copies])
def require_ready(run,plan):
    pilot_path=run/'gina-geometry-pilot.json'
    if not pilot_path.exists():raise RuntimeError('No verified pilot; long measurement remains disabled')
    pilot=json.loads(pilot_path.read_text())
    # A successful file transfer is not proof of NPC identity or vertex coverage.
    if not pilot.get('identityVerified') or not pilot.get('vertexIndexCoverageVerified'):
        raise RuntimeError('Not start-ready: independent GPU instance identity and indexed-vertex coverage are unverified. No long series started.')
    raise RuntimeError('Readiness gate pending implementation of producer-linked identity evidence; do not request a headset test.')

def prepare(run):
    rows=capture(run)
    indices={r['index'] for r in rows if r['event']=='npc' and 'gina' in r.get('asset','')}
    if len(indices)!=1:raise RuntimeError('Expected one Gina CPU instance')
    cpu=indices.pop();catalog=read(run/'shaders/pipelines.jsonl');pipelines={r['pipeline'] for r in catalog if any(s.get('module')==SHADER for s in r.get('stages',[]))}
    candidates={r['pipeline'] for r in rows if r.get('bindings') and r.get('pipeline') in pipelines}
    if len(candidates)!=1:raise RuntimeError(f'Ambiguous consumer pipelines: {candidates}')
    pipeline=candidates.pop();rows=capture(run,pipeline);draws=[r for r in rows if r.get('pipeline')==pipeline and r.get('bindings')]
    ranges=set();tables=set()
    for draw in draws:
        b={x['binding']:x for x in draw['bindings'] if x.get('setNumber')==0 and x.get('element')==0}
        for binding,relative,size in [(8,0,65520),(15,(cpu+20480)*64,64)]+[(14,i*65520,65520) for i in range(11)]:
            x=b[binding]
            if not x.get('resolved') or not x.get('generation') or relative+size>x['size']:raise RuntimeError('Unresolved or out-of-bounds target')
            item=(x['buffer'],x['offset']+relative,size,x['generation']);ranges.add(item)
            if binding==8:tables.add(item)
    if len(tables)!=1 or len(ranges)>16:raise RuntimeError('Unsupported rotating descriptor layout')
    plan=dict(release='r212',pipeline=pipeline,cpuIndex=cpu,instance=cpu+20480,instanceMapping='CPU instance + 20480; validate executed records; physical model index is separate',table=list(tables.pop()),ranges=sorted(ranges),shader=SHADER)
    (run/'gina-geometry-plan.json').write_text(json.dumps(plan,indent=2));(run/'gina-geometry-ranges.txt').write_text(''.join(f'{b} {o} {n} {g}\n' for b,o,n,g in plan['ranges']))
    result=validate(run,capture(run,pipeline,run/'gina-geometry-ranges.txt'),plan);(run/'gina-geometry-pilot.json').write_text(json.dumps(result,indent=2));print('CANDIDATE PAYLOAD COMPLETE. NOT READY: independent instance identity and full indexed-vertex coverage still required.',flush=True)
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);ap.add_argument('--prepare',action='store_true');ap.add_argument('--duration',type=int,default=180);a=ap.parse_args()
    if a.prepare:prepare(a.run)
    else:
        plan=json.loads((a.run/'gina-geometry-plan.json').read_text())
        require_ready(a.run,plan)
        state=dict(status='recording',samples=[],duration=a.duration);start=time.monotonic();out=a.run/f'gina-geometry-series-{time.strftime("%Y%m%d-%H%M%S")}.json'
        try:
            for i in range((a.duration+11)//12):
                time.sleep(max(0,start+i*12-time.monotonic()))
                result=validate(a.run,capture(a.run,plan['pipeline'],a.run/'gina-geometry-ranges.txt'),plan);result['elapsed']=time.monotonic()-start;state['samples'].append(result);out.write_text(json.dumps(state,indent=2));print(f'Sample {i+1}: session {result["session"]}, {result["copies"]} completed GPU ranges.',flush=True)
            time.sleep(max(0,start+a.duration-time.monotonic()));state['status']='complete'
        except Exception as ex:state['status']='failed';state['error']=str(ex);raise
        finally:out.write_text(json.dumps(state,indent=2));print(out,flush=True)
