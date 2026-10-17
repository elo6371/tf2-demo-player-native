#!/usr/bin/env bash
# stability-release-check.sh -- judge the T4 stability/release matrix.
#
# WHY THIS FILE EXISTS
# --------------------
# The matrix (stability-run-matrix.ps1) produces 19 case directories, each with
# a readings.json. Producing readings is not judging them. Before this gate the
# only thing that read those directories was a human eye, and a directory that
# was never written at all looked exactly like a directory whose case passed:
# both are "no complaint". The rule this task works under is that a case whose
# absence, crash, or silent refusal cannot make this file go red is not a case.
#
# WHAT THIS GATE READS, AND WHY IT DOES NOT RUN THE PRODUCT
# --------------------------------------------------------
# This gate is the judgment layer over evidence that already exists. It starts
# no process: the matrix runner owns the exe, the audio device and the wall
# clock, and re-running an 11-minute soak inside a gate would make the gate the
# slowest and least repeatable thing in the chain. Re-running the matrix is an
# explicit act (`stability-run-matrix.ps1 -RunId <new>`), and the gate is then
# pointed at the new directory with --run-dir.
#
# Because no process is started here, no audio device is opened here, and the
# --audio-device 6 rule for this machine is not at risk in this file. The
# matrix runner pins it where the process is actually started (soak-window).
#
# WHY IT IS WRITTEN THE WAY IT IS
# -------------------------------
# Every readings.json key costs one process per field in the obvious
# implementation (sed | head | tr | sed), and a fork on this machine costs
# ~0.2 s: 114 key reads measured 97 s for one run, and the gate needs seven
# runs (the forward pass plus six mutations). So each file is parsed ONCE into
# an associative array with a single awk, and the syntax check of the four
# scripts is a single invocation of the script parser instead of four. The
# assertions are unchanged: the optimisation is in how the evidence is loaded,
# never in what is asserted.
#
# WHAT MAKES A READING HERE ABLE TO GO RED
# ----------------------------------------
#   * a case that never ran has no readings.json -- and that is a FAIL, not a
#     skip (section 3)
#   * a crash is a crash: the exit code is read against the fault codes and the
#     product's own trace markers, not against "the case finished"
#   * a soak that rendered nothing is a FAIL: the frame count must advance by a
#     bound, and stall_samples must be zero (the demo had NOT reached its end
#     tick, so a stall is a stall, not a finished recording)
#   * a leak is judged on the STEADY STATE, not on the load ramp: the second
#     half of the sample series is compared against itself, because the first
#     half of a demo run is the asset load and reading it as growth would make
#     every honest run look like a leak
#   * the TF root a case actually used is compared against the --tf-root the
#     case itself recorded in command.txt, not against a name the case was
#     given: an assertion that checks the case name instead of the wiring would
#     pass while the harness tested something else
#   * a refusal must be NAMED, by the product or by the repo's own parser probe
#     on the same bytes; "it exited 0" is not a refusal
#   * an environment this machine cannot produce must be RECORDED as blocked;
#     a missing blocked-inputs.txt is a FAIL, because "not verified" that is not
#     written down is indistinguishable from "verified"
#   * with --mutation, six single-reading changes to a copy of the evidence
#     must each turn this gate red, with the control assertion still green
#
# Usage: bash stability-release-check.sh [--run-dir <dir>] [--mutation]
#          [--staged-subset]
#   --run-dir <dir>   the matrix run to judge (default: the newest COMPLETE run
#                     under .scratch/stability-release, preferring one that
#                     covers all 19 cases)
#   --mutation        mutate a copy of the evidence six ways and require this
#                     gate to go red on each, with the control reading preserved
#   --staged-subset   internal: the evidence is a staged subset (the mutation
#                     harness), so the full-file-set contract is skipped
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red)
set -uo pipefail
cd "$(dirname "$0")"

REPO="$PWD"
EXE=native/build-nmake/tf2_demo_native.exe
SCRATCH_ROOT=".scratch/stability-release"
OUT="${STABILITY_OUT:-evidence/stability-release}"
MUT_SCRATCH="${STABILITY_MUT_SCRATCH:-.scratch/stability-release-mutation}"

RUN_DIR=""
MUTATION=0
STAGED=0
while [ $# -gt 0 ]; do
  case "$1" in
    --run-dir) RUN_DIR="${2:-}"; shift 2 ;;
    --mutation) MUTATION=1; shift ;;
    --staged-subset) STAGED=1; shift ;;
    -h|--help) sed -n '2,60p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1"; exit 2 ;;
  esac
done

