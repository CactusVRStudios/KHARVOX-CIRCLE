"""Paired native skinning/consumer pilot. Readiness requires complete indexed geometry."""
import argparse,json,struct,importlib.util,time,collections,math,hashlib
from pathlib import Path

def load(name,file):
 s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('capture_data','gpu-capture-data.py');j=load('jobs','skinning-jobs.py')
def chunk(b,o,n):return [(b['buffer'],o+i,min(65536,n-i),b['generation']) for i in range(0,n,65536)]
def model_allocation(addresses,pointer):
 matches=[a for a in addresses if a.get('address') and int(a['address'],16)<=pointer and pointer+192<=int(a['address'],16)+a['bytes']]
 if len(matches)!=1:raise ValueError('Missing or ambiguous physical model allocation')
 return matches[0]

def model_ranges(addresses,pointers):
 regions={};ranges=[]
 for pointer in sorted(set(pointers)):
  a=model_allocation(addresses,pointer);regions[(a['buffer'],a['generation'])]=a
  offset=pointer-int(a['address'],16);ranges+=chunk(a,offset,192)
 return list(regions.values()),ranges
def prepare(run,projection=False):
 inv=m.capture.capture(run,extra=['-NpcConsumers']);(run/'paired-inventory.json').write_text(json.dumps(inv))
 draws=[d for d in inv if d.get('event')=='CmdDrawIndexedIndirectCount']
 cpus=[c for c in inv if c['event']=='npc' and 'gina' in c['asset']]
 if not draws or not cpus:raise ValueError('Scene not ready: no character consumers/Gina')
 indices={c['index'] for c in cpus}
 if len(indices)!=1:raise ValueError('Ambiguous Gina identity')
 index=indices.pop();catalog=m.capture.read(run/'shaders/pipelines.jsonl');anchors={x['pipeline'] for x in catalog if any(s['module']=='59d9a748b6815ee5' for s in x['stages'])};groups=collections.Counter((x['eye'],x['cb']) for x in draws);main=max(groups,key=groups.get);d=max([d for d in draws if (d['eye'],d['cb'])==main and d['pipeline'] in anchors],key=lambda d:d['pass']);b=m.bindings(d)
 cat=m.capture.read(run/'shaders/pipelines.jsonl');pipelines={r['pipeline'] for r in cat if any(s['module']=='2d357153fa50c442' for s in r['stages'])}
 if len(pipelines)!=1:raise ValueError('Ambiguous writer shader')
 pipeline=pipelines.pop();writers=m.capture.capture(run,pipeline);(run/'paired-writer-inventory.json').write_text(json.dumps(writers))
 addresses={r['buffer']:r for r in writers if r['event']=='buffer_address'}
 metadata=[r for r in addresses.values() if r['bytes']==256000]
 if len(metadata)!=1:raise ValueError(f'Model metadata allocation ambiguous: {len(metadata)}')
 meta=metadata[0]
 # Select a pass by executed Gina commands, not by the last recorded draw call.
 # Late culling passes can contain the same pipelines with all counts zero.
 probe_ranges=chunk(b[(0,8)],b[(0,8)]['offset'],2*65520)+chunk(meta,0,meta['bytes'])
 probe=m.capture.capture(run,d['pipeline'],m.ranges_file(run/'paired-discovery-ranges.txt',probe_ranges),['-PhysicalRanges'])
 (run/'paired-discovery.json').write_text(json.dumps(probe));cap=m.Capture(run,probe);candidates=[];mvpIndices=set();base=int(meta['address'],16);otherModels=set()
 for draw in probe:
  if draw.get('event')!='CmdDrawIndexedIndirectCount':continue
  table=m.bindings(draw).get((0,8))
  if not table:continue
  count=struct.unpack('<I',cap.read(draw,draw['countBuffer'],draw['countOffset'],4))[0]
  if count>draw['maxCount']:raise ValueError('Discovery indirect count exceeds maximum')
  for n in range(count):
   command=struct.unpack('<IIIiI',cap.read(draw,draw['indirect'],draw['offset']+n*draw['stride'],20))
   if not command[0] or not command[1]:continue
   ri=command[4]&0xffffff
   record=struct.unpack('<4i2Q6I',cap.read(draw,table['buffer'],table['offset']+ri*56,56,table['generation']))
   if not base<=record[4] or record[4]+192>base+meta['bytes']:
    otherModels.add(record[4]);continue
   model_index=struct.unpack('<i',cap.read(draw,meta['buffer'],record[4]-base+188,4,meta['generation']))[0]
   if model_index==index and command[0] and command[1]:
    candidates.append(draw);mvpIndices.add(record[0])
 scopes=collections.defaultdict(set)
 for draw in candidates:scopes[(draw['pass'],draw['renderWidth'],draw['renderHeight'],draw['attachmentSignature'])].add(draw['eye'])
 paired=[scope for scope,eyes in scopes.items() if eyes=={0,1}]
 if not paired:raise ValueError('Discovery found no executed Gina consumer in both eyes')
 scope=max(paired,key=lambda x:x[0]);d=next(draw for draw in candidates if (draw['pass'],draw['renderWidth'],draw['renderHeight'],draw['attachmentSignature'])==scope);b=m.bindings(d)
 # The discovery intentionally captures only identity inputs; use the original
 # inventory descriptors for the other stable allocations.
 original=next(x for x in draws if x['pipeline']==d['pipeline']);b=m.bindings(original)
 ranges=chunk(b[(0,14)],b[(0,14)]['offset'],1536*1024)+chunk(b[(0,8)],b[(0,8)]['offset'],2*65520)
 ranges+=chunk(meta,0,meta['bytes'])
 extraModels,extraRanges=model_ranges(list(addresses.values()),otherModels);ranges+=extraRanges
 # The observed second model arena is 10,240,000 bytes. Capture its bounded
 # 256-KiB prefix as discovery data, not as identity evidence. Each consumed
 # pointer still needs unique address containment, generation and index bytes.
 for allocation in addresses.values():
  if allocation['bytes']==10240000 and allocation['buffer']!=meta['buffer']:
   extraModels.append(allocation);ranges+=chunk(allocation,0,256000)
 for binding,width in [(12,4),(16,64)]:ranges+=chunk(b[(0,binding)],b[(0,binding)]['offset']+index*width,width)
 args={}
 for w in writers:
  if w.get('event')!='dispatch_indirect':continue
  a=w['argumentBindings'][0];key=(a['buffer'],a['generation']);args[key]=max(args.get(key,0),w['offset']+12)
 ranges += [(buf,0,n,gen) for (buf,gen),n in args.items()]
 joint_buffers={}
 for w in writers:
  if w.get('event')=='dispatch_indirect':
   jb=m.bindings(w)[(0,9)];joint_buffers[(jb['buffer'],jb['generation'])]=jb
 for jb in joint_buffers.values():
  for joint in {c['joint'] for c in cpus}:ranges+=chunk(jb,jb['offset']+joint*48,128*48)
 ranges+=chunk(d['index'],1024*1024*1024,8*1024*1024)
 # The live 16427-vertex Gina surface is assigned to batch 9. Snapshot after
 # that final native batch so earlier jobs and the larger surface are covered.
 plan=dict(pipeline=pipeline,passNumber=d['pass'],renderWidth=d['renderWidth'],renderHeight=d['renderHeight'],attachmentSignature=d['attachmentSignature'],cpuIndex=index,metadata=meta,ranges=sorted(set(ranges)),indexRanges=[],batch=9,shader='2d357153fa50c442',rasterAudit=True)
 plan['modelAllocations']=list({(x['buffer'],x['generation']):x for x in [meta]+extraModels}.values())
 if projection:
  if len(mvpIndices)!=1:raise ValueError('Ambiguous independently discovered Gina MVP index')
  plan.update(mvpIndex=mvpIndices.pop(),vertexShaders={x['pipeline']:next((s['module'] for s in x['stages'] if s['stage']==1),'') for x in catalog})
 (run/'paired-plan.json').write_text(json.dumps(plan,indent=2));return plan

