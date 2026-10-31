import ctypes as c,struct,json
from ctypes import wintypes as w
from pathlib import Path
k=c.WinDLL('kernel32');d=c.WinDLL('C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbghelp.dll')
k.GetCurrentProcess.restype=w.HANDLE;hp=k.GetCurrentProcess()
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL];assert d.SymInitializeW(hp,str(Path('bin').resolve()),False)
d.SymLoadModuleExW.argtypes=[w.HANDLE,w.HANDLE,w.LPCWSTR,w.LPCWSTR,c.c_ulonglong,w.DWORD,c.c_void_p,w.DWORD];d.SymLoadModuleExW.restype=c.c_ulonglong
base=d.SymLoadModuleExW(hp,None,str(Path('bin/KharvoxCircle.dll').resolve()),None,0x10000000,0,None,0);assert base
class SI(c.Structure):
 _fields_=[('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
CB=c.WINFUNCTYPE(w.BOOL,c.POINTER(SI),w.ULONG,c.c_void_p)
found=[]
@CB
def cb(p,size,ctx):
 s=p.contents;name=c.string_at(c.addressof(s)+SI.Name.offset,s.NameLen).decode(errors='replace');found.append(dict(name=name,rva=hex(s.Address-base),size=s.Size));return True
d.SymEnumSymbols.argtypes=[w.HANDLE,c.c_ulonglong,c.c_char_p,CB,c.c_void_p]
d.SymEnumSymbols(hp,base,b'*laser*',cb,None)
Path('logs/r248-laser-symbols.json').write_text(json.dumps(found,indent=2));print(json.dumps([x for x in found if x['size']>8 or 'weapon' in x['name']],indent=2)[:9000])
