"""Motor-specific offline adaptation of CircleVR's type-info reader.

Motor 1.0.17.0 uses class records with a leading metadata pointer and 0x58 stride,
observed around idRenderView/idRenderSystemLocal in this executable. Variable records
retain the donor's 0x48 stride. These are reflection facts, not validated live camera addresses.
"""
import argparse
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'python-deps'))
import numpy as np
from extract_typeinfo import Image, is_class_record, read_variables, print_class

def extract(im):
    q = im.qwords
    lo, hi = im.sections['.rdata']
    in_rdata = (q >= im.base + lo) & (q < im.base + hi)
    n = len(q)
    candidates = set(map(int, np.nonzero(in_rdata[:n-11] & in_rdata[1:n-10] & ((q[2:n-9] >> np.uint64(32)) == 0))[0]))
    seen = set()
    tables = []
    classes = {}
    for c in sorted(candidates):
        if c in seen: continue
        k, records = c, []
        while k in candidates and is_class_record(im, k*8):
            seen.add(k)
            records.append(k*8)
            k += 11
        if len(records) < 8: continue
        tables.append({'first_name_rva':hex(records[0]),'records':len(records),'stride':0x58})
        for rec in records:
            name = im.cstr(im.ptr(rec))
            variables = read_variables(im, im.ptr(rec+32))
            size = im.i32(rec+16)
            # Reject inconsistent fields instead of turning guessed layouts into engine offsets.
            if size > 0 and any(v['offset'] < 0 or (v['size'] > 0 and v['offset']+v['size'] > size) for v in variables): continue
            classes.setdefault(name, {'name':name,'super':im.cstr(im.ptr(rec+8)),'size':size,'record_rva':rec-8,'variables':variables})
    return tables, classes

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe')
    parser.add_argument('--json')
    parser.add_argument('--class',dest='classes',action='append')
    parser.add_argument('--grep')
    args = parser.parse_args()
    im = Image(args.exe)
    tables, classes = extract(im)
    print(f'Motor 0x58 class probe: {len(tables)} tables, {len(classes)} classes; timestamp {im.timestamp:#x}',file=sys.stderr)
    if args.json:
        Path(args.json).write_text(json.dumps({'pe_timestamp':im.timestamp,'class_stride':0x58,'tables':tables,'classes':classes},indent=2),encoding='utf-8')
    for name in args.classes or []: print_class(classes,name,args.grep)

if __name__=='__main__': main()
