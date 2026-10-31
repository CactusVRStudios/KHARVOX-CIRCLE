"""Reproduce the bounded r197 Satipo cache comparison (specific capture IDs)."""
import argparse
import json
import math
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("run", type=Path)
args = parser.parse_args()
def blob(i):
    return (args.run / f"gpu-buffer-{i}.bin").read_bytes()
def record(i):
    return struct.unpack_from("<II", blob(i), 8)  # surface cache entry 1
def compare(i, j):
    a, b = blob(i), blob(j)
    assert len(a) == len(b) == 32256
    distances = []
    for offset in range(0, len(a), 16):
        x = struct.unpack_from("<3e", a, offset)
        y = struct.unpack_from("<3e", b, offset)
        if x != y:
            distances.append(math.sqrt(sum((v-w)**2 for v, w in zip(x, y))))
    return {"byteIdentical": a == b, "changedPositionRecords": len(distances),
            "maxLocalDisplacementDelta": max(distances, default=0)}

result = {"session": 13, "nativeModel": 97156, "drawInstance": 117636}
for eye, uniform, current, previous, count in (
        ("left", 40, 42, 45, 49), ("right", 50, 52, 55, 59)):
    frame = int(struct.unpack_from("<f", blob(uniform), 48)[0])
    cur, prev = record(current), record(previous)
    result[eye] = {
        "frame": frame, "currentRecord": cur, "previousRecord": prev,
        "currentValid": cur[0] == ((3 << 16) | (frame & 65535)),
        "previousValid": prev[0] == ((3 << 16) | ((frame-1) & 65535)),
        "drawCount": struct.unpack("<I", blob(count))[0]}
result["currentLeftVsCurrentRight"] = compare(43, 54)
result["currentLeftVsPreviousRight"] = compare(43, 56)
result["previousLeftVsPreviousRight"] = compare(47, 56)
result["limitation"] = (
    "Post-pass snapshots, not shader instruction tracing. Captured payload prefix "
    "does not prove exact head vertex coverage or visual causality.")
print(json.dumps(result, indent=2))
