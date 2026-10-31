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
model=int(args.address[0],0);rows=[];deadline=time.monotonic()+60
while time.monotonic()<deadline:
 asset=(read(u64(model+0x90),180) or b'').split(b'\0')[0].decode(errors='replace')
 if asset!='props/lighter/particle_prop_lighter_01.prt':break
 runtime=u64(model+0x650);manager=u64(runtime+8);entries=u64(runtime+0x10);raw=read(runtime+0x18,4);n=struct.unpack('<i',raw)[0] if raw else 0
 ids=[]
 if 0<n<=128:
  for stageNumber in range(n):
   b=read(entries+stageNumber*0x38,0x38)
   if b:
    stage,idx=struct.unpack_from('<ii',b);ids.append(idx)
 table=u64(manager+0x20f40);raw=read(manager+0x20f50,4);count=struct.unpack('<i',raw)[0] if raw else 0
 def f(p,n):
  b=read(p,n*4);return struct.unpack('<'+'f'*n,b) if b else None
 row=dict(time=time.time(),model=hex(model),ids=ids,native=f(model+0x120,12),view=f(model+0x5a0,12),queueCount=count,records=[])
 if 0<count<32768:
  b=read(table,count*0xf8)
  if b:
   for i in range(count):
    rec=b[i*0xf8:(i+1)*0xf8];idx=struct.unpack_from('<i',rec,0xec)[0]
    if idx>=0 and idx in ids:row['records'].append(dict(index=idx,origin=struct.unpack_from('<3f',rec),quaternion=struct.unpack_from('<4f',rec,0x20),raw=rec.hex()))
  row['identityStable']=asset==(read(u64(model+0x90),180) or b'').split(b'\0')[0].decode(errors='replace') and runtime==u64(model+0x650)
  row['headerStable']=count==struct.unpack('<i',read(manager+0x20f50,4) or bytes(4))[0] and table==u64(manager+0x20f40)
 rows.append(row);time.sleep(.1)
Path(args.out).write_text(json.dumps(rows));k.CloseHandle(handle)
print('samples',len(rows),'with matching GPU records',sum(bool(r['records']) for r in rows))