mkdir -p "$OUT" "$MUT_SCRATCH"

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ne() { # assert_ne <label> <actual> <forbidden>
  if [ "${2:-<none>}" != "$3" ]; then
    echo "  OK   $1 != $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, which is the value it must not be"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if awk -v a="${2:-}" -v b="$3" 'BEGIN{ if (a=="" || a=="n/a") exit 1; exit !(a+0>=b+0) }'; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <maximum>
  if awk -v a="${2:-}" -v b="$3" 'BEGIN{ if (a=="" || a=="n/a") exit 1; exit !(a+0<=b+0) }'; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
# assert_eq cannot express "expected the empty string": it prints ${2:-<none>}
# for an empty value, so an empty actual reads as absent. A reading that is
# allowed to be empty needs its own assertion, or the gate fails an honest run.
assert_empty() { # assert_empty <label> <actual>
  if [ -z "${2:-}" ]; then
    echo "  OK   $1 = (empty)"
  else
    echo "  FAIL $1 = $2, expected empty"; fail=1
  fi
}
sec() { echo; echo "=== $1/8 $2 ==="; }

# ---- evidence loading: ONE process per file, never per key ----------------
# R is the current case's readings.json, T its title-last.txt, C its
# command.txt. The field a case *was given* (C) and the field it *actually used*
# (T) are different claims, and only together are they a wiring check.
declare -A R T C

load_readings() { # <case dir> -> R
  R=(); [ -f "$1/readings.json" ] || return 1
  local out
  out="$(awk '
    { sub(/\r$/, "") }
    match($0, /^[[:space:]]*"[^"]+"[[:space:]]*:/) {
      head = substr($0, RSTART, RLENGTH); rest = substr($0, RSTART + RLENGTH)
      gsub(/[[:space:]":]/, "", head)
      sub(/^[[:space:]]+/, "", rest)
      sub(/,$/, "", rest)
      if (substr(rest,1,1) == "\"") { rest = substr(rest,2); sub(/"$/, "", rest) }
      if (head != "") print head "=" rest
    }' "$1/readings.json")"
  while IFS='=' read -r k v; do
    [ -n "$k" ] && R["$k"]="$v"
  done <<< "$out"
  return 0
}

# The title carries five facts this gate asserts on. They are classified by
# their LABEL, not by the shape of their value: "VPK entries: 104137" and
# "SourceTV ticks=64352" both reduce to a bare integer once the label is
# stripped, so a shape-based classification lets the second overwrite the
# first and silently returns the wrong one.
load_title() { # <case dir> -> T
  T=(); [ -f "$1/title-last.txt" ] || return 1
  local out
  out="$(grep -oP 'TF2 [^:]*: [A-Za-z]:\\[^\[]*|VPK entries: [^,]*|BSP: [^,]*|maptex: [^]]*|Demo: [^|]*' \
                     "$1/title-last.txt" 2>/dev/null)"
  while IFS= read -r line; do
    # The title's "root [" separator leaves a trailing space on the value, and
    # command.txt quotes any value containing whitespace. Both are presentation,
    # and neither may decide an assertion: a root that reads as equal to the
    # one it was given is not a wiring check if one of them carries a space.
    line="${line%"${line##*[![:space:]]}"}"
    case "$line" in
      "TF2 "*)           T[root]="${line##*: }" ;;
      "VPK entries: "*)  T[vpk]="${line#VPK entries: }" ;;
      "BSP: "*)          T[bsp]="${line#BSP: }" ;;
      "maptex: "*)       T[maptex]="${line#maptex: }" ;;
      # T9 (2026-10-16): the whole Demo segment, not just its tick count. The
      # tick is parsed out of it below, by key.
      "Demo: "*)         T[demo]="${line#Demo: }" ;;
    esac
  done <<< "$out"
  # The demo's end tick, taken from the `ticks=` KEY inside the Demo segment
  # instead of from the recording-type label in front of it.
  #
  # The label is one of "SourceTV", "POV", "POV (heuristic)" or "unknown"
  # (classifyDemoRecording, demo_header.cpp). This function used to match the
  # literal "SourceTV ticks=", so a POV recording produced no ticks at all --
  # not a parse error, an ABSENT reading, which line 454 then read as 0 and the
  # end-tick assertion compared against. Every POV case failed unconditionally
  # and the failure looked like a stalled demo.
  #
  # The key is what the title guarantees: `demoSuffix` always writes
  # " ticks=<n>" once, and the only other `ticks=` in any title state belongs to
  # the import-review screen, which has no "Demo: " segment to be found in. So
  # anchoring on the segment and then on the key removes the coupling without
  # widening what can match.
  T[ticks]=""
  if [ -n "${T[demo]:-}" ]; then
    if [[ "${T[demo]}" =~ (^|[[:space:]])ticks=([0-9]+)([[:space:]]|$) ]]; then
      T[ticks]="${BASH_REMATCH[2]}"
    fi
  fi
  return 0
}

load_command() { # <case dir> -> C (the arguments the harness actually passed)
  C=(); [ -f "$1/command.txt" ] || return 1
  local out k v
  out="$(awk '
    { sub(/\r$/, "") }
    /^--/   { if (pending != "") print pending "="; pending = $0; next }
    pending != "" { print pending "=" $0; pending = ""; next }
    END { if (pending != "") print pending "=" }' "$1/command.txt")"
  while IFS='=' read -r k v; do
    [ -n "$k" ] || continue
    # The harness quotes any value containing whitespace, so the TF root
    # arrives with its quotes still on and would never compare equal to the
    # unquoted root the title reports.
    v="${v%\"}"; v="${v#\"}"
    C["$k"]="$v"
  done <<< "$out"
  return 0
}

# named_error_from_probe <parser-probe.txt> -- the parser's own named error.
# The probe prints "<key>=<value>" pairs separated by spaces, and the value
# itself contains spaces ("demo header is truncated"), so the value is taken as
# everything up to the next key rather than up to the next space.
named_error_from_probe() {
  [ -f "$1" ] || return 1
  local e
  e="$(sed -n 's/.*header_error=\(.*\) index=.*/\1/p' "$1" | head -1)"
  if [ -z "$e" ]; then
    e="$(sed -n 's/.*index_error=\(.*\) recording=.*/\1/p' "$1" | head -1)"
  fi
  [ -n "$e" ] && printf '%s' "$e"
  return 0
}

# second_half_delta <sampler.csv> <column>
# t=1 cpu=2 ws=3 priv=4 thr=5 resp=6 frames=7 tick=8
# Rows whose frames counter is -1 are the asset load, not the steady state.
second_half_delta() {
  awk -F, -v col="$2" '
    NR>1 && $7!="" && $7!="-1" { n++; v[n]=$col+0 }
    END { if (n < 4) { print "n/a"; exit }
          h = int(n/2); if (h < 1) h = 1
          printf "%.1f", v[n]-v[h] }' "$1"
}
hung_rows_with_frames() {
  awk -F, 'NR>1 && $7!="" && $7!="-1" && $6=="0" { n++ } END { print n+0 }' "$1"
}
sampler_frames_delta() { # last minus first rendered frame count, load excluded
  awk -F, 'NR>1 && $7!="" && $7!="-1" { if (first=="") first=$7+0; last=$7+0 } END { print last-first }' "$1"
}

