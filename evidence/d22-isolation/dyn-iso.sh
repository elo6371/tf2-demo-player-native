#!/usr/bin/env bash
# task-24 / D-22 dynamic sample: prove that six verify-all gates, when launched
# with the isolated settings flags, do not touch the user's real settings.
# Static scan (gate-launch-hygiene-check.sh) only proves the flags are IN the
# command line; only a real product launch proves the product accepted them.
#
# Usage:
#   bash evidence/d22-isolation/dyn-iso.sh [--tree DIR] [--out DIR] [--lease owner]
#   --tree  tree to test (default: repo root containing this script)
#   --out   scratch dir for logs (default: <tree>/.scratch/dyn-iso)
#   --lease owner name used in the banner (default: t94-hygiene)
#   --plan  print what would run and exit 0 (no build, no launch, no lock needed)
#   --skip-build  reuse an existing native/build-nmake/tf2_demo_native.exe
#
# MUST run under the host lock, e.g.:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t94-hygiene \
#     bash evidence/d22-isolation/dyn-iso.sh --tree /d/TF2_Native_Worktrees/t91-integration
#
# Exit codes: 0 PASS, 1 gate failed or AppData touched, 2 bad tree, 3 build failed.
# Banners for scraping:
#   GATE <script> rc=<n> token=<TOKEN=PASS|FAIL ...> log=<path>
#   APPDATA-UNCHANGED=YES|NO
#   DYNAMIC-ISO=PASS|FAIL gates=<n>/6 [appdata_touched=YES] [reason=build|tree]
set -uo pipefail

TREE=""; OUT=""; LEASE="t94-hygiene"; PLAN=0; SKIP_BUILD=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --tree)  TREE="${2:-}"; shift 2 ;;
    --out)   OUT="${2:-}"; shift 2 ;;
    --lease) LEASE="${2:-}"; shift 2 ;;
    --plan)  PLAN=1; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "DYNAMIC-ISO=FAIL reason=bad-arg arg=$1"; exit 2 ;;
  esac
done
if [ -z "$TREE" ]; then
  TREE=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fi
[ -d "$TREE" ] || { echo "DYNAMIC-ISO=FAIL reason=tree-missing tree=$TREE"; exit 2; }
cd "$TREE" || { echo "DYNAMIC-ISO=FAIL reason=tree-enter tree=$TREE"; exit 2; }
[ -f verify-all.sh ] || { echo "DYNAMIC-ISO=FAIL reason=not-a-repo tree=$TREE (no verify-all.sh)"; exit 2; }
if [ "$PLAN" = 1 ]; then
  AP_PLAN="$(cygpath -u "${LOCALAPPDATA:-C:/Users/Administrator/AppData/Local}" 2>/dev/null || echo "${LOCALAPPDATA:-C:/Users/Administrator/AppData/Local}")/TF2 Demo Player"
  echo "PLAN tree=$TREE"
  echo "PLAN head=$(git rev-parse HEAD 2>/dev/null || echo not-a-git-tree)"
  echo "PLAN build=$( [ "$SKIP_BUILD" = 1 ] && echo skipped || echo 'tf2_demo_native review_workspace_probe spectate_probe viewmodel_animation_probe weapon_animation_probe' )"
  echo "PLAN gates=frame-capture-check.sh idle-spin-check.sh review-workspace-check.sh spectate-check.sh viewmodel-animation-check.sh weapon-animation-check.sh (6)"
  echo "PLAN appdata-watched=$AP_PLAN/settings.ini + ui.ini (sha256 and size:mtime compared before/after)"
  echo "PLAN exit-code-semantics=0 PASS | 1 gate red or appdata touched | 2 bad tree | 3 build/exe failed"
  exit 0
fi
for need in build-target.sh evidence/d22-isolation/dyn-iso.sh; do
  [ -e "$need" ] || echo "WARN missing-expected-file=$need"
done

OUT="${OUT:-$TREE/.scratch/dyn-iso}"
mkdir -p "$OUT/out" "$OUT/trace"
echo "START $(date -Is) owner=$LEASE tree=$TREE"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo not-a-git-tree)"

