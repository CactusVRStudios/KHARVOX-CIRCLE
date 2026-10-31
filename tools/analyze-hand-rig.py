"""Analyze read-only Motor post-arm samples; this does not certify visible IK."""
import argparse
import json
import math
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("run", type=Path)
args = parser.parse_args()
pattern = re.compile(r"\[\s*([0-9.]+)\].*motor-arm-solved: rig (\w+) hand (\d) slot (\d) joint (\d+) parent (-?\d+) count (\d+) pos ([-\d.]+) ([-\d.]+) ([-\d.]+).*cached (\d) error ([-\d.]+)")
groups = {}
errors = []
for log in args.run.glob("circle-*.log"):
    for line in log.read_text(errors="replace").splitlines():
        m = pattern.search(line)
        if not m:
            continue
        t, rig, hand, slot, joint, parent, count, x, y, z, cached, error = m.groups()
        groups.setdefault((t, rig, hand), {})[int(slot)] = {
            "joint": int(joint), "parent": int(parent), "position": [float(x), float(y), float(z)]}
        if cached == "1":
            errors.append(float(error))
chains = []
for (t, rig, hand), slots in groups.items():
    if not all(s in slots for s in (3, 5, 6)):
        continue
    wrist, elbow, shoulder = (slots[s] for s in (3, 5, 6))
    chains.append({"time": float(t), "rig": rig, "native_slot": int(hand),
                   "shoulder": shoulder["joint"], "elbow": elbow["joint"], "wrist": wrist["joint"],
                   "parent_chain": wrist["parent"] == elbow["joint"] and elbow["parent"] == shoulder["joint"],
                   "upper_length": math.dist(shoulder["position"], elbow["position"]),
                   "lower_length": math.dist(elbow["position"], wrist["position"])})
report = {"samples": len(chains), "cached_comparisons": len(errors),
          "max_cached_position_error": max(errors, default=None),
          "copy_hierarchy_matches_native_cache": bool(errors) and max(errors) < .0001,
          "arm_chain_lengths_match_decl": bool(chains) and all(c["parent_chain"] and abs(c["upper_length"]-.3)<.001 and abs(c["lower_length"]-.2689)<.001 for c in chains),
          "first_chains": chains[:2], "visible_controller_ik_verified": False}
(args.run / "hand-rig-analysis.json").write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
