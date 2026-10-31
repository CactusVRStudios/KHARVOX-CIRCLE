"""r208: bounded ranges for the verified a5e424f70d8fb9aa geometry consumer.
Discovery is only a prefix; attribution requires decoding executed indirect commands,
56-byte draw records and their physical model metadata. No stable NPC indices assumed.
"""
import argparse,json
from pathlib import Path
SHADER='a5e424f70d8fb9aa'
def plan(rows, stages, render_pass, instance=None, model=None, joint=None, vertex=None):
    if SHADER not in stages: raise ValueError('Unverified vertex shader')
    draws=[r for r in rows if r.get('pass')==render_pass and r.get('bindings') and not r.get('tagMismatch')]
    left=[r for r in draws if r.get('eye')==0]; right=[r for r in draws if r.get('eye')==1]
    if not left or not right: raise ValueError('Both eyes required')
    token=left[0]['token']; backend=left[0]['backend']
    if not token or any(r['token']!=token or r['backend']!=backend+r['eye'] for r in draws): raise ValueError('Mixed stereo pair')
    fields={8:(0,65520)}
    if instance is not None: fields[15]=(instance*64,64)
    if model is not None: fields[12]=(model*4,4)
    if joint is not None: fields[13]=(joint*48,48*256)
    if vertex is not None: fields[14]=(vertex*12,12*512)
    ranges=set()
    for r in draws:
        bindings={b['binding']:b for b in r['bindings'] if b.get('setNumber')==0 and b.get('element')==0}
        for key,(offset,size) in fields.items():
            b=bindings.get(key)
            if not b or not b.get('resolved') or not b.get('generation'): raise ValueError(f'Unresolved binding {key}')
            size=b['size'] if size is None else size
            if offset<0 or size<=0 or size>65536 or offset+size>b['size']: raise ValueError(f'Out-of-bounds binding {key}')
            ranges.add((b['buffer'],b['offset']+offset,size,b['generation']))
    # Merge adjacent/overlapping indirect windows, preserving resource generations.
    merged=[]
    for b,o,n,g in sorted(ranges,key=lambda v:(v[0],v[3],v[1])):
        if merged and merged[-1][0]==b and merged[-1][3]==g and o<=merged[-1][1]+merged[-1][2] and max(o+n,merged[-1][1]+merged[-1][2])-merged[-1][1]<=65536:
            old=merged[-1];merged[-1]=(b,old[1],max(old[1]+old[2],o+n)-old[1],g)
        else: merged.append((b,o,n,g))
    if len(merged)>16: raise ValueError('More than 16 ranges; narrow the consumer')
    return merged
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path)
    p.add_argument('--pipeline',required=True);p.add_argument('--session',type=int,required=True);p.add_argument('--pass',dest='render_pass',type=int,required=True)
    for n in ('instance','model','joint','vertex'): p.add_argument('--'+n,type=int)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    cat=[json.loads(l) for l in (a.run/'shaders/pipelines.jsonl').read_text().splitlines()]
    stages={s['module'] for c in cat if c.get('pipeline')==a.pipeline for s in c['stages']}
    rows=[r for l in (a.run/'gpu-bindings.jsonl').read_text().splitlines() if (r:=json.loads(l)).get('session')==a.session and r.get('pipeline')==a.pipeline]
    result=plan(rows,stages,a.render_pass,a.instance,a.model,a.joint,a.vertex)
    a.out.write_text(''.join(f'{b} {o} {n} {g}\n' for b,o,n,g in result))
    print(f'{len(result)} ranges; {4*len(result)} immutable slots per bracketed stereo sample. Validate NPC attribution and table-prefix coverage before interpreting.')


