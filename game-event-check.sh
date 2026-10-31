#!/usr/bin/env bash
# game-event-check.sh -- T68: svc_GameEvent / svc_GameEventList decode gate.
#
# WHAT THIS PINS
#   1. Both demos decode exactly the events the T64 oracle recorded: 12,982 for
#      bagel and 10,703 for snakewater, one svc_GameEventList carrying 414
#      definitions, no decode failures and no unknown ids.
#   2. The six golden event types match the oracle byte for byte, first 50 and
#      last 50 rows, in wire order. The row is tick<TAB>event<TAB>values with
#      the values in the oracle's alphabetical field order; the probe prints
#      definition order, so the gate re-orders before comparing.
#   3. Every event at tick 334 matches the same-tick fixture in wire order
#      (209 bagel / 213 snakewater). That fixture is the reason the dedupe key
#      must be (tick, packetOrdinal, messageBit): a tick-only key keeps 1 row
#      here and loses 136 ammo_pickup events, which the negative-control
#      assertions below measure on the decoded data itself.
#   4. --events <n> honours n.
#
# MUTATION MODE
#   --mutation patches one source file, rebuilds the probe, runs the mutated
#   probe over bagel, and requires the named assertion to go red. It then
#   restores the file byte for byte (cmp), rebuilds, and requires the full
#   baseline to be green again. Three mutations: skip the event decode, take
#   the tick from the signon entry, and degrade the dedupe key to tick alone.
#   A mutation whose patch does not apply, or whose build does not compile, is
#   a FATAL, not a caught mutation.
#
# Usage: bash game-event-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, all three went red and
#        the restored baseline is green); 1 otherwise.
set -uo pipefail
cd "$(dirname "$0")"

PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
PROBE=native/build-nmake/demo_open_probe.exe
DEMO_BAGEL=D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem
DEMO_SNAKEWATER=D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
GOLDEN=evidence/t68-events/golden
SAME_TICK="$GOLDEN/same-tick-334.tsv"
SAME_TICK_SHA=9d6817c937678129365871e736c6af74393597a46e12cc196947927fa9be60be
OUT=evidence/t68-events/runs

MODE=""
if [ "$#" -gt 0 ]; then MODE="$1"; fi
MUTATION=0
case "$MODE" in
  --mutation) MUTATION=1 ;;
  "") ;;
  *) echo "usage: bash game-event-check.sh [--mutation]" >&2; exit 2 ;;
esac

fail=0
ok() { echo "  OK   $1 = $2"; }
bad() { echo "  FAIL $1 = $2, expected $3"; fail=1; }
assert_eq() { if [ "$2" = "$3" ]; then ok "$1" "$2"; else bad "$1" "$2" "$3"; fi; }

for input in "$PROBE" "$DEMO_BAGEL" "$DEMO_SNAKEWATER" "$GOLDEN/SUMMARY.tsv" "$SAME_TICK"; do
  if [ ! -e "$input" ]; then
    echo "FATAL: missing input $input" >&2
    exit 1
  fi
done
if [ ! -x "$PY" ]; then
  echo "FATAL: python not found at $PY" >&2
  exit 1
fi

