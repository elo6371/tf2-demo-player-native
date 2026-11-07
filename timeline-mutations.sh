#!/usr/bin/env bash
# timeline-mutations.sh -- the timeline gate has to be able to go red, and it has
# to name the clause that went red.
#
# WHY THIS EXISTS
#   T85 measured the playback clock by hand (31 runs) and found that nothing in
#   verify-all.sh would have caught a regression in it. timeline-check.sh turns
#   those readings into assertions, but a new gate is only a gate if someone has
#   watched it fail for the right reason. This file does that: two known defects,
#   injected one at a time, each of which must turn a NAMED assertion red.
#
# RUN IT UNDER THE HOST LOCK. It builds the product four times and launches it
# ten times; unlocked it would corrupt other members' captures.
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-timeline bash timeline-mutations.sh
#
# CASES
#   baseline  the untouched tree passes all four sections. If this fails, no
#             mutation result below means anything.
#   m1        the T27 capture hold is removed (native/src/main.cpp:9005-9019), so
#             --capture-tick N lands on the first drawn tick at or after N -- the
#             historical T26 defect. Section 1 must redden, naming
#             "seek 24001: captured frame drawn at the requested tick".
#   m2        --playback-speed 8 is played at 4 (native/src/main.cpp:2429). Section
#             2's speed reading must redden, naming
#             "speed 8x: the run really used the requested speed". The pixel
#             clause is expected to stay GREEN, and the script says so on the
#             record: the frame at a pinned tick is a pure function of the tick
#             (T85 measured it across 1x/8x/32x/64x), so it measures determinism,
#             not the speed mapping. The speed assertion is in the gate precisely
#             so this mutation has a red to reach.
#
# RULES (borrowed from spectate-mutations.sh, which paid for them)
#   * a mutant that does not compile is MUTATION-INVALID, not a catch;
#   * a gate that stays green under a mutant is MUTATION-GREEN -- a defect in the
#     gate, not a pass;
#   * red means a NAMED assertion failed; a build error or a timeout is not
#     evidence that the gate noticed;
#   * every mutant is reverted byte-for-byte (timeline-patchlib.py verify prints
#     applied=0) and the tree rebuilt before the next case; the run ends with a
#     clean tree and a green baseline;
#   * "git status is clean" is not proof enough that the sources came back (t85
#     once had an editor write nothing but still report a differ). So the run
#     also hashes native/src/main.cpp before the first mutant, after every
#     revert and at the end, prints all of them, and fails RESTORE if any hash
#     differs from the baseline one.
#
# Exit: 0 = every case behaved as designed and the tree is clean; 1 = otherwise.

set -uo pipefail
cd "$(dirname "$0")"

OUT="${MUT_OUT:-evidence/t85-timeline/gate/mutations}"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
PATCH="timeline-patchlib.py"
EXE=native/build-nmake/tf2_demo_native.exe
ONLY="${MUT_ONLY:-baseline,m1,m2}"
mkdir -p "$OUT"
: > "$OUT/mutation.log"
: > "$OUT/results.tsv"

say() { echo "$*" | tee -a "$OUT/mutation.log"; }
verdict=0
record() { printf '%s\t%s\n' "$1" "$2" >> "$OUT/results.tsv"; }

want() { case ",$ONLY," in *",$1,"*) return 0 ;; esac; return 1; }

build_target() { # build_target <tag>
  say "build: tag=$1"
  bash build-target.sh tf2_demo_native >> "$OUT/build-$1.log" 2>&1
  local rc=$?
  say "build: tag=$1 rc=$rc log=$OUT/build-$1.log"
  return $rc
}

gate_run() { # gate_run <tag> <sections>
  local tag="$1" sections="$2" dir="$OUT/gates/$1"
  mkdir -p "$dir"
  TIMELINE_SECTIONS="$sections" TIMELINE_OUT="$dir/timeline" \
    bash timeline-check.sh > "$dir/gate.log" 2>&1
  local rc=$?
  local summary
  summary=$(grep -a 'TIMELINE-CHECK=' "$dir/gate.log" | tail -1)
  say "gate: tag=$tag sections=$sections rc=$rc summary=${summary:-<none>} log=$dir/gate.log"
  printf '%s\t%s\t%s\n' "$tag" "$rc" "${summary:-<none>}" >> "$OUT/results.tsv"
  return $rc
}

