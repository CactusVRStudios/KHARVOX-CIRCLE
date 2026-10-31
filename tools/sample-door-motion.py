"""Read-only world motion sampling. No game state or input writes."""
import argparse,ctypes,json,struct,time,math
from pathlib import Path
from ctypes import wintypes as w
ap=argparse.ArgumentParser();ap.add_argument('pid',type=int);ap.add_argument('census',type=Path);ap.add_argument('--seconds',type=int,default=60);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--instance',required=True);a=ap.parse_args()
k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=w.BOOL;k.CloseHandle.argtypes=[w.HANDLE]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise ctypes.WinError(ctypes.get_last_error())
def read(p,n):
 b=ctypes.create_string_buffer(n);got=ctypes.c_size_t();return b.raw if k.ReadProcessMemory(h,p,b,n,ctypes.byref(got)) and got.value==n else None
def u64(p):
 b=read(p,8);return struct.unpack('<Q',b)[0] if b else 0
models=[r for r in json.loads(a.census.read_text())['models'] if a.instance in r['instance']]
start=time.monotonic();samples=[]
try:
 for tick in range(a.seconds*20+1):
  delay=start+tick/20-time.monotonic()
  if delay>0:time.sleep(delay)
  for r in models:
   model=int(r['model'],16);c=int(r['committed'],16);manager=int(r['manager'],16);index=r['index'];ib=read(c+0x30,4)
   if u64(model+0x88)!=c or u64(c+0x28)!=manager or not ib or struct.unpack('<I',ib)[0]!=index:continue
   native=read(u64(manager+0x108)+index*60,60);matrix=read(u64(manager+0xc0)+index*64,64);origin=read(model+0x120,60)
   if native and matrix and origin:samples.append({'tick':tick,'model':r['model'],'native':list(struct.unpack('<15f',native)),'matrix':matrix.hex(),'origin':list(struct.unpack('<15f',origin))})
  if tick%300==0:print('sample seconds',tick/20,'models',len(models),flush=True)
 result=[]
 for r in models:
  ss=[s for s in samples if s['model']==r['model']]
  if not ss:continue
  delta=max(max(abs(x-y) for x,y in zip(s['native'],ss[0]['native'])) for s in ss)
  od=max(max(abs(x-y) for x,y in zip(s['origin'],ss[0]['origin'])) for s in ss)
  result.append({**r,'samples':len(ss),'maxAffineChange':delta,'maxModelPoseChange':od,'cpuMatrixStates':len({s['matrix'] for s in ss})})
 a.out.write_text(json.dumps({'pid':a.pid,'duration':a.seconds,'models':result,'samples':samples,'limits':'Unsynchronized read-only CPU samples; movement evidence only, not an eye-pair or GPU proof.'},indent=2))
 print('changed models',sum(r['maxAffineChange']>1e-5 or r['maxModelPoseChange']>1e-5 for r in result),flush=True)
finally:k.CloseHandle(h)


