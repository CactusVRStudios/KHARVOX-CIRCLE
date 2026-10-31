import sys
from pathlib import Path
sys.argv=['motor-read-memory.py','54000','--out','logs/r248-laser-live.json']
exec(Path('tools/motor-read-memory.py').read_text().split('rows=[]')[0])
snap=k.CreateToolhelp32Snapshot(0x18,args.pid);mod=Module();mod.dwSize=ctypes.sizeof(mod);ok=k.Module32FirstW(snap,ctypes.byref(mod));dll=0
while ok:
 if mod.szModule.lower()=='kharvoxcircle.dll':dll=mod.modBaseAddr;break
 ok=k.Module32NextW(snap,ctypes.byref(mod))
k.CloseHandle(snap);assert dll
sym=json.loads(Path('logs/r248-laser-symbols.json').read_text());rva=int(next(x['rva'] for x in sym if x['name']=='circle::motor::laser::weapon'),16)
u64=lambda p:struct.unpack('<Q',read(p,8) or bytes(8))[0]
b=read(dll+rva,88);vals=struct.unpack('<10QI4x',b);names=['owner','entity','tick','life','active','state','attachment','model','pool','loadout','slot'];v=dict(zip(names,vals));out={'weapon':{a:hex(b) for a,b in v.items()}}
model=v['model'];pool=v['pool'];att=v['attachment'];muzzleHandle=u64(att+0x108);index=muzzleHandle&0xffffff;offset=(muzzleHandle>>40)&0x3ff;data=u64(pool);count=u64(pool+8)
out.update(model_flags=(read(model+0x183,1) or b'').hex(),model_pose=(read(model+0x120,60) or b'').hex(),muzzle_handle=hex(muzzleHandle),pool_data=hex(data),pool_count=count,index=index,offset=offset,generation=(read(data+index*32+12,2) or b'').hex(),expected_generation=hex((muzzleHandle>>24)&0xffff),muzzle=(read(data+(index+offset)*32,32) or b'').hex())

class Thread(ctypes.Structure):
 _fields_=[('size',w.DWORD),('cnt',w.DWORD),('id',w.DWORD),('pid',w.DWORD),('pri',w.LONG),('delta',w.LONG),('flags',w.DWORD)]
class TBI(ctypes.Structure):
 _fields_=[('status',ctypes.c_long),('teb',ctypes.c_void_p),('pid',ctypes.c_void_p),('tid',ctypes.c_void_p),('affinity',ctypes.c_size_t),('priority',ctypes.c_long),('basepriority',ctypes.c_long)]
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype=w.HANDLE
nt=ctypes.WinDLL('ntdll');nt.NtQueryInformationThread.argtypes=[w.HANDLE,w.ULONG,ctypes.c_void_p,w.ULONG,ctypes.c_void_p]
k.Thread32First.argtypes=[w.HANDLE,ctypes.POINTER(Thread)];k.Thread32Next.argtypes=[w.HANDLE,ctypes.POINTER(Thread)];sn=k.CreateToolhelp32Snapshot(4,0);t=Thread();t.size=ctypes.sizeof(t);ok=k.Thread32First(sn,ctypes.byref(t));hits=[];counts=[0,0,0,0]
while ok:
 if t.pid==args.pid:
  counts[0]+=1
  h=k.OpenThread(0x40,False,t.id);info=TBI()
  if h and nt.NtQueryInformationThread(h,0,ctypes.byref(info),ctypes.sizeof(info),None)==0:
   counts[1]+=1;stack=read(info.teb,24)
   if stack:
    top,low=struct.unpack_from('<QQ',stack,8);low=max(low+4096,top-256*1024);b=read(low,top-low)
    if b:
     counts[2]+=1;counts[3]+=b.count(struct.pack('<Q',v['active']));pos=b.find(struct.pack('<Q',v['active']))
     while pos>=0:
      q=low+pos-0x28
      if True:
       row={'thread':t.id,'query':hex(q),'components':[]}
       for off in range(0x28,0xa0,16):
        comp=u64(q+off);obj=u64(comp);row['components'].append({'offset':hex(off),'component':hex(comp),'object':hex(obj),'type':rtti(obj),'bytes':(read(comp,24) or b'').hex()})
       hits.append(row)
      pos=b.find(struct.pack('<Q',v['active']),pos+8)
  if h:k.CloseHandle(h)
 ok=k.Thread32Next(sn,ctypes.byref(t))
k.CloseHandle(sn);k.CloseHandle(handle)
Path('logs/r248-query-live.json').write_text(json.dumps(hits,indent=2));print(counts);print(json.dumps(hits,indent=2)[:13000])


