#!/usr/bin/env python3
"""build_sample_list.py -- emit the frozen T63 shared sample list.

Reads sample_detail.json (produced by build_sample_data.py) and the demo headers,
and writes sample_list.json. Deterministic: rerunning it on the same files
reproduces the same list (assert in the output).
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packet_clock

detail = json.load(open(os.path.join(HERE, "sample_detail.json")))
REPO_HEAD = "99eeea294cbe883e4c3e6b4b82c83de45d2ab926"

# demo id -> (name, role, bsp bytes or 0, known player entities)
ROLES = {
    "snakewater": ("S1", "primary pixel+data demo (BSP present)", 59590420, None),
    "review12": ("S2", "pixel+data demo (BSP present, cp_reckoner)", 45759167, None),
    "ashville73": ("S3", "data demo (koth; roster known from oracle enters; no BSP)", 0, [3, 7, 12]),
    "prodcutscout": ("S4", "data demo (koth_product; no BSP)", 0, None),
    "bagel": ("S5", "data demo (koth_bagel; camera+target known; no BSP)", 0, [1, 3]),
    "sunshine": ("X1", "pixel-only extra (cp_sunshine; clock unstable: no tick-exact assertions)", 30319857, None),
}
POINT_SPECS = {
    "snakewater": [("P0", 600, "negative-control (before-window)"), ("P1", 20000, "in-window"), ("P2", None, "last-100")],
    "review12": [("P0", 600, "negative-control (before-window)"), ("P1", 20000, "in-window"), ("P2", None, "last-100")],
    "ashville73": [("P0", 600, "negative-control (before-window)"), ("P1", 20000, "in-window"), ("P2", None, "last-100")],
    "prodcutscout": [("P0", 600, "negative-control (before-window)"), ("P1", 50000, "in-window"), ("P2", None, "last-100")],
    "bagel": [("P0", 600, "negative-control (before-window)"), ("P1", 60000, "in-window"), ("P2", None, "last-100")],
}

demos = []
for name, d in detail.items():
    did, role, bsp_bytes, players = ROLES[name]
    hdr = packet_clock.parse_header(open(d["path"], "rb").read(1072))
    frames = hdr["frames"]
    clock = d["clock"]
    offset = clock["last_server_tick"] - clock["last_packet_tick"]
    first_packet = clock["first_packet_tick"]
    points = []
    if name in POINT_SPECS:
        for pid, playback, kind in POINT_SPECS[name]:
            if playback is None:
                playback = clock["last_packet_tick"] - 100
            scene = first_packet + playback
            correct = playback + offset
            if kind.startswith("negative"):
                hud = "entity=before-window; stale=0; health=? team=? class=? clip=? uber=?"
                why = "history starts at server tick %d; current sceneTick=%d is below it, so no snapshot can resolve" % (clock["first_server_tick"], scene)
            else:
                hud = ("entity=checkpoint|available; resolved<=%d; 0<=stale<=worstGap(history-stats); "
                       "values are those of server tick ~%d (packet ~%d), NOT of playback position %d (correct server tick %d)"
                       % (scene, scene, scene - offset, playback, correct))
                why = "in-window point; the current build keys the snapshot by sceneTick=%d while the same playback frame is server tick %d" % (scene, correct)
            points.append({
                "id": "%s-%s" % (did, pid), "kind": kind,
                "playback_tick": playback, "scene_tick_current_build": scene,
                "correct_server_tick": correct,
                "clock_mismatch_ticks": correct - scene,
                "expected_current_hud": hud, "why": why,
                "canonical_probe": "entity_model_probe.exe --demo <path> --props-at %d --entity <id>" % correct,
            })
    demos.append({
        "id": did, "name": name, "role": role, "path": d["path"], "map": d["map"],
        "recording": d["recording"], "client": d["client"], "server": d["server"],
        "header_ticks": d["demo_ticks_header"], "packet_frames": frames,
        "playback_seconds": d["playback_seconds"], "size_bytes": d["size_bytes"],
        "sha256_full": d["sha256_full"], "sha256_header_1072": d["sha256_header_1072"],
        "bsp_present": d["bsp_present"], "bsp_bytes": bsp_bytes,
        "clock": {"first_packet_tick": first_packet, "first_server_tick": clock["first_server_tick"],
                  "last_packet_tick": clock["last_packet_tick"], "last_server_tick": clock["last_server_tick"],
                  "packet_to_server_offset": offset, "offset_constant": clock["offset_constant"],
                  "all_offsets": clock["server_minus_packet_offsets"]},
        "known_player_entities": players,
        "inspection_points": points,
        "tick_exact_usable": clock["offset_constant"] and name != "sunshine",
    })

out = {
    "schema": "t63-sample-list-v1", "frozen_on": "2026-10-24", "owner": "t63-sourcetv",
    "consumers": ["T63", "T64", "T65", "T67"],
    "repo": {"path": "D:/TF2_Native_Test", "branch": "p0-entity-protocol", "head": REPO_HEAD},
    "identity": {
        "product_exe": {"path": "native/build-nmake/tf2_demo_native.exe", "sha256": "f454718bbaff5825e4720b721f92da2b1689d29365d18a1e1f6f776afdf30a0a"},
        "probes": {
            "entity_protocol_probe.exe": "9588f239c755674ec04bcf1663249161abfdacf3bf921665e7479638b788c352",
            "entity_model_probe.exe": "eb02d7e82946f050a9b7b4587759a996872a45173eacd8d7d13afc7969ccc79f",
            "demo_open_probe.exe": "7de049951425c68459fd53e9c28bcac46962aee87d3df401a6a8617eb7493cf2",
        },
    },
    "clocks": {
        "playback_tick": "the product's g_playback.tick; demo packet-frame axis, 0..packet_frames",
        "demo_packet_tick": "cmdinfo tick of a demo packet; starts at first_packet_tick and advances with packets",
        "server_tick": "tick carried by net_Tick (type 3); entity history snapshots are keyed by THIS tick",
        "current_build_keying": "main.cpp:8226 sceneTick = firstPacketTick + g_playback.tick; main.cpp:8230 queryEntitySnapshotAtOrBeforeTick(sceneTick)",
        "correct_keying": "server_tick = playback_tick + packet_to_server_offset (offset measured on the packet stream; see packet_clock.py)",
        "known_mismatch": "for every demo here the current build is behind by (packet_to_server_offset - first_packet_tick) ticks",
    },
    "demos": demos,
    "known_readings": [
        {"id": "R1", "demo": "bagel", "server_tick": 129277, "entity": 1, "property": "DT_TFLocalPlayerExclusive.m_vecOrigin", "expected": [-3244.337891, 198.821442, 352.031250], "source": "evidence/fast/entity/slot-freshness.txt (chosen slot, lastWrite=129277 age=0)"},
        {"id": "R2", "demo": "bagel", "server_tick": 129277, "entity": 1, "property": "observer-focus", "expected": "mode=4 targetHandle=1718275 targetIndex=3 targetSerial=839 follows=1", "source": "evidence/observer-focus/gate.txt"},
        {"id": "R3", "demo": "ashville73", "server_tick": 16350, "property": "entity roster at packet tick 1", "expected": "entities 0..19 class 247 CTFPlayer; 26..29 class 318 CTFTeam; 31 class 249 CTFPlayerResource; 32 class 235 CTFObjectiveResource", "source": "evidence/final/oracle-enters-ashville73.txt"},
        {"id": "R4", "demo": "bagel", "playback_tick": 600, "expected": "entity=before-window stale=0 health=? team=? class=? clip=? uber=?", "source": "evidence/camera-ui/e2e.driver.txt:3"},
        {"id": "R5", "demo": "snakewater", "playback_tick": 3001, "expected": "health=1 team=1 class=0 clip=? uber=? (SUPERSEDED: pre-HEAD evidence; not reproducible under current clock keying)", "source": "evidence/entity-material/gate.txt:41"},
    ],
    "gaps": [
        "G1 no Payload SourceTV demo exists locally (982 pl_ demos are all POV); payload objective (CTeamTrainWatcher.m_flTotalProgress) has no demo -> not covered",
        "G2 bagel/snakewater/review12/prodcutscout player rosters are not known statically (their oracle enters files are 0-byte placeholders); entity ids must be discovered by the runtime prop-candidates pass",
        "G3 no BSP for koth_bagel_rc13 / koth_ashville_final1 / koth_product_final -> those demos are data-only for pixel tasks",
        "G4 SUNSHINE.dem has a non-constant packet->server offset -> excluded from tick-exact assertions (pixel-only extra)",
    ],
}
json.dump(out, open(os.path.join(HERE, "sample_list.json"), "w"), indent=1)
for d in demos:
    print(d["id"], d["name"], "frames", d["packet_frames"], "offset", d["clock"]["packet_to_server_offset"],
          "points", [(p["id"], p["playback_tick"], p["correct_server_tick"], p["clock_mismatch_ticks"]) for p in d["inspection_points"]])