# --- the thing under test: the user's real settings -------------------------
AP="$(cygpath -u "${LOCALAPPDATA:-C:/Users/Administrator/AppData/Local}" 2>/dev/null || echo "${LOCALAPPDATA:-C:/Users/Administrator/AppData/Local}")/TF2 Demo Player"
sha_of()  { if [ -f "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo MISSING; fi; }
stat_of() { if [ -f "$1" ]; then stat -c '%s:%Y' "$1"; else echo MISSING; fi; }
B_SET=$(sha_of "$AP/settings.ini");  B_UI=$(sha_of "$AP/ui.ini")
B_SET_ST=$(stat_of "$AP/settings.ini"); B_UI_ST=$(stat_of "$AP/ui.ini")
echo "APPDATA-BEFORE settings=$B_SET ($B_SET_ST) ui=$B_UI ($B_UI_ST)"

# --- build: product + the four probes the six gates need --------------------
if [ "$SKIP_BUILD" = 1 ]; then
  echo "BUILD skipped (--skip-build)"; BRC=0
else
  echo "== build targets: tf2_demo_native review_workspace_probe spectate_probe viewmodel_animation_probe weapon_animation_probe"
  bash build-target.sh tf2_demo_native review_workspace_probe spectate_probe viewmodel_animation_probe weapon_animation_probe > "$OUT/build.log" 2>&1
  BRC=$?
fi
echo "BUILD rc=$BRC log=$OUT/build.log"
if [ "$BRC" -ne 0 ]; then
  echo "BUILD-ERROR $(grep -iE 'error|FAILED' "$OUT/build.log" | tail -3)"
  echo "DYNAMIC-ISO=FAIL reason=build rc=$BRC"
  exit 3
fi
EXE=native/build-nmake/tf2_demo_native.exe
if [ -f "$EXE" ]; then
  echo "EXE $EXE bytes=$(stat -c %s "$EXE") sha256=$(sha256sum "$EXE" | cut -d' ' -f1)"
else
  echo "EXE-MISSING $EXE"; echo "DYNAMIC-ISO=FAIL reason=exe-missing"; exit 3
fi

# --- the six gates: each launches the product with isolated settings -------
FAIL=0; PASSED=0
run_gate() { # run_gate <script> <token> [env=val ...]
  local sc="$1" tok="$2"; shift 2
  local log="$OUT/${sc%.sh}.log"
  echo "== $sc (expects token $tok=PASS)"
  env "$@" bash "$sc" > "$log" 2>&1
  local rc=$?
  local line; line=$(grep -E "^${tok}=" "$log" | tail -1)
  echo "GATE $sc rc=$rc token=${line:-<missing>} log=$log"
  if [ "$rc" -eq 0 ] && [ -n "$line" ]; then PASSED=$((PASSED+1));
  else FAIL=$((FAIL+1)); echo "GATE-FAIL-DETAIL $(grep -iE '(^FAIL|ERROR|assert)' "$log" | tail -2)"; fi
}

run_gate frame-capture-check.sh FRAME-CAPTURE \
  FRAME_CAPTURE_OUT="$OUT/out/frame"
run_gate idle-spin-check.sh IDLE-SPIN \
  IDLE_SPIN_OUT="$OUT/out/idle"
run_gate review-workspace-check.sh REVIEW-WORKSPACE-CHECK \
  REVIEW_WORKSPACE_OUT="$OUT/out/review"
run_gate spectate-check.sh SPECTATE-CHECK \
  SPECTATE_OUT="$OUT/out/spectate"
run_gate viewmodel-animation-check.sh VIEWMODEL-ANIMATION \
  VIEWMODEL_ANIMATION_TRACE_DIR="$OUT/out/viewmodel"
run_gate weapon-animation-check.sh WEAPON-ANIMATION \
  WEAPON_ANIMATION_OUT="$OUT/out/weapon" WEAPON_ANIMATION_TRACE_DIR="$OUT/trace/weapon"

# --- the assertion: settings bytes AND mtime must be identical -------------
A_SET=$(sha_of "$AP/settings.ini");  A_UI=$(sha_of "$AP/ui.ini")
A_SET_ST=$(stat_of "$AP/settings.ini"); A_UI_ST=$(stat_of "$AP/ui.ini")
echo "APPDATA-AFTER  settings=$A_SET ($A_SET_ST) ui=$A_UI ($A_UI_ST)"
TOUCH=NO
if [ "$B_SET" != "$A_SET" ] || [ "$B_UI" != "$A_UI" ] || \
   [ "$B_SET_ST" != "$A_SET_ST" ] || [ "$B_UI_ST" != "$A_UI_ST" ]; then TOUCH=YES; fi
echo "APPDATA-UNCHANGED=$([ "$TOUCH" = NO ] && echo YES || echo NO)"
[ "$TOUCH" = NO ] || echo "APPDATA-DIFF settings: $B_SET/$B_SET_ST -> $A_SET/$A_SET_ST ; ui: $B_UI/$B_UI_ST -> $A_UI/$A_UI_ST"

echo "END $(date -Is)"
if [ "$FAIL" -eq 0 ] && [ "$TOUCH" = NO ]; then
  echo "DYNAMIC-ISO=PASS gates=$PASSED/6"; exit 0
fi
echo "DYNAMIC-ISO=FAIL gates_passed=$PASSED/6 appdata_touched=$TOUCH"; exit 1
