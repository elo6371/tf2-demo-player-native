#!/usr/bin/env bash
# run-e1-round5-matrix.sh -- E1 round 5 final four-gate matrix.
#
# WHAT THIS IS
#   One serialized run of the four E1 round-5 gates, with identity, host lock,
#   timeouts and line endings recorded in one place, so the matrix reads as one
#   table instead of four differently-shaped logs:
#
#     viewmodel-material   viewmodel-material-check.sh   (all 9 sections)
#     minimap-hud          minimap-hud-check.sh          (A1/A6/A7 known reds)
#     map-geometry         map-geometry-lighting-check.sh --only=cp_snakewater_final1
#                                                        --poses=p1; p2 is the known
#                                                        WINDOW-DEADLINE case, not run
#     companion            viewmodel-companion-check.sh  (known structural reds)
#
# USAGE
#   bash native/tools/run-e1-round5-matrix.sh --dry-run   # print the plan, run nothing
#   bash native/tools/run-e1-round5-matrix.sh --run       # execute the four gates
#   bash native/tools/run-e1-round5-matrix.sh --help
#
# WHY DRY-RUN IS EXPLICIT
#   The matrix is launched by the lead in one window; a typo in a path would
#   otherwise burn that window. --dry-run resolves every command line, every env
#   value, the exe pin and every precondition without starting a product, and it
#   prints SKIP-PRECONDITION rows instead of silently dropping a gate.
#
# HARD CONSTRAINTS (also printed by --help)
#   1. NEVER REBUILDS THE PRODUCT. The gate exe is pinned by sha256
#      0bc3ed077aae404634a9f11a5453e6d8f5235be56cc2b818a1626a39ecbab3e4
#      (3,070,464 B, rebuilt 2026-11-16 19:51 after the r1b range-depth capture
#      merge 07cb7bd0). This script never calls build-target.sh. Each gate's
#      before/after sha is compared with the pin; a gate counts only when
#      before == after == pin. A gate that would need a build is red, not rebuilt.
#   2. NO GIT MUTATION. No stash, no clean, no checkout, no commit. Other agents'
#      dirty files (docs/N-*, docs/PROJECT-*, evidence/t62-map-matrix, ...) are
#      left exactly as found; the matrix records a git-status delta instead.
#   3. NO --mutation. viewmodel-material, minimap-hud and companion all have
#      mutation modes that rebuild sources; none is passed. minimap-hud's A12
#      therefore reports skipped_controls=1, which is expected.
#   4. A FAILING GATE DOES NOT STOP THE MATRIX. Every gate runs; the summary is
#      the deliverable, and partial coverage that says so beats a clean early exit.
#   5. EVIDENCE PATHS ARE RELATIVE. Every OUT/TRACE/SCRATCH lives under
#      evidence/fast/e1-round5-matrix/<utc-tag>/ (gitignored). Several gates
#      prepend the repo root themselves, so an absolute TRACE_DIR would be joined
#      twice (the viewmodel-material TRACE_DIR trap in triage section 4.4).
#   6. EXPLICIT TIMEOUTS. Each gate runs under timeout(1) with the cap in the
#      table below; no gate may hang the window. Companion gets 1800 s even
#      though its per-launch RUN_TIMEOUT is 400 s, because it starts several
#      launches plus one history run.
#
# WHAT "PASS" MEANS HERE
#   rc=0, the gate's own <GATE>=PASS line in its log, and exe sha
#   before == after == the pinned sha. OK/FAIL line counts are reported next to
#   that as readings, not as the verdict.
#
# EXIT CODES
#   0  dry-run resolved everything, or a real run where all four gates passed
#   1  a real run where at least one gate ran and failed
#   2  a precondition was missing (SKIP-PRECONDITION), the exe pin did not
#      match, or the invocation itself was wrong
#
# KNOWN SIDE EFFECTS (recorded, never cleaned by this script)
#   * map-geometry seeds its own gitignored scratch (.scratch/t62-map-axes/inputs)
#     with settings.ini/ui.ini, so a run leaves the tree clean; nothing under
#     evidence/ is written outside the gate's own run directory.
#   * map-geometry without --re-pin never rewrites frame-pins/cull-reference; a
#     pin mismatch is a red reading, never a re-pin.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 2

PINNED_EXE_SHA=0bc3ed077aae404634a9f11a5453e6d8f5235be56cc2b818a1626a39ecbab3e4
EXE=native/build-nmake/tf2_demo_native.exe
HOSTLOCK=/d/TF2_Native_Worktrees/_shared/hostlock.sh

