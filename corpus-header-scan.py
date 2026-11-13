#!/usr/bin/env python3
"""corpus-header-scan.py -- header-only census of a whole demo corpus.

A second, independent implementation of the demo-header layout, written straight
from src/public/demofile/demoformat.h (Valve):

    char   demofilestamp[8];      // "HL2DEMO"
    int    demoprotocol;          // 3
    int    networkprotocol;
    char   servername[MAX_OSPATH];    // 260
    char   clientname[MAX_OSPATH];    // 260  "Name of client who recorded the game"
    char   mapname[MAX_OSPATH];       // 260
    char   gamedirectory[MAX_OSPATH]; // 260
    float  playback_time;
    int    playback_ticks;
    int    playback_frames;
    int    signonlength;
                                    // 8+4+4+260*4+4+4+4+4 = 1072

It costs one 1072-byte read per file, so it can cover a corpus that would take
hours to decode, and it gives the probe something to disagree with. Two things it
checks that the probe cannot check about itself:

  * every `autorecord_*` file is a client-recorded POV demo (TF2 writes that name
    from the client's own `record` command), so a non-POV verdict on one is a
    classifier bug;
  * whether the corpus contains any SourceTV demo at all.

--selftest flips the fields and asserts the classifier follows, so the verdict is
not a constant.

Usage:
  python corpus-header-scan.py --demos-dir DIR [--outdir DIR]
  python corpus-header-scan.py --selftest
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from collections import Counter
from pathlib import Path

HEADER_SIZE = 1072
SIGNATURE = b"HL2DEMO\x00"
DEFAULT_DEMOS = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"


def fixed_string(raw: bytes) -> str:
    return raw.split(b"\0")[0].decode("utf-8", "replace")


def classify(server_name: str, client_name: str) -> str:
    """Mirror namesIndicateSourceTv() in native/src/demo_header.cpp."""
    for name in (server_name, client_name):
        lowered = name.lower()
        if "sourcetv" in lowered or "hltv" in lowered:
            return "SourceTV"
    return "POV" if client_name else "unknown"


def parse_header(data: bytes) -> dict:
    if len(data) < HEADER_SIZE:
        raise ValueError(f"truncated header: {len(data)} < {HEADER_SIZE}")
    if data[:8] != SIGNATURE:
        raise ValueError(f"bad signature: {data[:8]!r}")
    demo_protocol, network_protocol = struct.unpack_from("<ii", data, 8)
    server_name = fixed_string(data[16:276])
    client_name = fixed_string(data[276:536])
    map_name = fixed_string(data[536:796])
    game_dir = fixed_string(data[796:1056])
    playback_time = struct.unpack_from("<f", data, 1056)[0]
    ticks, frames, signon_length = struct.unpack_from("<iii", data, 1060)
    return {
        "demo_protocol": demo_protocol,
        "network_protocol": network_protocol,
        "server_name": server_name,
        "client_name": client_name,
        "map": map_name,
        "game_dir": game_dir,
        "playback_time": round(playback_time, 2),
        "ticks": ticks,
        "frames": frames,
        "signon_length": signon_length,
        "recording": classify(server_name, client_name),
    }


def selftest() -> int:
    cases = [
        ("player records, hostname", "na.serveme.tf #633053", "皮革warrior", "POV"),
        ("stv records, hostname", "Matcha Bookable", "SourceTV Demo", "SourceTV"),
        ("stv in servername", "SourceTV Demo", "", "SourceTV"),
        ("hltv in servername", "HLTV Server", "someone", "SourceTV"),
        ("case insensitive", "Spire Server", "sourcetv", "SourceTV"),
        ("no names at all", "", "", "unknown"),
    ]
    fail = 0
    for label, server, client, want in cases:
        got = classify(server, client)
        ok = got == want
        if not ok:
            fail = 1
        print(f"  {'ok  ' if ok else 'FAIL'} {label:<26} -> {got:<9} (want {want})")
    print()
    print("HEADER-SCAN-SELFTEST=FAIL" if fail else "HEADER-SCAN-SELFTEST=PASS")
    return fail


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--demos-dir", default=DEFAULT_DEMOS)
    ap.add_argument("--outdir", default="evidence/header-scan")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    demos_dir = Path(args.demos_dir)
    demos = sorted(demos_dir.glob("*.dem"))
    if not demos:
        print(f"FATAL: no .dem under {demos_dir}", file=sys.stderr)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    rows, errors = [], []
    for demo in demos:
        try:
            with demo.open("rb") as handle:
                header = parse_header(handle.read(HEADER_SIZE))
        except (OSError, ValueError) as exc:
            errors.append({"demo": demo.name, "error": str(exc)})
            continue
        header["demo"] = demo.name
        header["size_mb"] = round(demo.stat().st_size / 2**20, 2)
        rows.append(header)

    by_recording = Counter(row["recording"] for row in rows)
    autorecord = [row for row in rows if row["demo"].startswith("autorecord_")]
    pov_violations = [row["demo"] for row in autorecord if row["recording"] != "POV"]
    source_tv = [row["demo"] for row in rows if row["recording"] == "SourceTV"]

    # A client-recorded POV demo can carry a non-empty servername (an IP, a
    # hostname) and a player name in clientname; a SourceTV demo puts the STV
    # client's name there. This is the only header-level signal that separates
    # them, so record the observed clientname values for the non-POV files.
    stv_client_names = sorted({row["client_name"] for row in rows
                               if row["recording"] == "SourceTV"})

    summary = {
        "scanned": len(rows),
        "unreadable": len(errors),
        "corpus_gb": round(sum(row["size_mb"] for row in rows) / 1024, 2),
        "recording_counts": dict(sorted(by_recording.items())),
        "autorecord_files": len(autorecord),
        "autorecord_pov_violations": pov_violations,
        "source_tv_files": source_tv,
        "source_tv_client_names": stv_client_names,
        "distinct_maps": len({row["map"] for row in rows}),
        "distinct_network_protocols": sorted({row["network_protocol"] for row in rows}),
        "distinct_demo_protocols": sorted({row["demo_protocol"] for row in rows}),
        "distinct_game_dirs": sorted({row["game_dir"] for row in rows}),
        "zero_tick_files": sum(1 for row in rows if row["ticks"] == 0),
        "errors": errors,
    }
    (outdir / "header-scan-summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    with (outdir / "header-scan.csv").open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()), extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    for key, value in summary.items():
        if key in ("errors", "source_tv_files", "autorecord_pov_violations"):
            shown = value if isinstance(value, list) and len(value) <= 12 else f"<{len(value)} items>"
            print(f"{key}={shown}")
        else:
            print(f"{key}={value}")

    bad = bool(errors) or bool(pov_violations)
    print()
    print("HEADER-SCAN=FAIL" if bad else "HEADER-SCAN=PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
