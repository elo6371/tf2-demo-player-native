#!/usr/bin/env python3
"""Generate T68 golden event samples and same-tick statistics from the T64 oracle TSVs.

Input : D:/TF2_Native_Test/evidence/t64-events/oracle/<demo>.events.tsv
Output: D:/TF2_Native_Test/evidence/t68-events/golden/<demo>.<event>.tsv
        D:/TF2_Native_Test/evidence/t68-events/golden/SUMMARY.tsv
        D:/TF2_Native_Test/evidence/t68-events/same-tick-stats.json
No product files are touched. Deterministic: stable ordering, LF endings, UTF-8.
"""
import hashlib
import json
import os
import re
import sys
from collections import Counter, defaultdict

ORACLE = "D:/TF2_Native_Test/evidence/t64-events/oracle"
OUT = "D:/TF2_Native_Test/evidence/t68-events"
GOLDEN = os.path.join(OUT, "golden")
TARGETS = [
    "player_death",
    "teamplay_round_start",
    "teamplay_round_win",
    "controlpoint_starttouch",
    "controlpoint_endtouch",
    "teamplay_point_captured",
]
DEMOS = [
    ("bagel", "bagel.koth_bagel_rc13.events.tsv"),
    ("snakewater", "snakewater.cp_snakewater_final1.events.tsv"),
]

DEF_RE = re.compile(r"^# def id=(\d+) name=(\S+) fields=(.*)$")


def parse_tsv(path):
    meta = {}
    defs = {}          # id -> (name, [field names in definition order])
    rows = []          # (tick, event_id, name, {key: value}, field_order)
    with open(path, "r", encoding="utf-8", newline="") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if line.startswith("# demo="):
                body = line[len("# demo="):]
                first, _sep, tail = body.partition(" ")
                meta["demo"] = first
                for token in tail.split(" "):
                    if "=" in token:
                        key, value = token.split("=", 1)
                        meta[key] = value
                continue
            if line.startswith("# header "):
                for token in line[len("# header "):].split(" "):
                    if "=" in token:
                        key, value = token.split("=", 1)
                        meta[key] = value
                continue
            match = DEF_RE.match(line)
            if match:
                event_id = int(match.group(1))
                name = match.group(2)
                fields = [item.split(":", 1)[0] for item in match.group(3).split(",") if item]
                defs[event_id] = (name, fields)
                continue
            if line.startswith("#"):
                continue
            if not line:
                continue
            parts = line.split("\t", 3)
            tick = int(parts[0])
            event_id = int(parts[1])
            name = parts[2]
            payload = parts[3] if len(parts) > 3 else ""
            values = {}
            order = []
            if payload:
                for pair in payload.split(";"):
                    if "=" in pair:
                        key, value = pair.split("=", 1)
                    else:
                        key, value = pair, ""
                    values[key] = value
                    order.append(key)
            rows.append((tick, event_id, name, values, order))
    return meta, defs, rows


def sanitize(value):
    return value.replace("\t", " ").replace("\n", " ").replace("\r", " ")