# A stale binary is a gate reading green for the wrong reason. The build tree
# has no header dependencies (see build-target.sh), so a source newer than the
# probe means the probe may not contain the code this gate claims to test.
STALE=$(find native/src native/include native/tools -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$PROBE" -print -quit 2>/dev/null)
if [ -n "$STALE" ]; then
  echo "FATAL: STALE-BINARY: $STALE is newer than $PROBE; run 'bash build-target.sh demo_open_probe' first" >&2
  exit 1
fi

mkdir -p "$OUT"
echo "=== T68 game-event gate: probe=$PROBE"

# check_demo <demo> <dump> <events> <distinct_ticks> <tick334_rows> <ammo334_rows>
# Reads the probe's one summary line and its event rows, prints OK/FAIL lines,
# exits 1 if any assertion failed.
check_demo() {
  "$PY" - "$1" "$2" "$GOLDEN" "$SAME_TICK" "$3" "$4" "$5" "$6" <<'PYEOF'
import os
import re
import sys

demo, dump_path, golden_dir, same_tick_path = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
expected_events, expected_ticks, expected_t334, expected_ammo = (int(x) for x in sys.argv[5:9])

fail = 0
def ok(label, value):
    print(f"  OK   {label} = {value}")
def bad(label, value, expected):
    global fail
    fail += 1
    print(f"  FAIL {label} = {value}, expected {expected}")
def expect(label, got, want):
    if got == want:
        ok(label, got)
    else:
        bad(label, got, want)
def expect_rows(label, got, want):
    global fail
    if got == want:
        ok(label, f"{len(got)} rows")
        return
    fail += 1
    n = min(len(got), len(want))
    idx = next((i for i in range(n) if got[i] != want[i]), n)
    g = got[idx] if idx < len(got) else "<missing>"
    w = want[idx] if idx < len(want) else "<missing>"
    print(f"  FAIL {label} = {len(got)} rows, expected {len(want)}; first difference at row {idx}: got {g!r}, want {w!r}")

# The probe writes with std::cout in text mode, so redirected rows are CRLF.
raw = open(dump_path, "rb").read().replace(b"\r\n", b"\n")
lines = raw.split(b"\n")
if lines and lines[-1] == b"":
    lines.pop()
if not lines:
    print("  FAIL dump = empty, expected a summary line")
    sys.exit(1)
summary = lines[0].decode("utf-8", "replace")

def summary_field(name):
    match = re.search(r"(?:^| )" + re.escape(name) + r"=(-?\d+)(?: |$)", summary)
    return int(match.group(1)) if match else -1

rows = []
shape_ok = True
for line in lines[1:]:
    parts = line.split(b"\t", 3)
    if len(parts) < 3:
        bad("row_shape", line[:60], "tick<TAB>id<TAB>name[<TAB>fields]")
        shape_ok = False
        continue
    try:
        tick = int(parts[0])
        event_id = int(parts[1])
    except ValueError:
        bad("row_shape", line[:60], "integer tick and id")
        shape_ok = False
        continue
    fields = []
    if len(parts) == 4 and parts[3]:
        for item in parts[3].decode("utf-8").split(";"):
            key, _, value = item.partition("=")
            fields.append((key, value))
    rows.append({"tick": tick, "id": event_id, "name": parts[2].decode("utf-8"), "fields": fields})

if not shape_ok:
    print(f"CHECKS-FAILED={fail}")
    sys.exit(1)

expect("scan", summary_field("scan"), 1)
expect("eventlists", summary_field("eventlists"), 1)
expect("eventdefs", summary_field("eventdefs"), 414)
expect("eventfail", summary_field("eventfail"), 0)
expect("eventunknown", summary_field("eventunknown"), 0)
expect("event_dupes", summary_field("event_dupes"), 0)
expect("event_count", summary_field("events"), expected_events)
expect("eventdecoded", summary_field("eventdecoded"), expected_events)
expect("event_rows", len(rows), expected_events)
expect("event_rows_vs_count", len(rows), summary_field("events"))

# ---- golden event samples -------------------------------------------------
def parse_golden(path):
    header = {}
    fields = []
    sections = {}
    section = None
    for line in open(path, encoding="utf-8").read().split("\n"):
        if not line:
            continue
        if line.startswith("# "):
            body = line[2:]
            if body.startswith("section="):
                section = body[len("section="):]
                sections[section] = []
            elif body.startswith("fields="):
                value = body[len("fields="):]
                fields = value.split(",") if value else []
            else:
                for token in body.split():
                    if "=" in token:
                        key, value = token.split("=", 1)
                        header[key] = value
        elif section is not None:
            sections[section].append(line)
    return header, fields, sections

def render(row, fields):
    values = dict(row["fields"])
    if set(values) != set(fields):
        return None
    return "\t".join([str(row["tick"]), row["name"]] + [values[name] for name in fields])

per_event = {}
for row in rows:
    per_event.setdefault(row["name"], []).append(row)

for event in ("player_death", "teamplay_round_start", "teamplay_round_win",
              "controlpoint_starttouch", "controlpoint_endtouch", "teamplay_point_captured"):
    golden_path = os.path.join(golden_dir, f"{demo}.{event}.tsv")
    header, fields, sections = parse_golden(golden_path)
    total = int(header["total"])
    first = sections.get("first50", [])
    last = sections.get("last50", [])
    got = per_event.get(event, [])
    expect(f"golden {demo}.{event} total", len(got), total)
    expect(f"golden {demo}.{event} summary_total", int(header["total"]), total)
    if got:
        expect(f"golden {demo}.{event} first_tick", got[0]["tick"], int(header["first_tick"]))
        expect(f"golden {demo}.{event} last_tick", got[-1]["tick"], int(header["last_tick"]))
    rendered = [render(row, fields) for row in got]
    if any(value is None for value in rendered):
        bad(f"golden {demo}.{event} fields", "field set differs", ",".join(fields))
        continue
    expect_rows(f"golden {demo}.{event} first50", rendered[:len(first)], first)
    expect_rows(f"golden {demo}.{event} last50", rendered[-len(last):] if last else [], last)

# ---- same-tick fixture ----------------------------------------------------
expected = []
for line in open(same_tick_path, encoding="utf-8").read().split("\n"):
    if not line or line.startswith("#"):
        continue
    parts = line.split("\t", 4)
    if len(parts) < 5 or parts[0] != demo:
        continue
    expected.append((int(parts[2]), parts[3], parts[4]))
same_tick_rows = [row for row in rows if row["tick"] == 334]
got = [(row["tick"], row["name"], ";".join(f"{key}={value}" for key, value in row["fields"])) for row in same_tick_rows]
expect(f"same_tick {demo} rows", len(got), expected_t334)
expect_rows(f"same_tick {demo} wire_order", got, expected)

# ---- tick-only negative control ------------------------------------------
# These numbers are the reason the key includes packetOrdinal and messageBit.
# They are computed from the decoded rows, not from the fixture, so a decoder
# that silently drops same-tick events cannot hide behind the fixture.
ticks = sorted({row["tick"] for row in rows})
expect(f"tick_only {demo} distinct_ticks", len(ticks), expected_ticks)
expect(f"tick_only {demo} tick334_full", len(same_tick_rows), expected_t334)
expect(f"tick_only {demo} tick334_collapsed", len({row["tick"] for row in same_tick_rows}), 1)
ammo = [row for row in same_tick_rows if row["name"] == "ammo_pickup"]
expect(f"ammo_pickup {demo} tick334_full", len(ammo), expected_ammo)
expect(f"ammo_pickup {demo} tick334_collapsed", len({row["tick"] for row in ammo}), 1)

print(f"CHECKS-FAILED={fail}")
sys.exit(1 if fail else 0)
PYEOF
}

run_demo() {
  local demo="$1" path="$2" expected="$3" distinct="$4" t334="$5" ammo="$6"
  echo "=== $demo"
  local dump="$OUT/$demo.events.txt"
  "$PROBE" --scan --events 0 "$path" > "$dump" 2> "$OUT/$demo.stderr.txt"
  local rc=$?
  assert_eq "$demo probe rc" "$rc" 0
  if [ "$rc" -ne 0 ]; then
    sed 's/^/    /' "$OUT/$demo.stderr.txt" | tail -5
    return
  fi
  if ! check_demo "$demo" "$dump" "$expected" "$distinct" "$t334" "$ammo"; then
    fail=1
  fi
}

# The same-tick fixture is a hand-copied oracle artefact; its hash is pinned so
# an accidental edit cannot quietly weaken the negative control.
actual_sha=$(sha256sum "$SAME_TICK" | awk '{print $1}')
assert_eq "golden same-tick sha256" "$actual_sha" "$SAME_TICK_SHA"

run_demo bagel "$DEMO_BAGEL" 12982 6071 209 136
run_demo snakewater "$DEMO_SNAKEWATER" 10703 4850 213 136

# --events <n> must honour n. n=1 still scans the whole demo, so the summary
# counters stay at the full totals and only the dumped row count changes.
echo "=== --events 1"
"$PROBE" --scan --events 1 "$DEMO_BAGEL" > "$OUT/bagel.events-limit1.txt" 2>&1
limit_rc=$?
assert_eq "events-limit probe rc" "$limit_rc" 0
if [ "$limit_rc" -eq 0 ]; then
  assert_eq "events-limit event_rows" "$(grep -o ' event_rows=[0-9]*' "$OUT/bagel.events-limit1.txt" | head -1 | cut -d= -f2)" 1
  assert_eq "events-limit first row" "$(sed -n '2p' "$OUT/bagel.events-limit1.txt" | tr -d '\r')" "$(sed -n '2p' "$OUT/bagel.events.txt" | tr -d '\r')"
fi

if [ "$MUTATION" -eq 0 ]; then
  if [ "$fail" -eq 0 ]; then
    echo "GATE=PASS"
    exit 0
  fi
  echo "GATE=FAIL"
  exit 1
fi

# ==========================================================================
# Mutation mode
# ==========================================================================
MUTATION_FAIL=0
SOURCE=""
BACKUP=""
restore_backup() {
  if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
    cp "$BACKUP" "$SOURCE"
  fi
}
trap restore_backup EXIT
mkdir -p "$OUT/mutation"

if [ "$fail" -ne 0 ]; then
  echo "FATAL: the baseline is not green; a mutation cannot be discriminated" >&2
  exit 1
fi

baseline_quiet() {
  local tag="$1"
  local dump="$OUT/$tag"
  "$PROBE" --scan --events 0 "$DEMO_BAGEL" > "$dump-bagel.txt" 2>&1 || return 1
  check_demo bagel "$dump-bagel.txt" 12982 6071 209 136 > "$dump-bagel-check.txt" 2>&1 || return 1
  "$PROBE" --scan --events 0 "$DEMO_SNAKEWATER" > "$dump-snakewater.txt" 2>&1 || return 1
  check_demo snakewater "$dump-snakewater.txt" 10703 4850 213 136 > "$dump-snakewater-check.txt" 2>&1 || return 1
  return 0
}

# run_mutation_check <tag> <expected FAIL label>
run_mutation_check() {
  local tag="$1" expected="$2"
  local dump="$OUT/mutation/$tag-bagel.txt"
  local check="$OUT/mutation/$tag-check.txt"
  "$PROBE" --scan --events 0 "$DEMO_BAGEL" > "$dump" 2>&1
  local probe_rc=$?
  echo "mutation $tag probe rc=$probe_rc"
  check_demo bagel "$dump" 12982 6071 209 136 > "$check" 2>&1
  local check_rc=$?
  if [ "$probe_rc" -eq 0 ] && [ "$check_rc" -ne 0 ] && grep -Fq "FAIL $expected" "$check"; then
    echo "MUTATION-CAUGHT=PASS $tag (FAIL $expected)"
    grep -F "FAIL $expected" "$check" | head -1 | sed 's/^/    /'
    return 0
  fi
  echo "MUTATION-CAUGHT=FAIL $tag (expected FAIL $expected, probe rc=$probe_rc, check rc=$check_rc)"
  grep -F "FAIL " "$check" | head -5 | sed 's/^/    /'
  return 1
}

# restore_and_verify <tag> -- byte restore, rebuild, full baseline green.
restore_and_verify() {
  local tag="$1"
  if [ ! -f "$BACKUP" ]; then
    echo "FATAL: the backup for $tag is missing before restore" >&2
    exit 1
  fi
  cp "$BACKUP" "$SOURCE"
  if ! cmp -s "$BACKUP" "$SOURCE"; then
    echo "FATAL: $SOURCE was not restored byte for byte" >&2
    exit 1
  fi
  echo "RESTORED=1 $tag"
  rm -f "$BACKUP"
  BACKUP=""
  SOURCE=""
  bash build-target.sh demo_open_probe > "$OUT/mutation/$tag-restore-build.log" 2>&1
  local build_rc=$?
  echo "restore build rc=$build_rc"
  if [ "$build_rc" -ne 0 ]; then
    echo "FATAL: the restored build did not compile" >&2
    tail -20 "$OUT/mutation/$tag-restore-build.log" >&2
    exit 1
  fi
  if baseline_quiet "$tag-restored"; then
    echo "BASELINE-GREEN=1 $tag"
  else
    echo "BASELINE-GREEN=0 $tag"
    grep -h "FAIL " "$OUT/$tag-restored-bagel-check.txt" "$OUT/$tag-restored-snakewater-check.txt" 2>/dev/null | head -5 | sed 's/^/    /'
    MUTATION_FAIL=1
  fi
}

echo
echo "=== mutation m1: the event body decode is skipped ==="
SOURCE=native/src/demo_header_events.cpp
BACKUP=.scratch/game-event-m1.bak
cp "$SOURCE" "$BACKUP"
if ! "$PY" - "$SOURCE" <<'PYEOF'
import io
import sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "  std::uint32_t payloadBits = 0;\n"
    "  if (!bits.read(11, payloadBits)) return false;\n"
    "  if (payloadBits > bits.remaining()) return false;\n"
    "  MessageBits payload = bits.limited(payloadBits);\n"
)
inject = (
    "  std::uint32_t payloadBits = 0;\n"
    "  if (!bits.read(11, payloadBits)) return false;\n"
    "  if (payloadBits > bits.remaining()) return false;\n"
    "  // MUTATION m1: consume the declared length and stop. The packet stays\n"
    "  // aligned but no event is decoded or counted.\n"
    "  if (payloadBits <= bits.remaining()) return bits.skip(payloadBits);\n"
    "  MessageBits payload = bits.limited(payloadBits);\n"
)
assert text.count(anchor) == 1, "readGameEvent payload anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED m1")
PYEOF
then
  echo "FATAL: the m1 mutation did not apply; the mutated build would prove nothing" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
