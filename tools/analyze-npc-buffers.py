"""Read-only r178 CPU trace analysis. Never infers GPU completion from CPU order."""
import argparse
import collections
import json
import re
from pathlib import Path


def analyze(lines):
    counts = collections.Counter()
    frames, joints, uploads, resources, writers = {}, {}, {}, {}, {}
    identities = {}
    parts = collections.defaultdict(collections.Counter)
    samples = collections.defaultdict(list)

    def record(kind, data):
        counts[kind] += 1
        if len(samples[kind]) < 12:
            samples[kind].append(data)

    for line in lines:
        match = re.search(r"motor-buffer-(ring|joints|skin|upload|model|resources|bind|identity|placement|result): (.*)", line)
        if not match:
            continue
        kind, body = match.groups()
        words = body.split()
        d = dict(zip(words[::2], words[1::2]))
        counts["events_" + kind] += 1
        if kind == "identity":
            if d["model"] in identities or len(identities)<256:
                identities[d["model"]]=d
        elif kind in ("placement", "result"):
            if d["model"] not in parts and len(parts)>=256:
                continue
            part = parts[d["model"]]
            if kind == "placement":
                part["applied_pairs"] += int(d["applied"])
                part["max_before_position_delta"] = max(part["max_before_position_delta"], float(d["beforePositionDelta"]))
                part["max_before_axis_delta"] = max(part["max_before_axis_delta"], float(d["beforeAxisDelta"]))
            else:
                part["numeric_upload_pairs"] += 1
                part["max_upload_matrix_delta"] = max(part["max_upload_matrix_delta"], float(d["matrixDelta"]))
                part["max_upload_position_delta"] = max(part["max_upload_position_delta"], float(d["positionDelta"]))
                part["max_upload_axis_delta"] = max(part["max_upload_axis_delta"], float(d["axisDelta"]))
                if d.get("previousValid") == "1":
                    part["previous_matrix_pairs"] += 1
                    part["max_previous_matrix_delta"] = max(
                        part["max_previous_matrix_delta"], float(d["previousMatrixDelta"]))
        elif kind == "ring" and d.get("active") == "1":
            key = d["frame"]
            slots = (d["current"], d["previous"])
            if key in frames and frames[key] != slots:
                record("inconsistent_slots_same_render", d)
            frames[key] = slots
            if d.get("first") == "1" and d.get("ordered") == "0":
                record("out_of_order_first_request", d)
            if d.get("tagMismatch") == "1":
                record("changed_tag_same_render", d)
        elif kind in ("resources", "bind"):
            key = (d["manager"], d["frame"])
            value = (d["slot"], d["joints"], d["matrices"])
            if key in resources and resources[key] != value:
                record("upload_bind_resource_difference", d)
            resources[key] = value
            if kind == "resources" and d.get("nativeUpload") == "1":
                for resource in ("joints", "matrices"):
                    address = d[resource]
                    writer_key = (d["manager"], resource, address)
                    old = writers.get(writer_key)
                    if old and old["frame"] != d["frame"]:
                        distance = (int(d["frame"]) - int(old["frame"])) & 0xffffffff
                        if distance <= 1:
                            record("adjacent_render_resource_rewrite_candidate",
                                   {"before": old, "after": d, "resource": resource})
                    writers[writer_key] = d
        elif kind == "joints":
            key = (d["source"], d["token"])
            if d["eye"] == "0":
                joints[key] = d
            elif key in joints:
                left = joints[key]
                if (int(d["frame"]) - int(left["frame"])) & 0xffffffff == 1:
                    counts["matched_joint_pairs"] += 1
                    if any(left[k] != d[k] for k in ("current", "previous", "descriptorHash")):
                        record("joint_pair_difference", {"left": left, "right": d})
                del joints[key]
        elif kind == "upload":
            key = (d["model"], d["manager"], d["index"], d["token"])
            if d["eye"] == "0":
                uploads[key] = d
            elif key in uploads:
                left = uploads.pop(key)
                if (int(d["frame"]) - int(left["frame"])) & 0xffffffff == 1:
                    counts["matched_npc_upload_pairs"] += 1
                    if d["model"] in parts or len(parts)<256:
                        part = parts[d["model"]]
                        part["matched_upload_pairs"] += 1
                        part["matrix_hash_differences"] += left["matrixHash"] != d["matrixHash"]
                        part["joint_index_differences"] += left["joint"] != d["joint"]
                    if left["joint"] != d["joint"] or left["matrixHash"] != d["matrixHash"]:
                        record("npc_upload_difference", {"left": left, "right": d})
                    if left["skinValid"] == d["skinValid"] == "1":
                        def selected(x):
                            s = x["skinSlot"]
                            return (x.get("handle" + s), x.get("offset" + s))
                        if selected(left) != selected(d):
                            record("different_geometry_selection_not_proven_fault",
                                   {"left": left, "right": d})
        # Bounded memory even for a long log or traces containing no right eyes.
        for cache in (frames, joints, uploads, resources, writers):
            if len(cache) > 8192:
                cache.clear()
    return {
        "counts": dict(counts),
        "identities": identities,
        "model_parts": {model: {"identity": identities.get(model), "metrics": dict(metrics)}
                        for model, metrics in parts.items()},
        "examples": dict(samples),
        "limits": [
            "CPU observations, not final GPU draw captures or completion fences.",
            "Descriptor hash is not a joint-payload hash; equal hashes are not proof of equal skinned geometry.",
            "Different geometry slots can be valid double buffering. No GPU lifetime violation is inferred.",
            "Only registered nearby NPCs and validated native hook sites are covered.",
            "A clean or empty trace does not prove absence of the visual bug.",
        ],
    }


