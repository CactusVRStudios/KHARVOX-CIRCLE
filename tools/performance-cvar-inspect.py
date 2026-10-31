"""Read-only native observation: PROCESS_VM_READ + QUERY_INFORMATION, never writes or hooks."""
import argparse,ctypes,json,struct,sys,math
from ctypes import wintypes as w
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import numpy as np
from extract_typeinfo import Image
ap=argparse.ArgumentParser();ap.add_argument('pid',type=int);ap.add_argument('--xrefs',default='logs/r148-cvar-xrefs.json');ap.add_argument('--out',required=True);args=ap.parse_args()
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

import capstone
im=Image(entry.szExePath);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
refs=json.loads(Path(args.xrefs).read_text())['xrefs'];rows=[]
def ptr(p):
 b=read(p,8);return struct.unpack('<Q',b)[0] if b else 0
def string(p):return (read(p,400) or b'').split(b'\0')[0].decode('ascii',errors='replace')
for ref in refs:
 at=int(ref['xref_rva'],16);name=im.cstr(int(ref['string_rva'],16));ins=list(md.disasm(im.mem[at:at+65],at))
 for i in ins[1:]:
  if i.mnemonic=='mov' and i.operands[0].type==capstone.x86.X86_OP_MEM and i.operands[0].mem.base==capstone.x86.X86_REG_RIP and i.operands[1].type==capstone.x86.X86_OP_REG and i.reg_name(i.operands[1].reg)=='rax':
   wrapper=i.address+i.size+i.disp-0x30
   if string(ptr(base+wrapper+0x30))!=name:break
   internal=ptr(base+wrapper);v=read(internal+8,8)
   if not v:break
   integer,floating=struct.unpack('<if',v)
   row=dict(name=name,rva=hex(wrapper),internal=hex(internal),integer=integer,floating=floating,description=string(ptr(base+wrapper+0x40)))
   if row not in rows:rows.append(row)
   break
Path(args.out).write_text(json.dumps(rows,indent=2));k.CloseHandle(handle)
for row in rows:print(row)