bash build-target.sh demo_open_probe > "$OUT/mutation/m1-build.log" 2>&1
m1_build=$?
echo "mutation m1 build rc=$m1_build"
if [ "$m1_build" -ne 0 ]; then
  echo "FATAL: the m1 mutated build did not compile; the mutation proves nothing" >&2
  tail -20 "$OUT/mutation/m1-build.log" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
run_mutation_check m1 "event_count" || MUTATION_FAIL=1
restore_and_verify m1

echo
echo "=== mutation m2: the event tick comes from the signon entry ==="
SOURCE=native/src/demo_header.cpp
BACKUP=.scratch/game-event-m2.bak
cp "$SOURCE" "$BACKUP"
if ! "$PY" - "$SOURCE" <<'PYEOF'
import io
import sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "    const bool streamOk = decodeDemoMessageStream(payload, payload.size() * 8u, entry.tick,\n"
    "                                                  summary.lastNetworkTick, summary, stream,\n"
    "                                                  static_cast<std::uint32_t>(entryIndex));\n"
)
inject = (
    "    // MUTATION m2: use the signon entry's tick (INT32_MIN on bagel) instead\n"
    "    // of the cmd=2 packet tick the event belongs to.\n"
    "    const bool streamOk = decodeDemoMessageStream(payload, payload.size() * 8u,\n"
    "                                                  static_cast<std::int32_t>(-2147483647 - 1),\n"
    "                                                  summary.lastNetworkTick, summary, stream,\n"
    "                                                  static_cast<std::uint32_t>(entryIndex));\n"
)
assert text.count(anchor) == 1, "scanKnownDemoMessages decode call anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED m2")
PYEOF
then
  echo "FATAL: the m2 mutation did not apply; the mutated build would prove nothing" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