def main():
    os.makedirs(GOLDEN, exist_ok=True)
    summary_lines = [
        "# T68 golden event samples - summary",
        "# source=evidence/t64-events/oracle/*.events.tsv (T64 oracle, tf-demo-parser 0.5.1)",
        "# demo\tevent\ttotal\tfirst_tick\tlast_tick\tfields",
    ]
    stats = {}
    golden_hashes = {}
    for demo_key, filename in DEMOS:
        path = os.path.join(ORACLE, filename)
        meta, defs, rows = parse_tsv(path)
        sha = meta.get("sha256", "")
        demo_path = meta.get("demo", "")
        print("parsed", demo_key, "rows", len(rows), "defs", len(defs), "sha", sha)
        by_name = defaultdict(list)
        for row in rows:
            by_name[row[2]].append(row)
        # same-tick statistics over all events
        per_tick = Counter(row[0] for row in rows)
        multi = {tick: count for tick, count in per_tick.items() if count > 1}
        top = sorted(per_tick.items(), key=lambda item: (-item[1], item[0]))[:5]
        top_detail = []
        for tick, count in top:
            names = [row[2] for row in rows if row[0] == tick]
            top_detail.append({
                "tick": tick,
                "count": count,
                "events": names,
            })
        ticks = sorted(per_tick)
        # same-tick duplicates restricted to one event name (max events of the SAME name on one tick)
        same_name = defaultdict(int)
        for (tick, name) in [(row[0], row[2]) for row in rows]:
            same_name[(tick, name)] += 1
        max_same_name = max(same_name.items(), key=lambda item: item[1]) if same_name else None
        stats[demo_key] = {
            "demo_path": demo_path,
            "sha256": sha,
            "map": meta.get("map", ""),
            "header_ticks": int(meta.get("ticks", "0")),
            "events_total": len(rows),
            "distinct_event_names": len(by_name),
            "distinct_ticks": len(per_tick),
            "ticks_with_more_than_one_event": len(multi),
            "max_events_per_tick": max(per_tick.values()) if per_tick else 0,
            "min_tick": ticks[0] if ticks else None,
            "max_tick": ticks[-1] if ticks else None,
            "top5_ticks": top_detail,
            "max_same_name_events_on_one_tick": (
                {"tick": max_same_name[0][0], "event": max_same_name[0][1], "count": max_same_name[1]}
                if max_same_name else None
            ),
        }
        print(demo_key, "distinct_ticks", len(per_tick), "multi_ticks", len(multi),
              "max_per_tick", max(per_tick.values()), "range", ticks[0], ticks[-1])
        for item in top_detail:
            print("  top tick", item["tick"], "count", item["count"], "names", Counter(item["events"]))
        if max_same_name:
            print("  max same-name on one tick:", max_same_name[0][0], max_same_name[0][1], max_same_name[1])
        # golden samples
        for event_name in TARGETS:
            event_rows = by_name.get(event_name, [])
            if not event_rows:
                print("  MISSING", event_name)
                continue
            field_names = sorted(defs[event_rows[0][1]][1])
            first = event_rows[:50]
            last = event_rows[-50:]
            ticks_for_event = [row[0] for row in event_rows]
            lines = [
                "# demo_path=%s sha256=%s demo=%s map=%s event=%s total=%d first_tick=%d last_tick=%d"
                % (demo_path, sha, demo_key, meta.get("map", ""), event_name, len(event_rows),
                   min(ticks_for_event), max(ticks_for_event)),
                "# fields=" + ",".join(field_names),
                "# section=first50",
            ]
            for section, section_rows in (("first50", first), ("last50", last)):
                if section == "last50":
                    lines.append("# section=last50")
                for tick, _event_id, _name, values, _order in section_rows:
                    cells = [str(tick), event_name]
                    for key in field_names:
                        cells.append(sanitize(values.get(key, "")))
                    lines.append("\t".join(cells))
            out_path = os.path.join(GOLDEN, "%s.%s.tsv" % (demo_key, event_name))
            with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write("\n".join(lines) + "\n")
            digest = hashlib.sha256(open(out_path, "rb").read()).hexdigest()
            golden_hashes[os.path.basename(out_path)] = digest
            summary_lines.append(
                "%s\t%s\t%d\t%d\t%d\t%s"
                % (demo_key, event_name, len(event_rows), min(ticks_for_event), max(ticks_for_event),
                   ",".join(field_names))
            )
            print("  golden", os.path.basename(out_path), "rows", len(event_rows), "first", len(first), "last", len(last))
    summary_path = os.path.join(GOLDEN, "SUMMARY.tsv")
    with open(summary_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(summary_lines) + "\n")
    golden_hashes["SUMMARY.tsv"] = hashlib.sha256(open(summary_path, "rb").read()).hexdigest()
    stats_path = os.path.join(OUT, "same-tick-stats.json")
    with open(stats_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(stats, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    stats["golden_sha256"] = golden_hashes
    print(json.dumps(stats, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