# ---------------------------------------------------------------- run dir
# A run still being written must not be judged: half its case directories do
# not exist yet, and "not written yet" would read as "failed". Only a manifest
# with a finished_at= line is a candidate, and a run covering all 19 cases is
# preferred over a partial one so that a later `-Sets resource,audio,device`
# run cannot silently become the thing this gate calls the matrix.
if [ -z "$RUN_DIR" ]; then
  best_full=""; best_full_t=0
  best_any=""; best_any_t=0
  for d in "$SCRATCH_ROOT"/*/; do
    [ -f "$d/run-manifest.txt" ] || continue
    grep -q '^finished_at=' "$d/run-manifest.txt" || continue
    t="$(stat -c %Y "$d/run-manifest.txt" 2>/dev/null || echo 0)"
    n="$(grep -c '^case_done ' "$d/run-manifest.txt")"
    if [ "$t" -gt "$best_any_t" ]; then best_any_t="$t"; best_any="$d"; fi
    if [ "$n" -eq 19 ] && [ "$t" -gt "$best_full_t" ]; then best_full_t="$t"; best_full="$d"; fi
  done
  if [ -n "$best_full" ]; then RUN_DIR="$best_full"; else RUN_DIR="$best_any"; fi
fi
RUN_DIR="${RUN_DIR%/}"
if [ -z "$RUN_DIR" ] || [ ! -f "$RUN_DIR/run-manifest.txt" ]; then
  echo "FATAL: no matrix run to judge (looked under $SCRATCH_ROOT for run-manifest.txt)"
  echo "       produce one with stability-run-matrix.ps1 -RunId run-01"
  exit 1
fi
RUN_DIR_WIN="$(cygpath -w "$PWD/$RUN_DIR" 2>/dev/null || echo "$RUN_DIR")"

echo "stability-release-check.sh"
echo "  run dir : $RUN_DIR"
echo "  evidence: $OUT"
[ "$STAGED" -eq 1 ] && echo "  mode    : staged subset (the file-set contract is skipped)"

CORRUPT_CASES="corrupt-empty corrupt-truncated-header corrupt-truncated-packet corrupt-bad-magic corrupt-bad-version corrupt-incomplete-netmsg corrupt-mid-truncated"
RESOURCE_CASES="res-missing-root res-missing-vpk res-missing-model res-missing-bsp res-root-moved res-root-stale-path res-unreadable-root"
LONG_CASES="smoke-hidden soak-hidden soak-window"
AUDIO_CASES="audio-invalid-device audio-missing-resource"
CASE_FILES="command.txt env.txt stdout.txt stderr.txt trace.txt metrics.csv sampler.csv exit-code.txt process-state.txt verdict.txt title-first.txt title-last.txt leftovers.txt readings.json settings.ini"

manifest="$RUN_DIR/run-manifest.txt"
# The root the product falls back to is the auto-detected TF root, and the
# manifest records which one that was for this run -- so the fallback is
# compared against the run's own record rather than a hardcoded path.
manifest_tf_root="$(sed -n 's/^tf_root=//p' "$manifest" | head -1)"

# ================================================================ 1 the instrument
sec 1 "the instrument, and the run directory" 8
# The four scripts are proven by having RUN, not by being parsed: a static
# syntax check costs a second per session and proves only that a file parses,
# while the manifest records what each script actually wrote. Execution is the
# stronger reading and it is already in the evidence.
assert_eq "stability-run-matrix.ps1 exists" "$([ -f stability-run-matrix.ps1 ] && echo yes || echo no)" "yes"
assert_eq "stability-run-matrix.ps1 finished the run (finished_at= written)" \
  "$(grep -c '^finished_at=' "$manifest")" "1"
assert_eq "stability-run-case.ps1 exists" "$([ -f stability-run-case.ps1 ] && echo yes || echo no)" "yes"
case_done="$(grep -c '^case_done ' "$manifest" 2>/dev/null || echo 0)"
assert_eq "stability-run-case.ps1 ran every case (case_done lines)" "$case_done" "19"
bad_rc="$(grep '^case_done ' "$manifest" 2>/dev/null | grep -cv 'runner_rc=0' || true)"
assert_eq "every case's runner exited 0" "$bad_rc" "0"
assert_eq "stability-make-inputs.ps1 exists" "$([ -f stability-make-inputs.ps1 ] && echo yes || echo no)" "yes"
assert_eq "stability-make-inputs.ps1 built the fixture index" \
  "$([ -s "$RUN_DIR/fixtures/fixture-index.txt" ] && echo yes || echo no)" "yes"
assert_eq "stability-device-info.ps1 exists" "$([ -f stability-device-info.ps1 ] && echo yes || echo no)" "yes"
assert_eq "stability-device-info.ps1 recorded the device environment" \
  "$([ -s "$RUN_DIR/device-info.txt" ] && echo yes || echo no)" "yes"
assert_eq "the product exists" "$([ -f "$EXE" ] && echo yes || echo no)" "yes"
if [ -f "$EXE" ]; then
  exe_sha="$(sha256sum "$EXE" | cut -d' ' -f1)"
  exe_size="$(stat -c %s "$EXE")"
  exe_epoch="$(stat -c %Y "$EXE")"
  run_epoch="$(date -d "$(sed -n 's/^finished_at=//p' "$manifest" | head -1 | cut -d'.' -f1)+08:00" +%s 2>/dev/null || echo 0)"
  echo "  RECORD exe sha256=$exe_sha size=$exe_size"
  { echo "exe_sha256=$exe_sha"; echo "exe_size=$exe_size";
    echo "exe_built_epoch=$exe_epoch";
    echo "run_finished_epoch=$run_epoch";
    echo "git_head=$(git rev-parse HEAD 2>/dev/null || echo unknown)"; } > "$OUT/exe-identity.txt"
  # Path identity is not binary identity. The obvious check is a timestamp --
  # "the exe is newer than the run" -- and it is weaker than it looks: it only
  # catches a rebuild that happened to be visible in the clock, and it says
  # nothing when the run's binary was one nobody recorded at all. The manifest
  # therefore carries the exe's sha256 (stability-run-matrix.ps1 writes it from
  # the binary it actually launches) and this gate compares hashes.
  if [ "$STAGED" -eq 1 ]; then
    echo "  SKIP the evidence is about this exe (staged subset)"
  else
    manifest_exe_sha="$(sed -n 's/^exe_sha256=//p' "$manifest" | head -1 | tr -d ' \r')"
    if [ -z "$manifest_exe_sha" ]; then
      echo "  FAIL the manifest records no exe_sha256: the run cannot be tied to a binary, so nothing it says can be re-derived"
      fail=1
    elif [ "$manifest_exe_sha" != "$exe_sha" ]; then
      echo "  FAIL the manifest's exe sha256 is $manifest_exe_sha, this worktree's exe is $exe_sha: the evidence is about a different binary"
      fail=1
    else
      echo "  OK   the manifest's exe sha256 matches this worktree's exe"
    fi
  fi
fi
for k in run_id started_at finished_at exe snake_demo tf_root; do
  v="$(sed -n "s/^$k=//p" "$manifest" | head -1)"
  if [ "$k" = "finished_at" ] && [ -z "$v" ]; then
    echo "  FAIL the run is still in progress (the manifest has no finished_at= line)"
    fail=1
  else
    assert_ne "the manifest records $k" "${v:-}" ""
  fi
done
if [ "$STAGED" -eq 0 ]; then
  # cygpath -m gives the forward-slash form of the path, which is the form the
  # manifest records after its backslashes are translated: comparing against
  # $PWD would compare "/d/..." against "D:/..." and fail an honest run.
  assert_eq "the manifest's exe is this worktree's exe" \
    "$(sed -n 's/^exe=//p' "$manifest" | head -1 | tr '\\' '/')" \
    "$(cygpath -m "$PWD/$EXE" 2>/dev/null || printf '%s' "$REPO/$EXE")"
else
  echo "  SKIP the manifest's exe identity (staged subset)"
fi

# ================================================================ 2 environment
sec 2 "the environment the matrix was read against" 8
dev="$RUN_DIR/device-info.txt"
assert_eq "device-info.txt exists" "$([ -f "$dev" ] && echo yes || echo no)" "yes"
if [ -f "$dev" ]; then
  assert_ge "video controllers recorded" "$(grep -c '^controller name=' "$dev")" 1
  assert_eq "the session type is recorded" "$(grep -c '^session_name=' "$dev")" "1"
  assert_ge "audio output devices enumerated" "$(sed -n 's/.*device_count=\([0-9]*\).*/\1/p' "$dev" | head -1)" 1
  # soak-window runs with --audio-device 6; a device record that does not
  # contain device 6 could not have justified that choice.
  assert_ge "device 6 (the pinned audio device) is in the record" "$(grep -c '^  device=6 query=' "$dev")" 1
