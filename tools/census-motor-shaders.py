"""Read-only SPIR-V reflection of captured Motor shaders; no transformations."""
import json
import struct
import sys
from collections import Counter
from pathlib import Path

def string(words): return struct.pack('<'+'I'*len(words),*words).split(b'\0',1)[0].decode('utf-8','replace')
def reflect(path):
    raw=path.read_bytes()
    if len(raw)%4: return None
    words=struct.unpack('<'+'I'*(len(raw)//4),raw)
    if len(words)<5 or words[0]!=0x07230203: return None
    names={}; member_names={}; decorations={}; member_decorations={}; types={}; variables=[]; entries=[]
    i=5
    while i<len(words):
        count,op=words[i]>>16,words[i]&0xffff
        if count<1 or i+count>len(words): return None
        args=words[i+1:i+count]
        if op==5: names[args[0]]=string(args[1:])
        elif op==6: member_names[(args[0],args[1])]=string(args[2:])
        elif op==15: entries.append({'model':args[0],'name':string(args[2:])})
        elif 19<=op<=39 and args: types[args[0]]={'op':op,'args':args[1:]}
        elif op==59: variables.append(args[:3])
        elif op==71: decorations.setdefault(args[0],{})[args[1]]=args[2:]
        elif op==72: member_decorations.setdefault((args[0],args[1]),{})[args[2]]=args[3:]
        i+=count
    buffers=[]
    for pointer,var,storage in variables:
        if storage not in (2,12): continue
        ptr=types.get(pointer,{})
        if ptr.get('op')!=32: continue
        target=ptr['args'][1]
        typ=types.get(target,{})
        if typ.get('op')!=30: continue
        deco=decorations.get(var,{})
        members=[]
        for index,kind in enumerate(typ['args']):
            md=member_decorations.get((target,index),{})
            members.append({'index':index,'name':member_names.get((target,index),''),'offset':md.get(35,[None])[0], 'type':types.get(kind),'matrix_stride':md.get(7,[None])[0]})
        buffers.append({'name':names.get(var,''),'type_name':names.get(target,''),'storage_class':storage,'set':deco.get(34,[None])[0],'binding':deco.get(33,[None])[0],'members':members})
    return {'hash':path.stem,'entries':entries,'buffers':buffers}

root=Path(sys.argv[1]); shaders=[]; stages=Counter(); layouts=Counter(); matrix_candidates=[]
for path in root.glob('modules/*.spv'):
    data=reflect(path)
    if not data: continue
    shaders.append(data)
    for entry in data['entries']: stages[entry['model']]+=1
    for buffer in data['buffers']:
        schema=json.dumps(buffer,sort_keys=True)
        layouts[schema]+=1
        if any(member.get('type',{}).get('op')==24 for member in buffer['members']): matrix_candidates.append({'shader':data['hash'],'buffer':buffer})
out={'module_count':len(shaders),'execution_models':dict(stages),'matrix_buffer_count':len(matrix_candidates),'matrix_buffers':matrix_candidates,'layouts':[{'count':count,'buffer':json.loads(schema)} for schema,count in layouts.most_common()]}
destination=root/'motor-reflection.json'
destination.write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps({k:out[k] for k in ('module_count','execution_models','matrix_buffer_count')}))
print(f'Complete read-only reflection: {destination}')