MODE=""
case "${1:-}" in
  --dry-run) MODE=dry ;;
  --run) MODE=run ;;
  -h|--help) sed -n '2,72p' "$0"; exit 0 ;;
  "" ) echo "usage: $0 --dry-run|--run  (see --help)" >&2; exit 2 ;;
  *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
esac

RUN_TAG=$(date -u +%Y%m%dT%H%M%SZ)
BASE_REL=evidence/fast/e1-round5-matrix
RUN_REL=$BASE_REL/$RUN_TAG
META_REL=$RUN_REL/matrix
GATES_REL=$RUN_REL/gates
RUN_DIR=$ROOT/$RUN_REL
META_DIR=$ROOT/$META_REL
mkdir -p "$META_DIR" "$ROOT/$GATES_REL" || exit 2

EXE_SHA=$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)
PIN_STATE=OK
[ "$EXE_SHA" = "$PINNED_EXE_SHA" ] || PIN_STATE="MISMATCH actual=${EXE_SHA:-absent}"
HEAD_SHORT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_PORCELAIN_BEFORE=$META_REL/git-status-before.txt

echo "E1 ROUND-5 MATRIX -- mode=$MODE"
echo "  head=$HEAD_SHORT"
echo "  exe=$EXE"
echo "  exe_sha=$PIN_STATE"
echo "  hostlock=$HOSTLOCK"
echo "  run_dir=$RUN_REL (relative, gitignored)"
echo

# --- the gate table -----------------------------------------------------------
# name|script|hostlock owner|timeout cap s|extra args|env assignments|verdict token
# Order: cheapest and most stable first, so a window that ends early still has
# the most evidence. Env values use __G__ = this gate's directory under gates/.
gate_table() {
  cat <<'TABLE'
viewmodel-material|viewmodel-material-check.sh|card-arch|2400||VIEWMODEL_MATERIAL_OUT=__G__/out VIEWMODEL_MATERIAL_TRACE_DIR=__G__/trace|VIEWMODEL-MATERIAL
minimap-hud|minimap-hud-check.sh|card-arch|900||MINIMAP_CHECK_OUT=__G__/out MINIMAP_CHECK_SCRATCH=__G__/scratch MINIMAP_CHECK_DEMO_SUNSHINE=E:/demo测试/cp_sunshine/STV/SUNSHINE.dem|MINIMAP-HUD
map-geometry|map-geometry-lighting-check.sh|t62-mapaxes|1800|--only=cp_snakewater_final1 --poses=p1|HOLD_LOCK=1 MAP_AXIS_OUT=__G__/map-axes|MAP-GEOMETRY-LIGHTING
companion|viewmodel-companion-check.sh|card-arch|1800||VIEWMODEL_COMPANION_TRACE_DIR=__G__/trace|VIEWMODEL-COMPANION
TABLE
}

# preconditions: print what is missing for one gate (empty = runnable).
gate_missing() {
  local name="$1" missing=""
  local demo_pov="E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem"
  local demo_mh="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
  local demo_sunshine="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem"
  case "$name" in
    viewmodel-material)
      [ -x native/build-nmake/viewmodel_material_probe.exe ] || missing="$missing viewmodel_material_probe.exe"
      [ -d evidence/viewmodel/fixtures/vmfix-tf ] || missing="$missing evidence/viewmodel/fixtures/vmfix-tf"
      [ -e "$demo_pov" ] || missing="$missing $demo_pov"
      ;;
    minimap-hud)
      [ -e "$demo_mh" ] || missing="$missing $demo_mh"
      [ -e "$demo_sunshine" ] || missing="$missing $demo_sunshine"
      ;;
    map-geometry)
      [ -x native/build-nmake/world_geometry_probe.exe ] || missing="$missing world_geometry_probe.exe"
      [ -d evidence/t62-map-matrix ] || missing="$missing evidence/t62-map-matrix"
      [ -e evidence/t62-map-matrix/cp_snakewater_final1/frame-pins-p1.txt ] || missing="$missing frame-pins-p1.txt"
      ;;
    companion)
      [ -x native/build-nmake/viewmodel_animation_probe.exe ] || missing="$missing viewmodel_animation_probe.exe"
      [ -e evidence/viewmodel/animation-inputs.txt ] || missing="$missing evidence/viewmodel/animation-inputs.txt"
      [ -e "$demo_pov" ] || missing="$missing $demo_pov"
      ;;
  esac
  printf '%s' "${missing# }"
}

gate_token_present() { # the gate file must contain its own verdict token
  grep -q "$2=PASS" "$1" 2>/dev/null
}