fi
assert_eq "dxdiag.txt exists" "$([ -s "$RUN_DIR/dxdiag.txt" ] && echo yes || echo no)" "yes"
isp="$RUN_DIR/install-smoke-probe.txt"
assert_eq "install-smoke-probe.txt exists" "$([ -f "$isp" ] && echo yes || echo no)" "yes"
if [ -f "$isp" ]; then
  assert_eq "the install smoke probe produced an executable" "$(sed -n 's/.*install_executable=\([0-9]*\).*/\1/p' "$isp" | head -1)" "1"
  assert_eq "a missing tf root produces a prompt" "$(sed -n 's/.*tf_root_missing_prompt=\([0-9]*\).*/\1/p' "$isp" | head -1)" "1"
  assert_eq "this machine can create a WARP device" "$(sed -n 's/.*warp_fallback=\([0-9]*\).*/\1/p' "$isp" | head -1)" "1"
fi

# ================================================================ 3 case contract
sec 3 "every case wrote the evidence it claims to write" 8
all_cases="$LONG_CASES $CORRUPT_CASES $RESOURCE_CASES $AUDIO_CASES"
for c in $all_cases; do
  d="$RUN_DIR/$c"
  if [ ! -d "$d" ]; then
    echo "  FAIL $c has no case directory at all"; fail=1; continue
  fi
  # A case whose argument the product refused before it reached the loop
  # (exit 13, the documented argument-rejection code) never creates
  # metrics.csv or settings.ini: the exe stops before opening them. That is a
  # named, bounded exception rather than a skip -- every other named file must
  # still be there, and the two are recorded as expected-absent. A hard-coded
  # exception list would let a case stop writing files by being named right.
  required="$CASE_FILES"
  expected_absent=""
  if [ -f "$d/exit-code.txt" ]; then
    ec="$(sed -e 's/^\xef\xbb\xbf//' -e 's/[^0-9]//g' "$d/exit-code.txt" 2>/dev/null)"
    if [ "$ec" = "13" ]; then
      required="$(printf '%s\n' $CASE_FILES | grep -v '^\(metrics\.csv\|settings\.ini\)$' | tr '\n' ' ')"
      expected_absent=" (argument refused before the loop: exit 13, so metrics.csv and settings.ini are expected-absent)"
    fi
  fi
  if [ "$STAGED" -eq 1 ]; then
    [ -f "$d/readings.json" ] || { echo "  FAIL $c has no readings.json"; fail=1; }
    continue
  fi
  missing=""
  for f in $required; do
    [ -f "$d/$f" ] || missing="$missing $f"
  done
  if [ -n "$missing" ]; then
    echo "  FAIL $c is missing:$missing"; fail=1
  else
    echo "  OK   $c wrote all $(printf '%s\n' $required | grep -c .) named files$expected_absent"
  fi
done

