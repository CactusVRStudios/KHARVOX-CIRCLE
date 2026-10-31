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

model=int(args.address[0],0);world=u64(model+0x108)
table=u64(world+0x588);count=struct.unpack('<I',read(world+0x600,4))[0]
assert 0<count<=1048576
pointers=struct.unpack('<'+'Q'*count,read(table,count*8));out=[];eligible=0;unread=0
for p in pointers:
 if not p:continue
 b=read(p,0x190)
 if not b or len(b)<0x190:unread+=1;continue
 asset=(read(u64(p+0x90),256) or b'').split(b'\0')[0].decode('utf8',errors='replace')
 instance=(read(u64(p+0x98),256) or b'').split(b'\0')[0].decode('utf8',errors='replace')
 first=bool(b[0x183]&2)
 policy=not(first or 'actor_player' in instance or ':wear[' in instance or ':hair_' in instance or 'indiana' in asset) and asset.endswith('.idasset') and asset.startswith(('models/architecture/','models/setdressing/','models/gameplay/'))
 eligible+=int(policy)

 committed=u64(p+0x88);manager=u64(committed+0x28);ib=read(committed+0x30,4);index=struct.unpack('<I',ib)[0] if ib else None
 out.append(dict(model=hex(p),asset=asset,instance=instance,rtti=rtti(p),firstPerson=first,policyEligible=policy,committed=hex(committed),manager=hex(manager),index=index,placementSource=hex(u64(manager+0x108)+index*60) if manager and index is not None else None,position=struct.unpack_from('<3f',b,0x120)))
by_model={r['model']:r for r in out}
for r in out:
 raw_reference=read(int(r['model'],16)+0x1c8,8)
 valid_reference=raw_reference is not None and len(raw_reference)==8 and u64(int(r['model'],16)+0x88)==int(r['committed'],16)
 reference=struct.unpack('<Q',raw_reference)[0] if valid_reference else None
 r['referenceReadValid']=valid_reference
 r['referenceModel']=hex(reference) if reference is not None else None
 target=by_model.get(r['referenceModel'])
 r['referenceInSameCensus']=bool(target)
 if target:r['referenceAsset']=target['asset'];r['referenceInstance']=target['instance']
result=dict(pid=args.pid,world=hex(world),count=count,unread=unread,policyEligibleTotal=eligible,listHeaderStable=table==u64(world+0x588) and count==struct.unpack('<I',read(world+0x600,4))[0],models=out,limits='Read-only unsynchronized CPU snapshot. Name matching identifies candidates, not movement or runtime hook registration. referenceModel (+0x1c8) is a native render reference, not proof of entity-level attachment ownership; a zero reference does not imply an unbound entity.')
Path(args.out).write_text(json.dumps(result,indent=2));print('models',len(out),'stable',result['listHeaderStable']);from collections import Counter;print(Counter((r['rtti'] or {}).get('type') for r in out))
k.CloseHandle(handle)
