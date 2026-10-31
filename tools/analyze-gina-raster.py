"""Resolve static/dynamic raster state and compare same-pair pass metadata.

Metadata-only indirect calls do not prove execution or Gina ownership. This
report deliberately distinguishes them from independently identified GPU draws.
"""
import argparse, collections, json
from pathlib import Path

SCALARS={2:'lineWidth',3:'depthBias',5:'depthBounds',1000267000:'cullMode',
 1000267001:'frontFace',1000267006:'depthTestEnable',1000267007:'depthWriteEnable',
 1000267008:'depthCompareOp',1000267009:'depthBoundsTestEnable',
 1000267010:'stencilTestEnable',1000377001:'rasterizerDiscardEnable',1000377002:'depthBiasEnable'}
HAIR={'58effbcec00dc285','a39cad8addc3c423','914b3f91e69fc307'}
KEYS=('session','token','backend','eye','cb','commandGeneration','drawNumber','pass')
REQUIRED=('cullMode','frontFace','depthTestEnable','depthWriteEnable','depthCompareOp',
 'depthBiasEnable','depthBias','depthBoundsTestEnable','depthBounds','rasterizerDiscardEnable','stencilTestEnable')

def effective(pipeline, draw):
 static=pipeline.get('rasterStatic');live=draw.get('raster')
 if static is None or live is None:raise ValueError('Missing static or dynamic raster metadata')
 states=set(static['dynamicStates']);result={k:v for k,v in static.items() if k!='dynamicStates'};unknown=[]
 for state,key in SCALARS.items():
  if state in states:
   if key not in live['dynamic']:unknown.append(key);result.pop(key,None)
   else:result[key]=live['dynamic'][key]
 for key,base,countstate in [('viewports',0,1000267003),('scissors',1,1000267004)]:
  countkey='viewportCount' if key=='viewports' else 'scissorCount'
  if countstate in states:
   result[countkey]=live['dynamic'].get(countkey)
  count=result.get(countkey)
  source=live.get(key,{}) if base in states or countstate in states else static.get(key,{})
  if not isinstance(count,int) or not 0<count<=32:unknown.append(countkey);result[key]={}
  else:
   result[key]={str(i):source.get(str(i)) for i in range(count)}
   if any(v is None for v in result[key].values()):unknown.append(key)
 unknown.extend(k for k in REQUIRED if k not in result)
 # Untracked dynamic states must remain visible, even when not needed for
 # the narrow viewport/depth-bias comparison (e.g. blend/stencil operations).
 unsupported=sorted(states-set(SCALARS)-{0,1,1000267003,1000267004})
 return dict(values=result,missing=sorted(set(unknown)),untrackedDynamicStates=unsupported)

def analyze(rows,result,catalog):
 token=result['token'];pipelines={p['pipeline']:p for p in catalog};draws=[r for r in rows if r['event']=='raster_draw']
 if any(r['event'] in ('raster_truncated','truncated','submit_error') for r in rows):raise ValueError('Incomplete raster census')
 if not draws or any(d['token']!=token for d in draws):raise ValueError('Missing or mixed raster eye pair')
 frames={e:{d['backend'] for d in draws if d['eye']==e} for e in (0,1)}
 if any(len(f)!=1 for f in frames.values()) or next(iter(frames[1]))!=next(iter(frames[0]))+1:raise ValueError('Nonadjacent raster backends')
 submits={(r['cb'],r['commandGeneration'],r['eye'],r['backend']):r for r in rows if r['event']=='submit' and r.get('token')==token}
 mapped={};groups=collections.defaultdict(lambda:{0:[],1:[]})
 for d in draws:
  submit=submits.get((d['cb'],d['commandGeneration'],d['eye'],d['backend']))
  if not submit or submit.get('tagMismatch') or submit.get('submitNumber')!=1 or not d['primary']:raise ValueError('Unverified raster command submission')
  if not d.get('attachments',{}).get('known') or any(v.get('live') is False for v in d['attachments'].get('views',[]) if v.get('imageView') not in ('0x0',None)):raise ValueError('Unknown or stale raster attachments')
  p=pipelines.get(d['pipeline'])
  if p is None:raise ValueError('Missing raster pipeline catalog entry')
  state=effective(p,d);mapped[tuple(d[k] for k in KEYS)]=(d,state)
  attachments=d.get('attachments') or {};formats=tuple(v.get('format') for v in attachments.get('views',[]))
  # Do not match pass ordinals across unrelated command buffers. Compare
  # state sets for shader pipeline + target formats + dimensions, explicitly
  # a group comparison, not an invented per-object/depth correspondence.
  groups[(d['pipeline'],formats,d['renderWidth'],d['renderHeight'])][d['eye']].append((d,state))
 verified=[]
 for h in result['hits']:
  identity=h.get('drawIdentity',{});pair=mapped.get(tuple(identity.get(k) for k in KEYS))
  if pair is None:raise ValueError('Identified Gina draw missing from same-pair raster census')
  d,state=pair
  if state['missing']:raise ValueError('Incomplete effective raster state on Gina: '+str(state['missing']))
  verified.append(dict(eye=h['eye'],recordIndex=h['recordIndex'],drawIdentity=identity,pipeline=h['pipeline'],effective=state,attachments=d['attachments']))
 output=[]
 for (pipeline,formats,w,h),eyes in groups.items():
  p=pipelines[pipeline];mods=[s['module'] for s in p['stages']];allrows=eyes[0]+eyes[1]
  state_sets={e:sorted({json.dumps(s['values'],sort_keys=True) for _,s in eyes[e]}) for e in (0,1)}
  depth=any(v.get('aspectMask',0)&2 for d,_ in allrows for v in (d.get('attachments') or {}).get('views',[]))
  output.append(dict(pipeline=pipeline,shaders=mods,hairShader=bool(HAIR&set(mods)),depthAttachment=depth,
   formats=formats,width=w,height=h,drawCalls=[len(eyes[e]) for e in (0,1)],
   passes={e:sorted({d['pass'] for d,_ in eyes[e]}) for e in (0,1)},
   effectiveStateSetsEqual=bool(eyes[0] and eyes[1]) and state_sets[0]==state_sets[1],
   states={e:[json.loads(v) for v in state_sets[e]] for e in (0,1)},
   missing=sorted({x for _,s in allrows for x in s['missing']}),
   untrackedDynamicStates=sorted({x for _,s in allrows for x in s['untrackedDynamicStates']}),
   execution='recorded commands; indirect execution and NPC identity unverified outside identified Gina draws'))
 return dict(token=token,backends={e:next(iter(f)) for e,f in frames.items()},rasterDraws=len(draws),
  ginaRasterVerified=True,gina=verified,groups=output,
  scope='Viewport/scissor/cull/depth state at recorded commands, same token and adjacent backends. Cross-pass groups are not proof of Gina ownership, shader output, depth contents or fragment coverage.')

if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);a.add_argument('capture',type=Path);a.add_argument('result',type=Path);a.add_argument('--output',type=Path);args=a.parse_args()
 rows=json.loads(args.capture.read_text());result=json.loads(args.result.read_text());catalog=[json.loads(x) for x in (args.run/'shaders/pipelines.jsonl').read_text().splitlines() if x]
 report=analyze(rows,result,catalog);output=args.output or args.capture.with_name(args.capture.stem+'-raster.json');output.write_text(json.dumps(report,indent=2));print(output)
