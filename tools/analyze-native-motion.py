"""Inspect r184 native RGBA16F motion captures without changing game images.

Motion maps are not disparity maps. Unregistered L/R pixel differences are not
evidence of a stereo defect; identify scene regions in matching color captures.
"""
import argparse
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image


def inspect(sidecar, maps):
    text = sidecar.read_text(encoding="utf-8-sig")
    report = {"source": str(sidecar.resolve()), "eyes": {}}
    arrays = {}
    for eye in ("L", "R"):
        header = re.search(
            rf"eye {eye}: tick (\d+), backend frame (\d+), eye frame (\d+);.*?"
            r"Motor Streamline input, scale ([^ ]+) ([^ ]+) jitter ([^ ]+) ([^ ;]+); clipToPrevious ([^=;]+)",
            text,
        )
        grid = re.search(rf"eye {eye}: [^\n]+, VkFormat (\d+), (\d+)x(\d+), (\d+) bytes", text)
        if not header or not grid:
            raise ValueError(f"Missing Motor metadata for {eye}")
        fmt, width, height, size = map(int, grid.groups())
        if fmt != 97 or not (0 < width <= 16384 and 0 < height <= 16384):
            raise ValueError(f"Unexpected format/size for {eye}")
        path = sidecar.with_name(sidecar.stem + f"-{eye}.raw")
        if size != width * height * 8 or path.stat().st_size != size:
            raise ValueError(f"Incomplete image for {eye}")
        raw = np.fromfile(path, dtype="<f2").reshape(height, width, 4)
        mv = raw[:, :, :2].astype(np.float32)
        scale = np.array(header.group(4, 5), dtype=np.float32)
        scaled = mv * scale
        finite = np.isfinite(scaled).all(axis=2)
        valid = scaled[finite]
        if not valid.size:
            raise ValueError(f"No finite motion vectors for {eye}")
        arrays[eye] = scaled
        info = {
            "tick": int(header[1]), "backend": int(header[2]), "eye_sequence": int(header[3]),
            "width": width, "height": height, "scale": scale.tolist(),
            "jitter": [float(header[6]), float(header[7])],
            "clip_to_previous": [float(x) for x in header[8].split()],
            "nonfinite_pixels": int((~finite).sum()),
            "nonzero_fraction": float(np.any(valid != 0, axis=1).mean()),
            "scaled_xy_median": np.median(valid, axis=0).tolist(),
            "scaled_abs_xy_percentiles_50_90_99": np.percentile(np.abs(valid), [50, 90, 99], axis=0).tolist(),
            "scaled_xy_range": [valid.min(axis=0).tolist(), valid.max(axis=0).tolist()],
        }
        if len(info["clip_to_previous"]) != 16:
            raise ValueError(f"Incomplete matrix for {eye}")
        # Spatial medians help locate anomalous regions without pretending the
        # two eyes view the same geometry at identical image coordinates.
        info["tiles_4x4_median"] = []
        for row in np.array_split(scaled, 4, axis=0):
            for tile in np.array_split(row, 4, axis=1):
                values = tile[np.isfinite(tile).all(axis=2)]
                info["tiles_4x4_median"].append(np.median(values, axis=0).tolist() if values.size else None)
        report["eyes"][eye] = info
    left, right = [report["eyes"][eye] for eye in ("L", "R")]
    report["identity_valid"] = left["tick"] == right["tick"] and left["backend"] + 1 == right["backend"]
    if not report["identity_valid"]:
        raise ValueError("L/R metadata do not identify one adjacent render pair")
    if maps:
        bound = max(0.0001, max(max(e["scaled_abs_xy_percentiles_50_90_99"][2]) for e in (left, right)))
        report["map_half_range_scaled_xy"] = bound
        for eye, mv in arrays.items():
            rgb = np.full((*mv.shape[:2], 3), 128, dtype=np.uint8)
            rgb[:, :, :2] = np.clip(np.nan_to_num(mv) / bound * 127 + 128, 0, 255).astype(np.uint8)
            rgb[~np.isfinite(mv).all(axis=2)] = [255, 0, 255]
            output = sidecar.with_name(sidecar.stem + f"-{eye}-motion.png")
            Image.fromarray(rgb).save(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="Sidecar or directory containing native motion captures")
    parser.add_argument("--maps", action="store_true", help="Write XY diagnostic maps, red=X green=Y, grey=zero")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    files = [args.path] if args.path.is_file() else sorted(args.path.rglob("*.txt"))
    reports, rejected = [], []
    for file in files:
        if not file.with_name(file.stem + "-L.raw").exists():
            continue
        try:
            reports.append(inspect(file, args.maps))
        except (OSError, ValueError) as error:
            rejected.append({"source": str(file), "error": str(error)})
    output = {
        "note": "Native per-eye motion inputs. Not a disparity map or a diagnosis of the artifact. "
                "Compare scene regions using matching color captures and camera metadata.",
        "pairs": reports, "rejected": rejected,
    }
    args.out.write_text(json.dumps(output, indent=2, allow_nan=False), encoding="utf-8")
    print(json.dumps({"pairs": len(reports), "rejected": rejected, "output": str(args.out)}))


if __name__ == "__main__":
    main()
