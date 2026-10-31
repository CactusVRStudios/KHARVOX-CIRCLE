"""Read-only targeted hair pipeline inventory; never infers NPC ownership from draw count."""
import argparse, importlib.util, json
from pathlib import Path

s = importlib.util.spec_from_file_location('capture_data', Path(__file__).with_name('gpu-capture-data.py'))
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)
HAIR = {'58effbcec00dc285', 'a39cad8addc3c423', '914b3f91e69fc307'}

def analyze(rows, pipeline):
    if any(r['event'] in ('truncated', 'capture_rejected', 'submit_error') for r in rows):
        raise ValueError('Incomplete hair inventory')
    draws = [r for r in rows if r.get('pipeline') == pipeline and r.get('event') == 'draw_indexed']
    if not draws or {d['eye'] for d in draws} != {0, 1}:
        raise ValueError('No paired direct hair draws')
    token = draws[0]['token']
    left = next(d['backend'] for d in draws if d['eye'] == 0)
    if any(d['token'] != token or d['backend'] != left+d['eye'] for d in draws):
        raise ValueError('Mixed hair pair')
    output = []
    for d in draws:
        submits = [r for r in rows if r['event'] == 'submit' and r.get('cb') == d['cb'] and r.get('commandGeneration') == d['commandGeneration']]
        if len(submits) != 1 or submits[0].get('submitNumber') != 1 or submits[0].get('tagMismatch') or submits[0].get('descriptorChanged'):
            raise ValueError('Unverified hair submission')
        bindings = m.bindings(d)
        output.append(dict(eye=d['eye'], passNumber=d['pass'], drawNumber=d['drawNumber'],
            cb=d['cb'], commandGeneration=d['commandGeneration'],
            nonzeroDirectDraw=d['count'] > 0 and d['instances'] > 0,
            count=d['count'], instances=d['instances'],
            modelBuffer=bindings.get((0,4)), tileMetadata=bindings.get((0,9)),
            tileBuffer=bindings.get((0,10)), strandBuffer=bindings.get((0,11)),
            sceneDepth=bindings.get((0,7)), raster=d.get('raster'),
            attachments=d.get('attachments')))
    return dict(token=token, pipeline=pipeline, draws=output,
        drawCounts=[sum(d['eye']==e for d in output) for e in (0,1)],
        npcOwnership='unresolved: fragment model index is loaded through second push-constant physical pointer +16; graphics push state is not captured by r217',
        scope='Direct nonzero submitted commands; no proof of fragment coverage or Gina ownership. Inventory begins at first selected left-eye pipeline draw.')

if __name__ == '__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('run',type=Path);args=a.parse_args()
    if (args.run/'gina-measurement.lock').exists():raise ValueError('Do not overlap an active geometry measurement')
    catalog=m.capture.read(args.run/'shaders/pipelines.jsonl')
    pipelines=sorted({p['pipeline'] for p in catalog if any(s['module'] in HAIR for s in p['stages'])})
    if not pipelines:raise ValueError('No verified hair shader pipeline')
    reports=[]
    for i,pipeline in enumerate(pipelines):
        rows=m.capture.capture(args.run,pipeline)
        (args.run/f'hair-targeted-{i}-inventory.json').write_text(json.dumps(rows))
        try:reports.append(analyze(rows,pipeline))
        except ValueError as e:reports.append(dict(pipeline=pipeline,status='unverified',reason=str(e)))
    out=args.run/'hair-targeted-findings.json';out.write_text(json.dumps(reports,indent=2))
    print(out)
    for r in reports:print(r['pipeline'],r.get('drawCounts'),r.get('reason',r.get('npcOwnership')))
