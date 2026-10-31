"""Read-only census of the existing Motor render-world model list; no hooks/writes."""
import argparse,ctypes,json,math,struct
from ctypes import wintypes as w
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('pid',type=int);ap.add_argument('model',type=lambda s:int(s,0));ap.add_argument('--out',required=True);a=ap.parse_args()
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.CloseHandle.argtypes=[w.HANDLE]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise ctypes.WinError(ctypes.get_last_error())
def read(p,n):
 b=ctypes.create_string_buffer(n);received=ctypes.c_size_t()
 return b.raw if k.ReadProcessMemory(h,p,b,n,ctypes.byref(received)) and received.value==n else None
def u64(p):
 b=read(p,8);return struct.unpack('<Q',b)[0] if b else 0
def text(p):return (read(p,256) or b'').split(b'\0')[0].decode('utf-8',errors='replace')
try:
 world=u64(a.model+0x108);origin=struct.unpack('<3f',read(a.model+0x120,12));table=u64(world+0x588)
 count=struct.unpack('<I',read(world+0x600,4))[0]
 if not 0<count<=1048576:raise RuntimeError('Invalid model list count')
 raw=read(table,count*8)
 if raw is None:raise RuntimeError('Model list unreadable')
 rows=[];near=0;unread=0
 for (model,) in struct.iter_unpack('<Q',raw):
  if not model:continue
  b=read(model,0x190)
  if b is None:unread+=1;continue
  xyz=struct.unpack_from('<3f',b,0x120)
  if not all(math.isfinite(x) for x in xyz):continue
  distance=math.dist(xyz,origin)
  if distance>50:continue
  near+=1;asset=text(struct.unpack_from('<Q',b,0x90)[0]);instance=text(struct.unpack_from('<Q',b,0x98)[0])
  if not any(x in (asset+' '+instance).lower() for x in ('characters/','npc_','actor_','indiana','gp_ai_','gina','models/wear/')):continue
  rows.append(dict(model=hex(model),asset=asset,instance=instance,position=xyz,distance=distance,firstPerson=bool(b[0x183]&2),committed=hex(struct.unpack_from('<Q',b,0x88)[0]),renderWorld=hex(struct.unpack_from('<Q',b,0x108)[0])))
 stable=table==u64(world+0x588) and count==struct.unpack('<I',read(world+0x600,4))[0]
 result=dict(pid=a.pid,access='read-only 0x410',world=hex(world),origin=origin,count=count,near=near,unread=unread,listHeaderStable=stable,models=rows,limits='Live unsynchronized CPU snapshot; render-world membership is not screen visibility or actor ownership.')
 Path(a.out).write_text(json.dumps(result,indent=2));print('count',count,'near',near,'character parts',len(rows),'list header stable',stable)
 for r in rows:
  if '.md6' in r['asset']:print(r['model'],r['instance'],r['asset'],round(r['distance'],2),r['position'])
finally:k.CloseHandle(h)
