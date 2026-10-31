"""Bounded read-only stack observations of Motor's native air-effect query.

No suspension, hooks, remote execution or process writes. Stack remnants are
evidence candidates, not a synchronized call trace.
"""
import sys, time
from pathlib import Path

pid = sys.argv[1]
sys.argv = ['motor-read-memory.py', pid, '--out', 'logs/r306-air-live.json']
exec(Path('tools/motor-read-memory.py').read_text().split('rows=[]')[0])
u64 = lambda a: struct.unpack('<Q', read(a, 8) or bytes(8))[0]

class Thread(ctypes.Structure):
    _fields_ = [('size',w.DWORD),('cnt',w.DWORD),('id',w.DWORD),('pid',w.DWORD),('pri',w.LONG),('delta',w.LONG),('flags',w.DWORD)]
class TBI(ctypes.Structure):
    _fields_ = [('status',ctypes.c_long),('teb',ctypes.c_void_p),('pid',ctypes.c_void_p),('tid',ctypes.c_void_p),('affinity',ctypes.c_size_t),('priority',ctypes.c_long),('basepriority',ctypes.c_long)]
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype=w.HANDLE
nt=ctypes.WinDLL('ntdll');nt.NtQueryInformationThread.argtypes=[w.HANDLE,w.ULONG,ctypes.c_void_p,w.ULONG,ctypes.c_void_p]
k.Thread32First.argtypes=[w.HANDLE,ctypes.POINTER(Thread)];k.Thread32Next.argtypes=[w.HANDLE,ctypes.POINTER(Thread)]
sn=k.CreateToolhelp32Snapshot(4,0);t=Thread();t.size=ctypes.sizeof(t);ok=k.Thread32First(sn,ctypes.byref(t));stacks=[]
while ok:
    if t.pid==args.pid:
        h=k.OpenThread(0x40,False,t.id);info=TBI()
        if h and nt.NtQueryInformationThread(h,0,ctypes.byref(info),ctypes.sizeof(info),None)==0:
            stack=read(info.teb,24)
            if stack:
                top,low=struct.unpack_from('<QQ',stack,8)
                stacks.append((t.id,max(low+4096,top-256*1024),top))
        if h:k.CloseHandle(h)
    ok=k.Thread32Next(sn,ctypes.byref(t))
k.CloseHandle(sn)
hits={}
for sample in range(12):
    for tid,low,top in stacks:
        b=read(low,top-low)
        if not b:continue
        # Return from the resolved entity query in AirAttackEffectStateJob.
        needle=struct.pack('<Q',base+0x18f92e4)
        pos=b.find(needle)
        while pos>=0:
            q=low+pos+8+0xf0
            row={'thread':tid,'query':hex(q),'entity':hex(u64(u64(q+0x18))),'components':[]}
            for off in range(0x28,0xa0,16):
                comp=u64(q+off)
                row['components'].append({'offset':hex(off),'address':hex(comp),'bytes':(read(comp,64) or b'').hex()})
            hits[(tid,q)]=row
            pos=b.find(needle,pos+8)
    time.sleep(.1)
k.CloseHandle(handle)
out={'pid':args.pid,'moduleBase':hex(base),'access':'read-only','stackCount':len(stacks),'candidates':list(hits.values())}
Path(args.out).write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
