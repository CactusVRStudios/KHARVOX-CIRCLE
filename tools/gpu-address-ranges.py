"""Resolve a shader device address using an r189 inventory; never guess a buffer."""
import argparse
import json
from pathlib import Path


def resolve(rows, address, size):
    if size <= 0 or size > 65536 or address % 4 or size % 4 or address + size > 2**64:
        raise ValueError("Require an aligned, non-overflowing range of at most 64 KiB")
    live = {}
    for row in rows:
        if row["event"] == "start":
            live.clear()  # Each inventory republishes all known live addresses.
        elif row["event"] == "destroy_buffer":
            live.pop(row["buffer"], None)
        elif row["event"] == "buffer_address":
            live[row["buffer"]] = row
    matches = []
    for row in live.values():
        base = int(row["address"], 16)
        if base and base <= address and address + size <= base + row["bytes"]:
            matches.append((row["buffer"], address - base, size, row["generation"]))
    if len(matches) != 1:
        raise ValueError(f"Address has {len(matches)} candidate buffers; ownership unresolved")
    return matches[0]


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run", type=Path)
    ap.add_argument("address", type=lambda s: int(s, 0))
    ap.add_argument("size", type=lambda s: int(s, 0))
    args = ap.parse_args()
    rows = [json.loads(line) for line in (args.run / "gpu-bindings.jsonl").read_text().splitlines()]
    print(*resolve(rows, args.address, args.size))