def acquire(run,plan):
 extra=['-ViewConstants','-MvpIndexPlusOne',str(plan['mvpIndex']+1)] if 'mvpIndex' in plan else []
 f=m.ranges_file(run/'paired-ranges.txt',plan['ranges']+plan['indexRanges']);rows=m.capture.capture(run,plan['pipeline'],f,['-SkinningInputs','-SkinningBatch',str(plan['batch']),'-PairedNpcConsumers','-PhysicalRanges','-Pass',str(plan['passNumber']),'-RenderWidth',str(plan['renderWidth']),'-RenderHeight',str(plan['renderHeight']),'-AttachmentSignature',str(plan['attachmentSignature'])]+extra);(run/'paired-capture.json').write_text(json.dumps(rows));return rows

def refine_coverage(run,plan,rows,result):
 # A failed capture is never repaired using bytes from another pair. Refine
 # only the next request, which must independently pass every validation.
 missing=[x['address'] for x in result['uncovered'] if x['kind']=='model']
 if any(x['kind']!='model' for x in result['uncovered']):raise ValueError('Unsupported uncovered draw-table range')
 addresses=[x for x in rows if x['event']=='buffer_address']
 # Payload sessions enumerate only explicitly requested allocations. The
 # discovery inventory supplies candidate addresses for the NEXT request;
 # native generation checks and next-pair metadata still validate them anew.
 inventory=run/'paired-writer-inventory.json'
 if inventory.exists():
  candidates={(x['buffer'],x['generation']):x for x in json.loads(inventory.read_text()) if x['event']=='buffer_address'}
  candidates.update({(x['buffer'],x['generation']):x for x in addresses});addresses=list(candidates.values())
 regions,ranges=model_ranges(addresses,missing)
 known={(x['buffer'],x['generation']):x for x in plan.get('modelAllocations',[plan['metadata']])}
 known.update({(x['buffer'],x['generation']):x for x in regions})
 old={tuple(x) for x in plan['ranges']};new=old|set(ranges)
 for h in result['hits']:
  if not h['vertexCoverage']:
   if not h.get('coverageError','').startswith('Missing bytes '+h['index']['buffer']+' '):raise ValueError('Unsupported missing vertex payload')
   # Exact missing index spans are small, and are validated afresh next pair.
   new.update(tuple(x) for x in result['indexRanges'] if x[0]==h['index']['buffer'])
 if new==old:raise ValueError('Coverage refinement made no progress')
 m.ranges_file(run/'paired-refined-ranges.txt',new)
 plan['ranges']=sorted(new);plan['modelAllocations']=list(known.values())
 (run/'paired-plan.json').write_text(json.dumps(plan,indent=2))