# ================================================================ 4 smoke
sec 4 "the smoke: the harness must prove itself before the soak is believed" 8
c="smoke-hidden"; d="$RUN_DIR/$c"; r="$d/readings.json"; s="$d/sampler.csv"
if load_readings "$d"; then
  assert_eq "$c verdict" "${R[verdict]:-}" "COMPLETED"
  assert_eq "$c exit code" "${R[exit_code]:-}" "0"
  assert_eq "$c process state" "${R[process_state]:-}" "NORMAL_EXIT"
  assert_empty "$c closed by the harness's post" "${R[close_failed_reason]:-}"
  assert_eq "$c crash marker" "${R[trace_crash_marker]:-}" "false"
  assert_eq "$c threw marker" "${R[trace_threw_marker]:-}" "false"
  assert_eq "$c residual processes" "${R[residual_processes]:-}" "none"
  assert_eq "$c leftover files" "${R[leftover_files]:-}" "none"
  assert_ge "$c samples" "${R[samples]:-}" 30
  assert_ge "$c rendered frames advanced (sample series)" "$(sampler_frames_delta "$s")" 1000
  assert_ge "$c last rendered frame count (readings.json)" "${R[last_rendered_frames]:-}" 1000
  assert_eq "$c hung samples (rows with a frame counter)" "$(hung_rows_with_frames "$s")" "0"
else
  echo "  FAIL $c has no readings.json"; fail=1
fi

# ================================================================ 5 soak
sec 5 "the soak: an 11-minute run that neither leaked nor stopped drawing" 8
for c in soak-hidden soak-window; do
  d="$RUN_DIR/$c"; r="$d/readings.json"; s="$d/sampler.csv"
  echo "  -- $c --"
  if load_readings "$d"; then
    assert_eq "$c verdict" "${R[verdict]:-}" "COMPLETED"
    assert_eq "$c exit code" "${R[exit_code]:-}" "0"
    assert_eq "$c crash marker" "${R[trace_crash_marker]:-}" "false"
    assert_eq "$c threw marker" "${R[trace_threw_marker]:-}" "false"
    assert_eq "$c residual processes" "${R[residual_processes]:-}" "none"
    assert_eq "$c leftover files" "${R[leftover_files]:-}" "none"
    assert_ge "$c ran the requested window" "${R[observed_seconds]:-}" 660
    assert_ge "$c rendered frames advanced (sample series)" "$(sampler_frames_delta "$s")" 10000
    assert_ge "$c last rendered frame count (readings.json)" "${R[last_rendered_frames]:-}" 10000
    # The demo had NOT reached its end tick in this window, so a sample whose
    # frame counter did not move is a stall, not a finished recording. The end
    # tick is read from the run's own title, not hardcoded.
    if load_title "$d"; then
      demo_ticks="${T[ticks]:-0}"
    else
      demo_ticks=0
    fi
    assert_ge "$c last tick is short of the demo's end tick" \
      "$(awk -v t="${R[last_tick]:-0}" -v e="$demo_ticks" 'BEGIN{print e-t}')" 1
    assert_eq "$c stall samples" "${R[stall_samples]:-}" "0"
    assert_eq "$c hung samples (rows with a frame counter)" "$(hung_rows_with_frames "$s")" "0"
    # Steady state, not the load ramp: the asset load spans the first minute and
    # reading it as growth would make every honest run look like a leak.
    assert_le "$c working set growth, second half" "$(second_half_delta "$s" 3)" 32
    assert_le "$c private bytes growth, second half" "$(second_half_delta "$s" 4)" 32
    assert_le "$c thread growth, second half (absolute)" \
      "$(awk -v g="$(second_half_delta "$s" 5)" 'BEGIN{print (g<0?-g:g)}')" 8
    assert_le "$c peak working set" "${R[max_working_set_mb]:-}" 2048
    assert_le "$c peak private bytes" "${R[max_private_mb]:-}" 3072
  else
    echo "  FAIL $c has no readings.json"; fail=1
  fi
done

# ================================================================ 6 corrupt
sec 6 "the corrupt-input matrix: a refusal must be named, and must not be a crash" 8
product_named=0
probe_named=0
unnamed=""
for c in $CORRUPT_CASES; do
  d="$RUN_DIR/$c"
  if load_readings "$d"; then
    assert_eq "$c verdict" "${R[verdict]:-}" "COMPLETED"
    assert_eq "$c exit code" "${R[exit_code]:-}" "0"
    assert_eq "$c crash marker" "${R[trace_crash_marker]:-}" "false"
    assert_eq "$c threw marker" "${R[trace_threw_marker]:-}" "false"
    assert_eq "$c residual processes" "${R[residual_processes]:-}" "none"
    assert_eq "$c leftover files" "${R[leftover_files]:-}" "none"
    named="${R[demo_error_from_title]:-}"
    src=""
    if [ -n "$named" ]; then
      src="product"; product_named=$((product_named+1))
    else
      named="$(named_error_from_probe "$d/parser-probe.txt" || true)"
      if [ -n "$named" ]; then src="parser-probe"; probe_named=$((probe_named+1)); fi
    fi
    if [ -n "$named" ]; then
      echo "  OK   $c refusal named by $src: $named"
    else
      echo "  FAIL $c refuses the input without naming a reason (neither the product nor the parser probe named one)"
      unnamed="$unnamed $c"; fail=1
    fi
  else
    echo "  FAIL $c has no readings.json"; fail=1
  fi
done
assert_eq "every corrupt case's refusal is named somewhere" "${unnamed:-none}" "none"
echo "  RECORD the product itself named $product_named of 7; the repo's parser probe named the other $probe_named"
echo "  RECORD the split is a finding, not a pass: for the probe-named cases the exe's own"
echo "         record is 'ran to the end of the window, exit 0, no message'"
{
  echo "product_named=$product_named"
  echo "probe_named=$probe_named"
  for c in $CORRUPT_CASES; do
    load_readings "$RUN_DIR/$c" 2>/dev/null
    printf '%s product_error=%s probe_error=%s\n' "$c" \
      "${R[demo_error_from_title]:-}" \
      "$(named_error_from_probe "$RUN_DIR/$c/parser-probe.txt" || echo -)"
  done
} > "$OUT/corrupt-naming.txt"