named_red() { # named_red <tag> <assertion text without the FAIL prefix>
  grep -a -qF "FAIL $2" "$OUT/gates/$1/gate.log"
}

product_processes() {
  bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null \
    | grep -o 'product_processes=[0-9]*' | head -1
}

src_sha() { sha256sum native/src/main.cpp | cut -d' ' -f1; }

src_state() { # src_state <label> -- the source file must be byte-identical to the frozen one
  local now
  now=$(src_sha)
  if [ "$now" = "${BASE_SRC_SHA:-<unset>}" ]; then
    say "source: $1 sha256=$now (matches baseline)"
    return 0
  fi
  say "source: $1 sha256=$now MISMATCH baseline=${BASE_SRC_SHA:-<unset>}"
  return 1
}

restore_if_needed() {
  local case="$1"
  local state
  state=$("$PY" "$PATCH" verify "$case" 2>/dev/null | grep -o 'applied=[01]' | head -1)
  if [ "${state:-applied=?}" = "applied=1" ]; then
    say "restore: $case was still applied; reverting"
    "$PY" "$PATCH" restore "$case" | tee -a "$OUT/mutation.log"
  fi
}

cleanup() {
  if [ "${MUT_KEEP:-0}" != "1" ]; then
    restore_if_needed m1
    restore_if_needed m2
  fi
}
trap cleanup EXIT INT TERM

say "=== timeline-mutations: prove timeline-check.sh can fail ==="
say "exe     = $EXE"
say "sha256  = $(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1 || echo '<missing>')"
say "git     = $(git rev-parse HEAD 2>/dev/null || echo '<no-git>') $(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
say "cases   = $ONLY"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | tee "$OUT/status-begin.txt" | sed 's/^/lock: /' | tee -a "$OUT/mutation.log" >/dev/null

porcelain=$(git status --porcelain)
if [ -n "$porcelain" ]; then
  say "FATAL: the tree is dirty before the run:"
  echo "$porcelain" | sed 's/^/  /' | tee -a "$OUT/mutation.log"
  exit 1
fi

BASE_SRC_SHA=$(src_sha)
say "source: native/src/main.cpp sha256=$BASE_SRC_SHA (baseline, tree clean)"

say "--- build the untouched tree"
if ! build_target baseline; then
  say "MUTATION-INVALID baseline-build (the tree did not compile before any mutant)"
  record baseline-build FAIL
  exit 1
fi
say "baseline exe sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"

if want baseline; then
  say "--- baseline: the untouched tree must pass all four sections"
  if gate_run baseline 1,2,3,4; then
    say "MUTATION-BASELINE=GREEN"
    record baseline GREEN
  else
    say "MUTATION-BASELINE=RED (the gate fails on a healthy tree; nothing below is meaningful)"
    record baseline RED
    verdict=1
  fi
fi

if want m1; then
  say "--- m1: remove the T27 capture hold (main.cpp:9005-9019)"
  if "$PY" "$PATCH" apply m1 >> "$OUT/mutation.log" 2>&1; then
    say "source: m1 applied sha256=$(src_sha) baseline=$BASE_SRC_SHA"
    if build_target m1; then
      if gate_run m1-seek 1; then
        say "MUTATION m1 = GREEN (the gate stayed green after the hold was removed: the seek assertion is too weak)"
        record m1 GREEN
        verdict=1
      elif named_red m1-seek "seek 24001: captured frame drawn at the requested tick"; then
        say "MUTATION m1 = RED, named: seek 24001: captured frame drawn at the requested tick"
        record m1 RED
      else
        say "MUTATION m1 = RED-but-unnamed (the gate failed, but not on the assertion this mutant targets)"
        grep -a '^  FAIL' "$OUT/gates/m1-seek/gate.log" | tee -a "$OUT/mutation.log"
        record m1 RED-UNNAMED
        verdict=1
      fi
    else
      say "MUTATION m1 = INVALID (the mutant did not compile; not a catch)"
      record m1 INVALID
      verdict=1
    fi
    "$PY" "$PATCH" restore m1 >> "$OUT/mutation.log" 2>&1
    src_state "after m1 restore" || verdict=1
  else
    say "MUTATION m1 = INVALID (the patch did not apply; see $OUT/mutation.log)"
    record m1 PATCH-FAILED
    verdict=1
  fi