PRECOND_ROWS="$META_REL/preconditions.txt"
printf '%-20s %-12s %-7s %s\n' "gate" "hostlock" "cap" "preconditions"
gate_table | while IFS='|' read -r name script owner cap args envs token; do
  miss=$(gate_missing "$name")
  state=OK
  if [ ! -f "$script" ]; then state="MISSING: $script"; fi
  if ! gate_token_present "$script" "$token"; then state="$state MISSING: verdict-token $token"; fi
  if [ -n "$miss" ]; then state="$state MISSING: $miss"; fi
  printf '%-20s %-12s %-7s %s\n' "$name" "$owner" "${cap}s" "$state"
done
echo

# --- argv resolution ----------------------------------------------------------
# gate_argv <name> <script> <owner> <cap> <args> <envs> -> ARGV
# OUT/TRACE stay relative: $GATES_REL/<gate>/<sub>. Several gates prepend the
# repo root themselves, which is the trap tumble 4.4 documents.
gate_argv() {
  local name="$1" script="$2" owner="$3" cap="$4" args="$5" envs="$6"
  local g="$GATES_REL/$name" e
  envs=${envs//__G__/$g}
  ARGV=(bash "$HOSTLOCK" run "$owner" timeout -k 10 "$cap" env)
  for e in $envs; do ARGV+=("$e"); done
  ARGV+=(bash "$script")
  for e in $args; do ARGV+=("$e"); done
}

show_argv() { printf '%s ' "${ARGV[@]}"; echo; }

print_plan() {
  gate_table | while IFS='|' read -r name script owner cap args envs token; do
    gate_argv "$name" "$script" "$owner" "$cap" "$args" "$envs"
    echo "- $name  (owner=$owner cap=${cap}s)"
    echo "    $(show_argv)"
  done
}

if [ "$MODE" = dry ]; then
  echo "--- resolved command lines (nothing is executed in this mode) ---"
  print_plan
  echo
  echo "--- preconditions ---"
  gate_table | while IFS='|' read -r name script owner cap args envs token; do
    miss=$(gate_missing "$name")
    if [ -n "$miss" ]; then echo "  SKIP-PRECONDITION $name: missing $miss"; else echo "  OK   $name"; fi
  done
  echo
  echo "--- identity / safety ---"
  echo "  exe pin: $PIN_STATE"
  echo "  rebuilds: none (no build-target call in this script)"
  echo "  git mutation: none (no stash/clean/checkout/commit)"
  echo "  mutation modes: none passed (minimap-hud A12 reads skipped_controls=1)"
  echo "  caps: viewmodel-material 2400 s, minimap-hud 900 s, map-geometry 1800 s, companion 1800 s"
  echo "  evidence: $RUN_REL/ (relative, under the gitignored evidence/fast)"
  echo
  echo "DRY-RUN=OK gates=4 (SKIP rows are listed above, never dropped silently)"
  [ "$PIN_STATE" = OK ] || exit 2
  exit 0
fi

# --- the real matrix ----------------------------------------------------------
{
  echo "run_dir=$RUN_REL"
  echo "head=$HEAD_SHORT"
  echo "head_full=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "exe_sha_before=$EXE_SHA"
  echo "exe_pin=$PINNED_EXE_SHA"
  echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostlock_status_before:"
  bash "$HOSTLOCK" status 2>&1 | sed 's/^/  /'
} > "$META_REL/context.txt"
git status --porcelain > "$GIT_PORCELAIN_BEFORE" 2>&1

if [ "$PIN_STATE" != OK ]; then
  echo "PREFLIGHT-FAIL exe sha $PIN_STATE; refusing to measure a different binary" | tee "$META_REL/summary.txt"
  exit 2
fi

cr_in_dir() { # cr_in_dir <dir>; total CR bytes in text files (binaries skipped)
  local dir="$1" f cr total=0
  [ -d "$dir" ] || { echo 0; return; }
  while IFS= read -r f; do
    case "$f" in *.bmp|*.png|*.jpg|*.raw|*.ini) continue ;; esac
    cr=$(tr -cd '\r' < "$f" | wc -c | tr -d ' ')
    total=$((total + cr))
  done < <(find "$dir" -type f 2>/dev/null)
  echo "$total"
}

SUMMARY="$META_REL/summary.tsv"
printf 'gate\trc\tverdict\tok_lines\tfail_lines\tduration_s\texe_sha_before\texe_sha_after\teol_cr\tstate\n' > "$SUMMARY"
overall=0; skipped=0; failed=0; passed=0
gate_table > "$META_REL/gates.txt"

