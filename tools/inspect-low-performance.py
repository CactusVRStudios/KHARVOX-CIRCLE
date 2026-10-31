"""Read-only live preset audit. Usage: python tools/inspect-low-performance.py PID OUTPUT.json"""
import ctypes
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

root=Path(__file__).resolve().parents[1]
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
class Module(ctypes.Structure):
    _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',ctypes.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
k.Module32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Module)];k.Module32NextW.argtypes=[w.HANDLE,ctypes.POINTER(Module)]
snapshot=k.CreateToolhelp32Snapshot(0x18,int(sys.argv[1]));entry=Module();entry.dwSize=ctypes.sizeof(entry)
ok=k.Module32FirstW(snapshot,ctypes.byref(entry))
while ok and entry.szModule.lower()!='thegreatcircle.exe':ok=k.Module32NextW(snapshot,ctypes.byref(entry))
k.CloseHandle(snapshot)
assert ok,'Game module missing'
catalog=json.loads((root/'profiles/motor-builds.json').read_text())
digest=hashlib.sha256(Path(entry.szExePath).read_bytes()).hexdigest()
profile=next(p for p in catalog['profiles'] if p['sha256']==digest)
handle=k.OpenProcess(0x410,False,int(sys.argv[1]));assert handle
def read(at,size):
    out=ctypes.create_string_buffer(size);got=ctypes.c_size_t()
    assert k.ReadProcessMemory(handle,at,out,size,ctypes.byref(got)) and got.value==size
    return out.raw
def ptr(at):return struct.unpack('<Q',read(at,8))[0]
rows=[]
try:
    for name,value,canonical in re.findall(r'\{"([^"]+)", "([^"]+)", (0x[0-9a-f]+)\}',(root/'src/motor/low_performance.generated.hpp').read_text()):
        rva=int(catalog['addresses'][canonical]['steam'] if profile['store']=='steam' else canonical,16)
        wrapper=entry.modBaseAddr+rva
        assert read(ptr(wrapper+0x30),len(name)+1)==name.encode()+b'\0'
        internal=ptr(wrapper)
        current=read(ptr(internal),96).split(b'\0')[0].decode(errors='replace')
        integer,floating=struct.unpack('<if',read(internal+8,8))
        rows.append(dict(name=name,requested=value,current=current,integer=integer,floating=floating,matched=current==value))
finally:k.CloseHandle(handle)
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2))
print(f'{sum(r["matched"] for r in rows)}/{len(rows)} preset strings retained')
for row in rows:
    if not row['matched'] or row['name']=='r_enableDynamicGI':print(row)
