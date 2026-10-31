"""Executed indirect geometry identity discovery; bounded readbacks, no camera/input writes."""
import argparse,json,struct,importlib.util,collections
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);ap.add_argument('--pass',dest='pass_number',type=int,default=4);ap.add_argument('--label',default='discovery');ap.add_argument('--width',type=int,default=928);ap.add_argument('--height',type=int,default=992);a=ap.parse_args();run=a.run
s=importlib.util.spec_from_file_location('gpu',Path(__file__).with_name('gpu-capture-data.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
inv=json.load(open(run/'world-consumers-inventory.json'));cat=m.capture.read(run/'shaders/pipelines.jsonl');writer={c['pipeline'] for c in cat if any(s['module']=='2d357153fa50c442' for s in c['stages'])};assert len(writer)==1;pipeline=writer.pop()
# Model metadata is shader-layout specific. In 123fca's physical _139
# structure the model/joint-table index is member 16 at byte 100; byte 188
# belongs outside that structure and previously read unrelated float data.
# Never accept an in-range integer from an unverified shader as ownership.
model_offsets={'59d9a748b6815ee5':188,'123fca195c4dd714':100}
pipeline_offsets={c['pipeline']:next((model_offsets[s['module']] for s in c['stages'] if s['module'] in model_offsets),None) for c in cat}
ds=[d for d in inv if d['event']=='CmdDrawIndexedIndirectCount' and d['pass']==a.pass_number and d['renderWidth']==a.width];assert ds
b=m.bindings(next(d for d in ds if m.bindings(d).get((0,8))));table=b[(0,8)];addresses=list({(x['buffer'],x['generation']):x for x in inv if x['event']=='buffer_address' and int(x.get('address','0'),16)}.values())
ranges=[(table['buffer'],table['offset'],65520,table['generation']),(table['buffer'],table['offset']+65520,65520,table['generation'])]
def merge(ranges):
 out=[]
 for b,o,n,g in sorted(set(ranges),key=lambda x:(x[0],x[3],x[1])):
  if out and out[-1][0]==b and out[-1][3]==g and o<=out[-1][1]+out[-1][2]+4096 and max(o+n,out[-1][1]+out[-1][2])-out[-1][1]<=65536:
   old=out[-1];out[-1]=(b,old[1],max(o+n,old[1]+old[2])-old[1],g)
  else:out.append((b,o,n,g))
 return out
for attempt in range(4):
 f=m.ranges_file(run/f'{a.label}-ranges.txt',merge(ranges));rows=m.capture.capture(run,pipeline,f,['-SkinningInputs','-SkinningBatch','9','-PairedNpcConsumers','-PhysicalRanges','-Pass',str(a.pass_number),'-RenderWidth',str(a.width),'-RenderHeight',str(a.height)]);(run/f'{a.label}-capture-{attempt}.json').write_text(json.dumps(rows));cap=m.Capture(run,rows);draws=[d for d in rows if d['event']=='CmdDrawIndexedIndirectCount'];missing=[];hits=[];executed=0
 assert draws and len({d['token'] for d in draws})==1 and {d['eye'] for d in draws}=={0,1}
 for d in draws:
  table=m.bindings(d).get((0,8))
  if not table:missing.append({'reason':'missing table descriptor'});continue
  count=struct.unpack('<I',cap.read(d,d['countBuffer'],d['countOffset'],4))[0];assert count<=d['maxCount']
  for n in range(count):
   cmd=struct.unpack('<IIIiI',cap.read(d,d['indirect'],d['offset']+n*d['stride'],20))
   if not cmd[0] or not cmd[1]:continue
   executed+=1;ri=cmd[4]&0xffffff;offset=table['offset']+ri*56
   model_offset=pipeline_offsets.get(d['pipeline'])
   if model_offset is None:missing.append({'reason':'unverified shader metadata layout','pipeline':d['pipeline']});continue
   try:record=struct.unpack('<4i2Q6I',cap.read(d,table['buffer'],offset,56,table['generation']))
   except ValueError:
    ranges.append((table['buffer'],offset,56,table['generation']));missing.append({'reason':'table','index':ri});continue
   ptr=record[4];alloc=[v for v in addresses if int(v['address'],16)<=ptr and ptr+model_offset+4<=int(v['address'],16)+v['bytes']]
   if len(alloc)!=1:missing.append({'reason':'allocation','pointer':ptr});continue
   al=alloc[0];off=ptr-int(al['address'],16)
   try:idx=struct.unpack('<i',cap.read(d,al['buffer'],off+model_offset,4,al['generation']))[0]
   except ValueError:
    start=(off+model_offset)//65536*65536;ranges.append((al['buffer'],start,min(65536,al['bytes']-start),al['generation']));missing.append({'reason':'metadata','pointer':ptr});continue
   if not 0<=idx<1048576:missing.append({'reason':'unsupported metadata layout','pipeline':d['pipeline'],'pointer':ptr,'value':idx});continue
   hits.append({'eye':d['eye'],'pass':d['pass'],'pipeline':d['pipeline'],'drawNumber':d['drawNumber'],'command':n,'indexCount':cmd[0],'instanceCount':cmd[1],'record':ri,'modelPointer':hex(ptr),'modelIndex':idx,'modelIndexOffset':model_offset,'mvpIndex':record[0],'geometryMode':record[1]})
 print('attempt',attempt,'draws',len(draws),'executed',executed,'identified',len(hits),'missing',len(missing),'nextRanges',len(merge(ranges)),flush=True)
 # Empty indirect lists are valid captures, but cannot pass an object pilot.
 # A decoded index is not a live model identity until independently joined
 # to the native model/manager/index and the same capture's resources.
 result={'token':draws[0]['token'],'pass':a.pass_number,'draws':len(draws),'executed':executed,'hits':hits,'uncovered':missing,'complete':bool(executed) and not missing,'coverageComplete':not missing,'objectOwnershipVerified':False}
 (run/f'{a.label}-result.json').write_text(json.dumps(result,indent=2))
 if not missing:break
 if any(x['reason'] in ('allocation','missing table descriptor','unsupported metadata layout','unverified shader metadata layout') for x in missing):break
