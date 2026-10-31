"""Read-only offline Motor build comparison. Never modifies either executable.

Finds unique instruction sequences with only relocated operands masked, then
maps data references by the matched instruction's RIP-relative target. Output
is evidence for review, not permission to enable an unknown game build.
"""
import argparse, bisect, hashlib, json, re, struct, sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).parent/'python-deps'), str(Path(__file__).parent)]
import capstone as cs
import pefile
from extract_typeinfo import Image

ap=argparse.ArgumentParser()
ap.add_argument('reference');ap.add_argument('candidate');ap.add_argument('--out',required=True)
ap.add_argument('--reuse',action='store_true',help='Reuse evidence only for these exact file hashes')
args=ap.parse_args()
a,b=Image(args.reference),Image(args.candidate)
md=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_64);md.detail=True
pe=pefile.PE(args.reference);other=pefile.PE(args.candidate)
starts=sorted(x.struct.BeginAddress for x in pe.DIRECTORY_ENTRY_EXCEPTION)
lo,hi=b.sections['.text'];target=b.mem[lo:hi]
mapped={};evidence={};failed={}
if args.reuse and Path(args.out).exists():
    cache=json.loads(Path(args.out).read_text())
    assert cache['reference']['sha256']==hashlib.sha256(Path(args.reference).read_bytes()).hexdigest()
    assert cache['candidate']['sha256']==hashlib.sha256(Path(args.candidate).read_bytes()).hexdigest()
    for key,row in cache['addresses'].items():mapped[int(key,16)]=int(row['target'],16);evidence[int(key,16)]=row['evidence']

def pattern(start,end):
    raw=bytearray(a.mem[start:end]); mask=bytearray(len(raw))
    ins=list(md.disasm(raw,start))
    if not ins:return None
    raw=raw[:ins[-1].address+ins[-1].size-start];mask=mask[:len(raw)]
    for i in ins:
        offsets=[]
        if any(o.type==cs.x86.X86_OP_MEM and o.mem.base==cs.x86.X86_REG_RIP for o in i.operands):
            offsets.append((i.disp_offset,i.disp_size))
        if (i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL)) and i.imm_size:
            offsets.append((i.imm_offset,i.imm_size))
        if i.imm_size==8 and any(o.type==cs.x86.X86_OP_IMM and a.base<=o.imm<a.base+a.size for o in i.operands):
            offsets.append((i.imm_offset,i.imm_size))
        for off,n in offsets:
            pos=i.address-start+off;mask[pos:pos+n]=b'\1'*n
    return b''.join(b'.' if mask[i] else re.escape(bytes([c])) for i,c in enumerate(raw))

def code(rva):
    if rva in mapped:return mapped[rva]
    if not a.sections['.text'][0]<=rva<a.sections['.text'][1]:return None
    f=starts[max(0,bisect.bisect_right(starts,rva)-1)]
    boundaries=[i.address for i in md.disasm(a.mem[f:rva+16],f)]
    for size in (64,128,256,32,-96):
        # Include preceding instructions so small wrappers and epilogues are
        # not selected merely because they share a common prologue/return.
        before=[p for p in boundaries if max(f,rva-48)<=p<=rva]
        s=before[0] if before and rva in boundaries and size>0 else rva
        pat=pattern(s,min(rva+abs(size),a.sections['.text'][1]))
        if not pat:continue
        hits=list(re.finditer(pat,target,re.DOTALL))
        if len(hits)==1:
            result=lo+hits[0].start()+rva-s
            mapped[rva]=result;evidence[rva]={'kind':'unique-code','window':hex(s),'length':size,'candidate':hex(result)}
            return result
    return None

