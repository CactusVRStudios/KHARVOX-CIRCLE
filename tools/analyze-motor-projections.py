"""Check actual Motor CC8C30 output captured by the scoped VR adapter."""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python-deps"))
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("run", type=Path)
args = parser.parse_args()
results = []
for path in sorted(args.run.glob("motor-projections-eye*.bin")):
    data = path.read_bytes()
    def matrix(offset):
        return np.array(struct.unpack_from("<16f", data, offset - 0x18790)).reshape(4, 4)
    p, invp = matrix(0x18790), matrix(0x187D0)
    view, invview = matrix(0x18850), matrix(0x188D0)
    vp, invvp = matrix(0x18910), matrix(0x18990)
    centered, invcentered = matrix(0x18BD0), matrix(0x18C90)
    error = lambda a, b: float(np.max(np.abs(a - b)))
    checks = {
        "projection_inverse": error(p @ invp, np.eye(4)),
        "view_inverse": error(view @ invview, np.eye(4)),
        "world_vp": error(p @ view, vp),
        "world_inverse_vp": error(invview @ invp, invvp),
        "centered_reverse_inverse": error(centered @ invcentered, np.eye(4)),
    }
    right = (1 + p[0, 2]) / p[0, 0]
    left = (-1 + p[0, 2]) / p[0, 0]
    up = (1 + p[1, 2]) / p[1, 1]
    down = (-1 + p[1, 2]) / p[1, 1]
    row3 = centered[3, :3]
    axis = int(np.argmax(np.abs(row3)))
    centered_depth = [float(centered[2, axis] / -row3[axis]), float(centered[2, 3])]
    results.append({
        "file": path.name,
        "pass": all(np.isfinite(v) and v < 0.01 for v in checks.values()),
        "maximum_absolute_errors": checks,
        "projection": p.tolist(),
        "centered_depth": centered_depth,
        "fov_left_right_up_down_radians": np.arctan([left, right, up, down]).tolist(),
    })
report = {"snapshots": results, "pass": bool(results) and all(r["pass"] for r in results)}
(args.run / "motor-projection-analysis.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
sys.exit(0 if report["pass"] else 1)
