"""Read-only Motor type-table probe. Does not assume Eternal's record layout is valid."""
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'python-deps'))
import numpy as np
from extract_typeinfo import Image

im = Image(sys.argv[1])
out = {'image_base': hex(im.base), 'image_size': hex(im.size), 'pe_timestamp': im.timestamp, 'sections': im.sections, 'types': {}}
for name in ['renderView_t', 'idRenderView', 'idRenderSystemLocal', 'idPlayer']:
    start = 0
    records = []
    needle = name.encode() + b'\0'
    while True:
        rva = im.mem.find(needle, start)
        if rva < 0: break
        start = rva + len(needle)
        if rva > 0 and im.mem[rva-1] != 0: continue
        refs = np.nonzero(im.qwords == im.base + rva)[0]
        for index in refs[:24]:
            index = int(index)
            fields = []
            for i in range(max(0,index-2), min(len(im.qwords),index+12)):
                value = int(im.qwords[i])
                text = im.cstr(value-im.base, 160) if im.base <= value < im.base+im.size else ''
                if not all(32 <= ord(c) < 127 for c in text): text = ''
                fields.append({'rva':hex(i*8),'value':hex(value),'text':text})
            records.append({'string_rva':hex(rva),'reference_rva':hex(index*8),'fields':fields})
    out['types'][name] = records
print(json.dumps(out, indent=2))
