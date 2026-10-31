"""Bounded r_useRayTracing A/B test; restores numeric lanes, never edits EXE or configs.
Only the exact catalogued game builds and name-validated CVar are accepted.
Use 0/1 for the render-use gate, NOT the device-support gate.
"""
import argparse, ctypes, hashlib, json, struct, time
from pathlib import Path
from ctypes import wintypes as w

ap = argparse.ArgumentParser()
ap.add_argument('pid', type=int)
ap.add_argument('--seconds', type=int, default=120, choices=range(1, 181))
ap.add_argument('--effect', default='rt-use', choices=['rt-use'])
ap.add_argument('--value', type=int, choices=[0,1], required=True)
ap.add_argument('--stop-file', type=Path)
ap.add_argument('--out', type=Path, required=True)
args = ap.parse_args()
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
k.CloseHandle.argtypes = [w.HANDLE]
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k.WriteProcessMemory.argtypes = k.ReadProcessMemory.argtypes
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]; k.CreateToolhelp32Snapshot.restype = w.HANDLE
class Module(ctypes.Structure):
    _fields_ = [('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',ctypes.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
k.Module32FirstW.argtypes = [w.HANDLE, ctypes.POINTER(Module)]
k.Module32NextW.argtypes = [w.HANDLE, ctypes.POINTER(Module)]
h = k.OpenProcess(0x438, False, args.pid)
if not h: raise ctypes.WinError(ctypes.get_last_error())
def read(at, count):
    buf=ctypes.create_string_buffer(count); done=ctypes.c_size_t()
    if not k.ReadProcessMemory(h,at,buf,count,ctypes.byref(done)) or done.value!=count: raise RuntimeError('Native read failed')
    return buf.raw
def write(at, data):
    buf=ctypes.create_string_buffer(data); done=ctypes.c_size_t()
    if not k.WriteProcessMemory(h,at,buf,len(data),ctypes.byref(done)) or done.value!=len(data): raise RuntimeError('Native write failed')
    if read(at,len(data))!=data: raise RuntimeError('Native readback mismatch')
def pointer(at): return struct.unpack('<Q',read(at,8))[0]
changed=[]; report={'pid':args.pid,'effect':args.effect,'seconds':args.seconds,'value':args.value,'started_epoch':time.time(),'changes':[],'restored':False}
try:
    path=ctypes.create_unicode_buffer(32768); count=w.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(count)): raise RuntimeError('Cannot identify process')
    digest=hashlib.sha256(Path(path.value).read_bytes()).hexdigest()
    rt_rva={
        '16eab3f4827d37ef93111173a831b0f668ef8bdffa4442c740f2574f23dcc120':0x44dad90,
        '68ef9be325743e304062728f667fdc6b3fba42243f9065a9a6b48b1b23418311':0x4519d90,
    }.get(digest)
    if rt_rva is None:raise RuntimeError('Unsupported executable')
    report['sha256']=digest

    snap=k.CreateToolhelp32Snapshot(0x18,args.pid); module=Module();module.dwSize=ctypes.sizeof(module);base=None
    try:
        ok=k.Module32FirstW(snap,ctypes.byref(module))
        while ok:
            if module.szModule.lower()=='thegreatcircle.exe':base=module.modBaseAddr;break
            ok=k.Module32NextW(snap,ctypes.byref(module))
    finally:k.CloseHandle(snap)
    if not base: raise RuntimeError('Module not found')
    # Exactly one consumer per comparison; the other effect stays native.
    planned=[]
    rva,name,offset,fmt,value = rt_rva,'r_useRayTracing',8,'<Q',struct.unpack('<Q',struct.pack('<if',args.value,float(args.value)))[0]
    for rva,name,offset,fmt,value in [(rva,name,offset,fmt,value)]:
        obj=base+rva
        if read(pointer(obj+0x30),len(name)+1)!=name.encode()+b'\0':raise RuntimeError('Cvar name mismatch')
        at=pointer(obj)+offset; original=read(at,struct.calcsize(fmt)); old=struct.unpack(fmt,original)[0]
        valid=(struct.unpack('<if',original)[0] in range(5) and struct.unpack('<if',original)[1]==float(struct.unpack('<if',original)[0])) if fmt=='<Q' else 0<=old<=1
        if not valid:raise RuntimeError('Unexpected original effect setting')
        planned.append((at,original,struct.pack(fmt,value)))
        report['changes'].append({'name':name,'address':hex(at),'before':old,'during':value})
    for at,original,replacement in planned:
        changed.append((at,original,replacement));write(at,replacement)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(args.effect,'comparison active for',args.seconds,'seconds; automatic restoration follows.',flush=True)
    until=time.monotonic()+args.seconds
    while time.monotonic()<until and not (args.stop_file and args.stop_file.exists()):
        for at,original,replacement in planned:
            if read(at,len(replacement))!=replacement:
                raise RuntimeError('Engine replaced diagnostic value; stopping comparison')
        time.sleep(0.25)
finally:
    errors=[]
    for at,original,replacement in reversed(changed):
        try:
            if read(at,len(replacement))==replacement:write(at,original)
            else:errors.append('Value changed externally; not overwritten at '+hex(at))
        except Exception as e:errors.append(str(e))
    report['restored']=bool(changed) and not errors;report['restore_errors']=errors
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    k.CloseHandle(h)
    print(json.dumps(report),flush=True)
