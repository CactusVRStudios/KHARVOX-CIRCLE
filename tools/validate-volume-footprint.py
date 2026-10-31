"""Validate same-eye producer/consumer volumes and immutable GPU constants."""
import argparse,importlib.util,json
from pathlib import Path

def load(name,file):
    s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
lighting=load('lighting','lighting-images.py');temporal=load('temporal','analyze-temporal-images.py')

def validate_constants(rows,session,images):
    rows=[r for r in rows if r.get('session')==session]
    start=next(r for r in rows if r.get('event')=='temporal_start')
    main=start['lightingPipeline'];aux=start['auxiliaryPipeline'];result=[]
    for pipeline,kinds in ((main,('s0-b0','push-reference-prefix')),(aux,('s0-b0',))):
        for eye in (0,1):
            anchor=next(x for x in rows if x.get('event')=='lighting_binding' and x.get('pipeline')==pipeline and x['eye']==eye)
            for kind in kinds:
                pair=[]
                for after in (False,True):
                    matches=[x for x in rows if x.get('event')=='lighting_constants' and x.get('pipeline')==pipeline and x['eye']==eye and x['kind']==kind and x['after']==after]
                    if len(matches)!=1:raise ValueError('Missing or duplicate constants')
                    x=matches[0]
                    if x['status']!='recorded' or any(x[k]!=anchor[k] for k in ('token','backend','cb','commandGeneration')):raise ValueError('Constants not from image dispatch')
                    done=[d for d in rows if d.get('event')=='gpu_complete' and d.get('captureSession')==session and d.get('id')==x['id']]
                    if len(done)!=1 or done[0]['bytes']!=x['bytes']:raise ValueError('Incomplete constants GPU copy')
                    pair.append(x);result.append((x,done[0]))
                if any(pair[0][k]!=pair[1][k] for k in ('buffer','generation','offset','size')):raise ValueError('Constant allocation changed')
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--session',type=int,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    rows=list(lighting.rows(a.run/'gpu-bindings.jsonl'));primary=lighting.validate(rows,a.session,temporal)
    start=next(x for x in rows if x.get('session')==a.session and x.get('event')=='temporal_start')
    auxrows=[]
    for row in rows:
        x=dict(row)
        if x.get('event') in ('temporal_image','lighting_binding') and x.get('kind','').startswith('lighting-'):continue
        if x.get('kind','').startswith('aux-lighting-'):x['kind']=x['kind'][4:]
        if x.get('event')=='temporal_start':x.update(lightingPipeline=start['auxiliaryPipeline'],lightingTargetCount=3)
        auxrows.append(x)
    auxiliary=lighting.validate(auxrows,a.session,temporal)
    constants=validate_constants(rows,a.session,primary+auxiliary)
    import numpy as np
    a.out.mkdir(parents=True,exist_ok=True);report=[]
    for label,items in (('consumer',primary),('producer',auxiliary)):
        for x,d in items:
            name=f"{label}-eye{x['eye']}-{x['kind']}.npy";np.save(a.out/name,temporal.decode(a.run,x,d));report.append(dict(x,array=name,stage=label))
    for x,d in constants:
        raw=(a.run/d['file']).read_bytes()
        if len(raw)!=x['bytes']:raise ValueError('Truncated constants payload')
        name=f"{'consumer' if x['pipeline']==start['lightingPipeline'] else 'producer'}-eye{x['eye']}-{x['kind']}-{'after' if x['after'] else 'before'}.bin"
        (a.out/name).write_bytes(raw);report.append(dict(x,file=name))
    (a.out/'report.json').write_text(json.dumps({'validated':True,'records':report,'limit':'Sampling semantics and producer cutoff must still be reconstructed.'},indent=2))
    print('Validated producer, consumer, both eyes, DLSS boundaries and GPU constants')

if __name__=='__main__':main()
