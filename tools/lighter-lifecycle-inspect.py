"""Read-only native observation: PROCESS_VM_READ + QUERY_INFORMATION, never writes or hooks."""
import argparse,ctypes,json,struct,sys,math
from ctypes import wintypes as w
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))


ap=argparse.ArgumentParser();ap.add_argument('pid',type=int);ap.add_argument('--rva',action='append',default=[]);ap.add_argument('--address',action='append',default=[]);ap.add_argument('--bytes',type=int,default=128);ap.add_argument('--follow-rtti',action='store_true');ap.add_argument('--out',required=True);args=ap.parse_args()
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
class Module(ctypes.Structure):
    _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',ctypes.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
k.Module32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Module)];k.Module32NextW.argtypes=[w.HANDLE,ctypes.POINTER(Module)]
snapshot=k.CreateToolhelp32Snapshot(0x18,args.pid);entry=Module();entry.dwSize=ctypes.sizeof(entry);base=None
ok=k.Module32FirstW(snapshot,ctypes.byref(entry))
while ok:
    if entry.szModule.lower()=='thegreatcircle.exe':base=entry.modBaseAddr;break
    ok=k.Module32NextW(snapshot,ctypes.byref(entry))
k.CloseHandle(snapshot)
if not base:raise RuntimeError('Game module not found')
handle=k.OpenProcess(0x410,False,args.pid)
if not handle:raise ctypes.WinError(ctypes.get_last_error())
def read(address,size):
    buf=ctypes.create_string_buffer(size);received=ctypes.c_size_t()
    if not k.ReadProcessMemory(handle,address,buf,size,ctypes.byref(received)):return None
    return buf.raw[:received.value]
def rtti(address):
    first=read(address,8)
    if not first:return None
    vtable=struct.unpack('<Q',first)[0]
    if not base<=vtable<base+entry.modBaseSize:return None
    colptr=read(vtable-8,8)
    if not colptr:return None
    col=read(struct.unpack('<Q',colptr)[0],24)
    if not col:return None
    signature,offset,cdOffset,typeRva,hierarchy,selfRva=struct.unpack('<6I',col)
    if signature!=1 or not 0<typeRva<entry.modBaseSize:return None
    typename=read(base+typeRva+16,160)
    if not typename:return None
    name=typename.split(b'\0')[0].decode('ascii',errors='replace')
    if not name.startswith('.?A'):return None
    return {'type':name,'vtable_rva':hex(vtable-base),'subobject_offset':offset}

def u64(p):
 b=read(p,8);return struct.unpack('<Q',b)[0] if b else 0

# Read-only lifecycle capture. Discover current instances again to handle recycling.
import time
import re
log=Path(args.address[0])
rows=[]; candidates=[];deadline=time.monotonic()+180;nextscan=0;nextsave=0;known=set()
while time.monotonic()<deadline:
 now=time.monotonic()
 if now>=nextscan:
  nextscan=now+0.5
  with log.open('rb') as stream:
   stream.seek(max(0,log.stat().st_size-4000000) if known else 0)
   tail=stream.read().decode(errors='replace')
  known.update(int(x,16) for x in re.findall(r'motor-(?:carried-model: registered|held-effect-anchor: applied) [^\n]*lighter[^\n]*?model ([0-9A-F]+)',tail))
  candidates=list(known)[-128:]
 row={'time':time.time(),'models':[]}
 for m in candidates:
  asset=(read(u64(m+0x90),180) or b'').split(b'\0')[0].decode(errors='replace')
  if 'lighter' not in asset:continue
  name=(read(u64(m+0x98),256) or b'').split(b'\0')[0].decode(errors='replace')
  c=u64(m+0x88);g=u64(c+0x28);ib=read(c+0x30,4);i=struct.unpack('<I',ib)[0] if ib else 0
  def floats(p,n):
   b=read(p,n*4);return struct.unpack('<'+'f'*n,b) if b and len(b)==n*4 else None
  d=dict(model=hex(m),asset=asset,name=name,committed=hex(c),manager=hex(g),index=i,native=floats(m+0x120,15),source=floats(u64(g+0x108)+i*60,15),matrix=floats(u64(g+0xc0)+i*64,16))
  if asset.endswith('.prt'):
   d.update(particleOrigin=floats(m+0x4cc,3),particleAxis=floats(m+0x4e4,9),particleVelocity=floats(m+0x514,3),particleWorld=floats(m+0x550,16),view=floats(m+0x5a0,15),decl=hex(u64(m+0x498)))
  d['identityStable']=c==u64(m+0x88) and asset==(read(u64(m+0x90),180) or b'').split(b'\0')[0].decode(errors='replace') and name==(read(u64(m+0x98),256) or b'').split(b'\0')[0].decode(errors='replace')
  d['flags87']=(read(m+0x87,1) or b'\0')[0]
  d['firstPerson']=bool((read(m+0x183,1) or b'\0')[0]&2)
  d['interpolated']=floats(m+0xc0,15)
  row['models'].append(d)
 rows.append(row)
 if now>=nextsave:
  nextsave=now+10;Path(args.out).write_text(json.dumps(rows,allow_nan=True));print('samples',len(rows),'live models',len(row['models']),flush=True)
 time.sleep(.05)
Path(args.out).write_text(json.dumps(rows,allow_nan=True));k.CloseHandle(handle)
print('captured',len(rows),'samples; identities',sorted({(m['model'],m['name']) for r in rows for m in r['models']}))
