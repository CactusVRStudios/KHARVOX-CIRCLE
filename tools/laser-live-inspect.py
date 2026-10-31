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
worldHandle=u64(att+0x100);wi=worldHandle&0xffffff;wo=(worldHandle>>40)&0x3ff;out['world_muzzle_handle']=hex(worldHandle);out['world_muzzle']=(read(data+(wi+wo)*32,32) or b'').hex();out['world_muzzle_floats']=struct.unpack('<8f',read(data+(wi+wo)*32,32));
Path(args.out).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2));k.CloseHandle(handle)

