"""Bounded single-effect comparison for the fingerprinted Motor build.

Validates native cvar names and readbacks; restores original bytes in finally.
Does not alter saved settings, camera, mandatory RT or executable bytes.
Distortion consumer C1E6AE reads internal +8; film grain C6F93F reads +0C.
GI temporal blend is read at BFFB96, internal +0C; value 1 is a diagnostic only.
"""
import argparse, ctypes, hashlib, json, struct, time
from pathlib import Path
from ctypes import wintypes as w

ap = argparse.ArgumentParser()
ap.add_argument('pid', type=int)
ap.add_argument('--seconds', type=int, default=120, choices=range(1, 181))
ap.add_argument('--effect', required=True, choices=['gi-prefilter','gi-screen-trace','shadow-tile-visibility','distortion','grain','gi-blend','native-taa','aa-off','secondary-animation','skin-blas-refresh','specular-tracing','specular-occlusion','specular-lighting','shadow-slice-cull','motionblur','hair-thickness','hair-lod','hair-rendering','hair-shadows','effect-particles','particles','scatter-history','water-reflections','water-history','water-grid-history','refraction-history','refraction-mask','refraction-layer-refresh','transparent-denoising','sss'])
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
changed=[]; report={'pid':args.pid,'effect':args.effect,'seconds':args.seconds,'changes':[],'restored':False}
try:
    path=ctypes.create_unicode_buffer(32768); count=w.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(count)): raise RuntimeError('Cannot identify process')
    catalog=json.loads((Path(__file__).resolve().parent.parent/'profiles/motor-builds.json').read_text())
    digest=hashlib.sha256(Path(path.value).read_bytes()).hexdigest()
    profile=next((p for p in catalog['profiles'] if p['sha256']==digest),None)
    if not profile: raise RuntimeError('Unsupported executable')
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
    rva,name,offset,fmt,value = {
        'gi-prefilter': (0x44f9560,'r_irradianceVolumeFinalGatherDenoiserPrefilter',8,'<Q',0),
        'gi-screen-trace': (0x44f9200,'r_irradianceVolumeFinalGatherSSTraceRadius',8,'<Q',0),
        'shadow-tile-visibility': (0x452ade0,'r_shadowUseTileBinningVisibility',8,'<Q',0),
        'distortion': (0x44c4200,'r_skipDistortionSurfaces',8,'<i',1),
        'grain': (0x44d3160,'r_filmGrainRatio',12,'<f',0.),
        'gi-blend': (0x44bab60,'r_irradianceVolumeFinalGatherTemporalBlendRatio',12,'<f',1.),
        'native-taa': (0x44cccd0,'r_antialiasing',8,'<Q',0x3f80000000000001),
        'aa-off': (0x44cccd0,'r_antialiasing',8,'<Q',0),
        # Verified GOG/Steam constructors; Steam 94AA94 / 94DB4D publish
        # bit24 to the native skinned surface only when this control is zero.
        'secondary-animation': (0x446c3b0,'r_skipSecondaryAnimation',8,'<Q',0x3f80000000000001),
        'skin-blas-refresh': (0x445bd40,'r_skinnedModelBlasRebuildFrequency',8,'<Q',0x3f80000000000001),
        'specular-tracing': (0x44be910,'rt_enableSpecularTracing',8,'<Q',0),
        'specular-occlusion': (0x44ba880,'r_irradianceVolumeFinalGatherSpecularOcclusion',8,'<Q',0),
        'specular-lighting': (0x44ea9b0,'r_skipSpecularLighting',8,'<Q',0x3f80000000000001),
        # Verified independent GOG/Steam constructors; bypass point-light slice culling only.
        'shadow-slice-cull': (0x44ea710,'r_shadowSkipSliceViewCull',8,'<Q',0x3f80000000000001),
        'motionblur': (0x44d84e0,'r_motionblur',8,'<Q',0),
        # Verified scalar consumers89AF66/89AF84 and LOD branches893418/89B50F.
        'hair-thickness': (0x4459470,'r_hairThicknessScale',12,'<f',.25),
        'hair-lod': (0x4457240,'r_hairEnableLodding',8,'<Q',0),
        # Diagnostic isolation only. Independently checked GOG/Steam constructors
        # in logs/r181-hair-{gog,steam}-xrefs.json. Native hair submission reads
        # r_skipHair at 89A42E; this is not a persistent bald-NPC workaround.
        'hair-rendering': (0x44cd300,'r_skipHair',8,'<Q',0x3f80000000000001),
        'hair-shadows': (0x44573f0,'r_skipHairShadow',8,'<Q',0x3f80000000000001),
        # Effect generation's existing early-out (GOG89D672); leave ordinary
        # geometry, hair and mandatory RT alone. Both constructors checked.
        'effect-particles': (0x4459870,'r_skipEffectParticles',8,'<Q',0x3f80000000000001),
        # Separate particle-model submit/generate path (8A17DE/8A3815).
        # Do not confuse this with r_skipParticlesSurfaceSpawn at44C4B90.
        'particles': (0x44599f0,'r_skipParticles',8,'<Q',0x3f80000000000001),
        # C32635/C3272A read this weight in both refraction paths, including
        # the masked path which ignores r_refractionTAA. Native reset uses 1.
        'refraction-history': (0x44cdd10,'r_refractionTAABlendRatio',12,'<f',1.),
        # Steam C497B3 reads wrapper450CF30, then branches to the unmasked
        # accumulation path at C498B8. Name independently observed live.
        'refraction-mask': (0x450cf30,'r_transparencyEnableRefractionMask',8,'<Q',0),
        # Steam C48983/C4898A reads internal +0x0c. Keep the two-update cap;
        # vary only depth-based sharing of the refraction source.
        'refraction-layer-refresh': (0x450c920,'r_refractionForceRefreshThreshold',12,'<f',0.),
        # Steam C2AF83/C2DAC1 gate the dedicated transparent denoiser.
        'transparent-denoising': (0x44fe600,'rt_enableTransparencyDenoising',8,'<Q',0x3f80000000000001),
        'sss': (0x44c6350,'r_skipSSSRendering',8,'<Q',0x3f80000000000001),
        # Motor consumers C42CB2, CCFDA7/C6FC7E and CCF55F respectively.
        'scatter-history': (0x44cec70,'r_lightScatteringTAA',8,'<Q',0),
        'water-reflections': (0x4523910,'r_waterReflections',8,'<Q',0),
        'water-history': (0x44e7050,'r_waterReflectionsTAA',8,'<Q',0),
        'water-grid-history': (0x44e4c10,'r_waterGridTAA',8,'<Q',0),
    }[args.effect]
    for rva,name,offset,fmt,value in [(rva,name,offset,fmt,value)]:
        if args.effect in ('shadow-tile-visibility','gi-screen-trace','gi-prefilter','refraction-mask','refraction-layer-refresh','transparent-denoising','water-reflections') and profile['store']!='steam':
            raise RuntimeError('Shadow tile visibility comparison is verified for Steam only')
        if profile['store']=='steam':
            # Diagnostic-only address, derived independently from both native cvar constructors.
            diagnostic_addresses={0x44f9560:'0x44f9560',0x44f9200:'0x44f9200',0x452ade0:'0x452ade0',0x44ea710:'0x4529710',0x445bd40:'0x449ad40',0x44be910:'0x44fd910',0x44ba880:'0x44f9880',0x44bab60:'0x44f9b60',0x44ea9b0:'0x45299b0',0x44cd300:'0x450c300',0x44573f0:'0x44963f0',0x4459870:'0x4498870',0x44599f0:'0x44989f0'}
            diagnostic_addresses.update({0x44cdd10:'0x450cd10',0x44c6350:'0x4505350'})
            diagnostic_addresses.update({0x450cf30:'0x450cf30'})
            diagnostic_addresses.update({0x450c920:'0x450c920',0x44fe600:'0x44fe600'})
            # Independently resolved Steam wrapper in r148-cvar-live.json;
            # exact native name and original scalar values are checked below.
            diagnostic_addresses.update({0x44cec70:'0x450dc70'})
            # Steam wrapper independently observed in r148-cvar-live.json.
            # Name and scalar readback are still validated before any write.
            diagnostic_addresses.update({0x44e7050:'0x4526050',0x4523910:'0x4523910'})
            entry=({'steam':diagnostic_addresses[rva]} if rva in diagnostic_addresses else catalog['addresses'].get(hex(rva)))
            if not entry:raise RuntimeError('Comparison address not mapped for Steam')
            rva=int(entry['steam'],16)
        obj=base+rva
        if read(pointer(obj+0x30),len(name)+1)!=name.encode()+b'\0':raise RuntimeError('Cvar name mismatch')
        at=pointer(obj)+offset; original=read(at,struct.calcsize(fmt)); old=struct.unpack(fmt,original)[0]
        valid=(struct.unpack('<if',original)[0] in range(5) and struct.unpack('<if',original)[1]==float(struct.unpack('<if',original)[0])) if fmt=='<Q' else 0<=old<=1
        if args.effect=='gi-screen-trace':
            integer,floating=struct.unpack('<if',original);valid=0<=integer<=128 and floating==float(integer)
        if args.effect=='skin-blas-refresh':
            integer,floating=struct.unpack('<if',original);valid=1<=integer<=120 and floating==float(integer)
        if args.effect=='refraction-layer-refresh':valid=old==7.0
        if args.effect=='transparent-denoising':valid=original==struct.pack('<if',0,0.)
        if not valid:raise RuntimeError('Unexpected original effect setting')
        planned.append((at,original,struct.pack(fmt,value)))
        report['changes'].append({'name':name,'address':hex(at),'before':old,'during':value})
    for at,original,replacement in planned:
        changed.append((at,original,replacement));write(at,replacement)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(args.effect,'comparison active for',args.seconds,'seconds; automatic restoration follows.',flush=True)
    until=time.monotonic()+args.seconds
    while time.monotonic()<until and not (args.stop_file and args.stop_file.exists()):
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

