"""Plan bounded copies of verified motion-shader inputs, never inferred by binding number alone."""
import argparse
import json
from pathlib import Path

COMPUTE = "f6f0f641d50fbea0"
MOTION = "73e9816e2872c5f9"


def plan(draws, stages, instance, model):
    if instance < 0 or model < 0:
        raise ValueError("Indices must be nonnegative and confirmed for this scene.")
    if COMPUTE in stages:
        fields = {0: (0, None), 4: (instance * 48, 48), 6: (instance * 48, 48)}
    elif MOTION in stages:
        fields = {12: (instance * 64, 64), 16: (instance * 64, 64),
                  13: (model * 4, 4), 17: (model * 4, 4)}
    else:
        raise ValueError("Unverified shader layout; do not reuse another variant's indices.")
    eyes = {}
    for row in draws:
        if row.get("tagMismatch") or row.get("eye") not in (0, 1):
            continue
        eyes.setdefault(row["eye"], row)
    if len(eyes) != 2:
        raise ValueError("A complete two-eye inventory is required.")
    left, right = eyes[0], eyes[1]
    if not left.get("token") or left["token"] != right["token"] or right["backend"] != left["backend"] + 1:
        raise ValueError("Inventory eyes do not form one adjacent stereo pair.")
    result = set()
    for row in (left, right):
        bindings = {b["binding"]: b for b in row["bindings"]
                    if b.get("setNumber") == 0 and b.get("element") == 0}
        for binding, (relative, length) in fields.items():
            b = bindings.get(binding)
            if not b or not b.get("resolved") or not b.get("generation"):
                raise ValueError(f"Missing resolved binding {binding}.")
            length = b["size"] if length is None else length
            if not 0 < length <= 65536 or relative + length > b["size"]:
                raise ValueError(f"Binding {binding}: requested record outside descriptor range.")
            offset = b["offset"] + relative
            if offset % 4 or length % 4:
                raise ValueError("Unaligned range.")
            result.add((b["buffer"], offset, length, b["generation"]))
    if len(result) > 16:
        raise ValueError("Capture exceeds the bounded range count.")
    return sorted(result)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run", type=Path)
    ap.add_argument("--pipeline", required=True)
    ap.add_argument("--instance", type=int, required=True)
    ap.add_argument("--model", type=int, required=True)
    ap.add_argument("--session", type=int, help="Use a complete inventory session, not a later filtered copy.")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    catalog = [json.loads(line) for line in (args.run / "shaders/pipelines.jsonl").read_text().splitlines()]
    pipeline = next(x for x in reversed(catalog) if x.get("pipeline") == args.pipeline)
    rows = [json.loads(line) for line in (args.run / "gpu-bindings.jsonl").read_text().splitlines()]
    draws = [x for x in rows if x.get("pipeline") == args.pipeline and x.get("bindings")]
    session = args.session if args.session is not None else max(x["session"] for x in draws)
    draws = [x for x in draws if x["session"] == session]
    ranges = plan(draws, {x["module"] for x in pipeline["stages"]}, args.instance, args.model)
    args.out.write_text("".join(f"{b} {o} {n} {g}\n" for b, o, n, g in ranges))
    print(f"{len(ranges)} ranges from session {session}; runtime must still validate resource generations.")
    print("These are input records. Copies after a draw do not prove contents at shader execution.")


if __name__ == "__main__":
    main()
