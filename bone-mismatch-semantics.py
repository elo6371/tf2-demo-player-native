#!/usr/bin/env python3
"""bone-mismatch-semantics.py -- T62: what boneMismatch counts, and how to assert it.

WHY THIS FILE EXISTS
  The trace counter 'scene: anim ... boneMismatch=N' reads like a health metric,
  so the obvious gate is N == 0. That assertion is wrong on this tree, and pinning
  it would fail every run that drives a player skeleton. What the counter actually
  counts:

    it is incremented at most ONCE PER SKINNED INSTANCE PER SCENE UPDATE, when
    that instance reported a skin matrix count that disagrees with its declared
    bone counts (main.cpp:9179-9182, the condition is
    skinBones != validBones || skinBones != skinMatrices.size()). It is NOT reset
    between updates (resetPerUpdate does not clear it), so the value in a trace is
    a running total over every update the process ran.

  The structural reason a mismatch exists at all is a helper bone: a bone that has
  a valid bind transform but no channel in the animation file, so skinBones is
  strictly smaller than validBones. On this corpus that is exactly demo (84 vs 82),
  heavy (79 vs 77) and scout (78 vs 76); the other six classes have no gap and so
  never increment the counter. The counter therefore separates 'this model has
  helper bones' from 'this model does not' -- it is an asset property, not a
  fidelity measurement, and a gate must not treat growth as a defect.

  Evidence: /d/_shared/t62-p1-bonemismatch/REPORT.md (semantics, code anchors,
  census cross-check over 19,487 updates), and the negative controls this file
  can generate (--mutate).

WHAT IT ASSERTS
  Tier A -- structural invariants, true on any corpus:
    A1  boneMismatch is monotone non-decreasing (one increment per instance, never
        a decrement).
    A2  every update's delta is <= that update's skinned count (one per instance).
    A3  every update's delta is <= that update's playerDraws (same bound, tighter).
    A4  where the per-update census is complete (playerDraws <= TRACE_CAP, the
        trace caps anim: ent= lines at 8), delta equals the number of census
        models that carry helper bones.
    A5  where the census is truncated, delta - census > 0 (truncation can only
        under-count the witness lines, never over-count the increments).
    A6  skinned + bindPose == draws, and gpuSkin uploaded == matrices.
    A7  the run is whole: the trace carries a capture: armed line (it reached
        --capture-tick) and closes as many scene: begin blocks as it opens. Every
        rule above is satisfied by a prefix of a run, and a prefix says nothing
        about the frame the gate exists to judge -- the known-invalid
        round1/animated.trace (4642 updates, stopped at scene tick 52543 of
        64161, no frame written) passes A1-A6 and is rejected only here.
  Tier B -- driver-path existence, binary across corpora:
    B1  a driven run (the trace carries anim: ent= lines) ends with boneMismatch>0
        and status=ok.
    B2  an undriven run ends with boneMismatch==0 and status=missing.
  Tier C -- corpus band, WARN only, never a verdict:
    C1  bm / updates inside [4.5, 5.0] (this corpus measured 4.8237-4.8605). It
        depends on the class mix and the demo length, so it is a snapshot, not an
        invariant.

WHY A GREEN RUN CANNOT BE VACUOUS
  --mutate writes a trace that breaks exactly one Tier A rule (a decrement, or an
  increment no census line explains), and --expect-fail requires this checker to
  reject it. The gate runs both controls, so 'pass' cannot mean 'the checks never
  fired'.

ANTI-PATTERNS THIS FILE EXISTS TO STOP
  * assert boneMismatch == 0 on any run that drives a player skeleton;
  * pin the absolute totals (29058 / 35577 / 37217 on this corpus);
  * read growth as 'the skeleton is falling apart' rather than 'another update
    drew a model with helper bones';
  * assert the gap on the trace counter -- the gap belongs to the probe's bone
    table, where it is exactly 2 for demo, heavy and scout.

Usage: python bone-mismatch-semantics.py <trace> [<trace> ...]
       python bone-mismatch-semantics.py --expect-fail <trace>
       python bone-mismatch-semantics.py --mutate decrease|unattributed <src> <dst>
Exit: 0 = every trace passed, or the expected failure was produced; 1 = a check
      failed, or an expected failure did not appear; 2 = bad arguments.
"""

import re
import sys

BEGIN = re.compile(r"^[0-9.]+ scene: begin ")
SET = re.compile(r"^[0-9.]+ scene: setInstances draws=(\d+) .*?playerDraws=(\d+)")
ENT = re.compile(r"^[0-9.]+ anim: ent=\d+ model=(\S+) state=(\S+) ")
ANIM = re.compile(
    r"^[0-9.]+ scene: anim skinned=(\d+) bindPose=(\d+) matrices=(\d+) "
    r"gpuSkin=(\d+)/(\d+) boneMismatch=(\d+)")
