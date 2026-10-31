"""Validated immutable GPU capture access, including bounded adjacent fragments."""
import json,importlib.util
from pathlib import Path
s=importlib.util.spec_from_file_location('capture',Path(__file__).with_name('capture-gina-geometry.py'));capture=importlib.util.module_from_spec(s);s.loader.exec_module(capture)
class Capture:
 def __init__(self,run,rows):
  self.run=Path(run);self.rows=rows;self.copies=[r for r in rows if r['event']=='gpu_copy'];self.data={};done={r['id']:r for r in rows if r['event']=='gpu_complete'}
  if any(c.get('status')!='recorded' for c in self.copies):raise ValueError('Native GPU copy rejected; incomplete capture')
  if len({r['id'] for r in self.copies})!=len(self.copies) or len(done)!=sum(r['event']=='gpu_complete' for r in rows):raise ValueError('Duplicate GPU copy/completion identity')
  if set(done)!={r['id'] for r in self.copies}:raise ValueError('Unmatched GPU completion')
  if any(r['event'] in ('truncated','capture_rejected','submit_error') for r in rows):raise ValueError('Incomplete capture')
  for c in self.copies:
   if c.get('status')!='recorded':raise ValueError('GPU copy failed: '+str(c))
   end=done.get(c['id'])
   if not end or end['captureSession']!=c['session']:raise ValueError('Missing completion')
   path=(self.run/end['file']).resolve()
   if path.parent!=self.run.resolve():raise ValueError('Invalid payload path')
   data=path.read_bytes()
   if len(data)!=c['copyBytes'] or len(data)!=end['bytes'] or capture.fnv64(data)!=int(end['hash'],16):raise ValueError('Corrupt payload')
   ss=[r for r in rows if r['event']=='submit' and r.get('cb')==c['cb'] and r.get('commandGeneration')==c['commandGeneration']]
   if len(ss)!=1 or ss[0].get('submitNumber')!=1 or ss[0].get('descriptorChanged') or ss[0].get('tagMismatch'):raise ValueError('Unstable submission')
   self.data[c['id']]=data
 def read(self,draw,buffer,offset,size,generation=None,phase='after_render_pass'):
  keys=('session','cb','commandGeneration','eye','token','backend')+(('pass',) if phase=='after_render_pass' else ('drawNumber',))
  copies=[c for c in self.copies if all(c[k]==draw[k] for k in keys) and c['phase']==phase and c['buffer']==buffer and (generation is None or c['generation']==generation)]
  result=bytearray(size);covered=bytearray(size)
  for c in copies:
   lo=max(offset,c['copyOffset']);hi=min(offset+size,c['copyOffset']+c['copyBytes'])
   if hi<=lo:continue
   piece=self.data[c['id']][lo-c['copyOffset']:hi-c['copyOffset']];a=lo-offset;b=hi-offset
   if any(covered[a:b]) and any(covered[a+i] and result[a+i]!=v for i,v in enumerate(piece)):raise ValueError('Conflicting overlapping copies')
   result[a:b]=piece;covered[a:b]=bytes([1])*(b-a)
  if not all(covered):raise ValueError(f'Missing bytes {buffer} {offset} {size}')
  return bytes(result)

def bindings(draw):return {(b['setNumber'],b['binding']):b for b in draw.get('bindings',[]) if b.get('element')==0}
def ranges_file(path,ranges):
 rows=sorted({tuple(row) for row in ranges});path=Path(path)
 if not 1<=len(rows)<=192:raise ValueError('Range count outside native limit')
 path.write_text(''.join(f'{b} {o} {n} {g}\n' for b,o,n,g in rows));return path
