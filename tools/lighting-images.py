"""Request and validate bounded dispatch-boundary images. No UI automation.

Pipeline addresses are resolved from this launch's shader catalogue, never
reused across launches. Semantic labels require independent engine evidence.
"""
import argparse
import importlib.util
import json
from pathlib import Path


def rows(path):
    with path.open(encoding='utf-8-sig') as f:
        for line in f:
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue  # a concurrently appended final line is not evidence


def request(run, module, bindings, x, y, raster=False, selected_pipeline=None):
    launch = json.loads((run/'launch.json').read_text(encoding='utf-8-sig'))
    if not launch.get('diagnostic_temporal_images'):
        raise ValueError('Launch lacks diagnostic image allocation')
    if raster and not any('raster sampled-input capture supported' in p.read_text(encoding='utf-8',errors='replace') for p in run.glob('circle-*.log')):
        raise ValueError('Running DLL does not acknowledge raster image support; restart with the diagnostic build first')
    if not (0 <= x <= 1 and 0 <= y <= 1) or not 1 <= len(bindings) <= 8:
        raise ValueError('Invalid crop or binding count')
    targets = [tuple(map(int, b.split(':'))) for b in bindings]
    if any(len(t) != 3 or not 0 <= t[0] < 8 or t[1] < 0 or not 0 <= t[2] < 4096 for t in targets):
        raise ValueError('Expected set:binding:element')
    if len(set(targets)) != len(targets):
        raise ValueError('Duplicate targets')
    catalogue = list(rows(run/'shaders/pipelines.jsonl'))
    pipelines = {p['pipeline'] for p in catalogue if p.get('kind') == ('graphics' if raster else 'compute')
                 and any(s['module']==module and s['stage']==(16 if raster else 32) for s in p.get('stages', []))}
    if selected_pipeline is not None:
        pipelines = {p for p in pipelines if int(p,16)==int(selected_pipeline,16)}
    if len(pipelines) != 1:
        raise ValueError(f'Module has {len(pipelines)} pipelines; resolve ambiguity first')
    pipeline = next(iter(pipelines))
    if not (run/'shaders/modules'/f'{module}.spv').is_file():
        raise ValueError('Shader binary missing')
    evidence = set()
    for record in rows(run/'gpu-bindings.jsonl'):
        events=('draw_indexed','CmdDrawIndexedIndirect','CmdDrawIndexedIndirectCount') if raster else ('dispatch','dispatch_indirect')
        if record.get('pipeline') != pipeline or record.get('event') not in events:
            continue
        for b in record.get('bindings', []):
            key = (b.get('setNumber'), b.get('binding'), b.get('element'))
            if b.get('resolved') and b.get('imageViewLive') and b.get('baseMip') == 0 and b.get('baseLayer') == 0:
                evidence.add(key)
    if not set(targets) <= evidence:
        raise ValueError(f'Targets absent from live descriptor inventory: {set(targets)-evidence}')
    for name in ('temporal-images.request', 'lighting-images.targets', 'raster-images.enabled'):
        if (run/name).exists():
            raise ValueError('A request is already pending')
    text = f'{pipeline} {len(targets)}\n' + ''.join(f'{s} {b} {e}\n' for s,b,e in targets)
    (run/'lighting-images.targets').write_text(text, encoding='ascii')
    if raster:(run/'raster-images.enabled').write_text('1', encoding='ascii')
    (run/'temporal-images.request').write_text(f'{x} {y}\n', encoding='ascii')
    print(json.dumps({'armed': True, 'pipeline': pipeline, 'module': module, 'targets': targets,
                      'requiredSlots': 8+(2 if raster else 4)*len(targets), 'raster':raster, 'completed': False}))


