"""Temporary live comparison: writes six validated stage flags and restores them in finally."""
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
handle=k.OpenProcess(0x438,False,args.pid)
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
k.WriteProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.WriteProcessMemory.restype=w.BOOL
model=int(args.address[0],0);decl=u64(model+0x498);table=u64(decl+0xc8)
asset=(read(u64(model+0x90),160) or b'').split(b'\0')[0]
instance=(read(u64(model+0x98),200) or b'').split(b'\0')[0]
assert asset==b'props/lighter/particle_prop_lighter_01.prt' and b'actor_player_' in instance and instance.endswith(b'-flame')
count=struct.unpack('<i',read(decl+0xd0,4))[0];assert count==6
saved=[]
def write(address,data):
 n=ctypes.c_size_t();b=ctypes.create_string_buffer(data)
 if not k.WriteProcessMemory(handle,address,b,len(data),ctypes.byref(n)) or n.value!=len(data):raise ctypes.WinError(ctypes.get_last_error())
try:
 for i in range(count):
  at=table+i*0x1538+0x4c8;old=read(at,1);assert old==b'\x01';saved.append((at,old));write(at,b'\x00')
 print('LIVE COMPARISON ACTIVE: lockToView disabled for 45 seconds',flush=True)
 time.sleep(45)
finally:
 if u64(model+0x498)==decl and u64(decl+0xc8)==table:
  for at,old in saved:write(at,old)
  print('RESTORED original six stage flags',flush=True)
 else:print('Original asset allocation no longer present; no stale writes',flush=True)
 k.CloseHandle(handle)
Path(args.out).write_text('Temporary lockToView comparison completed; original flags restored when allocation remained valid.')
