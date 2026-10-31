import sys
from pathlib import Path
sys.argv=['motor-read-memory.py','7304','--out','logs/r254-live-weapon.json']
"""Read-only native observation: PROCESS_VM_READ + QUERY_INFORMATION, never writes or hooks."""
import argparse,ctypes,json,struct,sys,math
from ctypes import wintypes as w
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import numpy as np
from extract_typeinfo import Image
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

snap=k.CreateToolhelp32Snapshot(0x18,args.pid);mod=Module();mod.dwSize=ctypes.sizeof(mod);ok=k.Module32FirstW(snap,ctypes.byref(mod));dll=0
while ok:
 if mod.szModule.lower()=='kharvoxcircle.dll':dll=mod.modBaseAddr;break
 ok=k.Module32NextW(snap,ctypes.byref(mod))
k.CloseHandle(snap)
assert dll
symbols=json.loads(Path('logs/r254-hands-symbols.json').read_text())
def sym(n):return dll+int(next(s['rva'] for s in symbols if s['name']=='circle::motor::hands::'+n),16)
u64=lambda a:struct.unpack('<Q',read(a,8))[0]
out={n:u64(sym(n)) for n in ['combatOwner','weaponKindEntity','combatLoadout']}
out['pose']=struct.unpack('<i',read(sym('equippedPose'),4))[0]
out['slot']=struct.unpack('<I',read(sym('combatSlot'),4))[0]
rig=u64(sym('appliedTargets')+360);out['rig']=hex(rig);out['rig_type']=rtti(rig)
out['arms']=[{'slot':h,'wrist':struct.unpack('<h',read(rig+0x720+h*0xa4+0x6a,2))[0],'weapon':struct.unpack('<h',read(rig+0x720+h*0xa4+0x64,2))[0],'reference':struct.unpack('<h',read(rig+0x720+h*0xa4+0x68,2))[0]} for h in range(2)]
Path(args.out).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2));k.CloseHandle(handle)
