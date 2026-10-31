"""Reflect actual SPIR-V image operations and join them to live descriptor bindings.
Uses the Khronos SPIRV-Headers core grammar. Names are evidence, not stage labels.
"""
import argparse,json,struct
from pathlib import Path
from collections import Counter

def reflect(path,grammar):
    data=path.read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
    if words[0]!=0x07230203:raise ValueError('Not SPIR-V')
    instructions=[];definitions={};decorations={};i=5
    while i<len(words):
        n,opcode=words[i]>>16,words[i]&65535
        if not n or i+n>len(words):raise ValueError('Malformed instruction')
        spec=grammar.get(opcode,{});name=spec.get('opname',str(opcode));args=words[i+1:i+n];instructions.append((name,args))
        for index,operand in enumerate(spec.get('operands',[])):
            if operand['kind']=='IdResult':definitions[args[index]]=(name,args);break
        if name=='OpDecorate':decorations.setdefault(args[0],{})[args[1]]=args[2:]
        i+=n
    def roots(ident,seen=frozenset()):
        if ident in seen:return set()
        seen=seen|{ident};op,args=definitions.get(ident,('',()))
        if op=='OpVariable':return {ident}
        if op in ('OpLoad','OpAccessChain','OpInBoundsAccessChain','OpPtrAccessChain','OpCopyObject','OpSampledImage','OpImage'):
            return roots(args[2],seen)
        if op=='OpSelect':return roots(args[3],seen)|roots(args[4],seen)
        if op=='OpPhi':return set().union(*(roots(x,seen) for x in args[2::2]))
        return set()
    uses=[]
    for op,args in instructions:
        if op=='OpImageWrite':ident=args[0];access='write'
        elif op.startswith('OpImageSample') or op.startswith('OpImageSparseSample') or op in ('OpImageRead','OpImageFetch','OpImageGather','OpImageDrefGather','OpImageSparseRead','OpImageSparseFetch'):
            ident=args[2];access='read'
        else:continue
        bindings=[]
        for root in roots(ident):
            d=decorations.get(root,{})
            if 34 in d and 33 in d:bindings.append((d[34][0],d[33][0]))
        uses.append(dict(operation=op,access=access,bindings=sorted(bindings),unresolved=not bindings))
    counts=Counter(op for op,args in instructions)
    return dict(module=path.stem,imageOperations=uses,
                rayOperations={k:v for k,v in counts.items() if 'Ray' in k},
                comparisonSamples=sum(v for k,v in counts.items() if 'Dref' in k))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--session',type=int,required=True)
    p.add_argument('--grammar',type=Path,default=Path('logs/spirv.core.grammar.json'));p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    grammar={x['opcode']:x for x in json.loads(a.grammar.read_text())['instructions']}
    pipelines={x['pipeline']:x for x in map(json.loads,(a.run/'shaders/pipelines.jsonl').read_text().splitlines())}
    reflected={};draws=[]
    with (a.run/'gpu-bindings.jsonl').open() as f:
        for line in f:
            try:r=json.loads(line)
            except json.JSONDecodeError:continue
            if r.get('session')!=a.session or r.get('event') not in ('dispatch','dispatch_indirect','draw_indexed','CmdDrawIndexedIndirectCount'):continue
            stages=pipelines.get(r.get('pipeline'),{}).get('stages',[])
            for stage in stages:
                module=stage['module']
                if module not in reflected:reflected[module]=reflect(a.run/'shaders/modules'/f'{module}.spv',grammar)
                info=reflected[module];images=[]
                for b in r.get('bindings',[]):
                    if not b.get('imageViewLive') or not b.get('resolved'):continue
                    ops=[u for u in info['imageOperations'] if (b['setNumber'],b['binding']) in u['bindings']]
                    if ops:images.append(dict(binding=b,access=sorted({u['access'] for u in ops}),operations=sorted({u['operation'] for u in ops})))
                if images:draws.append(dict(serial=r['serial'],eye=r['eye'],backend=r['backend'],pipeline=r['pipeline'],module=module,images=images))
    a.out.write_text(json.dumps(dict(modules=reflected,draws=draws,limits='Static accesses may be conditional. No runtime branch execution, full-frame completeness or semantic shadow identity inferred.'),indent=2))
    for module,r in reflected.items():
        if r['comparisonSamples'] or r['rayOperations']:
            writes={(x['binding']['setNumber'],x['binding']['binding'],x['binding'].get('format')) for d in draws if d['module']==module for x in d['images'] if 'write' in x['access']}
            print(module,'depth comparisons',r['comparisonSamples'],'rays',r['rayOperations'],'outputs',sorted(writes))

if __name__=='__main__':main()
