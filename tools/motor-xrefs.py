"""Read-only PE string/reflection xrefs and unwind-backed disassembly. No live writes."""
import argparse, json, re, struct, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import capstone, pefile
from extract_typeinfo import Image
ap=argparse.ArgumentParser();ap.add_argument('exe');ap.add_argument('names',nargs='*');ap.add_argument('--rva',action='append',default=[]);ap.add_argument('--quiet',action='store_true');ap.add_argument('--out',required=True);args=ap.parse_args()
im=Image(args.exe); pe=pefile.PE(args.exe,fast_load=True);pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']]);lo,hi=im.sections['.text'];code=im.mem[lo:hi]
unwind=[(x.struct.BeginAddress,x.struct.EndAddress) for x in pe.DIRECTORY_ENTRY_EXCEPTION]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
targets={int(rva,0):rva for rva in args.rva}
for name in args.names:
    start=0
    while True:
        pos=im.mem.find(name.encode(),start)
        if pos<0:break
        start=pos+len(name)+1
        string_start=im.mem.rfind(b'\0',max(0,pos-512),pos)+1
        whole=im.cstr(string_start,1024)
        if all(32<=ord(c)<127 or c in '\r\n\t' for c in whole):targets[string_start]=name
refs=[]
for match in re.finditer(rb'[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',code,re.DOTALL):
    rva=lo+match.start();target=rva+7+struct.unpack_from('<i',match.group(),3)[0]
    if target not in targets:continue
    function=next(((begin,end) for begin,end in unwind if begin<=rva<end),None)
    begin,end=function if function else (max(lo,rva-96),min(hi,rva+128))
    # Do not flood output with enormous functions; retain the xref neighborhood.
    disbegin=begin if rva-begin<500 else max(begin,rva-80)
    disend=min(end,max(rva+200,disbegin+500))
    instructions=[{'rva':hex(i.address-im.base),'bytes':i.bytes.hex(),'instruction':i.mnemonic+' '+i.op_str} for i in md.disasm(im.mem[disbegin:disend],im.base+disbegin)]
    # Chained unwind ranges may start inside an earlier instruction. In that case
    # the measured RIP-relative xref is a known instruction boundary.
    if not any(int(i['rva'],16)==rva for i in instructions):
        instructions=[{'rva':hex(i.address-im.base),'bytes':i.bytes.hex(),'instruction':i.mnemonic+' '+i.op_str} for i in md.disasm(im.mem[rva:disend],im.base+rva)]
    refs.append({'name':targets[target],'string_rva':hex(target),'xref_rva':hex(rva),'function_rva':hex(begin),'function_end':hex(end),'instructions':instructions})
Path(args.out).write_text(json.dumps({'timestamp':hex(im.timestamp),'xrefs':refs},indent=2),encoding='utf-8')
if not args.quiet:
    for ref in refs[:40]:print(ref['name'],ref['xref_rva'],'function',ref['function_rva'])
    if len(refs)>40:print(len(refs)-40,'further references retained in JSON')
print('Saved',args.out,'references',len(refs))
