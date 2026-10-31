"""Read-only thread CPU/start-address census. Does not suspend threads or collect stacks."""
import argparse, ctypes as c, json, time
from ctypes import wintypes as w
from pathlib import Path

ap=argparse.ArgumentParser()
ap.add_argument('pid',type=int)
ap.add_argument('--seconds',type=float,default=5)
ap.add_argument('--out',type=Path,required=True)
a=ap.parse_args()
if not 1 <= a.seconds <= 30: raise ValueError('seconds must be 1..30')
k=c.WinDLL('kernel32',use_last_error=True);nt=c.WinDLL('ntdll')
class Thread(c.Structure):
    _fields_=[('size',w.DWORD),('usage',w.DWORD),('id',w.DWORD),('owner',w.DWORD),('basePriority',w.LONG),('deltaPriority',w.LONG),('flags',w.DWORD)]
class Module(c.Structure):
    _fields_=[('size',w.DWORD),('moduleId',w.DWORD),('pid',w.DWORD),('globalUsage',w.DWORD),('processUsage',w.DWORD),('base',c.c_void_p),('bytes',w.DWORD),('handle',w.HMODULE),('name',w.WCHAR*256),('path',w.WCHAR*260)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Thread32First.argtypes=k.Thread32Next.argtypes=[w.HANDLE,c.POINTER(Thread)]
k.Module32FirstW.argtypes=k.Module32NextW.argtypes=[w.HANDLE,c.POINTER(Module)]
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype=w.HANDLE
k.GetThreadTimes.argtypes=[w.HANDLE]+[c.POINTER(w.FILETIME)]*4
k.GetThreadDescription.argtypes=[w.HANDLE,c.POINTER(c.c_wchar_p)];k.GetThreadDescription.restype=c.c_long
k.LocalFree.argtypes=[c.c_void_p];k.CloseHandle.argtypes=[w.HANDLE]
nt.NtQueryInformationThread.argtypes=[w.HANDLE,w.ULONG,c.c_void_p,w.ULONG,c.c_void_p];nt.NtQueryInformationThread.restype=c.c_long
modules=[];snap=k.CreateToolhelp32Snapshot(0x18,a.pid)
try:
    m=Module();m.size=c.sizeof(m);ok=k.Module32FirstW(snap,c.byref(m))
    while ok:
        modules.append((m.base,m.base+m.bytes,m.name,m.path));ok=k.Module32NextW(snap,c.byref(m))
finally:k.CloseHandle(snap)
def cpu(h):
    times=[w.FILETIME() for _ in range(4)]
    if not k.GetThreadTimes(h,*[c.byref(t) for t in times]):return None
    return sum((t.dwHighDateTime<<32)+t.dwLowDateTime for t in times[2:])/1e7
handles=[];snap=k.CreateToolhelp32Snapshot(4,0)
try:
    t=Thread();t.size=c.sizeof(t);ok=k.Thread32First(snap,c.byref(t))
    while ok:
        if t.owner==a.pid:
            h=k.OpenThread(0x40,False,t.id)
            if h:handles.append((t.id,h,cpu(h)))
        ok=k.Thread32Next(snap,c.byref(t))
finally:k.CloseHandle(snap)
rows=[];start=time.perf_counter()
try:
    time.sleep(a.seconds);elapsed=time.perf_counter()-start
    for tid,h,before in handles:
        after=cpu(h)
        if before is None or after is None:continue
        addr=c.c_void_p();status=nt.NtQueryInformationThread(h,9,c.byref(addr),c.sizeof(addr),None)
        desc=c.c_wchar_p();name=''
        if k.GetThreadDescription(h,c.byref(desc))>=0 and desc:
            name=desc.value;k.LocalFree(c.cast(desc,c.c_void_p))
        module=next((m for m in modules if addr.value and m[0]<=addr.value<m[1]),None)
        rows.append(dict(tid=tid,core_percent=100*(after-before)/elapsed,name=name,start=hex(addr.value or 0),start_query_status=status,module=module[2] if module else None,rva=hex(addr.value-module[0]) if module else None))
finally:
    for _,h,_ in handles:k.CloseHandle(h)
rows.sort(key=lambda r:r['core_percent'],reverse=True)
result=dict(pid=a.pid,seconds=elapsed,wall_time=time.time(),core_equivalents=sum(r['core_percent'] for r in rows)/100,threads=rows)
a.out.write_text(json.dumps(result,indent=2))
print(json.dumps(dict(result,threads=rows[:25]),indent=2))
