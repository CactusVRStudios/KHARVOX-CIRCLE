"""Bounded performance comparison; validated executable/name and automatic restore.

The generated live manifests are specific to the verified Steam build. Not a
launcher preset: disabling particles can hide gameplay cues.
"""
import argparse, ctypes, hashlib, json, struct, time
from pathlib import Path
from ctypes import wintypes as w

ap = argparse.ArgumentParser()
ap.add_argument('pid', type=int)
ap.add_argument('--seconds', type=int, default=120, choices=range(1, 181))
ap.add_argument('--effect', default='screen-effects', choices=['screen-effects','particles','vegetation-shadows','shadows','vegetation'])
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

import statistics, subprocess
changed=[];report={'pid':args.pid,'effect':args.effect,'phases':[],'changes':[],'restored':False}
try:
 path=ctypes.create_unicode_buffer(32768);count=w.DWORD(len(path));k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(count))
 digest=hashlib.sha256(Path(path.value).read_bytes()).hexdigest()
 catalog=json.loads(Path('profiles/motor-builds.json').read_text());profile=next((p for p in catalog['profiles'] if p['sha256']==digest),None)
 if not profile:raise RuntimeError('Unsupported executable')
 snap=k.CreateToolhelp32Snapshot(0x18,args.pid);module=Module();module.dwSize=ctypes.sizeof(module);base=None
 try:
  ok=k.Module32FirstW(snap,ctypes.byref(module))
  while ok:
   if module.szModule.lower()=='thegreatcircle.exe':base=module.modBaseAddr;break
   ok=k.Module32NextW(snap,ctypes.byref(module))
 finally:k.CloseHandle(snap)
 if not base:raise RuntimeError('Module not found')
 report['profile']=profile['id']
 frame=base+(int(catalog['addresses']['0x4a71950']['steam'],16) if profile['store']=='steam' else 0x4a71950)
 # Manifest generated read-only from this executable's constructors; validate names again.
 manifest=json.loads(Path('logs/r148-cvar-live.json').read_text())+json.loads(Path('logs/r148-extra-live.json').read_text())
 if profile['store']!='steam':raise RuntimeError('Diagnostic manifest is Steam-specific; do not reuse for another build')
 wanted={'screen-effects':{'r_lightScattering':0,'r_skipSSSRendering':1,'r_skipDecals':1,'r_skipGeoDecals':1},
 'particles':{'r_skipParticles':1,'r_skipGPUParticles':1},
 'vegetation-shadows':{'r_skipEcotopeSunShadows':1,'r_skipEcotopeLocalShadows':1},
 'shadows':{'r_shadowEnableQuality':2,'r_contactShadows':0,'r_skipHairShadow':1},
 'vegetation':{'r_skipEcotope':1,'r_skipDetailTris':1}}[args.effect]
 planned=[]
 for name,value in wanted.items():
  row=next(x for x in manifest if x['name']==name);wrapper=base+int(row['rva'],16)
  if read(pointer(wrapper+0x30),len(name)+1)!=name.encode()+b'\0':raise RuntimeError('Name mismatch')
  addr=pointer(wrapper)+8;old=read(addr,8);integer,floating=struct.unpack('<if',old)
  if integer not in (0,1,2,3) or floating!=float(integer):raise RuntimeError('Unexpected native value')
  new=struct.pack('<if',value,float(value));planned.append((addr,old,new))
  report['changes'].append(dict(name=name,before=integer,during=value))
 def measure(label):
  start=time.monotonic();samples=[];gpu=[];lastgpu=-10
  while time.monotonic()-start<args.seconds:
   t=time.monotonic()-start;samples.append([t,struct.unpack('<I',read(frame,4))[0]])
   if t-lastgpu>=3:
    lastgpu=t
    try:
     text=subprocess.run(['nvidia-smi','--query-gpu=utilization.gpu,utilization.memory,power.draw','--format=csv,noheader,nounits'],capture_output=True,text=True,timeout=2).stdout.strip()
     gpu.append([t]+[float(v.strip()) for v in text.split(',')])
    except Exception:pass
   if args.stop_file and args.stop_file.exists():raise RuntimeError('Comparison stopped')
   time.sleep(.25)
  used=[x for x in samples if x[0]>=3]
  fps=(used[-1][1]-used[0][1])/2/(used[-1][0]-used[0][0])
  row=dict(phase=label,stereo_pairs_per_second=fps,samples=samples,gpu=gpu,wall_end=time.time())
  report['phases'].append(row);args.out.write_text(json.dumps(report,indent=2));print(label,round(fps,2),'native stereo pairs/s',flush=True)
 measure('baseline_before')
 for addr,old,new in planned:changed.append((addr,old,new));write(addr,new)
 print('Comparison active:',wanted,flush=True)
 measure('effects_changed')
 for addr,old,new in reversed(changed):
  if read(addr,8)!=new:raise RuntimeError('Cvar changed externally')
  write(addr,old)
 changed.clear();report['restored']=True
 measure('baseline_after')
finally:
 errors=[]
 for addr,old,new in reversed(changed):
  try:
   if read(addr,8)==new:write(addr,old)
   else:errors.append('Externally changed value not overwritten')
  except Exception as e:errors.append(str(e))
 if changed:report['restored']=not errors
 report['restore_errors']=errors;args.out.write_text(json.dumps(report,indent=2));k.CloseHandle(h)
 print('Restored:',report['restored'],flush=True)