CAP = re.compile(r"capture: armed .*?frames=(\d+)")
DONE = re.compile(r"^[0-9.]+ scene: done\s*$")

GAP_MODELS = ("models/player/demo.mdl", "models/player/heavy.mdl",
              "models/player/scout.mdl")
TRACE_CAP = 8  # main.cpp caps the anim: ent= witness lines at 8 per update


def analyse(path):
    """The per-update rows of a trace, plus the capture frame count."""
    updates, current, frames = [], None, None
    began = ended = 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if BEGIN.match(line):
                began += 1
                current = {"playerDraws": 0, "ent": []}
                continue
            if DONE.match(line):
                ended += 1
                continue
            if current is None:
                continue
            match = SET.match(line)
            if match:
                current["draws"] = int(match.group(1))
                current["playerDraws"] = int(match.group(2))
                continue
            match = ENT.match(line)
            if match:
                current["ent"].append(match.group(1))
                continue
            match = ANIM.match(line)
            if match:
                current.update(
                    skinned=int(match.group(1)), bindPose=int(match.group(2)),
                    matrices=int(match.group(3)), gpu=int(match.group(4)),
                    uploaded=int(match.group(5)), bm=int(match.group(6)),
                    status=line.split("status=")[1].split()[0])
                updates.append(current)
                current = None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = CAP.search(line)
            if match:
                frames = int(match.group(1))
    return updates, frames, began, ended


def check(path):
    updates, frames, began, ended = analyse(path)
    parts = path.replace("\\", "/").split("/")
    name = parts[-2] + "/" + parts[-1]
    fails, warns = [], []
    if not updates:
        return name, None, ["no scene: anim lines"], []
    previous = None
    count = 0
    negative = over_skinned = over_players = 0
    bad_sum = bad_upload = 0
    uncapped = fitted = 0
    residual = {}
    for update in updates:
        count += 1
        if previous is not None:
            delta = update["bm"] - previous
            if delta < 0:
                negative += 1
            if delta > update["skinned"]:
                over_skinned += 1
            if delta > update["playerDraws"]:
                over_players += 1
            census = sum(1 for model in update["ent"] if model in GAP_MODELS)
            if update["playerDraws"] <= TRACE_CAP:
                uncapped += 1
                if census == delta:
                    fitted += 1
                else:
                    residual["uncapped-miss"] = residual.get("uncapped-miss", 0) + 1
            else:
                residual[delta - census] = residual.get(delta - census, 0) + 1
        # A6: the two counts around the counter must stay reconciled, or the
        # counter is being read from a trace whose bookkeeping is already broken.
        if update.get("draws") is not None and \
                update["skinned"] + update["bindPose"] != update["draws"]:
            bad_sum += 1
        if update["uploaded"] != update["matrices"]:
            bad_upload += 1
        previous = update["bm"]
    driven = any(update["ent"] for update in updates)
    last = updates[-1]
    if negative:
        fails.append("%d negative deltas (a counter may not decrease)" % negative)
    if over_skinned:
        fails.append("%d updates with delta > skinned" % over_skinned)
    if over_players:
        fails.append("%d updates with delta > playerDraws" % over_players)
    if driven and uncapped and fitted != uncapped:
        fails.append("census fit %d/%d on uncapped updates" % (fitted, uncapped))
    if driven and residual.get("uncapped-miss"):
        fails.append("%d uncapped updates the census does not explain"
                     % residual["uncapped-miss"])
    if driven and any(key <= 0 for key in residual if key != "uncapped-miss"):
        fails.append("capped residual not strictly positive: %s" % residual)
    if driven and last["bm"] == 0:
        fails.append("driven run reports boneMismatch=0")
    if not driven and last["bm"] != 0:
        fails.append("undriven run reports boneMismatch!=0")
    if bad_sum:
        fails.append("%d updates where skinned + bindPose != draws" % bad_sum)
    if bad_upload:
        fails.append("%d updates where gpuSkin uploaded != matrices" % bad_upload)
    if began == 0:
        fails.append("no scene: begin blocks (not a scene trace)")
    elif began != ended:
        fails.append("begin=%d done=%d (an update was cut mid-write)"
                     % (began, ended))
    if frames is None:
        fails.append("no capture: armed line (the run stopped before the capture tick)")
    if driven and last["status"] != "ok":
        fails.append("driven run status=%s" % last["status"])
    if not driven and last["status"] != "missing":
        warns.append("undriven status=%s (expected missing)" % last["status"])
    ratio = last["bm"] / float(count)
    if driven and not (4.5 <= ratio <= 5.0):
        warns.append("ratio %.4f outside the recorded corpus band 4.5-5.0 "
                     "(measurement, not an invariant)" % ratio)
    stats = dict(updates=count, bm=last["bm"],
                 first=updates[0]["bm"], frames=frames, ratio=ratio,
                 blocks=began, closed=ended,
                 uncapped=uncapped, fit=fitted,
                 residual=dict(sorted(residual.items(), key=lambda item: str(item[0]))),
                 driven=driven, skinned=last["skinned"], bindPose=last["bindPose"],
                 matrices=last["matrices"], gpu=last["gpu"], uploaded=last["uploaded"],
                 draws=last.get("draws"))
    return name, stats, fails, warns