# Every executable-address literal in Motor, plus its GUI integration.
files=list(Path('src/motor').glob('*.cpp'))+list(Path('src/motor').glob('*.hpp'))+list(Path('src/motor').glob('*.inl'))+[Path('src/circlevr/src/vkcore/ui_engine.cpp')]
requested=set()
for p in files:
    if p.name.endswith('_test.cpp') or p.name.startswith('build_'):continue
    for line in p.read_text().splitlines():
        if line.strip().startswith('//'):continue
        requested.update(int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',line) if 0x400000<=int(x,16)<a.size)
for rva in sorted(requested):
    if rva<a.sections['.text'][1]:code(rva)

# Named IAT imports are store-independent identities, not fixed import offsets.
imports={}
for dll in other.DIRECTORY_ENTRY_IMPORT:
    for imp in dll.imports:imports[(dll.dll,imp.name,imp.ordinal)]=imp.address-b.base
for dll in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in dll.imports:
        old=imp.address-a.base;new=imports.get((dll.dll,imp.name,imp.ordinal))
        if old in requested and new is not None:
            mapped[old]=new;evidence[old]={'kind':'named-import','dll':dll.dll.decode(),'name':str(imp.name),'candidate':hex(new)}

# RIP-relative loads/LEAs give independent evidence for globals/vtables/cvars.
refs={r:[] for r in requested if r>=a.sections['.text'][1] and r not in mapped}
oldlo,oldhi=a.sections['.text']
for m in re.finditer(rb'(?:[\x40-\x4f])?[\x8d\x8b\x89\x80\x83\xff\x39\x3b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',a.mem[oldlo:oldhi],re.DOTALL):
    at=oldlo+m.start()
    # Most candidates are unrelated; avoid decoding every instruction.
    disp=struct.unpack_from('<i',m.group(),len(m.group())-4)[0]
    if not any(at+len(m.group())+disp+extra in refs for extra in (0,1,4)):continue
    i=next(md.disasm(a.mem[at:at+16],at),None)
    if not i:continue
    for o in i.operands:
        if o.type==cs.x86.X86_OP_MEM and o.mem.base==cs.x86.X86_REG_RIP:
            dest=at+i.size+o.mem.disp
            if dest in refs:refs[dest].append(at)
for rva,sites in refs.items():
    results={};proof=[]
    for site in sites:
        found=code(site)
        if found is None:continue
        i=next(md.disasm(b.mem[found:found+16],found))
        dest=next(found+i.size+o.mem.disp for o in i.operands if o.type==cs.x86.X86_OP_MEM and o.mem.base==cs.x86.X86_REG_RIP)
        results[dest]=results.get(dest,0)+1;proof.append([hex(site),hex(found),hex(dest)])
    if len(results)==1:
        mapped[rva]=next(iter(results));evidence[rva]={'kind':'rip-target','references':proof,'candidate':hex(mapped[rva])}
    else:failed[rva]={'reason':'no unique data target','references':proof}

# Reviewed interior fields; class/field equivalence is recorded independently
# in steam-type-layout-comparison.json (all 14425 classes match). In particular
# renderSystem+158 is the guard, +160 the separately matched counter. The
# global frame-time packet is copied as a block; +18 is the simulation token.
if (a.timestamp,a.size,b.timestamp,b.size)==(0x6a16bd47,0x746e000,0x6a16bd62,0x74ee000):
    for rva,parent in [(0x3f061c0,0x3f061b8),(0x4a71948,0x4a717f0),(0x4c523a8,0x4c52390)]:
        if rva in requested and parent in mapped:
            dest=mapped[parent]+rva-parent
            assert a.mem[rva:rva+8]==b.mem[dest:dest+8]
            mapped[rva]=dest;evidence[rva]={'kind':'reviewed-interior-field','parent':hex(parent),'offset':rva-parent,'candidate':hex(dest)}
    # Ambiguous tiny registration functions need the name anchor, not just a
    # wildcard prologue. Both instructions at 1EA33B name this exact cvar;
    # both following stores write wrapper+30, and 1EA380 takes that wrapper.
    for im in (a,b):
        nameAt=0x1ea33b+7+struct.unpack_from('<i',im.mem,0x1ea33b+3)[0]
        assert im.cstr(nameAt)=='r_lensFlaresRatio'
    old=0x1ea380+7+struct.unpack_from('<i',a.mem,0x1ea380+3)[0]
    dest=0x1ea380+7+struct.unpack_from('<i',b.mem,0x1ea380+3)[0]
    assert old==0x44d4150
    mapped[old]=dest;evidence[old]={'kind':'named-cvar-registration','name':'r_lensFlaresRatio','referenceSite':'0x1ea380','candidateSite':'0x1ea380','candidate':hex(dest)}

# Interior fields of a verified global/vtable retain their measured offset.
# Require the stored pointer value (when present) to resolve to matched code.
for rva in requested-mapped.keys():
    for parent in sorted(mapped,reverse=True):
        if parent<a.sections['.text'][1] or not 0<rva-parent<=0x100:continue
        oldptr=a.u64(rva)-a.base
        newptr=b.u64(mapped[parent]+rva-parent)-b.base
        if a.sections['.text'][0]<=oldptr<a.sections['.text'][1] and code(oldptr)==newptr:
            mapped[rva]=mapped[parent]+rva-parent;evidence[rva]={'kind':'interior-function-pointer','parent':hex(parent),'function':hex(oldptr),'candidate':hex(mapped[rva])};break

report={'reference':{'path':args.reference,'sha256':hashlib.sha256(Path(args.reference).read_bytes()).hexdigest(),'timestamp':hex(a.timestamp),'imageSize':hex(a.size)},
        'candidate':{'path':args.candidate,'sha256':hashlib.sha256(Path(args.candidate).read_bytes()).hexdigest(),'timestamp':hex(b.timestamp),'imageSize':hex(b.size)},
        'addresses':{hex(r):{'target':hex(mapped[r]),'evidence':evidence[r],
            'referenceBytes':a.mem[r:r+32].hex(),'candidateBytes':b.mem[mapped[r]:mapped[r]+32].hex()} for r in sorted(requested) if r in mapped},
        'unresolved':{hex(r):failed.get(r,{'reason':'no unique instruction match'}) for r in sorted(requested) if r not in mapped}}
Path(args.out).write_text(json.dumps(report,indent=2))
print('Mapped',len(report['addresses']),'of',len(requested),'addresses; unresolved:',list(report['unresolved']))
