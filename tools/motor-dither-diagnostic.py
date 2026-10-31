"""Bounded proximity-dither comparison; restore native cvar values in finally.

Only the fingerprinted Motor build and these three float consumers are supported.
No saved settings, camera, collision, RT or executable bytes are changed.
The near-model formula clamps (distance-end)/(start-end); negative endpoints
make all nonnegative distances opaque. Vegetation documents zero as disabled.
"""
import argparse, ctypes, hashlib, json, struct, time
from pathlib import Path
from ctypes import wintypes as w

ap = argparse.ArgumentParser()
ap.add_argument('pid', type=int)
ap.add_argument('--seconds', type=int, default=45, choices=range(1, 61))
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
changed=[]; report={'pid':args.pid,'seconds':args.seconds,'changes':[],'restored':False}
try:
    path=ctypes.create_unicode_buffer(32768); count=w.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(count)): raise RuntimeError('Cannot identify process')
    digest=hashlib.sha256(Path(path.value).read_bytes()).hexdigest()
    profiles={
        '16eab3f4827d37ef93111173a831b0f668ef8bdffa4442c740f2574f23dcc120': (0x454a980,0x454aa00,0x44c87e0),
        '68ef9be325743e304062728f667fdc6b3fba42243f9065a9a6b48b1b23418311': (0x4589980,0x4589a00,0x45077e0),
    }
    if digest not in profiles: raise RuntimeError('Unsupported executable')
    report['executable_sha256']=digest
    snap=k.CreateToolhelp32Snapshot(0x18,args.pid); module=Module();module.dwSize=ctypes.sizeof(module);base=None
    try:
        ok=k.Module32FirstW(snap,ctypes.byref(module))
        while ok:
            if module.szModule.lower()=='thegreatcircle.exe':base=module.modBaseAddr;break
            ok=k.Module32NextW(snap,ctypes.byref(module))
    finally:k.CloseHandle(snap)
    if not base: raise RuntimeError('Module not found')
    # Validate all names and values before changing any of them.
    planned=[]
    for rva,(name,value) in zip(profiles[digest],[('r_closeDistanceDitherDistanceStart',-1.),('r_closeDistanceDitherDistanceEnd',-2.),('r_vegetationDitherDistance',0.)]):
        obj=base+rva
        if read(pointer(obj+0x30),len(name)+1)!=name.encode()+b'\0':raise RuntimeError('Cvar name mismatch')
        at=pointer(obj)+0xc; original=read(at,4); old=struct.unpack('<f',original)[0]
        if not 0<=old<=100:raise RuntimeError('Unexpected original proximity setting')
        planned.append((at,original,struct.pack('<f',value)))
        report['changes'].append({'name':name,'address':hex(at),'before':old,'during':value})
    for at,original,replacement in planned:
        changed.append((at,original,replacement));write(at,replacement)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('Proximity dither comparison active for',args.seconds,'seconds; automatic restoration follows.',flush=True)
    time.sleep(args.seconds)
finally:
    errors=[]
    for at,original,replacement in reversed(changed):
        try:
            if read(at,4)==replacement:write(at,original)
            else:errors.append('Value changed externally; not overwritten at '+hex(at))
        except Exception as e:errors.append(str(e))
    report['restored']=bool(changed) and not errors;report['restore_errors']=errors
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    k.CloseHandle(h)
    print(json.dumps(report),flush=True)