def mutate(kind, source, target):
    """Write a trace that breaks exactly one Tier A rule."""
    with open(source, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")
    blocks, current = [], None
    for index, line in enumerate(lines):
        if BEGIN.match(line):
            current = {"ent": [], "anim": None}
        elif current is not None:
            if ENT.match(line):
                current["ent"].append(index)
            else:
                match = ANIM.match(line)
                if match:
                    current["anim"] = index
                    current["bm"] = int(match.group(6))
                    current["players"] = 0
    # playerDraws lives on setInstances, which precedes the anim line in a block.
    blocks, current = [], None
    for index, line in enumerate(lines):
        if BEGIN.match(line):
            current = {"ent": [], "anim": None, "players": 0}
        elif current is not None:
            match = SET.match(line)
            if match:
                current["players"] = int(match.group(2))
                continue
            if ENT.match(line):
                current["ent"].append(index)
                continue
            match = ANIM.match(line)
            if match:
                current["anim"] = index
                current["bm"] = int(match.group(6))
                blocks.append(current)
                current = None
    edited = 0
    if kind == "decrease":
        for at in range(1, len(blocks)):
            here, before = blocks[at], blocks[at - 1]
            if here["bm"] > before["bm"] > 1:
                lines[here["anim"]] = re.sub(
                    r"boneMismatch=\d+", "boneMismatch=%d" % (before["bm"] - 1),
                    lines[here["anim"]])
                edited = 1
                break
    elif kind == "unattributed":
        # Skip the first block: its delta is never computed, so editing its
        # census cannot break A4.
        for block in blocks[1:]:
            census = sum(1 for index in block["ent"]
                         if ENT.match(lines[index]).group(1) in GAP_MODELS)
            if census > 0 and block["players"] <= TRACE_CAP:
                for index in block["ent"]:
                    lines[index] = re.sub(
                        r"model=\S+", "model=models/player/medic.mdl", lines[index])
                edited = 1
                break
    else:
        raise SystemExit("unknown mutation: %s" % kind)
    if not edited:
        raise SystemExit("no place to apply the %s mutation in %s" % (kind, source))
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("MUTATED %s -> %s (%s)" % (source, target, kind))


def main(argv):
    if argv[:1] == ["--mutate"]:
        if len(argv) != 4:
            return 2
        mutate(argv[1], argv[2], argv[3])
        return 0
    expect_fail = argv[:1] == ["--expect-fail"]
    if expect_fail:
        argv = argv[1:]
    if not argv:
        print("usage: bone-mismatch-semantics.py [--expect-fail] <trace> [...]")
        return 2
    rc = 0
    for path in argv:
        name, stats, fails, warns = check(path)
        if expect_fail:
            if fails:
                print("[REJECTED-AS-REQUIRED] %s" % name)
                for line in fails:
                    print("        FAIL %s" % line)
                continue
            print("[VACUOUS] %s passed under --expect-fail" % name)
            rc = 1
            continue
        print("[%s] %s" % ("PASS" if not fails else "FAIL", name))
        if stats:
            print("        updates=%d blocks=%d/%d boneMismatch=%d first=%d "
                  "frames=%s ratio=%.4f driven=%s last-skinned=%d/%d/%d"
                  % (stats["updates"], stats["blocks"], stats["closed"],
                     stats["bm"], stats["first"], stats["frames"],
                     stats["ratio"], stats["driven"], stats["skinned"],
                     stats["bindPose"], stats["matrices"]))
            print("        uncapped=%d census-fit=%d/%d residual=%s"
                  % (stats["uncapped"], stats["fit"], stats["uncapped"],
                     stats["residual"]))
        for line in fails:
            print("        FAIL %s" % line)
            rc = 1
        for line in warns:
            print("        WARN %s" % line)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
