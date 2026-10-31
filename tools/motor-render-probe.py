"""Bounded offline RTTI/vtable/unwind probe for Motor render scheduling. Never executes game code."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'python-deps'))
import capstone,pefile,numpy as np
from extract_typeinfo import Image
ap=argparse.ArgumentParser();ap.add_argument('exe');ap.add_argument('--type',action='append',default=[]);ap.add_argument('--function',action='append',default=[]);ap.add_argument('--out',required=True);a=ap.parse_args()
im=Image(a.exe);pe=pefile.PE(a.exe,fast_load=True);pe.parse_data_directories(directories=[3])
families={};records=[]
for record in pe.DIRECTORY_ENTRY_EXCEPTION:
    s=record.struct;begin,end,info=s.BeginAddress,s.EndAddress,s.UnwindData;seen=set()
    while info not in seen and 0<info<im.size-4:
        seen.add(info);header=im.mem[info:info+4]
        if not (header[0]>>3)&4:break
        at=info+((4+2*header[2]+3)//4*4);b,e,info=struct.unpack_from('<3I',im.mem,at);begin=min(begin,b);end=max(end,e)
    records.append((s.BeginAddress,s.EndAddress,begin));families[begin]=max(end,families.get(begin,0))
def bounds(rva):
    for b,e,root in records:
        if b<=rva<e:return root,families[root]
    return rva,rva+128
def refs(data):
    start=0
    while True:
        at=im.mem.find(data,start)
        if at<0:return
        yield at;start=at+1
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
out={'timestamp':hex(im.timestamp),'types':{},'functions':{}}
functions={int(f,0) for f in a.function}
for name in a.type:
    found=[]
    for at in refs(('.?AV'+name+'@@\0').encode()):
        descriptor=at-16
        for type_ref in refs(struct.pack('<I',descriptor)):
            col=type_ref-12
            if col<0:continue
            sig,offset,cd,td,hierarchy,selfrva=struct.unpack_from('<6I',im.mem,col)
            if sig!=1 or selfrva!=col:continue
            for slot in np.nonzero(im.qwords==im.base+col)[0]:
                vtable=int(slot)*8+8;entries=[]
                for index in range(40):
                    target=struct.unpack_from('<Q',im.mem,vtable+index*8)[0]-im.base
                    if not im.sections['.text'][0]<=target<im.sections['.text'][1]:break
                    entries.append({'slot':hex(index*8),'rva':hex(target),'function':list(map(hex,bounds(target)))})
                    if index<18:functions.add(target)
                found.append({'descriptor':hex(descriptor),'col':hex(col),'vtable':hex(vtable),'subobject':offset,'entries':entries})
    out['types'][name]=found
for rva in sorted(functions):
    lo,hi=bounds(rva);ins=[]
    for i in md.disasm(im.mem[lo:min(hi,lo+24000)],im.base+lo):
        row={'rva':hex(i.address-im.base),'bytes':i.bytes.hex(),'op':i.mnemonic+' '+i.op_str}
        for op in i.operands:
            if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                target=i.address+i.size+op.mem.disp-im.base;row['rip']=hex(target)
                if 0<=target<im.size:
                    text=im.cstr(target,140)
                    if text and all(32<=ord(c)<127 for c in text):row['string']=text
        ins.append(row)
    out['functions'][hex(rva)]={'bounds':[hex(lo),hex(hi)],'instructions':ins}
Path(a.out).write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps({'types':out['types'],'functions':{k:{'bounds':v['bounds'],'instructions':len(v['instructions'])} for k,v in out['functions'].items()}},indent=2))