bash build-target.sh demo_open_probe > "$OUT/mutation/m2-build.log" 2>&1
m2_build=$?
echo "mutation m2 build rc=$m2_build"
if [ "$m2_build" -ne 0 ]; then
  echo "FATAL: the m2 mutated build did not compile; the mutation proves nothing" >&2
  tail -20 "$OUT/mutation/m2-build.log" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
run_mutation_check m2 "golden bagel.teamplay_round_start first50" || MUTATION_FAIL=1
restore_and_verify m2

echo
echo "=== mutation m3: the dedupe key is degraded to the tick alone ==="
SOURCE=native/tools/demo_open_probe.cpp
BACKUP=.scratch/game-event-m3.bak
cp "$SOURCE" "$BACKUP"
if ! "$PY" - "$SOURCE" <<'PYEOF'
import io
import sys
path = sys.argv[1]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8").replace("\r\n", "\n") if crlf else raw.decode("utf-8")
anchor = (
    "          const auto key = std::make_tuple(event.tick, event.packetOrdinal, event.messageBit);\n"
)
inject = (
    "          // MUTATION m3: degrade the key to the tick alone -- the exact\n"
    "          // ambiguity the same-tick fixture exists to catch.\n"
    "          const auto key = std::make_tuple(event.tick, 0u, 0u);\n"
)
assert text.count(anchor) == 1, "probe dedupe key anchor"
text = text.replace(anchor, inject)
if crlf:
    text = text.replace("\n", "\r\n")
io.open(path, "wb").write(text.encode("utf-8"))
print("MUTATION-PATCHED m3")
PYEOF
then
  echo "FATAL: the m3 mutation did not apply; the mutated build would prove nothing" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
bash build-target.sh demo_open_probe > "$OUT/mutation/m3-build.log" 2>&1
m3_build=$?
echo "mutation m3 build rc=$m3_build"
if [ "$m3_build" -ne 0 ]; then
  echo "FATAL: the m3 mutated build did not compile; the mutation proves nothing" >&2
  tail -20 "$OUT/mutation/m3-build.log" >&2
  cp "$BACKUP" "$SOURCE"
  exit 1
fi
run_mutation_check m3 "same_tick bagel rows" || MUTATION_FAIL=1
restore_and_verify m3

echo
if [ "$MUTATION_FAIL" -eq 0 ] && [ "$fail" -eq 0 ]; then
  echo "MUTATION-MODE=PASS (3/3 mutations caught; sources byte-restored; restored baseline green)"
  exit 0
fi
echo "MUTATION-MODE=FAIL"
exit 1
