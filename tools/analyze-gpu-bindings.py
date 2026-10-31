"""r187 bounded Vulkan capture audit. No CPU index is accepted as GPU proof."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct

DRAW_EVENTS = {"draw", "draw_indexed", "CmdDrawIndirect", "CmdDrawIndexedIndirect",
               "CmdDrawIndirectCount", "CmdDrawIndexedIndirectCount",
               "dispatch", "dispatch_indirect"}


def command(row):
    return row.get("cb"), row.get("commandGeneration")


def fnv64(data):
    h = 14695981039346656037
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return hex(h)


def read_lines(path):
    if not path.exists():
        return []
    with path.open(encoding="utf-8-sig") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def spirv_bindings(path):
    """Decorated resource declarations, NOT proof that a draw executed a load."""
    if not path.exists():
        return {"missing": True}
    data = path.read_bytes()
    if len(data) % 4 or len(data) < 20:
        return {"invalid": True}
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    if words[0] != 0x07230203:
        return {"invalid": True}
    names, decorations = {}, {}
    i = 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 65535
        if n < 1 or i + n > len(words):
            return {"invalid": True}
        args = words[i+1:i+n]
        if op == 5 and len(args) >= 2:
            names[args[0]] = struct.pack("<%dI" % (len(args)-1), *args[1:]).split(b"\0")[0].decode(errors="replace")
        if op == 71 and len(args) >= 3 and args[1] in (33, 34):
            decorations.setdefault(args[0], {})[args[1]] = args[2]
        i += n
    return {"declarations": [{"id": k, "name": names.get(k, ""),
                              "set": d.get(34), "binding": d.get(33)}
                             for k, d in decorations.items()]}


def analyze(directory):
    rows = read_lines(directory / "gpu-bindings.jsonl")
    if not rows:
        raise ValueError("No capture records; a built or launched diagnostic is not GPU evidence.")
    events = collections.Counter(r["event"] for r in rows)
    complete = {r["id"]: r for r in rows if r["event"] == "gpu_complete"}
    submissions = collections.defaultdict(list)
    draws = collections.defaultdict(list)
    for r in rows:
        if r["event"] == "submit":
            submissions[command(r)].append(r)
        if r["event"] in DRAW_EVENTS:
            draws[command(r)].append(r)
    pipeline_rows = read_lines(directory / "shaders" / "pipelines.jsonl")
    pipelines = {r["pipeline"]: r for r in pipeline_rows}
    modules = {s.get("module") for r in pipeline_rows for s in r.get("stages", []) if s.get("module")}
    reflection = {m: spirv_bindings(directory / "shaders" / "modules" / (m + ".spv")) for m in modules}
    results, pairs = [], collections.defaultdict(list)
    for r in rows:
        if r["event"] != "gpu_copy" or r["status"] != "recorded":
            continue
        errors = []
        done = complete.get(r["id"])
        digest = None
        if not done:
            errors.append("GPU completion missing")
        else:
            path = directory / done["file"]
            if not path.exists():
                errors.append("payload missing")
            else:
                payload = path.read_bytes()
                if len(payload) != r["copyBytes"] or len(payload) != done["bytes"]:
                    errors.append("payload size mismatch")
                if fnv64(payload) != done["hash"]:
                    errors.append("payload hash mismatch")
                digest = hashlib.sha256(payload).hexdigest()
        submitted = submissions.get(command(r), [])
        if not submitted:
            errors.append("no submission of this command generation")
        if any(s.get("submitNumber", 1) != 1 for s in submitted):
            errors.append("command resubmitted")
        if any(s.get("descriptorChanged") or s.get("tagMismatch") for s in submitted):
            errors.append("descriptor or eye changed before submission")
        if r.get("eye") not in (0, 1) or not r.get("token"):
            errors.append("eye/pair unresolved")
        consumers = [d for d in draws.get(command(r), [])
                     if d.get("pass") == r.get("pass") and d.get("serial", 0) < r["serial"]]
        if r["phase"] == "before_dispatch":
            consumers = [d for d in consumers if d.get("drawNumber") == r.get("drawNumber")]
        if any(d.get("tagMismatch") for d in consumers):
            errors.append("eye changed during recording")
        set_ids = {b.get("set") for d in consumers for b in d.get("bindings", [])}
        if done and any(x["event"] == "descriptor_update" and x.get("set") in set_ids and
                        r["serial"] < x["serial"] < done["serial"] for x in rows):
            errors.append("descriptor updated before observed GPU completion")
        record = {k: r.get(k) for k in ("id", "session", "eye", "token", "backend", "cb",
                  "commandGeneration", "pass", "phase", "buffer", "generation", "copyOffset", "copyBytes",
                  "completeBoundRange")}
        record.update(sha256=digest, errors=errors, valid_snapshot=not errors,
                      matched_command_draws=len(consumers))
        results.append(record)
        if not errors:
            # Different handles may be different eye allocations; never silently
            # pair those by ordinal or assume their semantic roles match.
            key = (r["session"], r["token"], r["backend"]-r["eye"], r["buffer"], r["generation"],
                   r["copyOffset"], r["copyBytes"], r["phase"])
            pairs[key].append(record)
    comparisons = []
    for key, values in pairs.items():
        left, right = ([v for v in values if v["eye"] == e] for e in (0, 1))
        if not left or not right:
            continue
        comparisons.append({"key": list(key), "left_ids": [v["id"] for v in left],
                            "right_ids": [v["id"] for v in right],
                            "content_equal": len({v["sha256"] for v in values}) == 1,
                            "meaning": "same resource/range; NPC and shader-consumer attribution still required"})
    npc = [r for r in rows if r["event"] == "npc"]
    matrix_matches = []
    identities = {(r["token"], r["eye"], r["model"], r.get("cpuMatrix", "")): r for r in npc}
    for snap in results:
        if not snap["valid_snapshot"]:
            continue
        candidates = [(key, n) for key, n in identities.items()
                      if key[0] == snap["token"] and key[1] == snap["eye"] and len(key[3]) == 128]
        if not candidates:
            continue
        payload = (directory / complete[snap["id"]]["file"]).read_bytes()
        for key, n in candidates:
            needle = bytes.fromhex(key[3])
            offset = payload.find(needle)
            # Report a literal match, not a guessed binding slot or vertex output.
            if offset >= 0:
                matrix_matches.append(dict(snapshot=snap["id"], buffer=snap["buffer"],
                                           offset=snap["copyOffset"]+offset, eye=snap["eye"],
                                           token=snap["token"], model=n["model"], asset=n["asset"],
                                           instance=n["instance"], meaning="CPU matrix bytes found in GPU snapshot; shader use unproven"))
    paired_ids = {i for pair in comparisons for i in pair["left_ids"] + pair["right_ids"]}
    complete_pair_capture = bool(results) and all(
        s["valid_snapshot"] and s["id"] in paired_ids for s in results)
    return {
        "events": dict(events), "npc_assets": sorted({r["asset"] for r in npc}),
        "npc_eyes": dict(collections.Counter(str(r["eye"]) for r in npc)),
        "draws": sum(map(len, draws.values())), "pipelines": len(pipelines),
        "shader_declarations": reflection,
        "valid_snapshots": sum(r["valid_snapshot"] for r in results),
        "complete_pair_capture": complete_pair_capture,
        "unpaired_snapshot_ids": [s["id"] for s in results if s["id"] not in paired_ids],
        "snapshots": results, "same_resource_comparisons": comparisons,
        "npc_matrix_byte_matches": matrix_matches,
        "npc_gpu_equivalence_proven": False,
        "limits": [
            "Eye labels are captured from the backend at command begin; an independent job/attachment identity remains to be verified.",
            "Graphics payloads are copied after the render pass, compute inputs before dispatch. Shader-written-in-pass and cross-queue hazards remain unproven.",
            "Samples may be truncated. Vertex pulling/device addresses, descriptor templates and push descriptors need separate coverage.",
            "NPC CPU owner/index records and pipeline declarations do not prove which NPC generated a draw or pixel.",
            "The capture adds GPU work and barriers; it can change the timing of a lifetime race.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze(args.directory)
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k not in
                      ("shader_declarations", "snapshots", "same_resource_comparisons", "npc_matrix_byte_matches")}, indent=2))


if __name__ == "__main__":
    main()