def self_test():
    lines = [
        "motor-buffer-ring: site 0 frame 10 active 1 eye 0 token 1 first 1 ordered 1 tagMismatch 0 current 0 previous 2",
        "motor-buffer-ring: site 1 frame 10 active 1 eye 0 token 1 first 0 ordered 1 tagMismatch 0 current 0 previous 2",
        "motor-buffer-ring: site 4 frame 12 active 1 eye 1 token 1 first 1 ordered 0 tagMismatch 0 current 1 previous 0",
        "motor-buffer-ring: site 3 frame 12 active 1 eye 1 token 2 first 0 ordered 1 tagMismatch 1 current 1 previous 0",
        "motor-buffer-joints: source ABC token 1 frame 10 eye 0 current 5 previous 4 descriptorHash abc",
        "motor-buffer-joints: source ABC token 1 frame 11 eye 1 current 6 previous 4 descriptorHash def",
        "motor-buffer-resources: frame 10 eye 0 token 1 manager AAA slot 0 joints BBB matrices CCC nativeUpload 1",
        "motor-buffer-bind: frame 10 eye 0 token 1 manager AAA slot 0 joints BBB matrices CCC",
        "motor-buffer-resources: frame 11 eye 1 token 1 manager AAA slot 1 joints BBB matrices DDD nativeUpload 1",
    ]
    lines += ["motor-buffer-identity: model NPC committed C asset md6def/characters/test.md6 instance actor_3 classification character_candidate owner 0 ownerKnown 0 renderBindingCandidate ABC firstPerson 0 playerName 0 playerEntity a71 playerFresh 1 x 1 y 2 z 3 distance 4 frame 10 eye 0",
              "motor-buffer-identity: model PLAYER committed D asset indiana_body instance player classification player_or_attachment owner a71 ownerKnown 1 firstPerson 1 x 0 y 0 z 0"]
    result = analyze(lines)
    assert result["identities"]["NPC"]["ownerKnown"] == "0"
    assert result["identities"]["PLAYER"]["ownerKnown"] == "1"
    assert result["identities"]["NPC"]["instance"] == "actor_3"
    c = result["counts"]
    assert c["out_of_order_first_request"] == c["changed_tag_same_render"] == 1
    assert c["matched_joint_pairs"] == c["joint_pair_difference"] == 1
    assert "inconsistent_slots_same_render" not in c
    assert "upload_bind_resource_difference" not in c
    assert c["adjacent_render_resource_rewrite_candidate"] == 1
    assert not analyze([])["counts"]
    trace = [
        "motor-buffer-placement: model NPC token 1 frame 11 eye 1 extended 1 leftFrame 10 beforePositionDelta 0.025 beforeAxisDelta 0.1 dx 0.01 dy 0 dz 0.025 applied 1",
        "motor-buffer-result: model NPC token 1 frame 11 eye 1 matrixDelta 0 positionDelta 0 axisDelta 0 jointL 3 jointR 3 extended 1 enabled 1",
        "motor-buffer-upload: model NPC manager M index 1 token 1 frame 10 eye 0 joint 3 matrixHash abc skinValid 0",
        "motor-buffer-upload: model NPC manager M index 1 token 1 frame 11 eye 1 joint 3 matrixHash abc skinValid 0",
        "motor-buffer-identity: model NPC instance npc_satipo_spawn_1:wear[FOOTWEAR]",
    ]
    part = analyze(trace)["model_parts"]["NPC"]
    assert part["identity"]["instance"].endswith("FOOTWEAR]")
    assert part["metrics"]["applied_pairs"] == part["metrics"]["matched_upload_pairs"] == 1
    assert part["metrics"]["max_before_position_delta"] == .025
    assert part["metrics"]["max_upload_matrix_delta"] == part["metrics"]["matrix_hash_differences"] == 0
    trace.append("motor-buffer-result: model NPC token 2 frame 13 eye 1 matrixDelta 0.2 positionDelta 0.2 axisDelta 0.1 jointL 3 jointR 3 extended 1 enabled 0")
    assert analyze(trace)["model_parts"]["NPC"]["metrics"]["max_upload_position_delta"] == .2
    trace.append("motor-buffer-result: model NPC token 3 frame 15 eye 1 matrixDelta 0 positionDelta 0 axisDelta 0 jointL 3 jointR 3 extended 1 enabled 1 previousValid 1 previousMatrixDelta 0.125")
    previous = analyze(trace)["model_parts"]["NPC"]["metrics"]
    assert previous["previous_matrix_pairs"] == 1 and previous["max_previous_matrix_delta"] == .125
    print("NPC buffer analyzer self-test passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.log:
            parser.error("log path required")
        with args.log.open(encoding="utf-8", errors="replace") as stream:
            result = analyze(stream)
        text = json.dumps(result, indent=2)
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        print(text)
