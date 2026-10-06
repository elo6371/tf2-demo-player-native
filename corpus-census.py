#!/usr/bin/env python3
"""corpus-census.py -- run entity_protocol_probe over a whole demo corpus.

Why a script and not a shell loop:
  * 1643 files / 41 GB. The shell loop cannot report progress, cannot resume,
    and cannot tell a slow demo from a hung one.
  * Every demo gets its own raw report on disk before anything is asserted, so a
    failure can be re-read instead of re-run.

What it asserts (per demo, all must be zero or empty):
    malformed_packets, unknown_message_packets, entity_failures,
    entity_update_header_failures, packet_entity_decode_failures,
    prop_missing_table, prop_index, prop_value
    message_types_seen_without_decoder == <none>
and, per demo, one of three outcomes:
    clean            -- index_state=ok, counters zero, full read
    skipped          -- index_state=truncated_tail (an interrupted recording ends
                        mid-entry; the whole entries before that point are still
                        scanned and their counters are still asserted to be zero)
                        or index_entries=0 (nothing in the file to read). A skip
                        is neither PASS nor FAIL: the file was not read in full,
                        so it is not evidence either way. Reported by name.
    dirty            -- anything else, including index_state=invalid (the indexer
                        walked into something it does not understand, which is a
                        real failure and must not be filed under "truncated"),
                        a missing field, or a non-zero counter.

What it reports but does not assert:
    delta_base_unavailable, history_gap  -- driven by the bounded history window,
    informational by design (see ACCEPTANCE doc).
    recording=                           -- the header-only classifier verdict.

Ground truth for the POV claim: TF2 writes `autorecord_*.dem` from the client's
own `record` command, so a file with that prefix IS a client-recorded POV demo.
The script therefore also checks that the classifier agrees, which is what turns
"recording=POV (heuristic)" from a guess into a validated reading.

Usage:
  python corpus-census.py --demos-dir <dir> --outdir <dir> [--workers 3]
                          [--sample N] [--limit N] [--resume]
Exit: 0 = every scanned demo clean.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MUST_BE_ZERO = [
    "malformed_packets",
    "unknown_message_packets",
    "entity_failures",
    "entity_update_header_failures",
    "packet_entity_decode_failures",
    "prop_missing_table",
    "prop_index",
    "prop_value",
]

REPORT_FIELDS = [
    "header", "index", "scan", "recording", "protocol", "map",
    "commands", "packets",
    "packets_scanned", "malformed_packets", "unknown_message_packets",
    "packet_entities", "packet_entity_updates", "delta_packets",
    "enter", "preserve", "leave", "delete",
    "entity_failures", "entity_update_header_failures", "packet_entity_decode_failures",
    "prop_missing_table", "prop_index", "prop_value",
    "delta_base_unavailable", "history_gap", "history_dropped_packets",
    "instance_baselines", "baseline_applied", "baseline_misses",
    "active_entities", "max_active_entities",
    "file_messages", "set_pause", "bsp_decals", "menus", "cmd_key_values",
    "string_table_user_data_max_bytes",
    "recording_stream", "recording_header_name", "server_info_count",
    "server_info_hltv", "server_info_replay_bit", "source_tv_flag",
    "index_state", "index_entries", "index_tail_bytes",
]

KV = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
RECORDING = re.compile(r"recording=(POV|SourceTV)([^\r\n]*)")


def _as_int(value):
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None

# A field that is absent is a FAILURE, not a skip. The first version of this
# script only parsed the `header=` line, so every counter below came back None,
# the "must be zero" loop skipped them all, and 24 demos reported clean while
# proving nothing. Keep this list explicit and assert on it.
REQUIRED_FIELDS = MUST_BE_ZERO + [
    "packets_scanned",
    "packet_entities",
    "packet_entity_updates",
    "enter", "preserve", "leave", "delete",
    "delta_base_unavailable", "history_gap",
    "instance_baselines",
    "string_table_user_data_max_bytes",
    "recording", "protocol", "map",
    # Stream-level recording signals. The header verdict alone cannot see the
    # in-stream svc_ServerInfo m_bIsHLTV bit, which is the only authoritative
    # SourceTV marker. Without these the census could not tell whether the
    # corpus contains a SourceTV demo at all.
    "recording_stream", "recording_header_name",
    "server_info_count", "server_info_hltv", "server_info_replay_bit",
    "source_tv_flag",
    # How far the indexer got. An interrupted recording ends mid-entry; the
    # entries before that point are still whole and are still scanned, so such a
    # file is neither a failure nor a silent skip -- it is a SKIP with a reason.
    "index_state", "index_entries", "index_tail_bytes",
]


def parse_report(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("message_types_seen_without_decoder:"):
            out["message_types_seen_without_decoder"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("unknown_message_types="):
            out["unknown_message_types"] = stripped.split("=", 1)[1]
            continue
        if stripped.startswith("message_type_histogram:"):
            continue
        # Every other line is a flat run of key=value pairs (the probe prints one
        # logical record per line). Collect them all, first write wins.
        for key, value in KV.findall(stripped):
            out.setdefault(key, value)
    match = RECORDING.search(text)
    if match:
        out["recording"] = match.group(1)
        out["recording_detail"] = "heuristic" if "heuristic" in match.group(2) else "declared"
    return out


def scan_one(probe: Path, demo: Path, timeout_s: int) -> dict:
    started = time.perf_counter()
    try:
        proc = subprocess.run(
            [str(probe), str(demo)],
            capture_output=True, text=True, timeout=timeout_s,
            encoding="utf-8", errors="replace",
        )
        text = (proc.stdout or "") + (proc.stderr or "")
        rc = proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        text = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        rc = -1
        timed_out = True
    elapsed = time.perf_counter() - started

    row = {"demo": demo.name, "size_mb": round(demo.stat().st_size / 2**20, 1),
           "rc": rc, "seconds": round(elapsed, 2), "timed_out": timed_out}
    row.update(parse_report(text))
    row["_raw"] = text
    return row


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--demos-dir", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--probe", default="native/build-nmake/entity_protocol_probe.exe")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--sample", type=int, default=0, help="evenly spaced stratified sample size, 0 = all")
    ap.add_argument("--limit", type=int, default=0, help="cap the number of demos after sampling")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--resume", action="store_true", help="skip demos whose raw report already exists")
    args = ap.parse_args()

    probe = Path(args.probe)
    if not probe.is_file():
        print(f"FATAL: probe not found: {probe}", file=sys.stderr)
        return 2

    demos_dir = Path(args.demos_dir)
    demos = sorted(demos_dir.glob("*.dem"))
    if not demos:
        print(f"FATAL: no .dem under {demos_dir}", file=sys.stderr)
        return 2

    full_bytes = sum(d.stat().st_size for d in demos)
    if args.sample and args.sample < len(demos):
        step = len(demos) / args.sample
        demos = [demos[int(i * step)] for i in range(args.sample)]
    if args.limit:
        demos = demos[: args.limit]

    total_bytes = sum(d.stat().st_size for d in demos)

    outdir = Path(args.outdir)
    reports_dir = outdir / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)

    pending = []
    resumed: list[dict] = []
    for demo in demos:
        raw = reports_dir / (demo.stem + ".txt")
        if args.resume and raw.exists() and raw.stat().st_size > 0:
            # A resumed run must still report on the skipped demos, otherwise the
            # summary silently describes only the tail of the corpus.
            row = {"demo": demo.name,
                   "size_mb": round(demo.stat().st_size / 2**20, 1),
                   "rc": 0, "seconds": 0.0, "timed_out": False, "resumed": True}
            row.update(parse_report(raw.read_text(encoding="utf-8", errors="replace")))
            resumed.append(row)
            continue
        pending.append((demo, raw))

    print(f"demos_total={len(demos)} pending={len(pending)} resumed={len(resumed)} "
          f"corpus_gb={total_bytes / 2**30:.1f} (full {full_bytes / 2**30:.1f}) "
          f"workers={args.workers}", flush=True)

    done = 0
    started = time.perf_counter()
    rows: list[dict] = list(resumed)

    def work(item):
        demo, raw = item
        row = scan_one(probe, demo, args.timeout)
        raw.write_text(row.pop("_raw"), encoding="utf-8", errors="replace")
        return row

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for row in pool.map(work, pending):
            rows.append(row)
            done += 1
            if done % 25 == 0 or done == len(pending):
                rate = done / max(time.perf_counter() - started, 1e-9)
                eta = (len(pending) - done) / max(rate, 1e-9)
                print(f"  progress {done}/{len(pending)}  {rate:.2f} demo/s  eta={eta/60:.1f} min",
                      flush=True)

    csv_path = outdir / "corpus.csv"
    fieldnames = ["demo", "size_mb", "rc", "seconds", "timed_out"] + REPORT_FIELDS + [
        "recording_detail", "unknown_message_types",
        "message_types_seen_without_decoder"]
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in sorted(rows, key=lambda r: r["demo"]):
            writer.writerow(row)

    # ---- assertions -------------------------------------------------------
    # Three outcomes, not two. A demo whose index stopped at a truncated tail is
    # scanned for the whole entries before that point and then recorded as SKIP
    # with a reason -- it must not read as PASS (the file was not read in full)
    # and must not read as FAIL (nothing is wrong with the decoder). Everything
    # else keeps the strict rule: an absent field is a failure.
    clean, skipped, dirty = [], [], []
    for row in rows:
        bad = []
        if row.get("header") != "1":
            bad.append("header")
        if row.get("timed_out"):
            bad.append("timeout")
        missing = [f for f in REQUIRED_FIELDS if row.get(f) is None]
        if missing:
            bad.append("missing:" + ",".join(missing))

        state = row.get("index_state")
        entries = _as_int(row.get("index_entries"))
        if state == "invalid":
            bad.append(f"index_state=invalid tail={row.get('index_tail_bytes')}")
        elif state == "ok" and row.get("index") != "1":
            bad.append("index_state=ok but index=0")

        # The counters below are asserted for truncated files too: the prefix that
        # was scanned must be just as clean as a complete file.
        for field in MUST_BE_ZERO:
            value = row.get(field)
            if value is not None and value != "0":
                bad.append(f"{field}={value}")
        coverage = row.get("message_types_seen_without_decoder")
        if coverage is not None and coverage != "<none>":
            bad.append(f"without_decoder:{coverage}")
        # The stream-level signals must be mutually consistent. An HLTV bit that
        # the classifier does not act on is exactly the failure this catches.
        if row.get("server_info_hltv") == "1" and row.get("recording_stream") != "SourceTV":
            bad.append(f"hltv_bit_ignored:{row.get('recording_stream')}")
        if row.get("recording_header_name") == "1" and row.get("recording_stream") != "SourceTV":
            bad.append(f"header_name_ignored:{row.get('recording_stream')}")
        if row.get("source_tv_flag") != row.get("server_info_hltv"):
            bad.append(f"source_tv_flag={row.get('source_tv_flag')} "
                       f"hltv={row.get('server_info_hltv')}")
        if row["demo"].startswith("autorecord_") and row.get("recording_stream") != "POV":
            bad.append(f"pov_ground_truth_violated:{row.get('recording_stream')}")

        if bad:
            dirty.append((row, bad))
        elif entries == 0:
            # Nothing in the file to index at all. Not a decoder failure, but
            # explicitly not evidence either.
            skipped.append((row, "no_entries"))
        elif row.get("packets_scanned") == "0":
            # The indexer produced entries and the scan still decoded nothing.
            # That is a probe failure: "nothing was checked" must never read as
            # "nothing was wrong".
            dirty.append((row, ["probe_decoded_nothing"]))
        elif state == "truncated_tail":
            skipped.append((row, "truncated_tail"))
        else:
            clean.append((row, []))

    # The classifier must agree with the autorecord_ ground truth.
    # Compare by demo name, not by dict membership: two demos can carry identical
    # readings, and `row not in pov_agree` would then drop the wrong one.
    pov_files = [r for r in rows if r["demo"].startswith("autorecord_")]
    pov_agree = [r for r in pov_files if str(r.get("recording", "")).startswith("POV")]
    agree_names = {r["demo"] for r in pov_agree}
    pov_disagree = [r for r in pov_files if r["demo"] not in agree_names]

    summary = {
        "scanned": len(rows),
        "clean": len(clean),
        "skipped": len(skipped),
        "dirty": len(dirty),
        "skip_reasons": dict(sorted(Counter(reason for _, reason in skipped).items())),
        "truncated_tail_files": sorted(r["demo"] for r, reason in skipped
                                       if reason == "truncated_tail"),
        "corpus_gb": round(sum(r["size_mb"] for r in rows) / 1024, 1),
        "wall_seconds": round(time.perf_counter() - started, 1),
        "sum_packets": sum(int(r.get("packets_scanned") or 0) for r in rows),
        "sum_packet_entities": sum(int(r.get("packet_entities") or 0) for r in rows),
        "sum_entity_updates": sum(int(r.get("packet_entity_updates") or 0) for r in rows),
        "sum_enter": sum(int(r.get("enter") or 0) for r in rows),
        "sum_preserve": sum(int(r.get("preserve") or 0) for r in rows),
        "sum_leave": sum(int(r.get("leave") or 0) for r in rows),
        "sum_delete": sum(int(r.get("delete") or 0) for r in rows),
        "autorecord_pov_files": len(pov_files),
        "autorecord_classified_pov": len(pov_agree),
        "autorecord_pov_disagreements": len(pov_disagree),
        "recording_kinds": sorted({r.get("recording") for r in rows if r.get("recording")}),
        "recording_stream_kinds": sorted(
            {r.get("recording_stream") for r in rows if r.get("recording_stream")}),
        "recording_heuristic_count": sum(1 for r in rows if r.get("recording_detail") == "heuristic"),
        # The whole point of adding the stream-level signals: does this corpus
        # actually contain a SourceTV demo? If this is 0, the acceptance doc's
        # "real SourceTV corpus" item is still open and must say so.
        "source_tv_files": sorted(
            r["demo"] for r in rows if r.get("recording_stream") == "SourceTV"),
        "server_info_hltv_files": sorted(
            r["demo"] for r in rows if r.get("server_info_hltv") == "1"),
        "server_info_replay_bit_files": sum(
            1 for r in rows if r.get("server_info_replay_bit") == "1"),
        "distinct_maps": len({r.get("map") for r in rows if r.get("map")}),
        "protocols": sorted({r.get("protocol") for r in rows if r.get("protocol")}),
        "message_types_seen_without_decoder": sorted(
            {r.get("message_types_seen_without_decoder") for r in rows
             if r.get("message_types_seen_without_decoder") not in (None, "<none>")}),
    }
    (outdir / "corpus-summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    if dirty:
        with (outdir / "corpus-failures.txt").open("w", encoding="utf-8") as fh:
            for row, bad in dirty:
                fh.write(f"{row['demo']}\t{'; '.join(bad)}\n")

    print()
    for key, value in summary.items():
        if key == "truncated_tail_files" and isinstance(value, list) and len(value) > 8:
            print(f"{key}=<{len(value)} files>")
        else:
            print(f"{key}={value}")
    print()
    if pov_disagree:
        print("POV CLASSIFIER DISAGREEMENTS:")
        for row in pov_disagree[:20]:
            print(f"  {row['demo']} -> {row.get('recording')}")
    if skipped:
        print("SKIPPED (scanned as far as the file allows; not evidence of a full read):")
        for row, reason in skipped[:20]:
            print(f"  {row['demo']}: {reason} "
                  f"index_entries={row.get('index_entries')} "
                  f"tail_bytes={row.get('index_tail_bytes')}")
    if dirty:
        print("DIRTY DEMOS:")
        for row, bad in dirty[:40]:
            print(f"  {row['demo']}: {'; '.join(bad)}")
    verdict = "CORPUS-CENSUS=PASS" if not dirty and not pov_disagree else "CORPUS-CENSUS=FAIL"
    print(verdict)
    return 0 if verdict.endswith("PASS") else 1


if __name__ == "__main__":
    sys.exit(main())
