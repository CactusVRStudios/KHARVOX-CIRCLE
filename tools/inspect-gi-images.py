"""Read-only Motor 1.0.17 GI idImage -> VkImage attribution; never writes memory."""
import argparse, ctypes, hashlib, json, struct
from ctypes import wintypes as w
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('pid',type=int);p.add_argument('run',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
launch=json.loads((a.run/'launch.json').read_text(encoding='utf-8-sig'))
expected='68ef9be325743e304062728f667fdc6b3fba42243f9065a9a6b48b1b23418311'
if hashlib.sha256(Path(launch['game']).read_bytes()).hexdigest()!=expected:raise RuntimeError('Requires verified Steam1.0.17.0')
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE;k.CloseHandle.argtypes=[w.HANDLE]
class Module(ctypes.Structure):
    _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',ctypes.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
k.Module32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Module)];k.Module32NextW.argtypes=[w.HANDLE,ctypes.POINTER(Module)]
snapshot=k.CreateToolhelp32Snapshot(0x18,a.pid);entry=Module();entry.dwSize=ctypes.sizeof(entry);base=None
ok=k.Module32FirstW(snapshot,ctypes.byref(entry))
while ok:
    if entry.szModule.lower()=='thegreatcircle.exe':
        if Path(entry.szExePath).resolve()!=Path(launch['game']).resolve():raise RuntimeError('Process path mismatch')
        base=entry.modBaseAddr;break
    ok=k.Module32NextW(snapshot,ctypes.byref(entry))
k.CloseHandle(snapshot)
if not base:raise RuntimeError('Game module absent')
handle=k.OpenProcess(0x410,False,a.pid)
if not handle:raise ctypes.WinError(ctypes.get_last_error())
observed=[];gpu_images=set()
with (a.run/'gpu-bindings.jsonl').open() as f:
    for line in f:
        try:r=json.loads(line)
        except json.JSONDecodeError:continue
        if r.get('event') in ('dispatch','dispatch_indirect'):
            observed.append(r)
            gpu_images.update(int(b['image'],16) for b in r.get('bindings',[]) if b.get('imageViewLive') and b.get('image'))
def read(address,size):
    buf=ctypes.create_string_buffer(size);received=ctypes.c_size_t()
    if not k.ReadProcessMemory(handle,address,buf,size,ctypes.byref(received)) or received.value!=size:raise RuntimeError('Incomplete read')
    return buf.raw
def ptr(address):return struct.unpack('<Q',read(address,8))[0]
try:
    device=ptr(base+0x4ab5a38)
    if not device:raise RuntimeError('No GI device')
    for attempt in range(20):
        stamp=read(device+0x1b8,4);slots=read(device+0x7f8,14*8);images=[]
        for index,image in enumerate(struct.unpack('<14Q',slots)):
            if not image:raise RuntimeError('GI images not allocated')
            raw=read(image,0x130)
            native48=struct.unpack_from('<Q',raw,0x48)[0]
            indirect=read(native48,256) if native48 else b''
            candidate=[dict(offset=hex(offset),image=hex(struct.unpack_from('<Q',indirect,offset)[0]))
                       for offset in range(0,len(indirect)-7,8) if struct.unpack_from('<Q',indirect,offset)[0] in gpu_images]
            direct=[dict(offset=hex(offset),image=hex(struct.unpack_from('<Q',raw,offset)[0]))
                    for offset in range(0,len(raw)-7,8) if struct.unpack_from('<Q',raw,offset)[0] in gpu_images]
            images.append(dict(slot=hex(0x7f8+index*8),native=hex(image),image=hex(struct.unpack_from('<Q',raw,0x48)[0]),
                               indirectCandidates=candidate,
                               directCandidates=direct,
                               dimensions=struct.unpack_from('<3i',raw,0xa4),
                               role=('count' if index<2 else 'depth' if index<4 else 'normal' if index<6 else 'SH')))
        if read(device+0x1b8,4)==stamp and read(device+0x7f8,14*8)==slots:break
    else:raise RuntimeError('GI slot mapping changed during every sample')
finally:k.CloseHandle(handle)
known={x['image']:x for x in images};matches=[]
with (a.run/'gpu-bindings.jsonl').open() as f:
    for line in f:
        try:r=json.loads(line)
        except json.JSONDecodeError:continue
        if r.get('event') not in ('dispatch','dispatch_indirect'):continue
        for b in r.get('bindings',[]):
            if b.get('image') in known and b.get('imageViewLive') and b.get('resolved'):
                matches.append(dict(session=r['session'],pipeline=r['pipeline'],eye=r['eye'],backend=r['backend'],
                                    binding=b,gi=known[b['image']]))
a.out.write_text(json.dumps(dict(device=hex(device),counter=struct.unpack('<I',stamp)[0],images=images,matches=matches,
    limits='Snapshot of current native GI slots. Historical image-handle reuse must be checked before treating older dispatches as current ownership.'),indent=2))
print(f'{len(images)} native GI images; {len(matches)} matching descriptor observations')