while IFS='|' read -r -u 3 name script owner cap args envs token; do
  echo
  echo "=== $name (owner=$owner cap=${cap}s) ==="
  miss=$(gate_missing "$name")
  if [ -n "$miss" ]; then
    echo "  SKIP-PRECONDITION missing: $miss"
    printf '%s\t-\tSKIP-PRECONDITION\t0\t0\t0\t%s\t%s\t-\tSKIP\n' "$name" "$EXE_SHA" "$EXE_SHA" >> "$SUMMARY"
    skipped=$((skipped + 1)); overall=2
    continue
  fi
  sha_before=$(sha256sum "$EXE" | cut -d' ' -f1)
  if [ "$sha_before" != "$PINNED_EXE_SHA" ]; then
    echo "  PREFLIGHT-FAIL exe sha $sha_before != pin $PINNED_EXE_SHA"
    printf '%s\t-\tPREFLIGHT-FAIL\t0\t0\t0\t%s\t%s\t-\tSKIP\n' "$name" "$sha_before" "$sha_before" >> "$SUMMARY"
    skipped=$((skipped + 1)); overall=2
    continue
  fi
  mkdir -p "$RUN_DIR/gates/$name" || exit 2
  gate_argv "$name" "$script" "$owner" "$cap" "$args" "$envs"
  raw_log="$RUN_DIR/gates/$name/gate.log"
  log="$META_REL/$name.log"
  start=$(date +%s)
  # stdin is /dev/null. This loop's stdin is the gate table, and the gate chain
  # runs native Windows binaries; under MSYS bash a native child that inherits
  # the loop's stdin handle consumes the remaining rows, so the matrix ended
  # after the first gate (the same trap release-rebuild.sh documents).
  "${ARGV[@]}" < /dev/null > "$raw_log" 2>&1
  rc=$?
  end=$(date +%s); dur=$((end - start))
  # The product prints through CRT text mode (D53 family), so the gate's own
  # output carries CRs. Keep that raw log next to the gate evidence (it is
  # counted as EOL-WARN, never as a matrix failure) and write the bookkeeping
  # copy LF-only, so the final camera-ui --check-eol over $META_REL is green.
  tr -d '\r' < "$raw_log" > "$log"
  sha_after=$(sha256sum "$EXE" | cut -d' ' -f1)
  verdict=$(grep -aE "^${token}=(PASS|FAIL)" "$raw_log" | tail -1)
  [ -n "$verdict" ] || verdict="$token=<none>"
  ok_lines=$(grep -acE '^  OK' "$raw_log" || true)
  fail_lines=$(grep -acE '^  FAIL' "$raw_log" || true)
  eol_cr=$(cr_in_dir "$RUN_DIR/gates/$name")
  state=OK
  if [ "$sha_before" != "$sha_after" ]; then
    state=EXE-SHA-DRIFT; overall=2
  elif [ "$rc" -ne 0 ]; then
    state=FAIL; failed=$((failed + 1)); overall=1
  elif [[ $verdict != "$token=PASS"* ]]; then
    state=FAIL; failed=$((failed + 1)); overall=1
  else
    passed=$((passed + 1))
  fi
  if [ "$eol_cr" != 0 ]; then
    echo "  EOL-WARN $name: $eol_cr CR bytes in its evidence (D53 family; WARN, not a matrix failure)"
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$rc" "$verdict" "$ok_lines" "$fail_lines" "$dur" "$sha_before" "$sha_after" "$eol_cr" "$state" >> "$SUMMARY"
  echo "  rc=$rc duration=${dur}s $verdict ok_lines=$ok_lines fail_lines=$fail_lines eol_cr=$eol_cr state=$state"
done 3< "$META_REL/gates.txt"

echo
echo "--- matrix summary ---"
if command -v column >/dev/null 2>&1; then column -t -s $'\t' "$SUMMARY"; else sed 's/\t/  /g' "$SUMMARY"; fi
{
  echo "passed=$passed failed=$failed skipped=$skipped"
  echo "exe_sha_after=$(sha256sum "$EXE" | cut -d' ' -f1)"
  echo "finished_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >> "$META_REL/context.txt"
git status --porcelain > "$META_REL/git-status-after.txt" 2>&1
echo
echo "--- tree delta (recorded, never cleaned by this script) ---"
diff "$GIT_PORCELAIN_BEFORE" "$META_REL/git-status-after.txt" | head -40 || true
echo
echo "--- matrix bookkeeping line endings ---"
bash camera-ui-check.sh --check-eol "$META_REL"
[ $? -eq 0 ] || overall=2
echo
if [ "$overall" -eq 0 ]; then
  echo "E1-ROUND5-MATRIX=PASS"
elif [ "$overall" -eq 2 ]; then
  echo "E1-ROUND5-MATRIX=INCOMPLETE (precondition or preflight)"
else
  echo "E1-ROUND5-MATRIX=FAIL"
fi
exit "$overall"
