"""Unwind-aware offline disassembly and call-site probe for a measured Motor RVA."""
import argparse,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import capstone,pefile
from extract_typeinfo import Image
ap=argparse.ArgumentParser();ap.add_argument('exe');ap.add_argument('rva',type=lambda x:int(x,0));ap.add_argument('--calls',action='store_true');ap.add_argument('--out',required=True);args=ap.parse_args()
im=Image(args.exe);pe=pefile.PE(args.exe,fast_load=True);pe.parse_data_directories(directories=[3])
def parent_range(record):
    begin,end,info=record.BeginAddress,record.EndAddress,record.UnwindData
    seen=set()
    while info not in seen and 0<info<im.size-4:
        seen.add(info);header=im.mem[info:info+4]
        if not (header[0]>>3)&4:break # UNW_FLAG_CHAININFO
        chain=info+((4+2*header[2]+3)//4*4)
        if chain+12>im.size:break
        parent_begin,parent_end,info=struct.unpack_from('<3I',im.mem,chain)
        begin=min(begin,parent_begin);end=max(end,parent_end)
    return begin,end
unwind=[(x.struct.BeginAddress,x.struct.EndAddress,parent_range(x.struct)) for x in pe.DIRECTORY_ENTRY_EXCEPTION]
families={}
for begin,end,parent in unwind:
    root=parent[0];families[root]=(root,max(end,parent[1],families.get(root,(root,0))[1]))
unwind=[(begin,end,families[parent[0]]) for begin,end,parent in unwind]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
function=next((x[2] for x in unwind if x[0]<=args.rva<x[1]),(args.rva,args.rva+256));results=[]
if not args.calls:
    spans=[(function[0],function[1],args.rva)]
else:
    lo,hi=im.sections['.text'];spans=[]
    for match in re.finditer(rb'\xe8....',im.mem[lo:hi],re.DOTALL):
        at=lo+match.start();target=at+5+struct.unpack_from('<i',match.group(),1)[0]
        if target!=args.rva:continue
        owner=next((x[2] for x in unwind if x[0]<=at<x[1]),(max(at-96,lo),at+20))
        spans.append((owner[0],owner[1],at))
for begin,end,focus in spans:
    inst=[]
    for i in md.disasm(im.mem[begin:min(end,focus+300)],im.base+begin):
        if i.address<im.base+focus-150:continue
        row={'rva':hex(i.address-im.base),'bytes':i.bytes.hex(),'instruction':i.mnemonic+' '+i.op_str}
        for op in i.operands:
            if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                target=i.address+i.size+op.mem.disp-im.base
                if 0<=target<im.size:row['rip_rva']=hex(target);row['rip_string']=im.cstr(target,100)
        inst.append(row)
    results.append({'function':hex(begin),'end':hex(end),'focus':hex(focus),'instructions':inst})
Path(args.out).write_text(json.dumps({'rva':hex(args.rva),'results':results},indent=2),encoding='utf-8')
print(len(results),'result(s) saved',args.out)