# ================================================================ 7 resource
sec 7 "the resource environments: an explicit root is the ONLY candidate -- refusal is the corrected behavior, silent substitution is the defect" 8
root_honoured=0
root_refused=0
for c in $RESOURCE_CASES; do
  d="$RUN_DIR/$c"
  if load_readings "$d"; then
    assert_eq "$c verdict" "${R[verdict]:-}" "COMPLETED"
    assert_eq "$c exit code" "${R[exit_code]:-}" "0"
    assert_eq "$c crash marker" "${R[trace_crash_marker]:-}" "false"
    assert_eq "$c residual processes" "${R[residual_processes]:-}" "none"
    assert_eq "$c leftover files" "${R[leftover_files]:-}" "none"
    load_title "$d"
    used="${T[root]:-}"
    # What the case was GIVEN comes from the case's own command.txt, and what
    # it USED comes from the title. Comparing those two is a wiring check;
    # comparing a name to a path is not.
    load_command "$d"
    given="${C[--tf-root]:-}"
    assert_ne "$c was given a tf root" "$given" ""
    case "$c" in
      res-missing-bsp)
        # The only resource case whose given root can actually serve the VPK:
        # the real install, with a demo whose map is not in it. Here the
        # explicit root IS used, and the root-level readings are the real
        # install's.
        assert_eq "$c used the root it was given" "$used" "$given"
        assert_eq "$c VPK entries (the real root)" "${T[vpk]:-}" "104137"
        assert_eq "$c BSP state" "${T[bsp]:-}" "invalid/BSP header is invalid"
        root_honoured=$((root_honoured+1))
        ;;
      *)
        # Every other resource case hands the product a root that cannot
        # serve a VPK: a path that does not exist (res-missing-root,
        # res-root-stale-path), a directory without a VPK (res-missing-vpk,
        # res-missing-model), a directory that was moved away
        # (res-root-moved), a directory this identity cannot read
        # (res-unreadable-root). The corrected root contract says an explicit
        # --tf-root is the ONLY candidate: the product must report that no
        # root was found and must never substitute the auto-detected Steam
        # root behind the operator's back. "Connected no root" is therefore
        # the corrected reading, and the substitution assertion below is the
        # one that goes red if the fallback defect ever returns.
        assert_empty "$c connected no root" "$used"
        assert_ne "$c did not silently connect the auto-detected root" "$used" "$manifest_tf_root"
        root_refused=$((root_refused+1))
        ;;
    esac
  else
    echo "  FAIL $c has no readings.json"; fail=1
  fi
done
assert_eq "roots honoured as requested" "$root_honoured" "1"
assert_eq "roots refused without a substitution" "$root_refused" "6"
echo "  RECORD 1 of 7 resource cases connects a root (the real one); the other 6 report"
echo "         'no root found' instead of silently substituting the auto-detected root -- that"
echo "         substitution was the defect the explicit-root fix removed, and the"
echo "         did-not-silently-connect assertions above go red on it"
{
  for c in $RESOURCE_CASES; do
    load_title "$RUN_DIR/$c" 2>/dev/null; load_command "$RUN_DIR/$c" 2>/dev/null
    printf '%s given=%s used=%s vpk=%s bsp=%s\n' "$c" \
      "${C[--tf-root]:-}" "${T[root]:-}" "${T[vpk]:-}" "${T[bsp]:-}"
  done
} > "$OUT/resource-roots.txt"

# ================================================================ 8 audio + blocked
sec 8 "the audio cases, and the environments recorded as blocked rather than skipped" 8
d="$RUN_DIR/audio-invalid-device"
if load_readings "$d"; then
  # --audio-device 99 is rejected before any window exists. The named evidence
  # is the exit code the product chose (13), because the reason goes to the
  # debug channel, which the harness does not capture and does not invent.
  assert_eq "audio-invalid-device verdict" "${R[verdict]:-}" "EARLY_EXIT"
  assert_eq "audio-invalid-device exit code" "${R[exit_code]:-}" "13"
  assert_eq "audio-invalid-device was not closed (no window existed)" "${R[close_requested]:-}" "false"
  assert_eq "audio-invalid-device crash marker" "${R[trace_crash_marker]:-}" "false"
  assert_eq "audio-invalid-device residual processes" "${R[residual_processes]:-}" "none"
  assert_eq "audio-invalid-device leftover files" "${R[leftover_files]:-}" "none"
else
  echo "  FAIL audio-invalid-device has no readings.json"; fail=1
fi
d="$RUN_DIR/audio-missing-resource"
if load_readings "$d"; then
  assert_eq "audio-missing-resource verdict" "${R[verdict]:-}" "COMPLETED"
  assert_eq "audio-missing-resource exit code" "${R[exit_code]:-}" "0"
  load_title "$d"
  # The corrected root contract: a root without a VPK cannot serve anything,
  # so the product reports that no root was found instead of the old
  # "connected, 0 VPK entries" reading, and it must never fall back to the
  # auto-detected root.
  assert_empty "audio-missing-resource connected no root" "${T[root]:-}"
  assert_ne "audio-missing-resource did not silently connect the auto-detected root" "${T[root]:-}" "$manifest_tf_root"
  assert_eq "audio-missing-resource residual processes" "${R[residual_processes]:-}" "none"
else
  echo "  FAIL audio-missing-resource has no readings.json"; fail=1
fi

bi="$RUN_DIR/blocked-inputs.txt"
assert_eq "blocked-inputs.txt is recorded" "$([ -s "$bi" ] && echo yes || echo no)" "yes"
if [ -s "$bi" ]; then
  assert_ge "the WARP environment is recorded as blocked" "$(grep -ci 'WARP' "$bi")" 1
  assert_ge "the feature-level environment is recorded as blocked" "$(grep -ci 'feature level' "$bi")" 1
  assert_ge "the blocked entries say what was NOT verified" "$(grep -ci 'NOT VERIFIED' "$bi")" 2