fi

if want m2; then
  say "--- m2: --playback-speed 8 is played at 4 (main.cpp:2429)"
  if "$PY" "$PATCH" apply m2 >> "$OUT/mutation.log" 2>&1; then
    say "source: m2 applied sha256=$(src_sha) baseline=$BASE_SRC_SHA"
    if build_target m2; then
      if gate_run m2-speed 2; then
        say "MUTATION m2 = GREEN (the gate stayed green after the speed mapping was broken: nothing reads the speed)"
        record m2 GREEN
        verdict=1
      elif named_red m2-speed "speed 8x: the run really used the requested speed"; then
        say "MUTATION m2 = RED, named: speed 8x: the run really used the requested speed"
        record m2 RED
      else
        say "MUTATION m2 = RED-but-unnamed (the gate failed, but not on the assertion this mutant targets)"
        grep -a '^  FAIL' "$OUT/gates/m2-speed/gate.log" | tee -a "$OUT/mutation.log"
        record m2 RED-UNNAMED
        verdict=1
      fi
      say "m2 note -- section 2's pixel clauses under the broken mapping:"
      grep -a 'same tick 600 at' "$OUT/gates/m2-speed/gate.log" | sed 's/^/  /' | tee -a "$OUT/mutation.log"
    else
      say "MUTATION m2 = INVALID (the mutant did not compile; not a catch)"
      record m2 INVALID
      verdict=1
    fi
    "$PY" "$PATCH" restore m2 >> "$OUT/mutation.log" 2>&1
    src_state "after m2 restore" || verdict=1
  else
    say "MUTATION m2 = INVALID (the patch did not apply; see $OUT/mutation.log)"
    record m2 PATCH-FAILED
    verdict=1
  fi
fi

say "--- restore: the tree must be byte-identical to the frozen sources"
for case in m1 m2; do
  "$PY" "$PATCH" verify "$case" | tee -a "$OUT/mutation.log"
done
if ! src_state "after all restores"; then
  say "RESTORE=FAIL (native/src/main.cpp is not byte-identical to the frozen source; git status alone cannot prove this)"
  record restore FAIL-SHA
  verdict=1
fi
if [ -n "$(git status --porcelain)" ]; then
  say "RESTORE=FAIL (the tree is still dirty)"
  git status --porcelain | sed 's/^/  /' | tee -a "$OUT/mutation.log"
  record restore FAIL
  verdict=1
else
  say "RESTORE=OK (git status is clean)"
  record restore OK
fi

say "--- rebuild the restored tree and re-run the whole gate"
if build_target restore; then
  if gate_run final-baseline 1,2,3,4; then
    say "FINAL-BASELINE=GREEN"
    record final-baseline GREEN
  else
    say "FINAL-BASELINE=RED (the restored tree does not pass its own gate)"
    record final-baseline RED
    verdict=1
  fi
else
  say "FINAL-BUILD=FAIL"
  record final-baseline BUILD-FAILED
  verdict=1
fi

pp=$(product_processes)
say "product_processes after batch: ${pp:-<unknown>}"
pp_count="${pp##*=}"
if [ "${pp_count:-0}" != "0" ]; then
  say "NOTE: product_processes is not 0; another member's product may be running. This script does not kill processes it does not own."
fi
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | tee "$OUT/status-end.txt" | sed 's/^/lock: /' | tee -a "$OUT/mutation.log" >/dev/null

echo
if [ "$verdict" -eq 0 ]; then
  echo "TIMELINE-MUTATIONS=PASS"
  exit 0
fi
echo "TIMELINE-MUTATIONS=FAIL"
exit 1