def analyze(run,rows,plan):
 cap=m.Capture(run,rows);draws=[d for d in rows if d.get('event')=='CmdDrawIndexedIndirectCount'];requests=set();hits=[];uncovered=[]
 tokens={d['token'] for d in draws}
 if len(tokens)!=1 or {d['eye'] for d in draws}!={0,1}:raise ValueError('Missing stereo consumer pair')
 frames={eye:{d['backend'] for d in draws if d['eye']==eye} for eye in (0,1)}
 if any(len(v)!=1 for v in frames.values()) or next(iter(frames[1]))!=next(iter(frames[0]))+1:raise ValueError('Nonadjacent stereo backends')
 token=next(iter(tokens));cpus=[c for c in rows if c['event']=='npc' and c['token']==token and c['index']==plan['cpuIndex'] and 'gina' in c['asset']]
 if {c['eye'] for c in cpus}!={0,1}:raise ValueError('Missing same-pair Gina identity')
 meta=plan['metadata'];base=int(meta['address'],16)
 for d in draws:
  b=m.bindings(d);table=b.get((0,8))
  if not table:continue
  count=struct.unpack('<I',cap.read(d,d['countBuffer'],d['countOffset'],4))[0]
  if count>d['maxCount']:raise ValueError('Invalid indirect count')
  if not count:continue
  args=cap.read(d,d['indirect'],d['offset'],(count-1)*d['stride']+20)
  for n in range(count):
   command=struct.unpack_from('<IIIiI',args,n*d['stride']);ri=command[4]&0xffffff
   if not command[0] or not command[1]:continue
   try:record=struct.unpack('<4i2Q6I',cap.read(d,table['buffer'],table['offset']+ri*56,56,table['generation']))
   except ValueError:uncovered.append(dict(kind='drawtable',index=ri));continue
   model=record[4]
   try:
    allocation=model_allocation(plan.get('modelAllocations',[meta]),model)
    live=[a for a in rows if a['event']=='buffer_address' and a['buffer']==allocation['buffer'] and a['generation']==allocation['generation'] and a['address']==allocation['address']]
    if not live:raise ValueError('Stale physical model allocation')
    model_index=struct.unpack('<i',cap.read(d,allocation['buffer'],model-int(allocation['address'],16)+188,4,allocation['generation']))[0]
   except ValueError as e:uncovered.append(dict(kind='model',address=model,reason=str(e)));continue
   if model_index!=plan['cpuIndex']:continue
   if command[1]!=1 or record[1]<2:raise ValueError('Unsupported Gina instancing/deformation path')
   cpu=next(c for c in cpus if c['eye']==d['eye']);world=b.get((0,16))
   if world and world['size']==6291456:
    world_data=cap.read(d,world['buffer'],world['offset']+model_index*64,64,world['generation'])
    if world_data!=bytes.fromhex(cpu['cpuMatrix']):raise ValueError('CPU/GPU Gina matrix mismatch')
   index=d['index'];width={0:2,1:4}.get(index['indexType'])
   if width is None:raise ValueError('Unsupported index type')
   first=index['offset']+command[2]*width;end=first+command[0]*width;aligned=first&~3;length=((end+3)&~3)-aligned
   for item in chunk(index,aligned,length):requests.add(item)
   hit=dict(eye=d['eye'],consumerStream=next(x for x in b.values() if x.get('size')==180393216),pipeline=d['pipeline'],recordIndex=ri,record=list(record),command=list(command),index=index,vertexCoverage=False,drawIdentity={k:d[k] for k in ('session','token','backend','eye','cb','commandGeneration','drawNumber','pass')},raster=d.get('raster'),attachments=d.get('attachments'))
   try:
    raw=cap.read(d,index['buffer'],aligned,length,index['generation']);offsets=j.indexed_offsets(raw,index['indexType'],(first-aligned)//width,command[0],command[3],record[1]);stream=next(x for x in b.values() if x.get('size')==180393216)
    xyz=[struct.unpack('<3f',cap.read(d,stream['buffer'],stream['offset']+o,12,stream['generation'])) for o in sorted(set(offsets))]
    hit.update(vertexCoverage=True,uniqueVertices=len(xyz),positions=[list(x) for x in xyz],vertexOffsets=sorted(set(offsets)))
   except ValueError as e:hit['coverageError']=str(e)
   if 'mvpIndex' in plan and hit['vertexCoverage']:
    hit['projection']=load('gina_projection','gina-projection.py').decode(cap,d,hit,cpu,rows,plan,b)
   hits.append(hit)
 if not hits:raise ValueError('No executed draw independently identifies Gina')
 links=link_writers(cap,rows,cpus,hits,plan) if all(h['vertexCoverage'] for h in hits) else []
 return dict(projectionRequested='mvpIndex' in plan,projectionVerified=all('projection' in h for h in hits),writerLinks=links,writerLinked=bool(links) and len(links)==len(hits),status='complete' if all(h['vertexCoverage'] for h in hits) and {h['eye'] for h in hits}=={0,1} and not uncovered else 'discovery',identityVerified=True,vertexIndexCoverageVerified=all(h['vertexCoverage'] for h in hits) and not uncovered,readyForUserTest=False,token=token,hits=hits,indexRanges=sorted(requests),uncovered=uncovered,copies=len(cap.copies))
def link_writers(cap,rows,cpus,hits,plan):
 candidates=[]
 for d in [r for r in rows if r['event']=='dispatch_indirect']:
  b=m.bindings(d);header=d.get('skinningHeader')
  if not header:raise ValueError('Missing batch header')
  def read(binding,size=None,phase='before_dispatch'):
   return cap.read(d,binding['buffer'],binding['offset'],size or binding['size'],binding['generation'],phase)
  head=read(header,20);lst=read(b[(1,1)]);records=read(b[(1,2)])
  if head!=read(header,20,'after_dispatch') or lst!=read(b[(1,1)],phase='after_dispatch') or records!=read(b[(1,2)],phase='after_dispatch'):raise ValueError('Job inputs changed at dispatch')
  boundaries=[r for r in rows if r['event']=='skinning_dispatch_boundary' and all(r.get(k)==d.get(k) for k in ('cb','commandGeneration','eye','token','backend','pipeline')) and r['drawNumber']<=d['drawNumber'] and r['jobSet']==b[(1,2)]['set'] and r['jobSetVersion']==b[(1,2)]['setVersion'] and r['push'][:16]==d['computePush'][:16]]
  for boundary in boundaries:
   arg=d['argumentBindings'][0]
   if boundary.get('indirect')!=arg['buffer']:raise ValueError('Indirect allocation changed within batch')
   raw=cap.read(d,arg['buffer'],boundary['offset'],12,arg['generation'],'before_dispatch')
   if raw!=cap.read(d,arg['buffer'],boundary['offset'],12,arg['generation'],'after_dispatch'):raise ValueError('Dispatch arguments changed')
   groups=struct.unpack('<3I',raw)
   for job in j.decode_jobs(bytes.fromhex(boundary['push']),bytes.fromhex(d['computePushValid']),head,lst,records,groups):candidates.append((d,b[(1,11)],boundary,job))
 links=[]
 for h in hits:
  r=h['record'];stream=h['consumerStream'];matches=[]
  for d,out,boundary,job in candidates:
   w=job['words']
   if d['eye']==h['eye'] and out['buffer']==stream['buffer'] and out['generation']==stream['generation'] and (w[1],w[2],w[3],w[10])==(r[6],r[7],r[10],r[1]-2):matches.append((d,out,boundary,job))
  if len(matches)!=1:raise ValueError(f'Gina draw has {len(matches)} matching executed skinning writers')
  d,out,boundary,job=matches[0]
  if job['jointBase'] not in {c['joint'] for c in cpus if c['eye']==h['eye']}:raise ValueError('Gina CPU and writer joint bases differ')
  maximum=0.;same=True
  for off,xyz in zip(h['vertexOffsets'],h['positions']):
   if not job['outputOffset']<=off or off+12>job['outputOffset']+job['outputBytes']:raise ValueError('Indexed vertex outside executed writer job')
   raw=cap.read(d,out['buffer'],out['offset']+off,12,out['generation'],'after_dispatch');v=struct.unpack('<3f',raw)
   if not all(math.isfinite(x) for x in (*v,*xyz)):raise ValueError('Nonfinite geometry')
   same &= raw==struct.pack('<3f',*xyz);maximum=max(maximum,math.dist(v,xyz))
  links.append(dict(eye=h['eye'],pipeline=h['pipeline'],recordIndex=h['recordIndex'],job=job['job'],batch=boundary['batch'],writerCommandBuffer=d['cb'],writerDispatch=boundary['drawNumber'],snapshotAfterDispatch=d['drawNumber'],jointBase=job['jointBase'],vertices=h['uniqueVertices'],writerConsumerEqual=same,maximumWriterConsumerDistance=maximum))
 return links

if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('run',type=Path);a.add_argument('--prepare',action='store_true');a.add_argument('--replay',action='store_true');args=a.parse_args();run=args.run.resolve()
 try:
  plan=prepare(run) if args.prepare else json.loads((run/'paired-plan.json').read_text());rows=json.loads((run/'paired-capture.json').read_text()) if args.replay else acquire(run,plan);result=analyze(run,rows,plan);(run/'paired-pilot.json').write_text(json.dumps(result,indent=2));print({k:v for k,v in result.items() if k not in ('hits','indexRanges')});print('Gina draws',len(result['hits']),'index ranges',len(result['indexRanges']))
 except Exception as e:
  (run/'paired-pilot.json').write_text(json.dumps(dict(status='blocked',readyForUserTest=False,reason=str(e)),indent=2));raise