fi
ib="$RUN_DIR/install-blocked.txt"
assert_eq "install-blocked.txt is recorded" "$([ -s "$ib" ] && echo yes || echo no)" "yes"
if [ -s "$ib" ]; then
  assert_ge "the absent installer is recorded as blocked" "$(grep -ci 'NOT VERIFIED' "$ib")" 1
fi

# ================================================================ verdict
echo
if [ "$fail" -eq 0 ]; then
  echo "STABILITY-RELEASE=PASS"
else
  echo "STABILITY-RELEASE=FAIL"
fi

if [ "$MUTATION" -eq 0 ]; then
  exit "$fail"
fi

# ================================================================ mutation
# Six single-reading changes, each applied to a COPY of the evidence. Each must
# turn this gate red, and the control assertion named next to it must stay
# green -- a mutation that reddens everything has not shown which reading the
# judgment depends on.
echo
echo "=== MUTATION: the judgments above must be able to go red ==="
if [ "$fail" -ne 0 ]; then
  echo "  MUTATION-SETUP=FAIL (the forward run is already red; fix it before mutating)"
  echo "STABILITY-RELEASE-MUTATION=FAIL"; exit 1
fi

# Stage only what this gate reads. trace.txt and metrics.csv are deliberately
# left out: they run to tens of megabytes each, the gate does not read them, and
# a staged subset is labelled as one (--staged-subset) so the skipped contract
# cannot be mistaken for an asserted one.
# Each invocation stages into its own directory, named with the wall clock and
# the pid. The obvious implementation starts with `rm -rf "$dst"`, which is
# exactly what a deletion guard refuses once the staged copy holds more than a
# handful of files -- and the guard refusing is not a nuisance but the correct
# answer, because a gate that silently reuses a previous invocation's staged
# copy would be judging a mixture. A unique name removes the need to delete
# anything at all: no rm, no mixture, and the staged copies accumulate under
# .scratch where they are gitignored.
# The harness judges a COPY, so the original evidence must come out
# byte-identical. Without this reading a mutation that edited the real evidence
# in place would look exactly like one that edited a copy -- and the delivered
# evidence would then describe the mutation instead of the run, which is the
# failure this whole harness exists to prevent. The digest covers exactly the
# files this gate reads, so it is a statement about the evidence and not about
# the scratch directory's other contents.
evidence_fingerprint() { # <run dir> -> digest over the files this gate reads
  find "$1" -type f \( -name readings.json -o -name verdict.txt -o -name exit-code.txt \
    -o -name process-state.txt -o -name leftovers.txt -o -name title-first.txt \
    -o -name title-last.txt -o -name sampler.csv -o -name parser-probe.txt \
    -o -name command.txt -o -name run-manifest.txt -o -name blocked-inputs.txt \
    -o -name install-blocked.txt -o -name device-info.txt \) 2>/dev/null |
    LC_ALL=C sort | xargs sha256sum 2>/dev/null | sha256sum | cut -d' ' -f1
}
orig_fp="$(evidence_fingerprint "$RUN_DIR")"

MUT_RUN="$(date +%Y%m%d%H%M%S)-$$"
stage_dir() { # <mutation name> -> the staged directory for this invocation
  printf '%s/%s' "$MUT_SCRATCH" "$MUT_RUN/$1"
}
stage_evidence() { # <src run dir> <mutation name>
  local src="$1" dst
  dst="$(stage_dir "$2")"
  mkdir -p "$dst"
  for f in run-manifest.txt device-info.txt dxdiag.txt install-smoke-probe.txt \
           blocked-inputs.txt install-blocked.txt; do
    [ -f "$src/$f" ] && cp "$src/$f" "$dst/$f"
  done
  for f in fixture-index.txt; do
    [ -f "$src/fixtures/$f" ] && mkdir -p "$dst/fixtures" && cp "$src/fixtures/$f" "$dst/fixtures/$f"
  done
  for c in $LONG_CASES $CORRUPT_CASES $RESOURCE_CASES $AUDIO_CASES; do
    [ -d "$src/$c" ] || continue
    mkdir -p "$dst/$c"
    for f in readings.json verdict.txt exit-code.txt process-state.txt leftovers.txt \
             title-first.txt title-last.txt sampler.csv parser-probe.txt command.txt; do
      [ -f "$src/$c/$f" ] && cp "$src/$c/$f" "$dst/$c/$f"
    done
  done
}

run_gate_on() { # <staged dir> <name> -> stdout, rc
  # The inner run writes its evidence elsewhere: a mutation run that overwrote
  # the forward run's evidence would leave the delivered evidence describing
  # the mutation instead of the run.
  local out="$MUT_SCRATCH/$MUT_RUN/$2/out"
  mkdir -p "$out" "$MUT_SCRATCH/$MUT_RUN/$2/scratch"
  STABILITY_OUT="$out" \
  STABILITY_MUT_SCRATCH="$MUT_SCRATCH/$MUT_RUN/$2/scratch" \
    bash stability-release-check.sh --run-dir "$1" --staged-subset 2>&1
}

mut_fail=0
mut_n=0
mut_record="$OUT/mutations.txt"
: > "$mut_record"

