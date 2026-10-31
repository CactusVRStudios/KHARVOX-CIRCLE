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


import time
class MBI(ctypes.Structure):
 _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',w.DWORD),('PartitionId',w.WORD),('RegionSize',ctypes.c_size_t),('State',w.DWORD),('Protect',w.DWORD),('Type',w.DWORD)]
k.VirtualQueryEx.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.POINTER(MBI),ctypes.c_size_t];k.VirtualQueryEx.restype=ctypes.c_size_t
probe=json.loads(Path('logs/tutorial-ui-functions.json').read_text())
patterns={struct.pack('<Q',base+int(v['vtable'],16)):name for name,vs in probe['types'].items() for v in vs if v['subobject']==0}
start=time.monotonic();address=0;total=0;objects=[];strings=[]
while time.monotonic()-start<45 and total<4*1024**3:
 m=MBI()
 if not k.VirtualQueryEx(handle,address,ctypes.byref(m),ctypes.sizeof(m)):break
 address=int(m.BaseAddress or 0)+m.RegionSize
 if m.State!=0x1000 or m.Type!=0x20000 or m.Protect&0x101 or not m.Protect&0xee:continue
 for pos in range(int(m.BaseAddress),address,4*1024**2):
  if time.monotonic()-start>=45:break
  b=read(pos,min(4*1024**2,address-pos))
  if not b:continue
  total+=len(b)
  for pattern,name in patterns.items():
   at=b.find(pattern)
   while at>=0:
    p=pos+at;raw=read(p,360) or b''
    row={'address':hex(p),'type':name,'raw':raw.hex()}
    if name=='idLogicUISWFTextElement' and len(raw)>=288:
     row['stringId']=struct.unpack_from('<I',raw,232)[0]
     row['backupBytes']=raw[240:288].hex()
     for off in (240,248,256):
      ptr=struct.unpack_from('<Q',raw,off)[0];v=read(ptr,256) if ptr>0x10000 else None
      if v:row['candidateText'+str(off)]=v.split(b'\0')[0].decode('utf8',errors='replace')
    objects.append(row);at=b.find(pattern,at+8)
  for needle in (b'Mystery Note','Mystery Note'.encode('utf-16le')):
   at=b.find(needle)
   if at>=0:strings.append({'address':hex(pos+at),'bytes':b[max(0,at-64):at+128].hex()})
result={'pid':args.pid,'base':hex(base),'elapsed':time.monotonic()-start,'bytesRead':total,'objects':objects,'strings':strings,'limits':'Read-only bounded unsynchronized scan; vtable matches may include inactive objects.'}
Path(args.out).write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ('objects','strings')}));print('objects',len(objects),'strings',len(strings));k.CloseHandle(handle)