def validate(records, session, temporal):
    records = [r for r in records if r.get('session') == session]
    cameras, standard = temporal.validate(records, session)
    starts = [r for r in records if r.get('event') == 'temporal_start']
    if len(starts) != 1 or not starts[0].get('lightingTargetCount'):
        raise ValueError('Build did not acknowledge lighting capture')
    count = starts[0]['lightingTargetCount']
    raster=starts[0].get('rasterImages',False)
    prefix='raster-' if raster else 'lighting-'
    bindings = [r for r in records if r.get('event') == 'lighting_binding' and r.get('kind','').startswith(prefix)]
    images = [r for r in records if r.get('event') == 'temporal_image' and r.get('kind', '').startswith(prefix)]
    done = {r['id']:r for r in records if r.get('event') == 'gpu_complete' and r.get('captureSession') == session}
    expected=(2 if raster else 4)*count
    if len(bindings) != expected or len(images) != expected:
        raise ValueError('Missing dispatch boundaries or descriptors')
    result = []
    seen = set()
    for x in images:
        key = (x['eye'], x['kind'])
        if key in seen or x.get('status') != 'recorded':
            raise ValueError('Duplicate or rejected image copy')
        seen.add(key)
        camera = cameras[x['eye']]
        if x['token'] != camera['token'] or x['backend'] != camera['backend']:
            raise ValueError('Lighting and DLSS use different eye frames')
        matching = [b for b in bindings if (b['eye'],b['kind']) == key and b.get('status') == 'resolved'
                    and b['pipeline'] == starts[0]['lightingPipeline'] and b['image'] == x['image']
                    and (b['cb'],b['commandGeneration']) == (x['cb'],x['commandGeneration'])]
        if len(matching) != 1:
            raise ValueError('Image not attributable to selected dispatch')
        if raster:
            selected=[r for r in records if r['event']=='raster_image_draw' and r['eye']==x['eye']
                      and r['token']==x['token'] and r['backend']==x['backend'] and r['pipeline']==starts[0]['lightingPipeline']
                      and (r['cb'],r['commandGeneration'],r['pass'])==(x['cb'],x['commandGeneration'],matching[0]['pass'])]
            if len(selected)!=1 or matching[0].get('captureBoundary')!='render-pass-end' or not x['after']:
                raise ValueError('Raster pass ownership or boundary not proven')
        completion = done.get(x['id'])
        if not completion or completion['bytes'] != x['bytes'] or completion['file'] != f"gpu-buffer-{x['id']}.bin":
            raise ValueError('GPU copy incomplete')
        submits = [r for r in records if r.get('event') == 'submit' and
                   (r.get('cb'),r.get('commandGeneration')) == (x['cb'],x['commandGeneration'])]
        if len(submits) != 1:
            raise ValueError('Missing or repeated command submission')
        if submits[0].get('descriptorChanged') or submits[0].get('tagMismatch') or submits[0].get('submitNumber',1)!=1:
            raise ValueError('Descriptor ownership changed before GPU submission')
        result.append((x,completion))
    for eye in (0,1):
        before = {x['kind'].removesuffix('-before'):x for x,_ in result if x['eye'] == eye and x['kind'].endswith('-before')}
        after = {x['kind'].removesuffix('-after'):x for x,_ in result if x['eye'] == eye and x['kind'].endswith('-after')}
        if raster:
            if before or len(after)!=count:raise ValueError('Missing raster pass images')
            continue
        if len(before) != count or before.keys() != after.keys():
            raise ValueError('Unpaired dispatch images')
        for key,a in before.items():
            b = after[key]
            if any(a[k] != b[k] for k in ('image','generation','cb','commandGeneration','format','x','y','cropWidth','cropHeight')):
                raise ValueError('Resource changed across dispatch')
            if a.get('cropDepth',1)!=b.get('cropDepth',1):
                raise ValueError('Volume depth changed across dispatch')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path)
    p.add_argument('--module');p.add_argument('--binding',action='append',default=[])
    p.add_argument('--raster',action='store_true',help='r315+: sampled graphics inputs at the containing render-pass end')
    p.add_argument('--pipeline',help='Select a current-run pipeline when the module has multiple variants; still requires live binding evidence')
    p.add_argument('--x',type=float,default=.5);p.add_argument('--y',type=float,default=.62)
    p.add_argument('--session',type=int);p.add_argument('--out',type=Path)
    a=p.parse_args()
    if a.module:
        request(a.run,a.module,a.binding,a.x,a.y,a.raster,a.pipeline);return
    if a.session is None or a.out is None:p.error('Validation requires --session and --out')
    spec=importlib.util.spec_from_file_location('temporal',Path(__file__).with_name('analyze-temporal-images.py'))
    temporal=importlib.util.module_from_spec(spec);spec.loader.exec_module(temporal)
    records=validate(list(rows(a.run/'gpu-bindings.jsonl')),a.session,temporal)
    import numpy as np
    a.out.mkdir(parents=True,exist_ok=True);report=[]
    for x,done in records:
        pixels=temporal.decode(a.run,x,done)
        name=f"eye{x['eye']}-{x['kind']}.npy";np.save(a.out/name,pixels)
        report.append(dict(x,array=name,nonfinite=int((~np.isfinite(pixels)).sum())))
    (a.out/'report.json').write_text(json.dumps({'validated':True,'images':report,
        'limits':'Selected GPU resource identities proven; semantics and motion-corresponding errors require analysis. Raster inputs are captured after their containing render pass, not at individual pixel execution. Compute before-images may be undefined for a fresh output.'},indent=2))
    print(f'Validated {len(records)} completed pass images plus eight DLSS-boundary images')


if __name__=='__main__':main()