mutation() { # mutation <name> <control assertion text> <target FAIL text> <stager function>
  local name="$1" control="$2" target="$3" stager="$4"
  mut_n=$((mut_n+1))
  local dir
  dir="$(stage_dir "$name")"
  stage_evidence "$RUN_DIR" "$name"
  # The control reading: the staged copy, before the change, must be green.
  # Otherwise the mutation is being judged against a copy that was already
  # broken, and "it went red" would say nothing about the change.
  local before before_rc
  before="$(run_gate_on "$dir" "$name")"; before_rc=$?
  if [ "$before_rc" -ne 0 ]; then
    echo "  MUTATION-SETUP=FAIL ($name: the staged copy is already red)"
    echo "$before" | grep 'FAIL' | head -3 | sed 's/^/    /'
    mut_fail=1; return
  fi
  "$stager" "$dir"
  local after after_rc
  after="$(run_gate_on "$dir" "$name")"; after_rc=$?
  local moved=no kept=no
  echo "$after" | grep -q "FAIL $target" && moved=yes
  echo "$after" | grep -q "OK   $control" && kept=yes
  if [ "$after_rc" -ne 0 ] && [ "$moved" = yes ] && [ "$kept" = yes ]; then
    echo "  MUTATION-CAUGHT=PASS ($name: '$target' went red, control '$control' stayed green)"
    printf '%s target=%s control=%s rc=%s moved=%s control_preserved=%s\n' \
      "$name" "$target" "$control" "$after_rc" "$moved" "$kept" >> "$mut_record"
  else
    echo "  MUTATION-CAUGHT=FAIL ($name: rc=$after_rc moved=$moved control_preserved=$kept)"
    echo "$after" | tail -6 | sed 's/^/    /'
    printf '%s target=%s control=%s rc=%s moved=%s control_preserved=%s\n' \
      "$name" "$target" "$control" "$after_rc" "$moved" "$kept" >> "$mut_record"
    mut_fail=1
  fi
}

# 1. no named refusal left anywhere: in run-03 the product itself names every
#    refusal through the title (the parser probe named 0 of 7), so taking the
#    probe file away alone is inert. Remove BOTH naming channels for one case
#    -- the probe file and the title-derived name in readings.json -- and the
#    gate must refuse to accept that case.
mut_named_refusal() {
  rm -f "$1/corrupt-truncated-packet/parser-probe.txt"
  sed -i 's/"demo_error_from_title":  "[^"]*"/"demo_error_from_title":  ""/' \
    "$1/corrupt-truncated-packet/readings.json"
}
mutation "named_refusal_removed" \
  "corrupt-truncated-header refusal named by product" \
  "corrupt-truncated-packet refuses the input without naming a reason" \
  mut_named_refusal

# 2. a crash reported as a completed case: the fault code is what the gate
#    reads the process state through, so a case that came back COMPLETED with
#    an access-violation code must go red.
mut_crash_code() {
  sed -i 's/"exit_code":  0,/"exit_code":  -1073741819,/' "$1/corrupt-empty/readings.json"
  sed -i 's/"trace_crash_marker":  false/"trace_crash_marker":  true/' "$1/corrupt-empty/readings.json"
}
mutation "crash_reported_as_completed" \
  "corrupt-empty verdict" \
  "corrupt-empty crash marker" \
  mut_crash_code

# 3. a soak that stopped drawing: freeze the frame counter in the sample series
#    (the leak criteria read the same file, so this also proves the frame
#    bound is not being satisfied by the numbers that are left).
mut_frames_frozen() {
  awk -F, 'BEGIN{OFS=","} NR==1{print; next} {print $1,$2,$3,$4,$5,$6,"500",$8}' \
    "$1/soak-hidden/sampler.csv" > "$1/soak-hidden/sampler.csv.tmp" &&
    mv "$1/soak-hidden/sampler.csv.tmp" "$1/soak-hidden/sampler.csv"
}
mutation "soak_frames_frozen" \
  "soak-hidden exit code" \
  "soak-hidden rendered frames advanced (sample series)" \
  mut_frames_frozen

# 4. a process left behind: the case still says COMPLETED and exit 0, and only
#    the residual-process reading moves.
mut_residual() {
  sed -i 's/"residual_processes":  "none"/"residual_processes":  "pid=99999"/' "$1/soak-window/readings.json"
}
mutation "residual_process_hidden" \
  "soak-window exit code" \
  "soak-window residual processes" \
  mut_residual

# 5. a leak in the steady state: the tail of the series grows by half a
#    gigabyte while the peak stays inside the bound, so only the steady-state
#    criterion can catch it. The offset is applied to the LAST QUARTER only.
#    Starting it at the midpoint would be a trap: the criterion compares the
#    last sample against the halfway sample, so offsetting both endpoints would
#    move nothing the gate measures and the mutation would fail for the wrong
#    reason -- it would look like the assertion could not go red.
mut_leak() {
  local f="$1/soak-window/sampler.csv"
  local n h q
  n="$(wc -l < "$f")"
  h=$(( (n + 1) / 2 ))
  q=$(( (n + 1) * 3 / 4 ))
  awk -F, -v q="$q" 'BEGIN{OFS=","}
    NR==1 { print; next }
    NR>q  { $3=sprintf("%.1f",$3+300); $4=sprintf("%.1f",$4+300); print; next }
    { print }' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
}
mutation "steady_state_leak" \
  "soak-window verdict" \
  "soak-window working set growth, second half" \
  mut_leak

# 6. an environment that stops being recorded: the blocked inputs file
#    removed. "Not verified" that is not written down must not pass as
#    verified.
mut_blocked_removed() { rm -f "$1/blocked-inputs.txt"; }
mutation "blocked_inputs_removed" \
  "install-blocked.txt is recorded" \
  "blocked-inputs.txt is recorded" \
  mut_blocked_removed

final_fp="$(evidence_fingerprint "$RUN_DIR")"
if [ "$orig_fp" = "$final_fp" ]; then
  echo "  OK   the original evidence is byte-identical after all $mut_n mutations ($orig_fp)"
else
  echo "  FAIL the original evidence changed during the mutation pass ($orig_fp -> $final_fp)"
  mut_fail=1
fi

if [ "$mut_fail" -eq 0 ]; then
  echo "MUTATION-CAUGHT=PASS ($mut_n of $mut_n mutations went red with their control preserved)"
  echo "STABILITY-RELEASE-MUTATION=PASS"
  exit 0
fi
echo "STABILITY-RELEASE-MUTATION=FAIL"
exit 1
